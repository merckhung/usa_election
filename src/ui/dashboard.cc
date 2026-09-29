#include "src/ui/dashboard.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>

#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkGradient.h"
#include "src/election/events.h"

namespace usa::ui {

using election::Candidate;
using election::Party;
using election::Race;
using election::ResultsStatus;
using election::Tally;

namespace {

constexpr SkColor kPanel = SkColorSetARGB(222, 10, 20, 33);
constexpr SkColor kPanelBorder = SkColorSetARGB(40, 255, 255, 255);
constexpr SkColor kText = SkColorSetRGB(242, 245, 248);
constexpr SkColor kText2 = SkColorSetRGB(160, 176, 195);
constexpr SkColor kText3 = SkColorSetRGB(110, 126, 146);
constexpr SkColor kAccent = SkColorSetRGB(245, 197, 66);
constexpr SkColor kTrack = SkColorSetARGB(60, 255, 255, 255);
constexpr uint32_t kNeutral = 0xFF2B3D52;
constexpr uint32_t kUncalled = 0xFF3A4A5E;

SkPaint Fill(SkColor c) {
  SkPaint p;
  p.setAntiAlias(true);
  p.setColor(c);
  return p;
}

void Panel(SkCanvas* c, const SkRect& r, float radius) {
  c->drawRRect(SkRRect::MakeRectXY(r, radius, radius), Fill(kPanel));
  SkPaint border = Fill(kPanelBorder);
  border.setStyle(SkPaint::kStroke_Style);
  border.setStrokeWidth(1);
  c->drawRRect(SkRRect::MakeRectXY(r.makeInset(0.5f, 0.5f), radius, radius), border);
}

void Bar(SkCanvas* c, const SkRect& r, float fraction, SkColor color, SkColor track = kTrack) {
  const float rad = r.height() * 0.5f;
  c->drawRRect(SkRRect::MakeRectXY(r, rad, rad), Fill(track));
  fraction = std::clamp(fraction, 0.f, 1.f);
  if (fraction <= 0) return;
  SkRect f = r;
  f.fRight = r.fLeft + std::max(r.height(), r.width() * fraction);
  c->drawRRect(SkRRect::MakeRectXY(f, rad, rad), Fill(color));
}

std::string Percent(double v, int decimals = 1) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.*f%%", decimals, v * 100.0);
  return buf;
}


struct Countdown {
  int days = 0;
  bool voting = false;
  bool after_polls = false;
};

Countdown ComputeCountdown(const std::string& date, std::chrono::system_clock::time_point now) {
  Countdown cd;
  int y = 0, mo = 0, d = 0;
  if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &mo, &d) != 3) return cd;
  std::tm tm{};
  tm.tm_year = y - 1900;
  tm.tm_mon = mo - 1;
  tm.tm_mday = d;
  // First polls open 06:00 ET (11:00 UTC; EST after Nov 1); the last close
  // at 01:00 ET the next day (Alaska's Aleutians).
  const std::time_t open_utc = timegm(&tm) + 11 * 3600;
  const std::time_t close_utc = open_utc + 19 * 3600;
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  if (t < open_utc) {
    cd.days = static_cast<int>((open_utc - t + 86399) / 86400);
  } else if (t < close_utc) {
    cd.voting = true;
  } else {
    cd.after_polls = true;
  }
  return cd;
}

const Race* RaceFor(const DashboardModel& m, int region) {
  return region >= 0 ? m.results->RaceForRegion(region) : nullptr;
}

const election::StateInfo* StateFor(const DashboardModel& m, int region) {
  if (region < 0) return nullptr;
  const geo::Region& r = m.tree->region(region);
  return r.state_code.empty() ? nullptr : m.data->State(r.state_code);
}

std::string SqMi(float km2) {
  return FormatThousands(static_cast<int64_t>(std::llround(km2 * 0.386102)));
}

bool HasVotes(const Tally& t) { return t.TotalVotes() > 0; }

}  // namespace

const char* ColorModeName(Lang lang, ColorMode m) {
  switch (m) {
    case ColorMode::kLeader: return Tr(lang, "mode.leader");
    case ColorMode::kProgress: return Tr(lang, "mode.progress");
    case ColorMode::kTurnout: return Tr(lang, "mode.turnout");
    default: return "";
  }
}

uint32_t MixColor(uint32_t a, uint32_t b, float t) {
  t = std::clamp(t, 0.f, 1.f);
  auto ch = [&](int shift) {
    const float x = ((a >> shift) & 0xFF) * (1 - t) + ((b >> shift) & 0xFF) * t;
    return static_cast<uint32_t>(std::lround(x)) << shift;
  };
  return 0xFF000000u | ch(16) | ch(8) | ch(0);
}

float Luminance(uint32_t c) {
  return (0.299f * ((c >> 16) & 0xFF) + 0.587f * ((c >> 8) & 0xFF) + 0.114f * (c & 0xFF)) / 255.f;
}

DashboardLayout Dashboard::Layout(int width, int height) {
  DashboardLayout l;
  l.scale = std::clamp(std::min(width / 1600.f, height / 900.f), 0.7f, 2.5f);
  l.panel_x = width - (430 + 16) * l.scale;
  return l;
}

SeatCount CountSeats(const DashboardModel& m, const std::string& office) {
  SeatCount sc;
  const auto& info = m.data->info();
  auto status_of = [&](const Race& race) -> const RaceStatus* {
    if (!m.race_status) return nullptr;
    auto it = m.race_status->find(race.id);
    return it == m.race_status->end() ? nullptr : &it->second;
  };
  if (office == election::kHouse) {
    sc.total = info.house.total;
    sc.majority = info.house.majority;
    sc.races = sc.total;
    const election::HouseTally t = m.results->House(0);
    sc.solid = t.won;
    sc.called = t.Called();
    if (sc.called == 0) {
      // Before any call: the sitting delegations, faded.
      for (const election::StateInfo& st : m.data->states()) {
        sc.leading["DEM"] += st.house_dem;
        sc.leading["REP"] += st.house_rep;
        if (st.house_other) sc.leading["IND"] += st.house_other;
      }
    }
    return sc;
  }
  if (office == election::kSenate) {
    sc.total = info.senate.total;
    sc.majority = info.senate.majority;
    for (const auto& [party, n] : info.senate.holdovers) sc.solid[info.senate.CaucusOf(party)] += n;
  } else {
    sc.total = static_cast<int>(m.data->states().size()) - 1;  // 50 governors (no DC)
    sc.majority = sc.total / 2 + 1;
    for (const election::StateInfo& st : m.data->states()) {
      if (st.governor_party.empty() || m.data->RaceForState(st.fips, election::kGovernor)) continue;
      sc.solid[st.governor_party]++;
    }
  }
  for (const Race* race : m.data->RacesFor(office)) {
    ++sc.races;
    const RaceStatus* rs = status_of(*race);
    if (rs && rs->called >= 0) {
      sc.solid[race->candidates[rs->called].party]++;
      ++sc.called;
      continue;
    }
    const int state = m.tree->FindByCode(race->state_code);
    if (state < 0) continue;
    const Tally& t = m.results->RaceTally(*race, state);
    if (HasVotes(t)) {
      sc.leading[race->candidates[t.Leader()].party]++;
    } else if (m.results->snapshot().status == ResultsStatus::kPreElection) {
      sc.leading[race->incumbent.party]++;  // seats up tonight, by holder (faded)
    }
  }
  return sc;
}

uint32_t Dashboard::RegionColor(const DashboardModel& m, int region, float* strength) {
  *strength = 0;
  const geo::Region& r = m.tree->region(region);
  if (m.office == election::kHouse) {
    // States by House seats called (the map has no district lines).
    const election::StateInfo* st = StateFor(m, region);
    if (!st || st->house_seats <= 0) return kNeutral;
    const election::HouseTally t = m.results->House(region);
    const int called = t.Called();
    const float county_dim = r.level == geo::Level::kState ? 1.f : 0.8f;
    if (m.mode == ColorMode::kProgress) {
      *strength = static_cast<float>(called) / st->house_seats * county_dim;
      return 0xFFF5A623;
    }
    if (called == 0) {
      *strength = 0.3f * county_dim;
      return m.data->party(st->house_dem >= st->house_rep ? "DEM" : "REP").color;
    }
    const int dem = t.Won("DEM"), rep = t.Won("REP");
    const float lean = static_cast<float>(std::abs(dem - rep)) / std::max(1, st->house_seats);
    *strength = std::clamp(0.4f + lean * 0.6f + 0.2f * called / st->house_seats, 0.4f, 1.f) *
                county_dim;
    if (dem == rep) return 0xFF8E5CC8;
    return m.data->party(dem > rep ? "DEM" : "REP").color;
  }
  const Race* race = RaceFor(m, region);
  if (!race) return kNeutral;
  const Tally& t = m.results->RaceTally(*race, region);
  switch (m.mode) {
    case ColorMode::kProgress:
      if (t.units_total <= 0) return kNeutral;
      *strength = static_cast<float>(t.Progress());
      return 0xFFF5A623;
    case ColorMode::kTurnout: {
      if (t.eligible <= 0 || t.ballots_cast <= 0) return kNeutral;
      *strength = std::clamp(static_cast<float>((t.Turnout() - 0.3) / 0.4), 0.05f, 1.f);
      return 0xFF2EC4B6;
    }
    default: {
      if (HasVotes(t)) {
        const std::vector<int> rank = t.Ranking();
        const double margin =
            t.Share(rank[0]) - (rank.size() > 1 ? t.Share(rank[1]) : 0.0);
        *strength = std::clamp(0.45f + static_cast<float>(margin) * 2.5f, 0.45f, 1.f);
        return m.data->party(race->candidates[rank[0]].party).color;
      }
      // Before counting: tint by the incumbent's party.
      *strength = r.level == geo::Level::kState ? 0.42f : 0.3f;
      return m.data->party(race->incumbent.party).color;
    }
  }
}

void Dashboard::Render(const DashboardModel& m, uint8_t* pixels) {
  const SkImageInfo info = SkImageInfo::MakeN32Premul(m.width, m.height);
  std::unique_ptr<SkCanvas> canvas =
      SkCanvas::MakeRasterDirect(info, pixels, static_cast<size_t>(m.width) * 4);
  SkCanvas* c = canvas.get();
  c->clear(SK_ColorTRANSPARENT);
  L_ = Localizer(m.lang);
  fonts_->SetJapanese(m.lang == Lang::kJa);
  const DashboardLayout layout = Layout(m.width, m.height);
  s_ = layout.scale;

  UpdateLive(m);
  hits_.clear();
  DrawInsets(c, m);
  DrawRings(c, m);
  DrawLabels(c, m);
  DrawCallouts(c, m);
  DrawChartView(c, m);  // secondary views slide over the map
  DrawHeader(c, m);
  DrawLeftColumn(c, m);
  const float margin = 16 * s_;
  const SkRect panel = SkRect::MakeLTRB(layout.panel_x, margin, m.width - margin,
                                        m.height - margin - 26 * s_);
  if (m.office == election::kHouse) {
    DrawHousePanel(c, m, panel);
  } else if (m.tree->region(m.focus).level == geo::Level::kNation) {
    DrawNationPanel(c, m, panel);
  } else {
    DrawRacePanel(c, m, panel);
  }
  DrawTimeline(c, m);
  DrawChartTabs(c, m);
  DrawPip(c, m);
  DrawFooter(c, m);
  DrawToasts(c, m);
  DrawFloaters(c);
  DrawNotice(c, m);
  if (chart_t_ < 0.3f) DrawTooltip(c, m);
  if (m.show_help) DrawHelp(c, m);
}

void Dashboard::DrawHeader(SkCanvas* c, const DashboardModel& m) {
  const float x = 18 * s_;
  float y = 44 * s_;
  const auto& info = m.data->info();
  const std::string title = L_.T("app.title");
  DrawText(c, title, x, y, fonts_->Bold(28 * s_), kText);
  float cx = x + TextWidth(fonts_->Bold(28 * s_), title) + 12 * s_;

  // Status chip.
  const auto& snap = m.results->snapshot();
  const std::string et = std::string(" ") + L_.T("timeline.et");
  std::string status;
  uint32_t status_color;
  if (snap.simulated) {
    status = L_.T("status.simulation");
    const std::string clock = election::TimeLabel(snap.updated_at);
    if (!clock.empty() && snap.status != ResultsStatus::kPreElection) status += " · " + clock + et;
    status_color = 0xFFD64545;
  } else if (snap.status == ResultsStatus::kPreElection) {
    const Countdown cd = ComputeCountdown(info.date, m.now);
    status = cd.voting        ? std::string(L_.T("status.voting"))
             : cd.after_polls ? std::string(L_.T("status.awaiting"))
                              : Fmt(L_.T("status.countdown"), {std::to_string(cd.days)});
    status_color = 0xFF3A6EA5;
  } else if (snap.status == ResultsStatus::kCounting) {
    status = L_.T("status.counting");
    const std::string clock = election::TimeLabel(snap.updated_at);
    if (!clock.empty()) status += " · " + clock + et;
    status_color = 0xFFE08E0B;
  } else {
    status = L_.T("status.final");
    status_color = 0xFF2E9E5B;
  }
  Chip(c, fonts_, status, cx, y - 22 * s_, 26 * s_, status_color, s_);

  y += 24 * s_;
  const int senate = static_cast<int>(m.data->RacesFor(election::kSenate).size());
  const int governors = static_cast<int>(m.data->RacesFor(election::kGovernor).size());
  DrawText(c, Fmt(L_.T("header.subtitle"),
                  {info.date, std::to_string(senate), std::to_string(governors)}),
           x, y, fonts_->Regular(14 * s_), kText2);

  // Breadcrumb + office tabs.
  y += 30 * s_;
  const std::vector<int> path = m.tree->Path(m.focus);
  float bx = x;
  for (size_t i = 0; i < path.size(); ++i) {
    const bool last = i + 1 == path.size();
    const SkFont f = last ? fonts_->Bold(19 * s_) : fonts_->Regular(19 * s_);
    std::string name = L_.RegionName(m.tree->region(path[i]));
    if (i == 0 && path.size() > 2) name = "US";
    bx += DrawText(c, name, bx, y, f, last ? kAccent : kText2);
    if (path[i] == m.pinned) bx += DrawText(c, " ★", bx, y, fonts_->Regular(15 * s_), kAccent);
    if (!last) bx += DrawText(c, "  ›  ", bx, y, fonts_->Regular(19 * s_), kText3);
  }
  DrawOfficeTabs(c, m, std::max(bx + 18 * s_, 250 * s_), y - 18 * s_);
}

void Dashboard::DrawOfficeTabs(SkCanvas* c, const DashboardModel& m, float x, float y) {
  const SkFont f = fonts_->Bold(12.5f * s_);
  const float h = 24 * s_;
  const char* offices[] = {election::kSenate, election::kGovernor, election::kHouse};
  float total = 0;
  std::vector<float> widths;
  for (const char* o : offices) {
    widths.push_back(TextWidth(f, L_.OfficeName(o)) + 20 * s_);
    total += widths.back() + 3 * s_;
  }
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(x - 3 * s_, y - 3 * s_, total + 3 * s_, h + 6 * s_),
                                   10 * s_, 10 * s_),
               Fill(SkColorSetARGB(200, 10, 20, 33)));
  float tx = x;
  for (size_t i = 0; i < std::size(offices); ++i) {
    const bool active = m.office == offices[i];
    const SkRect r = SkRect::MakeXYWH(tx, y, widths[i], h);
    if (active) c->drawRRect(SkRRect::MakeRectXY(r, 8 * s_, 8 * s_), Fill(SkColorSetRGB(58, 110, 165)));
    DrawText(c, L_.OfficeName(offices[i]), r.centerX(), r.fTop + 16.5f * s_, f,
             active ? SK_ColorWHITE : kText2, Align::kCenter);
    DashboardHit hit;
    hit.kind = DashboardHit::Kind::kOffice;
    hit.office = offices[i];
    AddHit(r, hit);
    tx += widths[i] + 3 * s_;
  }
  DrawText(c, L_.T("office.hint"), tx + 6 * s_, y + 16.5f * s_, fonts_->Regular(11 * s_), kText3);
}

void Dashboard::DrawPowerBar(SkCanvas* c, const DashboardModel& m, SkRect bar, int total,
                             int majority, const std::map<std::string, int>& solid,
                             const std::map<std::string, int>& leading, const std::string& key) {
  (void)majority;
  c->drawRRect(SkRRect::MakeRectXY(bar, 4 * s_, 4 * s_), Fill(kUncalled));
  if (total <= 0) return;
  const float unit = bar.width() / total;
  auto get = [](const std::map<std::string, int>& mp, const std::string& k) {
    auto it = mp.find(k);
    return it == mp.end() ? 0 : it->second;
  };
  c->save();
  c->clipRRect(SkRRect::MakeRectXY(bar, 4 * s_, 4 * s_), true);
  // Left: DEM solid, DEM leading, then other parties; right: REP.
  float left = bar.fLeft;
  auto seg_left = [&](const std::string& party, int n, float alpha, const std::string& k) {
    const float w = Approach(anim_, key + k, n * unit, 5);
    if (w <= 0.2f) return;
    SkPaint p = Fill(m.data->party(party).color);
    p.setAlphaf(alpha);
    c->drawRect(SkRect::MakeXYWH(left, bar.fTop, w, bar.height()), p);
    left += w;
  };
  seg_left("DEM", get(solid, "DEM"), 1.f, "ds");
  seg_left("DEM", get(leading, "DEM"), 0.45f, "dl");
  for (const auto& [party, n] : solid) {
    if (party != "DEM" && party != "REP") seg_left(party, n, 1.f, party + "s");
  }
  for (const auto& [party, n] : leading) {
    if (party != "DEM" && party != "REP") seg_left(party, n, 0.45f, party + "l");
  }
  float right = bar.fRight;
  auto seg_right = [&](int n, float alpha, const std::string& k) {
    const float w = Approach(anim_, key + k, n * unit, 5);
    if (w <= 0.2f) return;
    SkPaint p = Fill(m.data->party("REP").color);
    p.setAlphaf(alpha);
    c->drawRect(SkRect::MakeXYWH(right - w, bar.fTop, w, bar.height()), p);
    right -= w;
  };
  seg_right(get(solid, "REP"), 1.f, "rs");
  seg_right(get(leading, "REP"), 0.45f, "rl");
  c->restore();
  // Control line in the middle.
  SkPaint mid = Fill(SK_ColorWHITE);
  mid.setStrokeWidth(1.5f * s_);
  c->drawLine(bar.centerX(), bar.fTop - 3 * s_, bar.centerX(), bar.fBottom + 3 * s_, mid);
}

void Dashboard::DrawBalanceOfPower(SkCanvas* c, const DashboardModel& m, float x, float y,
                                   float w, float* height) {
  const float pad = 14 * s_;
  const float block = 64 * s_;
  const float h = pad * 2 + 26 * s_ + block * 3;
  *height = h;
  Panel(c, SkRect::MakeXYWH(x, y, w, h), 12 * s_);
  float ty = y + pad + 16 * s_;
  DrawText(c, L_.T("bop.title"), x + pad, ty, fonts_->Bold(16 * s_), kText);
  ty += 10 * s_;
  const auto& senate = m.data->info().senate;
  struct Row {
    const char* office;
    const char* title;
  };
  for (const Row& row : {Row{election::kSenate, "bop.senate"}, Row{election::kHouse, "bop.house"},
                         Row{election::kGovernor, "bop.governors"}}) {
    const SeatCount sc = CountSeats(m, row.office);
    const bool active = m.office == row.office;
    const SkRect hit_r = SkRect::MakeXYWH(x + 4 * s_, ty + 2 * s_, w - 8 * s_, block - 4 * s_);
    if (active) {
      c->drawRRect(SkRRect::MakeRectXY(hit_r, 8 * s_, 8 * s_), Fill(SkColorSetARGB(28, 255, 255, 255)));
    }
    DashboardHit hit;
    hit.kind = DashboardHit::Kind::kOffice;
    hit.office = row.office;
    AddHit(hit_r, hit);
    ty += 20 * s_;
    DrawText(c, L_.T(row.title), x + pad, ty, fonts_->Bold(12.5f * s_), active ? kAccent : kText2);
    const std::string called =
        row.office == election::kHouse
            ? Fmt(L_.T("bop.house_called"), {std::to_string(sc.called)})
            : Fmt(L_.T("bop.called"), {std::to_string(sc.called), std::to_string(sc.races)});
    DrawText(c, called, x + w - pad, ty, fonts_->Regular(11 * s_), kText3, Align::kRight);
    ty += 8 * s_;
    const SkRect bar = SkRect::MakeXYWH(x + pad, ty, w - 2 * pad, 12 * s_);
    DrawPowerBar(c, m, bar, sc.total, sc.majority, sc.solid, sc.leading, std::string("bop:") + row.office);
    ty += 28 * s_;
    auto count = [](const std::map<std::string, int>& mp, const std::string& k) {
      auto it = mp.find(k);
      return it == mp.end() ? 0 : it->second;
    };
    const int dem = count(sc.solid, "DEM"), rep = count(sc.solid, "REP");
    const int dem_l = count(sc.leading, "DEM"), rep_l = count(sc.leading, "REP");
    const election::Party& D = m.data->party("DEM");
    const election::Party& R = m.data->party("REP");
    const int dem_shown = static_cast<int>(std::lround(Roll(std::string("bopn:d") + row.office, dem)));
    const int rep_shown = static_cast<int>(std::lround(Roll(std::string("bopn:r") + row.office, rep)));
    float lx = x + pad;
    lx += DrawText(c, L_.PartyShort(D) + " " + std::to_string(dem_shown), lx, ty,
                   fonts_->Bold(14 * s_), D.color | 0xFF000000);
    if (dem_l) {
      DrawText(c, " +" + std::to_string(dem_l), lx, ty, fonts_->Regular(11.5f * s_), kText3);
    }
    int others = 0;
    for (const auto& [party, n] : sc.solid) {
      if (party != "DEM" && party != "REP") others += n;
    }
    if (others) {
      DrawText(c, L_.PartyShort(m.data->party("IND")) + " " + std::to_string(others), x + w / 2,
               ty, fonts_->Bold(12 * s_), m.data->party("IND").color | 0xFF000000, Align::kCenter);
    }
    std::string right = L_.PartyShort(R) + " " + std::to_string(rep_shown);
    const float rw = DrawText(c, right, x + w - pad, ty, fonts_->Bold(14 * s_), R.color | 0xFF000000,
                              Align::kRight);
    if (rep_l) {
      DrawText(c, "+" + std::to_string(rep_l) + " ", x + w - pad - rw, ty,
               fonts_->Regular(11.5f * s_), kText3, Align::kRight);
    }
    ty += block - 56 * s_;
    if (row.office == election::kSenate && !senate.tie_breaker_name.empty() && active) {
      // (VP tie-breaker noted in the Senate chart; keep the card compact.)
    }
  }
}

void Dashboard::DrawStateCard(SkCanvas* c, const DashboardModel& m, float x, float y, float w,
                              float* height) {
  const float pad = 14 * s_;
  const geo::Region& focus = m.tree->region(m.focus);
  const election::StateInfo* st = StateFor(m, m.focus);
  const Race* race = st && m.office != election::kHouse ? m.data->RaceForState(st->fips, m.office)
                                                        : nullptr;
  const float h = (race ? 178 : 150) * s_;
  *height = h;
  Panel(c, SkRect::MakeXYWH(x, y, w, h), 12 * s_);
  float ty = y + pad + 16 * s_;
  DrawText(c, L_.T("state.title"), x + pad, ty, fonts_->Bold(16 * s_), kText);
  if (st) {
    DrawText(c, st->abbr, x + w - pad, ty, fonts_->Bold(16 * s_), kText3, Align::kRight);
  }
  ty += 24 * s_;
  std::string facts = SqMi(focus.area_km2) + " sq mi";
  if (focus.level == geo::Level::kState && !focus.children.empty()) {
    facts += " · " + Fmt(L_.T("state.counties"), {std::to_string(focus.children.size())});
  }
  DrawText(c, facts, x + pad, ty, fonts_->Regular(13 * s_), kText2);
  if (!st) return;
  ty += 20 * s_;
  DrawText(c, Fmt(L_.T("state.polls"), {st->poll_close_label}), x + pad, ty,
           fonts_->Regular(13 * s_), kText2);
  // House delegation (before) + governor's party.
  ty += 26 * s_;
  float cx = x + pad;
  cx += DrawText(c, std::string(L_.T("state.house")) + " ", cx, ty, fonts_->Regular(12.5f * s_), kText3);
  const election::Party& D = m.data->party("DEM");
  const election::Party& R = m.data->party("REP");
  if (st->house_dem) {
    cx += Chip(c, fonts_, L_.PartyShort(D) + " " + std::to_string(st->house_dem), cx, ty - 14 * s_,
               19 * s_, D.color, s_) + 4 * s_;
  }
  if (st->house_rep) {
    cx += Chip(c, fonts_, L_.PartyShort(R) + " " + std::to_string(st->house_rep), cx, ty - 14 * s_,
               19 * s_, R.color, s_) + 4 * s_;
  }
  if (st->house_other) {
    const election::Party& I = m.data->party("IND");
    Chip(c, fonts_, L_.PartyShort(I) + " " + std::to_string(st->house_other), cx, ty - 14 * s_,
         19 * s_, I.color, s_);
  }
  ty += 26 * s_;
  cx = x + pad;
  cx += DrawText(c, std::string(L_.T("state.governor")) + " ", cx, ty, fonts_->Regular(12.5f * s_), kText3);
  if (!st->governor_party.empty()) {
    const election::Party& g = m.data->party(st->governor_party);
    cx += Chip(c, fonts_, L_.PartyShort(g), cx, ty - 14 * s_, 19 * s_, g.color, s_) + 6 * s_;
  }
  if (!m.data->RaceForState(st->fips, election::kGovernor)) {
    DrawText(c, L_.T("state.no_race"), cx, ty, fonts_->Regular(12 * s_), kText3);
  }
  if (race) {
    ty += 28 * s_;
    const election::Party& p = m.data->party(race->incumbent.party);
    cx = x + pad;
    cx += DrawText(c, std::string(L_.T("area.incumbent")) + " ", cx, ty,
                   fonts_->Regular(13 * s_), kText2);
    cx += DrawText(c, Ellipsize(fonts_->Bold(14 * s_), race->incumbent.name, w * 0.42f), cx, ty,
                   fonts_->Bold(14 * s_), kText) + 8 * s_;
    cx += Chip(c, fonts_, L_.PartyShort(p), cx, ty - 14 * s_, 19 * s_, p.color, s_) + 5 * s_;
    const std::string& status = race->incumbent.status;
    const char* key = status == "term-limited" ? "area.term_limited"
                      : status == "running"    ? "area.running"
                                               : "area.retiring";
    if (cx + ChipWidth(fonts_, L_.T(key), 19 * s_) < x + w - pad) {
      Chip(c, fonts_, L_.T(key), cx, ty - 14 * s_, 19 * s_, 0xFF9FB0C3, s_, true);
    }
  }
}

void Dashboard::DrawLeftColumn(SkCanvas* c, const DashboardModel& m) {
  const float x = 16 * s_;
  const float w = 330 * s_;
  float y = 150 * s_;
  const float bottom = m.height - 140 * s_;
  if (m.show_news) {
    const float news_h = feed_.empty() ? bottom - y : (bottom - y) * 0.68f;
    DrawNewsPanel(c, m, x, y, w, news_h);
    DrawFeed(c, m, x, y + news_h + 10 * s_, w, bottom - y - news_h - 10 * s_);
    return;
  }
  float h = 0;
  if (m.tree->region(m.focus).level == geo::Level::kNation) {
    DrawBalanceOfPower(c, m, x, y, w, &h);
  } else {
    DrawStateCard(c, m, x, y, w, &h);
  }
  y += h + 12 * s_;
  // Live event feed fills the rest of the column (above the timeline).
  DrawFeed(c, m, x, y, w, bottom - y);
}

void Dashboard::DrawNationPanel(SkCanvas* c, const DashboardModel& m, SkRect panel) {
  Panel(c, panel, 14 * s_);
  const float pad = 16 * s_;
  const float x = panel.fLeft + pad;
  const float w = panel.width() - 2 * pad;
  float y = panel.fTop + pad + 20 * s_;
  const std::vector<const Race*> races = m.data->RacesFor(m.office);
  int total_candidates = 0;
  for (const Race* r : races) total_candidates += static_cast<int>(r->candidates.size());

  DrawText(c, Fmt(L_.T("nation.title"), {L_.OfficeName(m.office), std::to_string(races.size())}),
           x, y, fonts_->Bold(22 * s_), kText);
  DrawText(c, Fmt(L_.T("nation.candidates"), {std::to_string(total_candidates)}), x + w, y,
           fonts_->Regular(13 * s_), kText2, Align::kRight);
  y += 20 * s_;

  // Chamber-wide bar (holdovers / governors not up + called + leading).
  bool any_votes = false;
  std::vector<std::pair<const Race*, int>> leaders;  // race, leading candidate or -1
  for (const Race* r : races) {
    const int state = m.tree->FindByCode(r->state_code);
    const Tally& t = state >= 0 ? m.results->RaceTally(*r, state) : Tally{};
    const int lead = HasVotes(t) ? t.Leader() : -1;
    any_votes |= lead >= 0;
    leaders.push_back({r, lead});
  }
  const SeatCount sc = CountSeats(m, m.office);
  const bool final = m.results->snapshot().status == ResultsStatus::kFinal;
  DrawText(c,
           L_.T(any_votes ? (final ? "nation.seats_final" : "nation.seats_leading")
                          : "nation.seats_incumbent"),
           x, y + 4 * s_, fonts_->Regular(12 * s_), kText3);
  DrawText(c, Fmt(L_.T("seats.majority"), {std::to_string(sc.majority)}), x + w, y + 4 * s_,
           fonts_->Regular(12 * s_), kText3, Align::kRight);
  y += 12 * s_;
  // Before counting, `leading` holds the seats up tonight by current holder.
  const std::map<std::string, int>& solid = sc.solid;
  const std::map<std::string, int>& leading = sc.leading;
  DrawPowerBar(c, m, SkRect::MakeXYWH(x, y, w, 16 * s_), sc.total, sc.majority, solid, leading,
               "nation:" + m.office);
  y += 30 * s_;
  std::map<std::string, int> totals = solid;
  for (const auto& [p, n] : leading) totals[p] += n;
  std::vector<std::pair<std::string, int>> ordered(totals.begin(), totals.end());
  std::sort(ordered.begin(), ordered.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
  float cx = x;
  for (const auto& [party, n] : ordered) {
    const Party& p = m.data->party(party);
    cx += Chip(c, fonts_, L_.PartyShort(p) + " " + std::to_string(n), cx, y - 14 * s_, 20 * s_,
               p.color, s_) + 6 * s_;
  }
  y += 14 * s_;

  // Race list.
  const float row_h = std::max(20 * s_, (panel.fBottom - pad - y) / std::max<size_t>(1, races.size()));
  const float avatar = std::min(row_h - 3 * s_, 24 * s_);
  const int hover_state = m.hover >= 0 ? m.tree->AncestorAt(m.hover, geo::Level::kState) : -1;
  for (const auto& [race, lead] : leaders) {
    const int state = m.tree->FindByCode(race->state_code);
    const float mid_y = y + row_h / 2;
    const SkRect row = SkRect::MakeXYWH(x - 6 * s_, y, w + 12 * s_, row_h);
    if (state >= 0 && state == hover_state) {
      c->drawRRect(SkRRect::MakeRectXY(row, 6 * s_, 6 * s_), Fill(SkColorSetARGB(40, 255, 255, 255)));
    }
    // Flash the row when the race changes (new leader, declaration, ...).
    if (auto it = flash_.find("row:" + race->id); it != flash_.end() && it->second > 0) {
      c->drawRRect(SkRRect::MakeRectXY(row, 6 * s_, 6 * s_),
                   Fill(SkColorSetARGB(static_cast<U8CPU>(90 * it->second), 245, 197, 66)));
    }
    AddHit(row, {DashboardHit::Kind::kRace, ChartKind::kMap, race});
    const Party& inc = m.data->party(race->incumbent.party);
    c->drawRect(SkRect::MakeXYWH(x - 2 * s_, mid_y - avatar * 0.4f, 3 * s_, avatar * 0.8f),
                Fill(lead >= 0 ? m.data->party(race->candidates[lead].party).color : inc.color));
    const float name_w = 38 * s_;
    DrawText(c, race->state_abbr + (race->special ? "*" : ""), x + 6 * s_, mid_y + 5 * s_,
             fonts_->Bold(13.5f * s_), kText);
    const float list_x = x + name_w;
    if (lead < 0) {
      // Before counting: the full set of candidates.
      float ax = list_x;
      const float step =
          std::min(avatar + 3 * s_, (x + w - list_x - 30 * s_) / race->candidates.size());
      for (const Candidate& cand : race->candidates) {
        avatars_->Draw(c, cand, m.data->party(cand.party), ax, mid_y - avatar / 2, avatar);
        ax += step;
      }
      std::string names;
      for (size_t k = 0; k < race->candidates.size() && k < 2; ++k) {
        names += (names.empty() ? "" : " vs ") + race->candidates[k].name;
      }
      DrawText(c, Ellipsize(fonts_->Regular(12 * s_), names, x + w - ax - 30 * s_), ax + 4 * s_,
               mid_y + 4.5f * s_, fonts_->Regular(12 * s_), kText2);
      DrawText(c, Fmt(L_.T("nation.count"), {std::to_string(race->candidates.size())}), x + w,
               mid_y + 4.5f * s_, fonts_->Regular(11.5f * s_), kText3, Align::kRight);
    } else {
      const Candidate& cand = race->candidates[lead];
      const Party& p = m.data->party(cand.party);
      const Tally& t = m.results->RaceTally(*race, state);
      avatars_->Draw(c, cand, p, list_x, mid_y - avatar / 2, avatar);
      DrawText(c, Ellipsize(fonts_->Bold(13 * s_), L_.CandidateName(cand), 118 * s_),
               list_x + avatar + 6 * s_, mid_y + 5 * s_, fonts_->Bold(13 * s_), kText);
      const float bx = list_x + avatar + 128 * s_;
      const float bw = w - (bx - x) - 54 * s_;
      const double share = Roll("share:" + race->id, t.Share(lead) * 10000) / 10000;
      Bar(c, SkRect::MakeXYWH(bx, mid_y - 4 * s_, bw, 8 * s_), static_cast<float>(share),
          p.color);
      DrawText(c, Percent(share), x + w, mid_y + 5 * s_, fonts_->Bold(12.5f * s_), kText,
               Align::kRight);
      if (const RaceStatus* st = StatusOf(m, *race)) {
        if (st->called == lead) {
          DrawStamp(c, list_x + avatar - 2 * s_, mid_y - avatar * 0.25f, avatar * 0.8f,
                    st->called_age);
        } else if (st->declared == lead) {
          Chip(c, fonts_, L_.T("chip.declared"), bx + bw - ChipWidth(fonts_, L_.T("chip.declared"), 15 * s_),
               mid_y - 18 * s_, 15 * s_, 0xFF9B59B6, s_);
        }
      }
      // Counting progress as a thin line under the row.
      Bar(c, SkRect::MakeXYWH(bx, mid_y + 6 * s_, bw, 2 * s_), static_cast<float>(t.Progress()),
          kAccent, SkColorSetARGB(25, 255, 255, 255));
    }
    y += row_h;
  }
}

void Dashboard::DrawHousePanel(SkCanvas* c, const DashboardModel& m, SkRect panel) {
  Panel(c, panel, 14 * s_);
  const float pad = 16 * s_;
  const float x = panel.fLeft + pad;
  const float w = panel.width() - 2 * pad;
  float y = panel.fTop + pad + 20 * s_;
  const geo::Region& focus = m.tree->region(m.focus);
  const election::StateInfo* focus_state = StateFor(m, m.focus);
  const election::Party& D = m.data->party("DEM");
  const election::Party& R = m.data->party("REP");
  if (focus_state) {
    DrawText(c, Fmt(L_.T("house.state_title"), {L_.StateName(*focus_state)}), x, y,
             fonts_->Bold(21 * s_), kText);
  } else {
    DrawText(c, L_.T("house.title"), x, y, fonts_->Bold(22 * s_), kText);
  }
  y += 22 * s_;
  const election::HouseTally t = m.results->House(focus_state ? m.focus : 0);
  const int seats = focus_state ? focus_state->house_seats : m.data->info().house.total;
  const int majority = focus_state ? seats / 2 + 1 : m.data->info().house.majority;
  DrawText(c, Fmt(L_.T("house.called"), {std::to_string(t.Called()), std::to_string(seats)}), x, y,
           fonts_->Regular(13 * s_), kAccent);
  if (!focus_state) {
    DrawText(c, Fmt(L_.T("seats.majority"), {std::to_string(majority)}), x + w, y,
             fonts_->Regular(13 * s_), kText3, Align::kRight);
  }
  y += 12 * s_;
  DrawPowerBar(c, m, SkRect::MakeXYWH(x, y, w, 18 * s_), seats, majority, t.won, {},
               "house:" + std::to_string(focus_state ? m.focus : 0));
  y += 44 * s_;
  const int dem = static_cast<int>(std::lround(Roll("house:d" + focus.code, t.Won("DEM"))));
  const int rep = static_cast<int>(std::lround(Roll("house:r" + focus.code, t.Won("REP"))));
  DrawText(c, std::to_string(dem), x, y, fonts_->Bold(34 * s_), D.color | 0xFF000000);
  DrawText(c, L_.PartyName(D), x, y + 18 * s_, fonts_->Regular(12 * s_), kText2);
  DrawText(c, std::to_string(rep), x + w, y, fonts_->Bold(34 * s_), R.color | 0xFF000000, Align::kRight);
  DrawText(c, L_.PartyName(R), x + w, y + 18 * s_, fonts_->Regular(12 * s_), kText2, Align::kRight);
  std::string before;
  if (focus_state) {
    before = L_.PartyShort(D) + " " + std::to_string(focus_state->house_dem) + " · " +
             L_.PartyShort(R) + " " + std::to_string(focus_state->house_rep);
  } else {
    const auto& b = m.data->info().house.before;
    auto get = [&](const char* k) { auto it = b.find(k); return it == b.end() ? 0 : it->second; };
    before = L_.PartyShort(D) + " " + std::to_string(get("DEM")) + " · " + L_.PartyShort(R) + " " +
             std::to_string(get("REP")) + " · " + L_.PartyShort(m.data->party("IND")) + " " +
             std::to_string(get("IND"));
  }
  DrawText(c, Fmt(L_.T("house.before"), {before}), x + w / 2, y - 6 * s_, fonts_->Regular(12 * s_),
           kText3, Align::kCenter);
  y += 40 * s_;

  // States, two columns: abbreviation + called seats bar.
  std::vector<const election::StateInfo*> states;
  for (const election::StateInfo& st : m.data->states()) {
    if (st.house_seats > 0 && (!focus_state || st.fips == focus_state->fips)) states.push_back(&st);
  }
  if (focus_state) {
    // One state: a seat-by-seat strip.
    const int n = focus_state->house_seats;
    const int cols = std::min(n, 10);
    const float cell = std::min(34 * s_, w / cols);
    const int dem_won = t.Won("DEM"), rep_won = t.Won("REP");
    for (int i = 0; i < n; ++i) {
      const float cx = x + (i % cols) * cell + cell / 2;
      const float cy = y + (i / cols) * cell + cell / 2;
      uint32_t col = kUncalled;
      if (i < dem_won) col = D.color;
      else if (i >= n - rep_won) col = R.color;
      c->drawCircle(cx, cy, cell * 0.38f, Fill(col));
    }
    y += ((n + cols - 1) / cols) * cell + 20 * s_;
    DrawText(c, Ellipsize(fonts_->Regular(11.5f * s_), L_.T("house.note"), w), x, y,
             fonts_->Regular(11.5f * s_), kText3);
    return;
  }
  const int rows = static_cast<int>((states.size() + 1) / 2);
  const float row_h = std::max(16 * s_, (panel.fBottom - pad - 18 * s_ - y) / std::max(1, rows));
  const float col_w = (w - 16 * s_) / 2;
  for (size_t i = 0; i < states.size(); ++i) {
    const election::StateInfo& st = *states[i];
    const float cx = x + (i / rows) * (col_w + 16 * s_);
    const float cy = y + (i % rows) * row_h + row_h / 2;
    const int state_region = m.tree->FindByCode(st.fips);
    const election::HouseTally ht = m.results->House(state_region >= 0 ? state_region : 0);
    DrawText(c, st.abbr, cx, cy + 4.5f * s_, fonts_->Bold(12 * s_), kText);
    const SkRect bar = SkRect::MakeXYWH(cx + 28 * s_, cy - 4 * s_, col_w - 58 * s_, 8 * s_);
    c->drawRRect(SkRRect::MakeRectXY(bar, 3 * s_, 3 * s_), Fill(kUncalled));
    const float unit = bar.width() / st.house_seats;
    c->drawRect(SkRect::MakeXYWH(bar.fLeft, bar.fTop, unit * ht.Won("DEM"), bar.height()), Fill(D.color));
    c->drawRect(SkRect::MakeXYWH(bar.fRight - unit * ht.Won("REP"), bar.fTop, unit * ht.Won("REP"),
                                 bar.height()),
                Fill(R.color));
    DrawText(c, std::to_string(ht.Called()) + "/" + std::to_string(st.house_seats), cx + col_w,
             cy + 4 * s_, fonts_->Regular(10.5f * s_), kText3, Align::kRight);
    DashboardHit hit;
    hit.kind = DashboardHit::Kind::kState;
    hit.state = st.fips;
    AddHit(SkRect::MakeXYWH(cx, cy - row_h / 2, col_w, row_h), hit);
  }
  DrawText(c, Ellipsize(fonts_->Regular(11 * s_), L_.T("house.note"), w), x, panel.fBottom - pad,
           fonts_->Regular(11 * s_), kText3);
}

void Dashboard::DrawRacePanel(SkCanvas* c, const DashboardModel& m, SkRect panel) {
  Panel(c, panel, 14 * s_);
  const geo::Region& focus = m.tree->region(m.focus);
  const Race* race = m.data->RaceForState(focus.state_code, m.office);
  const float pad = 16 * s_;
  const float x = panel.fLeft + pad;
  const float w = panel.width() - 2 * pad;
  float y = panel.fTop + pad + 22 * s_;
  if (!race) {
    DrawText(c, Ellipsize(fonts_->Bold(18 * s_), Fmt(L_.T("race.none"), {L_.OfficeName(m.office)}), w),
             x, y, fonts_->Bold(18 * s_), kText);
    // Point at the other office on the ballot here, if any.
    const char* other = m.office == election::kSenate ? election::kGovernor : election::kSenate;
    if (const Race* alt = m.data->RaceForState(focus.state_code, other)) {
      y += 30 * s_;
      DrawText(c, L_.RaceTitle(*alt) + "  →  O", x, y, fonts_->Regular(14 * s_), kAccent);
    }
    return;
  }
  const float chip_w = race->special ? ChipWidth(fonts_, "special", 20 * s_) : 0;
  const SkFont title_font = fonts_->Bold(22 * s_);
  DrawText(c, Ellipsize(title_font, L_.RaceTitle(*race), w - 8 * s_), x, y, title_font, kText);
  (void)chip_w;
  y += 22 * s_;
  const std::string scope = focus.level == geo::Level::kState
                                ? std::string(L_.T("race.scope_state"))
                                : Fmt(L_.T("race.scope_region"), {L_.RegionName(focus)});
  DrawText(c, scope, x, y, fonts_->Regular(14 * s_), kAccent);
  y += 18 * s_;

  const Tally& t = m.results->RaceTally(*race, m.focus);
  const bool counting = HasVotes(t);
  const RaceStatus* status = StatusOf(m, *race);
  // Rolling (animated) vote counts for this view.
  const std::string key_base = race->id + ":" + std::to_string(m.focus) + ":";
  std::vector<double> shown(race->candidates.size(), 0);
  std::vector<double> added(race->candidates.size(), 0);
  double shown_total = 0;
  for (size_t i = 0; i < shown.size(); ++i) {
    const double target = i < t.votes.size() ? static_cast<double>(t.votes[i]) : 0;
    shown[i] = Roll(key_base + std::to_string(i), target, &added[i]);
    shown_total += shown[i];
  }
  // Progress + turnout.
  Bar(c, SkRect::MakeXYWH(x, y, w, 6 * s_), static_cast<float>(t.Progress()), kAccent);
  y += 22 * s_;
  std::string progress =
      t.units_total > 0 ? Fmt(L_.T("race.progress"), {std::to_string(t.units_counted),
                                                        std::to_string(t.units_total),
                                                        Percent(t.Progress(), 0)})
                        : std::string(L_.T("race.not_started"));
  DrawText(c, progress, x, y, fonts_->Regular(13 * s_), kText2);
  if (t.ballots_cast > 0 && t.eligible > 0) {
    DrawText(c, Fmt(L_.T("race.turnout"), {Percent(t.Turnout())}), x + w, y,
             fonts_->Regular(13 * s_), kText2, Align::kRight);
  }
  y += 14 * s_;

  // Candidate cards.
  std::vector<int> order(race->candidates.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
  if (counting) {
    order = t.Ranking();
  }
  const float avail = panel.fBottom - pad - 40 * s_ - y;
  const float card_h = std::min(92 * s_, avail / std::max<size_t>(1, order.size()));
  const float photo = std::min(card_h - 16 * s_, 62 * s_);
  const bool final = m.results->snapshot().status == ResultsStatus::kFinal;
  const float list_top = y;
  // Cards slide to their new rank (drawn back-to-front so movers stay on top).
  std::vector<std::pair<float, size_t>> draw_order;
  for (size_t rank = 0; rank < order.size(); ++rank) {
    const float target_y = list_top + rank * card_h;
    const float cy_anim = Approach(card_y_, key_base + "y" + std::to_string(order[rank]), target_y, 6.f);
    draw_order.push_back({std::fabs(cy_anim - target_y), rank});
  }
  std::sort(draw_order.begin(), draw_order.end());
  for (const auto& [moving, rank] : draw_order) {
    const int i = order[rank];
    y = card_y_[key_base + "y" + std::to_string(i)];
    const Candidate& cand = race->candidates[i];
    const Party& p = m.data->party(cand.party);
    const SkRect card = SkRect::MakeXYWH(x - 6 * s_, y + 4 * s_, w + 12 * s_, card_h - 6 * s_);
    const bool leader = counting && rank == 0;
    const bool conceded = status && status->conceded == i;
    c->drawRRect(SkRRect::MakeRectXY(card, 10 * s_, 10 * s_),
                 Fill(leader ? SkColorSetARGB(46, 245, 197, 66) : SkColorSetARGB(18, 255, 255, 255)));
    // Gold glow when this candidate just took the lead / was projected.
    const float flash = flash_.count(race->id + ":" + std::to_string(i))
                            ? flash_[race->id + ":" + std::to_string(i)]
                            : 0.f;
    if (flash > 0) {
      SkPaint glow = Fill(SkColorSetARGB(static_cast<U8CPU>(220 * flash), 245, 197, 66));
      glow.setStyle(SkPaint::kStroke_Style);
      glow.setStrokeWidth((1.5f + 3 * flash) * s_);
      c->drawRRect(SkRRect::MakeRectXY(card.makeOutset(flash * 3 * s_, flash * 3 * s_), 12 * s_, 12 * s_),
                   glow);
    }
    if (conceded) c->saveLayerAlphaf(nullptr, 0.55f);
    const float cy = card.fTop + (card.height() - photo) / 2;
    avatars_->Draw(c, cand, p, x + 2 * s_, cy, photo);
    // Party letter badge.
    const std::string no = cand.party.substr(0, 1);
    const float br = 10 * s_;
    c->drawCircle(x + 2 * s_ + photo - br * 0.6f, cy + photo - br * 0.6f, br, Fill(SK_ColorWHITE));
    DrawText(c, no, x + 2 * s_ + photo - br * 0.6f, cy + photo - br * 0.6f + 4.5f * s_,
             fonts_->Bold(13 * s_), SK_ColorBLACK, Align::kCenter);

    const float tx = x + photo + 14 * s_;
    const float name_y = card.fTop + card.height() * 0.36f;
    float nx = tx;
    const SkFont name_font = fonts_->Bold(std::min(17 * s_, card_h * 0.24f));
    const int64_t votes = static_cast<int64_t>(std::llround(shown[i]));
    const std::string votes_text = counting ? FormatThousands(votes) : "0";
    // Name and badges must stay clear of the vote count on the right.
    const float limit = x + w - TextWidth(fonts_->Bold(19 * s_), votes_text) - 10 * s_;
    nx += DrawText(c, Ellipsize(name_font, L_.CandidateName(cand), limit - nx - 50 * s_), nx,
                   name_y, name_font, kText) + 8 * s_;
    const float chip_h = 19 * s_;
    auto chip = [&](const std::string& text, uint32_t color, bool outline) {
      if (nx + ChipWidth(fonts_, text, chip_h) > limit) return;
      nx += Chip(c, fonts_, text, nx, name_y - 15 * s_, chip_h, color, s_, outline) + 5 * s_;
    };
    chip(L_.PartyShort(p), p.color, false);
    if (leader) {
      const bool called = status && status->called == i;
      chip(L_.T(final || called ? "race.elected" : "race.leading"),
           final || called ? 0xFF2E9E5B : 0xFFE08E0B, false);
    }
    if (cand.incumbent) chip(L_.T("race.incumbent"), kAccent, true);
    if (status && status->declared == i && status->called != i) {
      chip(L_.T("chip.declared"), 0xFF9B59B6, false);
    }
    if (conceded) chip(L_.T("chip.conceded"), 0xFF7F8C8D, false);
    std::string sub = L_.CandidateAltName(cand, *m.data);
    if (!cand.endorsed_by.empty()) {
      std::string backers;
      for (const auto& e : cand.endorsed_by) {
        backers += (backers.empty() ? "" : L_.ListSeparator()) + L_.PartyShort(m.data->party(e));
      }
      sub = Fmt(L_.T("race.backed"), {backers}) + " · " + sub;
    }
    const float sub_w = DrawText(c, Ellipsize(fonts_->Regular(12 * s_), sub, w - photo - 200 * s_),
                                 tx, name_y + 17 * s_, fonts_->Regular(12 * s_), kText3);
    DrawNewsCounts(c, m, cand.id, tx + sub_w + 8 * s_, name_y + 17 * s_);

    const float bar_y = card.fBottom - 16 * s_;
    const float share = shown_total > 0 ? static_cast<float>(shown[i] / shown_total) : 0.f;
    Bar(c, SkRect::MakeXYWH(tx, bar_y, w - photo - 16 * s_, 7 * s_), share, p.color);
    // Votes, right aligned.
    DrawText(c, votes_text, x + w, name_y, fonts_->Bold(19 * s_), kText, Align::kRight);
    DrawText(c, counting ? Percent(share) : "—", x + w, name_y + 18 * s_, fonts_->Regular(13 * s_),
             kText2, Align::kRight);
    // "+N" floaters: batches arriving in quick succession are merged.
    const std::string fkey = key_base + "f" + std::to_string(i);
    float_pending_[fkey] += added[i];
    float& cooldown = float_cooldown_[fkey];
    cooldown -= dt_;
    if (float_pending_[fkey] > 0 && cooldown <= 0) {
      floaters_.push_back({"+" + FormatThousands(static_cast<int64_t>(float_pending_[fkey])), p.color,
                           x + w - TextWidth(fonts_->Bold(19 * s_), votes_text) - 8 * s_, name_y});
      float_pending_[fkey] = 0;
      cooldown = 0.9f;
    }
    if (conceded) c->restore();
    if (status && status->called == i) {
      DrawStamp(c, x + 2 * s_ + photo * 0.78f, cy + photo * 0.3f,
                std::min(50 * s_, card.height() * 0.7f), status->called_age);
    }
  }
  y = list_top + order.size() * card_h;
  // Footnote.
  y = panel.fBottom - pad - 4 * s_;
  const std::string note =
      counting ? Fmt(L_.T("race.totals"), {FormatThousands(t.TotalVotes()), FormatThousands(t.eligible)})
               : std::string(L_.T("race.order_note"));
  DrawText(c, Ellipsize(fonts_->Regular(12 * s_), note, w), x, y, fonts_->Regular(12 * s_), kText3);
}

void Dashboard::DrawLabels(SkCanvas* c, const DashboardModel& m) {
  std::vector<MapLabel> labels = m.labels;
  std::sort(labels.begin(), labels.end(),
            [](const MapLabel& a, const MapLabel& b) { return a.priority > b.priority; });
  std::vector<SkRect> placed;
  const float panel_x = Layout(m.width, m.height).panel_x;
  for (const MapLabel& l : labels) {
    const geo::Region& r = m.tree->region(l.region);
    const bool county = r.level == geo::Level::kState;
    const bool hovered = l.region == m.hover;
    const SkFont f = county || hovered ? fonts_->Bold((county ? 15 : 13.5f) * s_)
                                       : fonts_->Regular(13 * s_);
    const std::string name = L_.RegionName(r);
    const float tw = TextWidth(f, name);
    const SkRect rect = SkRect::MakeXYWH(l.x - tw / 2 - 3 * s_, l.y - 14 * s_, tw + 6 * s_,
                                         18 * s_);
    if (rect.fRight > panel_x || rect.fLeft < 0 || rect.fTop < 0 || rect.fBottom > m.height) {
      continue;
    }
    bool overlaps = false;
    for (const SkRect& p : placed) {
      if (SkRect::Intersects(p, rect)) {
        overlaps = true;
        break;
      }
    }
    if (overlaps && !hovered) continue;
    placed.push_back(rect);
    SkPaint halo;
    halo.setAntiAlias(true);
    halo.setStyle(SkPaint::kStroke_Style);
    halo.setStrokeWidth(3.5f * s_);
    halo.setStrokeJoin(SkPaint::kRound_Join);
    halo.setColor(SkColorSetARGB(200, 5, 12, 22));
    const float x0 = l.x - tw / 2;
    c->drawSimpleText(name.data(), name.size(), SkTextEncoding::kUTF8, x0, l.y, f, halo);
    DrawText(c, name, x0, l.y, f, hovered ? kAccent : kText);
  }
}

void Dashboard::DrawInsets(SkCanvas* c, const DashboardModel& m) {
  for (const InsetFrame& f : m.insets) {
    SkPathBuilder b;
    b.moveTo(f.x[0], f.y[0]);
    for (int k = 1; k < 4; ++k) b.lineTo(f.x[k], f.y[k]);
    b.close();
    SkPaint p = Fill(SkColorSetARGB(150, 160, 176, 195));
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(1.2f * s_);
    const float intervals[2] = {6 * s_, 5 * s_};
    p.setPathEffect(SkDashPathEffect::Make(intervals, 0));
    c->drawPath(b.detach(), p);
    DrawText(c, L_.T("inset"), f.x[3] + 4 * s_, f.y[3] - 5 * s_, fonts_->Regular(10.5f * s_),
             SkColorSetARGB(170, 160, 176, 195));
  }
}

void Dashboard::DrawTooltip(SkCanvas* c, const DashboardModel& m) {
  if (m.hover < 0) return;
  const geo::Region& r = m.tree->region(m.hover);
  const Race* race = RaceFor(m, m.hover);
  const bool house = m.office == election::kHouse;
  const election::StateInfo* st = StateFor(m, m.hover);
  const float pad = 10 * s_;
  const float w = 260 * s_;
  const char* level = L_.T(r.level == geo::Level::kState    ? "level.state"
                           : r.level == geo::Level::kCounty ? "level.county"
                                                            : "level.precinct");
  std::string title = L_.RegionName(r);
  if (r.level != geo::Level::kState && st) {
    title = IsCjk(m.lang) ? L_.StateName(*st) + " " + title : title + ", " + st->abbr;
  }
  std::string sub = house ? L_.OfficeName(election::kHouse) : race ? L_.RaceTitle(*race) : "";
  float h = pad * 2 + 40 * s_;
  const Tally* t = race ? &m.results->RaceTally(*race, m.hover) : nullptr;
  const bool has_votes = t && HasVotes(*t);
  if (has_votes) h += 22 * s_ * std::min<size_t>(3, race->candidates.size()) + 8 * s_;
  else h += 20 * s_;
  float x = m.mouse_x + 18 * s_;
  float y = m.mouse_y + 18 * s_;
  if (x + w > Layout(m.width, m.height).panel_x - 8 * s_) x = m.mouse_x - w - 18 * s_;
  if (y + h > m.height - 30 * s_) y = m.mouse_y - h - 12 * s_;
  const SkRect box = SkRect::MakeXYWH(x, y, w, h);
  Panel(c, box, 10 * s_);
  float ty = y + pad + 16 * s_;
  const float level_w = TextWidth(fonts_->Regular(11.5f * s_), level) + 8 * s_;
  DrawText(c, Ellipsize(fonts_->Bold(16 * s_), title, w - 2 * pad - level_w), x + pad, ty,
           fonts_->Bold(16 * s_), kText);
  DrawText(c, level, x + w - pad, ty, fonts_->Regular(11.5f * s_), kText3, Align::kRight);
  ty += 18 * s_;
  DrawText(c, Ellipsize(fonts_->Regular(11.5f * s_), sub, w - 2 * pad), x + pad, ty,
           fonts_->Regular(11.5f * s_), kText3);
  if (house && st) {
    ty += 22 * s_;
    const election::HouseTally ht = m.results->House(m.hover);
    const Party& D = m.data->party("DEM");
    const Party& R = m.data->party("REP");
    float cx = x + pad;
    cx += Chip(c, fonts_, L_.PartyShort(D) + " " + std::to_string(ht.Won("DEM")), cx, ty - 14 * s_,
               19 * s_, D.color, s_) + 5 * s_;
    cx += Chip(c, fonts_, L_.PartyShort(R) + " " + std::to_string(ht.Won("REP")), cx, ty - 14 * s_,
               19 * s_, R.color, s_) + 8 * s_;
    DrawText(c, Fmt(L_.T("house.called"), {std::to_string(ht.Called()), std::to_string(st->house_seats)}),
             cx, ty, fonts_->Regular(12 * s_), kText2);
  } else if (has_votes) {
    ty += 8 * s_;
    const std::vector<int> rank = t->Ranking();
    for (size_t k = 0; k < std::min<size_t>(3, rank.size()); ++k) {
      const Candidate& cand = race->candidates[rank[k]];
      const Party& p = m.data->party(cand.party);
      ty += 22 * s_;
      c->drawCircle(x + pad + 5 * s_, ty - 5 * s_, 5 * s_, Fill(p.color));
      DrawText(c, Ellipsize(fonts_->Regular(13.5f * s_), L_.CandidateName(cand), w - 90 * s_),
               x + pad + 16 * s_, ty, fonts_->Regular(13.5f * s_), kText);
      DrawText(c, Percent(t->Share(rank[k])), x + w - pad, ty, fonts_->Bold(13.5f * s_), kText,
               Align::kRight);
    }
  } else {
    ty += 22 * s_;
    const bool can_enter = !r.children.empty();
    DrawText(c, L_.T(race ? (can_enter ? "tip.no_votes_enter" : "tip.no_votes")
                     : st ? "tip.no_race"
                          : "tip.enter"),
             x + pad, ty, fonts_->Regular(12.5f * s_), kText2);
  }
}

void Dashboard::DrawFooter(SkCanvas* c, const DashboardModel& m) {
  const float y = m.height - 12 * s_;
  const SkFont f = fonts_->Regular(12 * s_);
  const float lang_w = DrawText(c, std::string("◐ ") + LangNativeName(m.lang), 16 * s_, y,
                              fonts_->Bold(12 * s_), kAccent) + 12 * s_;
  DrawText(c,
           Ellipsize(f, Fmt(L_.T("footer.controls"), {ColorModeName(m.lang, m.mode), L_.OfficeName(m.office)}),
                     m.width * 0.62f - lang_w),
           16 * s_ + lang_w, y, f, kText2);
  char buf[96];
  std::snprintf(buf, sizeof buf, "%.0f fps · ", m.fps);
  std::string right = buf + m.gpu + " · " + m.source;
  if (!m.source_error.empty()) right = "⚠ " + m.source_error + " · " + right;
  DrawText(c, Ellipsize(f, right, m.width * 0.34f), m.width - 16 * s_, y, f,
           m.source_error.empty() ? kText3 : SkColorSetRGB(255, 120, 100), Align::kRight);

  // Legend (bottom-left, above the footer).
  float lx = 16 * s_;
  const float ly = m.height - 40 * s_;
  if (m.mode == ColorMode::kLeader) {
    std::vector<std::string> shown = {"DEM", "REP"};
    for (const Race& r : m.data->races()) {
      for (const Candidate& cand : r.candidates) {
        if (std::find(shown.begin(), shown.end(), cand.party) == shown.end()) {
          shown.push_back(cand.party);
        }
      }
    }
    for (const Party& p : m.data->parties()) {
      if (std::find(shown.begin(), shown.end(), p.code) == shown.end()) continue;
      c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(lx, ly - 10 * s_, 12 * s_, 12 * s_),
                                       3 * s_, 3 * s_),
                   Fill(p.color));
      lx += 16 * s_;
      lx += DrawText(c, L_.PartyShort(p), lx, ly, fonts_->Regular(12 * s_), kText2) + 12 * s_;
    }
  } else {
    const uint32_t hi = m.mode == ColorMode::kProgress ? 0xFFF5A623 : 0xFF2EC4B6;
    const uint32_t lo = kNeutral;
    const SkColor4f cols[2] = {SkColor4f::FromColor(lo), SkColor4f::FromColor(hi)};
    const SkPoint pts[2] = {{lx, 0}, {lx + 160 * s_, 0}};
    SkPaint p;
    p.setAntiAlias(true);
    p.setShader(SkShaders::LinearGradient(
        pts, SkGradient(SkGradient::Colors(cols, {}, SkTileMode::kClamp), {})));
    c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(lx, ly - 10 * s_, 160 * s_, 12 * s_), 4 * s_,
                                     4 * s_),
                 p);
    const char* lo_txt = m.mode == ColorMode::kTurnout ? "30%" : "0%";
    const char* hi_txt = m.mode == ColorMode::kTurnout ? "70%" : "100%";
    DrawText(c, lo_txt, lx, ly - 14 * s_, fonts_->Regular(11 * s_), kText3);
    DrawText(c, hi_txt, lx + 160 * s_, ly - 14 * s_, fonts_->Regular(11 * s_), kText3,
             Align::kRight);
    DrawText(c, ColorModeName(m.lang, m.mode), lx + 170 * s_, ly, fonts_->Regular(12 * s_), kText2);
  }
}

void Dashboard::DrawHelp(SkCanvas* c, const DashboardModel& m) {
  const float w = 520 * s_, h = 360 * s_;
  const float x = (Layout(m.width, m.height).panel_x - w) / 2, y = (m.height - h) / 2;
  Panel(c, SkRect::MakeXYWH(x, y, w, h), 14 * s_);
  const char* keys[] = {"help.title", "help.1", "help.2", "help.3", "help.4",
                        "help.5",     "help.6", "help.7", "help.8", "help.9"};
  float ty = y + 42 * s_;
  for (size_t i = 0; i < std::size(keys); ++i) {
    const SkFont f = i == 0 ? fonts_->Bold(20 * s_) : fonts_->Regular(15 * s_);
    DrawText(c, Ellipsize(f, L_.T(keys[i]), w - 48 * s_), x + 24 * s_, ty, f,
             i == 0 ? kAccent : kText);
    ty += i == 0 ? 38 * s_ : 30 * s_;
  }
}

}  // namespace usa::ui
