#include "src/election/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace usa::election {

bool IsBreaking(EventType t) {
  switch (t) {
    case EventType::kLeadChange:
    case EventType::kVictoryDeclared:
    case EventType::kConcession:
    case EventType::kCalled:
    case EventType::kIncumbentTrailing:
    case EventType::kSeatFlip:
    case EventType::kChamberControl:
      return true;
    default:
      return false;
  }
}

const char* EventKey(EventType t) {
  switch (t) {
    case EventType::kFirstReturns: return "first";
    case EventType::kLeadChange: return "lead_change";
    case EventType::kVictoryDeclared: return "victory";
    case EventType::kConcession: return "concede";
    case EventType::kCalled: return "called";
    case EventType::kIncumbentTrailing: return "incumbent_trailing";
    case EventType::kCloseRace: return "close";
    case EventType::kFinal: return "final";
    case EventType::kSeatFlip: return "flip";
    case EventType::kPollsClosed: return "polls_closed";
    case EventType::kChamberControl: return "control";
  }
  return "";
}

std::string TimeLabel(const std::string& updated_at) {
  const size_t t = updated_at.find('T');
  if (t == std::string::npos || updated_at.size() < t + 6) return "";
  return updated_at.substr(t + 1, 5);
}

double MinutesAfterClose(const std::string& updated_at) {
  int y = 0, mo = 0, d = 0, h = 0, m = 0;
  if (std::sscanf(updated_at.c_str(), "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &m) != 5) return -1;
  // Election day is 2026-11-03; later days count past midnight.
  const int day_offset = (y == 2026 && mo == 11) ? d - 3 : 0;
  return day_offset * 1440.0 + (h - kNightStartHour) * 60.0 + m;
}

std::string NightClockLabel(double minutes) {
  const int total = static_cast<int>(std::floor(minutes)) + kNightStartHour * 60;
  const int wrapped = ((total % 1440) + 1440) % 1440;
  char buf[16];
  std::snprintf(buf, sizeof buf, "%02d:%02d", wrapped / 60, wrapped % 60);
  return buf;
}

std::string NightTimestamp(double minutes) {
  const int total = static_cast<int>(std::floor(minutes)) + kNightStartHour * 60;
  const int day = 3 + (total >= 0 ? total / 1440 : -1);
  char buf[40];
  std::snprintf(buf, sizeof buf, "2026-11-%02dT%s:00-05:00", day, NightClockLabel(minutes).c_str());
  return buf;
}

bool EventTracker::Decided(const Tally& t) {
  if (t.units_total <= 0 || t.units_counted <= 0) return false;
  const std::vector<int> rank = t.Ranking();
  if (rank.size() < 2) return t.units_counted >= t.units_total;
  const int64_t margin = t.votes[rank[0]] - t.votes[rank[1]];
  if (margin <= 0) return false;
  if (t.units_counted >= t.units_total) return true;
  // Require a meaningful share of the count before projecting.
  if (t.Progress() < 0.25) return false;
  const double per_unit = static_cast<double>(t.TotalVotes()) / t.units_counted;
  const double remaining = per_unit * (t.units_total - t.units_counted);
  // Race call: the trailing candidate would need more than a 70/30 split of
  // every vote still out (60/40 once three quarters are in), with 15% extra
  // for turnout uncertainty.
  const double share = t.Progress() >= 0.75 ? kCallShareLate : kCallShare;
  return static_cast<double>(margin) > remaining * share * 1.15;
}

bool EventTracker::IsCalled(const std::string& race_id) const {
  auto it = races_.find(race_id);
  return it != races_.end() && it->second.called;
}

int EventTracker::CalledFor(const std::string& race_id) const {
  auto it = races_.find(race_id);
  return it != races_.end() && it->second.called ? it->second.called_for : -1;
}

void EventTracker::Reset() {
  has_baseline_ = false;
  regions_.clear();
  races_.clear();
  chamber_called_.clear();
  last_minute_ = -1e9;
}

EventBatch EventTracker::Update(ResultsView& view, const std::vector<Race>& races) {
  EventBatch batch;
  batch.baseline = !has_baseline_;
  const std::string time = TimeLabel(view.snapshot().updated_at);

  for (size_t race_index = 0; race_index < races.size(); ++race_index) {
    const Race& race = races[race_index];
    const int county = tree_->FindByCode(race.state_code);  // the state
    if (county < 0) continue;
    // State + counties (+ precincts when present) of this race.
    std::vector<int> regions = {county};
    for (int c : tree_->region(county).children) {
      regions.push_back(c);
      for (int p : tree_->region(c).children) regions.push_back(p);
    }
    for (int r : regions) {
      const Tally& t = view.RaceTally(race, r);
      const int64_t total = t.TotalVotes();
      const int leader = total > 0 ? t.Leader() : -1;
      RegionState& prev = regions_[(static_cast<uint64_t>(race_index) << 32) | static_cast<uint32_t>(r)];
      if (!batch.baseline) {
        if (total > prev.total) {
          batch.deltas.push_back({r, &race, total - prev.total});
          if (r == county) batch.votes_added += total - prev.total;
        }
        if (prev.leader >= 0 && leader >= 0 && leader != prev.leader) {
          batch.flips.push_back({r, &race, leader, prev.leader});
        }
      }
      const int64_t prev_total = prev.total;
      prev.total = total;
      prev.leader = leader;

      if (r != county) continue;
      RaceState& rs = races_[race.id];
      const std::vector<int> rank = t.Ranking();
      const int64_t margin =
          total > 0 && rank.size() > 1 ? t.votes[rank[0]] - t.votes[rank[1]] : total;
      const double progress = t.Progress();
      auto emit = [&](EventType type, int who, int other) {
        ElectionEvent e;
        e.type = type;
        e.race = &race;
        e.leader = who;
        e.previous = other;
        e.margin = margin;
        e.progress = progress;
        e.time_label = time;
        batch.events.push_back(e);
        return &batch.events.back();
      };
      int incumbent = -1;
      for (size_t i = 0; i < race.candidates.size(); ++i) {
        if (race.candidates[i].incumbent) incumbent = static_cast<int>(i);
      }
      // Declarations (from the feed) seen so far for this race.
      std::vector<const Declaration*> decls;
      for (const Declaration& d : view.snapshot().declarations) {
        if (d.race_id == race.id) decls.push_back(&d);
      }
      if (batch.baseline) {
        rs.called = Decided(t);
        if (rs.called) rs.called_for = leader;
        rs.final = t.units_total > 0 && t.units_counted >= t.units_total;
        rs.close_alerted = true;
        rs.incumbent_alerted = incumbent >= 0 && leader >= 0 && leader != incumbent;
        rs.declarations_seen = decls.size();
        rs.leader = leader;
        for (const Declaration* d : decls) {
          if (d->type == Declaration::Type::kVictory) rs.declared = race.CandidateIndex(d->candidate_id);
        }
        continue;
      }
      if (prev_total == 0 && total > 0) {
        emit(EventType::kFirstReturns, leader, -1);
        rs.leader = leader;
      }
      // Lead changes need a small margin (0.25% of votes, at least 50) so a
      // neck-and-neck race does not flip-flop with every county batch.
      if (rs.leader >= 0 && leader >= 0 && leader != rs.leader &&
          margin >= std::max<int64_t>(50, total / 400)) {
        ElectionEvent* e = emit(EventType::kLeadChange, leader, rs.leader);
        e->after_declaration = rs.declared == rs.leader;
        rs.leader = leader;
      }
      for (size_t k = rs.declarations_seen; k < decls.size(); ++k) {
        const int who = race.CandidateIndex(decls[k]->candidate_id);
        if (decls[k]->type == Declaration::Type::kVictory) {
          rs.declared = who;
          ElectionEvent* e = emit(EventType::kVictoryDeclared, who, -1);
          if (!TimeLabel(decls[k]->time).empty()) e->time_label = TimeLabel(decls[k]->time);
        } else {
          ElectionEvent* e = emit(EventType::kConcession, who, leader);
          if (!TimeLabel(decls[k]->time).empty()) e->time_label = TimeLabel(decls[k]->time);
        }
      }
      rs.declarations_seen = decls.size();
      if (!rs.incumbent_alerted && incumbent >= 0 && leader >= 0 && leader != incumbent &&
          progress >= 0.3) {
        rs.incumbent_alerted = true;
        emit(EventType::kIncumbentTrailing, leader, incumbent);
      }
      if (!rs.close_alerted && progress >= 0.7 && total > 0 && margin * 100 < total) {
        rs.close_alerted = true;
        emit(EventType::kCloseRace, leader, rank.size() > 1 ? rank[1] : -1);
      }
      if (!rs.called && Decided(t)) {
        rs.called = true;
        rs.called_for = leader;
        emit(EventType::kCalled, leader, -1);
        const std::string& winner_party = race.candidates[leader].party;
        if (!race.incumbent.party.empty() && winner_party != race.incumbent.party) {
          ElectionEvent* e = emit(EventType::kSeatFlip, leader, incumbent);
          e->office = race.office;
          e->party = winner_party;
          e->previous_party = race.incumbent.party;
        }
      }
      if (!rs.final && t.units_total > 0 && t.units_counted >= t.units_total) {
        rs.final = true;
        emit(EventType::kFinal, leader, -1);
      }
    }
  }
  NationalEvents(view, &batch);
  has_baseline_ = true;
  return batch;
}

void EventTracker::NationalEvents(ResultsView& view, EventBatch* batch) {
  if (!data_) return;
  const double minute = MinutesAfterClose(view.snapshot().updated_at);
  const std::string time = TimeLabel(view.snapshot().updated_at);
  // Poll closings crossed since the previous snapshot, one event per time.
  if (!batch->baseline && minute > last_minute_) {
    std::map<double, std::vector<std::string>> closing;
    for (const StateInfo& st : data_->states()) {
      if (st.poll_close_minutes > last_minute_ && st.poll_close_minutes <= minute) {
        closing[st.poll_close_minutes].push_back(st.abbr);
      }
    }
    for (auto& [at, states] : closing) {
      ElectionEvent e;
      e.type = EventType::kPollsClosed;
      e.states = std::move(states);
      e.time_label = NightClockLabel(at);
      batch->events.push_back(std::move(e));
    }
  }
  last_minute_ = std::max(last_minute_, minute);

  // Chamber control: Senate = holdovers (with caucus) + called races.
  auto announce = [&](const Chamber& chamber, const std::map<std::string, int>& seats) {
    if (chamber.majority <= 0 || chamber_called_[chamber.office]) return;
    for (const auto& [party, n] : seats) {
      const bool tie_break = chamber.office == kSenate && party == chamber.tie_breaker_party &&
                             n * 2 == chamber.total;
      if (n >= chamber.majority || tie_break) {
        chamber_called_[chamber.office] = true;
        if (batch->baseline) return;
        ElectionEvent e;
        e.type = EventType::kChamberControl;
        e.office = chamber.office;
        e.party = party;
        e.seats = n;
        e.time_label = time;
        batch->events.push_back(std::move(e));
        return;
      }
    }
  };
  const Chamber& senate = data_->info().senate;
  std::map<std::string, int> senate_seats;
  for (const auto& [party, n] : senate.holdovers) senate_seats[senate.CaucusOf(party)] += n;
  for (const Race& race : data_->races()) {
    if (race.office != kSenate) continue;
    const int who = CalledFor(race.id);
    if (who >= 0) senate_seats[race.candidates[who].party]++;
  }
  announce(senate, senate_seats);
  const HouseTally house = view.House(0);
  announce(data_->info().house, house.won);
}

}  // namespace usa::election
