#include "src/election/results_source.h"

#include "src/election/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace usa::election {
namespace {

// SplitMix64: tiny, deterministic hash for reproducible simulations.
uint64_t Mix(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

uint64_t HashString(std::string_view s, uint64_t seed) {
  uint64_t h = seed ^ 0xCBF29CE484222325ull;
  for (unsigned char c : s) h = (h ^ c) * 0x100000001B3ull;
  return Mix(h);
}

double Unit(uint64_t h) { return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0); }

}  // namespace

FileResultsSource::FileResultsSource(const ElectionData* data, std::string path,
                                     double poll_interval_s)
    : data_(data), path_(std::move(path)), poll_interval_s_(poll_interval_s) {}

std::shared_ptr<const ResultsSnapshot> FileResultsSource::Poll(double now) {
  if (now - last_poll_ < poll_interval_s_) return nullptr;
  last_poll_ = now;
  std::error_code ec;
  const auto mtime = std::filesystem::last_write_time(path_, ec);
  if (ec) {
    last_error_ = "cannot stat " + path_;
    return nullptr;
  }
  if (loaded_once_ && mtime == last_mtime_) return nullptr;
  std::string text;
  if (!ReadFile(path_, &text)) {
    last_error_ = "cannot read " + path_;
    return nullptr;
  }
  auto snap = std::make_shared<ResultsSnapshot>();
  std::string error;
  if (!ParseResultsJson(text, *data_, snap.get(), &error)) {
    // Keep the previous snapshot; a writer may be mid-way through the file.
    last_error_ = error;
    return nullptr;
  }
  last_error_.clear();
  last_mtime_ = mtime;
  loaded_once_ = true;
  snap->version = ++version_;
  return snap;
}

std::string FileResultsSource::Describe() const { return "file: " + path_; }

SimulatedResultsSource::SimulatedResultsSource(const ElectionData* data,
                                               const geo::RegionTree* tree, uint64_t seed,
                                               double speed)
    : data_(data), tree_(tree), seed_(seed) {
  set_speed(speed);
  Build();
}

namespace {

// States that count most mail ballots after election day and are nowhere
// near done by the next morning.
bool SlowCountState(std::string_view abbr) {
  for (const char* s : {"CA", "WA", "OR", "NV", "AZ", "AK", "UT"}) {
    if (abbr == s) return true;
  }
  return false;
}

// Kumaraswamy(1.6, 3.2) sample from a uniform `u`: a few early batches, the
// bulk in the first third, a long tail.
double Kumaraswamy(double u) { return std::pow(1.0 - std::pow(1.0 - u, 1.0 / 3.2), 1.0 / 1.6); }

}  // namespace

void SimulatedResultsSource::Build() {
  village_units_.assign(tree_->size(), 0);
  const auto& races = data_->races();
  for (size_t ri = 0; ri < races.size(); ++ri) {
    const Race& race = races[ri];
    const int state = tree_->FindByCode(race.state_code);
    const StateInfo* info = data_->State(race.state_code);
    if (state < 0 || !info || race.candidates.empty()) continue;
    const size_t n = race.candidates.size();
    const uint64_t rh = HashString(race.id, seed_);
    const uint64_t state_h = HashString(info->abbr, seed_);

    // Random strengths, blind as to which major party does better: the two
    // main contenders (the Democrat and the Republican, or whoever stands in
    // for a missing major party, e.g. an independent) share the top band;
    // minor candidates get a few percent. The top two are then made close so
    // the count is competitive.
    std::vector<double> strength(n);
    bool has_dem = false, has_rep = false;
    for (const Candidate& c : race.candidates) {
      has_dem |= c.party == "DEM";
      has_rep |= c.party == "REP";
    }
    for (size_t i = 0; i < n; ++i) {
      const Candidate& c = race.candidates[i];
      const double u = Unit(HashString(c.id, seed_));
      const bool major = c.party == "DEM" || c.party == "REP" ||
                         (c.party == "IND" && (!has_dem || !has_rep));
      strength[i] = major ? 0.6 + 0.4 * u : 0.01 + 0.05 * u * u;
    }
    // Same-party rivals (top-two / top-four primaries): the first listed
    // leads that party's field.
    for (size_t i = 0; i < n; ++i) {
      for (size_t j = 0; j < i; ++j) {
        if (race.candidates[j].party == race.candidates[i].party && strength[i] > 0.3) {
          strength[i] *= 0.35;
          break;
        }
      }
    }
    std::vector<int> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = static_cast<int>(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return strength[a] > strength[b]; });
    const int front_a = order[0];
    const int front_b = n > 1 ? order[1] : order[0];
    const double tight = Unit(Mix(rh + 9));
    strength[front_b] = strength[front_a] * (tight < 0.12 ? 0.985 + 0.02 * Unit(Mix(rh + 1))
                                                         : 0.80 + 0.18 * Unit(Mix(rh + 1)));
    // Correlation between reporting time and lean: > 0 means early batches
    // favour B and late ones favour A (so A comes from behind).
    const double u = Unit(Mix(rh + 2));
    const double corr = u < 0.35 ? 0.0 : (u < 0.7 ? 1.0 : -1.0) * (0.35 + 0.5 * Unit(Mix(rh + 3)));

    // Statewide turnout scales with the House delegation (a population
    // proxy): ~330k votes per district in a midterm.
    const double state_votes =
        std::max(1, info->house_seats) * 330000.0 * (0.85 + 0.3 * Unit(Mix(state_h + 1)));
    const bool slow = SlowCountState(info->abbr);
    const double span = slow ? 900 + 300 * Unit(Mix(state_h + 2)) : 280 + 240 * Unit(Mix(state_h + 3));
    const double close = info->poll_close_minutes;

    const auto& counties = tree_->region(state).children;
    std::vector<double> weight(counties.size());
    double weight_sum = 0;
    for (size_t c = 0; c < counties.size(); ++c) {
      weight[c] = std::exp(5.0 * std::pow(Unit(HashString(tree_->region(counties[c]).code, seed_ ^ 0xC0)), 2.2));
      weight_sum += weight[c];
    }
    for (size_t c = 0; c < counties.size(); ++c) {
      const int v = counties[c];
      const uint64_t vh = HashString(tree_->region(v).code, seed_);
      const double county_votes = state_votes * weight[c] / weight_sum;
      // Bigger counties report in more batches.
      const int count = std::clamp(1 + static_cast<int>(std::log10(1 + county_votes / 2000) * 2.5 +
                                                        Unit(Mix(vh + 3)) * 2),
                                   1, 12);
      village_units_[v] = count;
      const double county_k = Kumaraswamy(Unit(Mix(vh + 1)));
      for (int s = 0; s < count; ++s) {
        const uint64_t sh = Mix(vh + 101 * (s + 1));
        // Batches spread around the county's own timing.
        const double k = std::clamp(0.5 * county_k + 0.5 * Kumaraswamy(Unit(Mix(sh + 1))), 0.0, 1.0);
        const double lean = corr * (1.0 - 2.0 * k) + 0.35 * (Unit(Mix(vh + 2)) * 2 - 1);
        Station st;
        const double jitter = (Unit(Mix(sh + 4)) - 0.5) * 12.0;
        st.minute = static_cast<float>(std::max(close + 3.0, close + 6 + span * k + jitter));
        // Late mail and provisional ballots: some counties' last batch is
        // only counted in the following days (after the night ends).
        if (count > 1 && s == count - 1 && Unit(Mix(sh + 9)) < 0.35) {
          st.minute = static_cast<float>(kCountMinutes + 60 + 2000 * Unit(Mix(sh + 10)));
        }
        st.race = static_cast<int>(ri);
        st.village = v;
        st.cast = std::max<int32_t>(
            40, static_cast<int32_t>(county_votes / count * (0.7 + 0.6 * Unit(Mix(sh + 5)))));
        st.eligible = static_cast<int32_t>(st.cast / (0.42 + 0.2 * Unit(Mix(sh + 6))));
        const int32_t valid = static_cast<int32_t>(st.cast * 0.99);
        std::vector<double> w(n);
        double sum = 0;
        for (size_t i = 0; i < n; ++i) {
          w[i] = strength[i] * (0.7 + 0.6 * Unit(Mix(sh + 17 * (i + 7))));
          if (static_cast<int>(i) == front_a) w[i] *= 1.0 - 0.3 * lean;
          if (static_cast<int>(i) == front_b && front_b != front_a) w[i] *= 1.0 + 0.3 * lean;
          sum += w[i];
        }
        st.votes_at = static_cast<uint32_t>(votes_.size());
        for (size_t i = 0; i < n; ++i) {
          votes_.push_back(static_cast<int32_t>(std::llround(valid * w[i] / sum)));
        }
        stations_.push_back(st);
      }
    }
  }
  std::stable_sort(stations_.begin(), stations_.end(),
                   [](const Station& a, const Station& b) { return a.minute < b.minute; });
  last_report_ = stations_.empty() ? 0 : stations_.back().minute;

  // House: every seat starts with its sitting party (vacancies and
  // independents go to a random major party) and ~8% flip. Seats are called
  // after the state's polls close; slow-count states leave some uncalled.
  const auto& states = data_->states();
  for (size_t si = 0; si < states.size(); ++si) {
    const StateInfo& st = states[si];
    const uint64_t sh = HashString(st.abbr, seed_ ^ 0x40E5);
    const bool slow = SlowCountState(st.abbr);
    for (int seat = 0; seat < st.house_seats; ++seat) {
      const uint64_t h = Mix(sh + 31 * (seat + 1));
      std::string party = seat < st.house_dem                  ? "DEM"
                          : seat < st.house_dem + st.house_rep ? "REP"
                          : Unit(Mix(h + 1)) < 0.5             ? "DEM"
                                                               : "REP";
      if (Unit(Mix(h + 2)) < 0.08) party = party == "DEM" ? "REP" : "DEM";
      const double k = Kumaraswamy(Unit(Mix(h + 3)));
      const double span = slow ? 800 : 300;
      house_calls_.push_back({static_cast<float>(st.poll_close_minutes + 1 + span * k),
                              static_cast<int>(si), party});
    }
  }
  std::sort(house_calls_.begin(), house_calls_.end(),
            [](const HouseCall& a, const HouseCall& b) { return a.minute < b.minute; });
  PlanDeclarations();
}

// Walks the count in time order and decides when campaigns speak: the leader
// declares victory once their margin comfortably exceeds the votes still out
// (a looser rule than a media projection, so it can come early, and
// occasionally prematurely); the runner-up concedes some minutes later if
// the lead holds.
void SimulatedResultsSource::PlanDeclarations() {
  const auto& races = data_->races();
  std::vector<std::vector<int64_t>> votes(races.size());
  std::vector<int> counted(races.size(), 0), total_units(races.size(), 0);
  std::vector<int64_t> total_votes(races.size(), 0);
  std::vector<int> declared(races.size(), -1);
  std::vector<float> declared_at(races.size(), 0);
  std::vector<bool> conceded(races.size(), false);
  for (size_t r = 0; r < races.size(); ++r) votes[r].assign(races[r].candidates.size(), 0);
  for (const Station& st : stations_) total_units[st.race]++;

  auto leaders = [&](int r, int* first, int* second) {
    *first = *second = -1;
    for (int i = 0; i < static_cast<int>(votes[r].size()); ++i) {
      if (*first < 0 || votes[r][i] > votes[r][*first]) {
        *second = *first;
        *first = i;
      } else if (*second < 0 || votes[r][i] > votes[r][*second]) {
        *second = i;
      }
    }
  };
  for (const Station& st : stations_) {
    const int r = st.race;
    for (size_t i = 0; i < votes[r].size(); ++i) {
      votes[r][i] += votes_[st.votes_at + i];
      total_votes[r] += votes_[st.votes_at + i];
    }
    counted[r]++;
    int a, b;
    leaders(r, &a, &b);
    if (b < 0) continue;
    const int64_t margin = votes[r][a] - votes[r][b];
    const double per_unit = static_cast<double>(total_votes[r]) / counted[r];
    const double remaining = per_unit * (total_units[r] - counted[r]);
    const double progress = static_cast<double>(counted[r]) / total_units[r];
    const uint64_t h = HashString(races[r].id, seed_ ^ 0xDEC1);
    // Campaigns speak once their own tallies look safe: a clear lead with
    // around half of the stations in, well before media projections.
    if (declared[r] < 0 && progress >= 0.4 &&
        (margin > 0.3 * remaining || (progress >= 0.55 && margin > 0.04 * total_votes[r]))) {
      declared[r] = a;
      // Speech at campaign HQ 5-25 minutes later.
      declared_at[r] = st.minute + 5 + 20 * static_cast<float>(Unit(Mix(h)));
      declarations_.push_back({declared_at[r], r, a, Declaration::Type::kVictory});
    }
    if (declared[r] == a && !conceded[r] && st.minute > declared_at[r] &&
        margin > 1.05 * remaining) {
      conceded[r] = true;
      declarations_.push_back({st.minute + 3 + 15 * static_cast<float>(Unit(Mix(h + 1))), r, b,
                               Declaration::Type::kConcede});
    }
  }
  std::sort(declarations_.begin(), declarations_.end(),
            [](const PlannedDeclaration& x, const PlannedDeclaration& y) { return x.minute < y.minute; });
}

void SimulatedResultsSource::SeekClock(double minutes) {
  clock_ = std::clamp(minutes, 0.0, kCountMinutes);
  emitted_reported_ = SIZE_MAX;  // force a new snapshot
  emitted_declared_ = SIZE_MAX;
  emitted_called_ = SIZE_MAX;
}

void SimulatedResultsSource::set_speed(double s) {
  s = std::clamp(s, kMinSpeed, kMaxSpeed);
  speed_ = std::exp2(std::round(std::log2(s)));
}

std::string SimulatedResultsSource::ClockLabel(double minutes) {
  return NightClockLabel(minutes);
}

std::shared_ptr<const ResultsSnapshot> SimulatedResultsSource::Poll(double now) {
  if (last_now_ >= 0 && !paused_ && clock_ < kCountMinutes) {
    clock_ = std::min(kCountMinutes, clock_ + (now - last_now_) * speed_ / 60.0);
  }
  last_now_ = now;
  // Like a real feed: publish at most ~5 times per second, and only when new
  // stations have reported.
  if (now - last_emit_ < 0.2 && emitted_reported_ != SIZE_MAX) return nullptr;
  const size_t reported = static_cast<size_t>(
      std::upper_bound(stations_.begin(), stations_.end(), clock_,
                       [](double t, const Station& s) { return t < s.minute; }) -
      stations_.begin());
  const size_t called = static_cast<size_t>(
      std::upper_bound(house_calls_.begin(), house_calls_.end(), clock_,
                       [](double t, const HouseCall& c) { return t < c.minute; }) -
      house_calls_.begin());
  const size_t declared = static_cast<size_t>(
      std::upper_bound(declarations_.begin(), declarations_.end(), clock_,
                       [](double t, const PlannedDeclaration& d) { return t < d.minute; }) -
      declarations_.begin());
  if (reported == emitted_reported_ && declared == emitted_declared_ &&
      called == emitted_called_) {
    return nullptr;
  }
  emitted_declared_ = declared;
  emitted_called_ = called;
  last_emit_ = now;
  emitted_reported_ = reported;
  auto snap = SnapshotAtClock(clock_);
  snap->version = ++version_;
  return snap;
}

std::string SimulatedResultsSource::Describe() const {
  char buf[96];
  std::snprintf(buf, sizeof buf, "SIMULATION %s ET ×%g%s", ClockLabel(clock_).c_str(), speed_,
                paused_ ? " (paused)" : "");
  return buf;
}

std::vector<int64_t> SimulatedResultsSource::InflowHistory(double bucket_minutes,
                                                           double until) const {
  std::vector<int64_t> out;
  const auto& races = data_->races();
  for (const Station& st : stations_) {
    if (st.minute > until) break;
    const size_t b = static_cast<size_t>(st.minute / bucket_minutes);
    if (out.size() <= b) out.resize(b + 1, 0);
    for (size_t i = 0; i < races[st.race].candidates.size(); ++i) out[b] += votes_[st.votes_at + i];
  }
  return out;
}

std::shared_ptr<ResultsSnapshot> SimulatedResultsSource::SnapshotAt(double p) const {
  return SnapshotAtClock(p * kCountMinutes);
}

std::shared_ptr<ResultsSnapshot> SimulatedResultsSource::SnapshotAtClock(double minutes) const {
  minutes = std::clamp(minutes, 0.0, kCountMinutes);
  auto snap = std::make_shared<ResultsSnapshot>();
  snap->simulated = true;
  snap->source = "SIMULATION - synthetic numbers, not real results";
  snap->updated_at = NightTimestamp(minutes);

  const auto& races = data_->races();
  // Every county appears (so progress denominators are right) even before
  // any of its batches report. Senate and governor races share counties.
  std::vector<std::vector<Tally*>> county_tally(races.size());
  for (const Station& st : stations_) {
    auto& slots = county_tally[st.race];
    if (slots.empty()) slots.assign(tree_->size(), nullptr);
    if (slots[st.village]) continue;
    const Race& race = races[st.race];
    Tally& t = snap->races[race.id][tree_->region(st.village).code];
    t.votes.assign(race.candidates.size(), 0);
    t.units_total = village_units_[st.village];
    t.has_data = true;
    slots[st.village] = &t;
  }
  size_t reported = 0;
  for (const Station& st : stations_) {
    if (st.minute > minutes) break;
    ++reported;
    Tally& t = *county_tally[st.race][st.village];
    for (size_t i = 0; i < t.votes.size(); ++i) t.votes[i] += votes_[st.votes_at + i];
    t.eligible += st.eligible;
    t.ballots_cast += st.cast;
    t.units_counted += 1;
  }
  for (const StateInfo& st : data_->states()) {
    if (st.house_seats > 0) snap->house[st.fips].seats = st.house_seats;
  }
  for (const HouseCall& c : house_calls_) {
    if (c.minute > minutes) break;
    snap->house[data_->states()[c.state].fips].won[c.party]++;
  }
  for (const PlannedDeclaration& d : declarations_) {
    if (d.minute > minutes) break;
    const Race& race = races[d.race];
    snap->declarations.push_back({race.id, race.candidates[d.candidate].id, d.type,
                                  NightTimestamp(d.minute)});
  }
  snap->status = reported == 0                ? ResultsStatus::kPreElection
                 : reported == stations_.size() ? ResultsStatus::kFinal
                                                : ResultsStatus::kCounting;
  return snap;
}

}  // namespace usa::election
