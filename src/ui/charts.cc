// Secondary (non-map) chart views and the picture-in-picture "latest" window.
//
//   F2 Trend    vote share over 18:00-06:00 ET, lead changes, race call
//   F3 Seats    hemicycle of the office shown: Senate 100 (holdovers +
//               tonight's 35), House 435, Governors 50 (called solid,
//               leading hollow, uncalled grey), majority marker
//   F4 Margins  races sorted by margin, bar-chart race
//   F5 Parties  votes by party across the office's races (House: seats)
//   F6 Grid     mini dashboards with progress rings (House: by state)
//
// The map stays the primary view: charts slide in over it (the map keeps
// animating, dimmed, behind) and M / Esc / the Map tab return to it.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/effects/SkDashPathEffect.h"
#include "src/election/events.h"
#include "src/ui/dashboard.h"

namespace usa::ui {

using election::Candidate;
using election::Race;
using election::Tally;

namespace {

constexpr SkColor kText = SkColorSetRGB(242, 245, 248);
constexpr SkColor kText2 = SkColorSetRGB(160, 176, 195);
constexpr SkColor kText3 = SkColorSetRGB(110, 126, 146);
constexpr SkColor kGold = SkColorSetRGB(245, 197, 66);
constexpr SkColor kRed = SkColorSetRGB(214, 48, 49);
constexpr SkColor kGrid = SkColorSetARGB(40, 255, 255, 255);

SkPaint P(SkColor c, float alpha = 1) {
  SkPaint p;
  p.setAntiAlias(true);
  p.setColor(c);
  p.setAlphaf(std::clamp(alpha, 0.f, 1.f) * SkColorGetA(c) / 255.f);
  return p;
}

SkPaint Stroke(SkColor c, float w, float alpha = 1) {
  SkPaint p = P(c, alpha);
  p.setStyle(SkPaint::kStroke_Style);
  p.setStrokeWidth(w);
  p.setStrokeCap(SkPaint::kRound_Cap);
  p.setStrokeJoin(SkPaint::kRound_Join);
  return p;
}

float Ease(float t) {
  t = std::clamp(t, 0.f, 1.f);
  return 1 - (1 - t) * (1 - t) * (1 - t);
}

float EaseBack(float t) {  // slight overshoot for "pop"
  t = std::clamp(t, 0.f, 1.f);
  const float c1 = 1.70158f, c3 = c1 + 1;
  return 1 + c3 * std::pow(t - 1, 3.f) + c1 * std::pow(t - 1, 2.f);
}

std::string Pct(double v, int d = 1) {
  char buf[24];
  std::snprintf(buf, sizeof buf, "%.*f%%", d, v * 100);
  return buf;
}

std::string ShortCounty(const Localizer& L, const geo::Region& r, Lang lang) {
  (void)lang;
  return r.abbr.empty() ? L.RegionName(r) : r.abbr;
}

// Races shown by the race charts for an office (the House has no candidate
// races in the data: fall back to the Senate).
std::vector<const Race*> ChartRaces(const DashboardModel& m) {
  return m.data->RacesFor(m.office == election::kHouse ? election::kSenate : m.office);
}

struct RaceState {
  const Race* race = nullptr;
  int county = -1;
  const Tally* tally = nullptr;
  int leader = -1, second = -1;
  double margin = 0;  // share points
  int64_t margin_votes = 0;
  bool has_votes = false;
};

RaceState StateOf(const DashboardModel& m, const Race& race) {
  RaceState s;
  s.race = &race;
  s.county = m.tree->FindByCode(race.state_code);
  if (s.county < 0) return s;
  s.tally = &m.results->RaceTally(race, s.county);
  if (s.tally->TotalVotes() <= 0) return s;
  const std::vector<int> rank = s.tally->Ranking();
  s.has_votes = true;
  s.leader = rank[0];
  s.second = rank.size() > 1 ? rank[1] : -1;
  s.margin = s.tally->Share(s.leader) - (s.second >= 0 ? s.tally->Share(s.second) : 0);
  s.margin_votes = s.tally->votes[s.leader] - (s.second >= 0 ? s.tally->votes[s.second] : 0);
  return s;
}

}  // namespace

const char* ChartKey(ChartKind k) {
  switch (k) {
    case ChartKind::kTrend: return "chart.trend";
    case ChartKind::kSeats: return "chart.seats";
    case ChartKind::kMargins: return "chart.margins";
    case ChartKind::kParties: return "chart.parties";
    case ChartKind::kGrid: return "chart.grid";
    default: return "chart.map";
  }
}

DashboardHit Dashboard::HitTest(float x, float y) const {
  // Last added wins (drawn on top).
  for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
    if (it->first.contains(x, y)) return it->second;
  }
  return {};
}

bool Dashboard::Captures(float x, float y) const {
  if (pip_rect_.contains(x, y)) return true;
  for (const auto& [r, hit] : hits_) {
    if (hit.kind == DashboardHit::Kind::kChartTab && r.contains(x, y)) return true;
  }
  return chart_t_ > 0.5f && last_chart_area_.contains(x, y);
}

SkRect Dashboard::ChartArea(const DashboardModel& m) const {
  const float panel_x = Layout(m.width, m.height).panel_x;
  // Below the header/breadcrumb, between the side columns, above the tabs.
  return SkRect::MakeLTRB(362 * s_, 128 * s_, panel_x - 16 * s_, m.height - 180 * s_);
}

void Dashboard::DrawChartTabs(SkCanvas* c, const DashboardModel& m) {
  const SkRect area = ChartArea(m);
  const SkFont f = fonts_->Bold(12.5f * s_);
  const float h = 26 * s_;
  std::vector<std::pair<std::string, float>> labels;
  float total = 0;
  for (int k = 0; k < static_cast<int>(ChartKind::kCount); ++k) {
    const std::string key = k == 0 ? "M" : "F" + std::to_string(k + 1);
    std::string label = std::string(L_.T(ChartKey(static_cast<ChartKind>(k)))) + "  " + key;
    const float w = TextWidth(f, label) + 22 * s_;
    labels.push_back({label, w});
    total += w + 4 * s_;
  }
  float x = area.centerX() - total / 2;
  const float y = area.fBottom + 10 * s_;
  const SkRect bar = SkRect::MakeXYWH(x - 6 * s_, y - 4 * s_, total + 8 * s_, h + 8 * s_);
  c->drawRRect(SkRRect::MakeRectXY(bar, 12 * s_, 12 * s_), P(SkColorSetARGB(200, 10, 20, 33)));
  const int active = static_cast<int>(m.chart);
  float active_x = x, active_w = 0;
  for (int k = 0; k < static_cast<int>(labels.size()); ++k) {
    const SkRect r = SkRect::MakeXYWH(x, y, labels[k].second, h);
    if (k == active) {
      active_x = x;
      active_w = labels[k].second;
    }
    AddHit(r, {DashboardHit::Kind::kChartTab, static_cast<ChartKind>(k), nullptr});
    x += labels[k].second + 4 * s_;
  }
  // Sliding highlight behind the active tab.
  const float hx = Approach(anim_, "tab_x", active_x, 10.f);
  const float hw = Approach(anim_, "tab_w", active_w, 10.f);
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(hx, y, hw, h), 9 * s_, 9 * s_),
               P(active == 0 ? SkColorSetRGB(58, 110, 165) : SkColorSetRGB(224, 142, 11)));
  x = area.centerX() - total / 2;
  for (int k = 0; k < static_cast<int>(labels.size()); ++k) {
    DrawText(c, labels[k].first, x + 11 * s_, y + 17.5f * s_, f, k == active ? SK_ColorWHITE : kText2);
    x += labels[k].second + 4 * s_;
  }
}

void Dashboard::DrawChartView(SkCanvas* c, const DashboardModel& m) {
  // Open/close transition; switching between charts restarts the build-in.
  if (m.chart != ChartKind::kMap && m.chart != shown_chart_) {
    shown_chart_ = m.chart;
    chart_age_ = 0;
  }
  chart_t_ += ((m.chart != ChartKind::kMap ? 1.f : 0.f) - chart_t_) * (1 - std::exp(-dt_ * 9));
  chart_age_ += dt_;
  last_chart_area_ = ChartArea(m);
  if (chart_t_ < 0.01f || shown_chart_ == ChartKind::kMap) return;
  const SkRect area = last_chart_area_;
  const float t = Ease(chart_t_);
  c->save();
  // Rise + scale in from the tab bar.
  c->translate(area.centerX(), area.fBottom);
  const float sc = 0.94f + 0.06f * t;
  c->scale(sc, sc);
  c->translate(-area.centerX(), -area.fBottom + (1 - t) * 30 * s_);
  c->saveLayerAlphaf(nullptr, t);
  c->drawRRect(SkRRect::MakeRectXY(area, 16 * s_, 16 * s_), P(SkColorSetARGB(238, 9, 17, 29)));
  c->drawRRect(SkRRect::MakeRectXY(area.makeInset(0.5f, 0.5f), 16 * s_, 16 * s_),
               Stroke(SkColorSetARGB(60, 255, 255, 255), 1));
  const SkRect inner = area.makeInset(22 * s_, 18 * s_);
  switch (shown_chart_) {
    case ChartKind::kTrend: DrawTrendChart(c, m, inner); break;
    case ChartKind::kSeats: DrawSeatArc(c, m, inner); break;
    case ChartKind::kMargins: DrawMargins(c, m, inner); break;
    case ChartKind::kParties: DrawParties(c, m, inner); break;
    case ChartKind::kGrid: DrawRaceGrid(c, m, inner); break;
    default: break;
  }
  DrawText(c, L_.T("chart.back"), area.fRight - 18 * s_, area.fTop + 26 * s_,
           fonts_->Regular(11.5f * s_), kText3, Align::kRight);
  c->restore();
  c->restore();
}

// ---------------------------------------------------------------- trend ----

void Dashboard::DrawTrendChart(SkCanvas* c, const DashboardModel& m, SkRect area) {
  const Race* race = m.chart_race;
  if (!race) return;
  DrawText(c, Fmt(L_.T("trend.title"), {L_.RaceTitle(*race)}), area.fLeft, area.fTop + 20 * s_,
           fonts_->Bold(20 * s_), kText);
  DrawText(c, L_.T("trend.hint"), area.fLeft, area.fTop + 40 * s_, fonts_->Regular(12 * s_), kText3);
  const RaceHistory* h = nullptr;
  if (m.history) {
    auto it = m.history->find(race->id);
    if (it != m.history->end()) h = &it->second;
  }
  // Keep only samples with votes.
  std::vector<size_t> idx;
  if (h) {
    for (size_t i = 0; i < h->minutes.size(); ++i) {
      int64_t sum = 0;
      for (int64_t v : h->votes[i]) sum += v;
      if (sum > 0) idx.push_back(i);
    }
  }
  const SkRect plot = SkRect::MakeLTRB(area.fLeft + 44 * s_, area.fTop + 64 * s_,
                                       area.fRight - 150 * s_, area.fBottom - 34 * s_);
  if (idx.size() < 2) {
    DrawText(c, L_.T("chart.nodata"), plot.centerX(), plot.centerY(), fonts_->Regular(16 * s_), kText2,
             Align::kCenter);
    return;
  }
  auto share = [&](size_t i, int cand) {
    int64_t sum = 0;
    for (int64_t v : h->votes[i]) sum += v;
    return sum > 0 ? static_cast<double>(h->votes[i][cand]) / sum : 0.0;
  };
  // Top candidates by latest votes (up to 4).
  const std::vector<int64_t>& last = h->votes[idx.back()];
  std::vector<int> cands(last.size());
  for (size_t i = 0; i < cands.size(); ++i) cands[i] = static_cast<int>(i);
  std::sort(cands.begin(), cands.end(), [&](int a, int b) { return last[a] > last[b]; });
  if (cands.size() > 4) cands.resize(4);
  // Y range (animated) from the visible series, after the first 5% counted.
  double lo = 1, hi = 0;
  for (size_t i : idx) {
    if (h->progress[i] < 0.05f && i != idx.back()) continue;
    for (int cnd : cands) {
      lo = std::min(lo, share(i, cnd));
      hi = std::max(hi, share(i, cnd));
    }
  }
  lo = std::max(0.0, std::floor((lo - 0.03) * 20) / 20);
  hi = std::min(1.0, std::ceil((hi + 0.03) * 20) / 20);
  const float y0 = Approach(anim_, "trend_lo:" + race->id, static_cast<float>(lo), 4);
  const float y1 = Approach(anim_, "trend_hi:" + race->id, static_cast<float>(hi), 4);
  auto X = [&](double minute) {
    return plot.fLeft + static_cast<float>(minute / election::kNightMinutes) * plot.width();
  };
  auto Y = [&](double v) {
    return plot.fBottom - static_cast<float>((v - y0) / std::max(0.01f, y1 - y0)) * plot.height();
  };
  // Counting progress as a soft area at the bottom (right axis 0-100%).
  {
    SkPathBuilder b;
    b.moveTo(X(h->minutes[idx.front()]), plot.fBottom);
    for (size_t i : idx) b.lineTo(X(h->minutes[i]), plot.fBottom - h->progress[i] * plot.height() * 0.25f);
    b.lineTo(X(h->minutes[idx.back()]), plot.fBottom);
    b.close();
    c->drawPath(b.detach(), P(SkColorSetRGB(88, 160, 230), 0.16f));
  }
  // Grid + axes.
  const float step = (y1 - y0) > 0.3f ? 0.1f : 0.05f;
  for (float v = std::ceil(y0 / step) * step; v <= y1 + 1e-4f; v += step) {
    c->drawLine(plot.fLeft, Y(v), plot.fRight, Y(v), Stroke(kGrid, 1));
    DrawText(c, Pct(v, 0), plot.fLeft - 6 * s_, Y(v) + 4 * s_, fonts_->Regular(10.5f * s_), kText3,
             Align::kRight);
  }
  for (int hr = 0; hr <= 12; hr += 2) {
    DrawText(c, election::NightClockLabel(hr * 60.0), X(hr * 60), plot.fBottom + 16 * s_,
             fonts_->Regular(10.5f * s_), kText3, Align::kCenter);
  }
  // Lead changes and the projection point, derived from the history.
  int prev_leader = -1;
  float called_at = -1;
  float last_label_x = -1e9f;
  for (size_t i : idx) {
    const auto& v = h->votes[i];
    const int leader = static_cast<int>(std::max_element(v.begin(), v.end()) - v.begin());
    if (prev_leader >= 0 && leader != prev_leader && h->progress[i] > 0.05f) {
      const float lx = X(h->minutes[i]);
      const bool label = lx - last_label_x > 44 * s_;
      if (label) last_label_x = lx;
      SkPaint dash = Stroke(SkColorSetRGB(224, 142, 11), 1.2f * s_, 0.8f);
      const float iv[2] = {4 * s_, 4 * s_};
      dash.setPathEffect(SkDashPathEffect::Make(iv, 0));
      c->drawLine(lx, plot.fTop, lx, plot.fBottom, dash);
      if (label) {
        DrawText(c, L_.T("ev.lead_change.title"), lx + 3 * s_, plot.fTop + 11 * s_,
                 fonts_->Bold(10.5f * s_), SkColorSetRGB(224, 142, 11));
      }
    }
    prev_leader = leader;
    if (called_at < 0 && h->progress[i] >= 0.25f) {
      std::vector<int64_t> s = v;
      std::sort(s.rbegin(), s.rend());
      int64_t total = 0;
      for (int64_t x : v) total += x;
      const double remaining =
          total * (1.0 / h->progress[i] - 1.0) * 1.15 *
          (h->progress[i] >= 0.75f ? election::EventTracker::kCallShareLate
                                   : election::EventTracker::kCallShare);
      if (s.size() > 1 && static_cast<double>(s[0] - s[1]) > remaining) called_at = h->minutes[i];
    }
  }
  if (called_at >= 0) {
    const float cx = X(called_at);
    c->drawLine(cx, plot.fTop, cx, plot.fBottom, Stroke(kRed, 1.5f * s_, 0.8f));
    DrawText(c, L_.T("ev.called.title"), cx + 3 * s_, plot.fTop + 24 * s_, fonts_->Bold(10.5f * s_), kRed);
  }
  c->save();
  c->clipRect(plot.makeOutset(8 * s_, 8 * s_));
  // Series: revealed left-to-right on open, the live end extends smoothly.
  const float reveal = Ease(chart_age_ / 0.9f);
  const double last_min = h->minutes[idx.back()];
  const float shown_last = Approach(anim_, "trend_end:" + race->id, static_cast<float>(last_min), 5);
  const double clip_min = std::min<double>(shown_last, h->minutes[idx.front()] +
                                                        (last_min - h->minutes[idx.front()]) * reveal);
  struct EndLabel {
    float y;
    int cand;
    double value;
  };
  std::vector<EndLabel> ends;
  for (int cnd : cands) {
    const auto& cand = race->candidates[cnd];
    const uint32_t color = m.data->party(cand.party).color;
    SkPathBuilder b;
    bool started = false;
    float ex = 0, ey = 0;
    double ev = 0;
    for (size_t k = 0; k < idx.size(); ++k) {
      const size_t i = idx[k];
      double minute = h->minutes[i];
      double value = share(i, cnd);
      if (minute > clip_min) {
        if (k == 0) break;
        const size_t p = idx[k - 1];
        const double t = (clip_min - h->minutes[p]) / std::max(1e-6, minute - h->minutes[p]);
        value = share(p, cnd) + (value - share(p, cnd)) * t;
        minute = clip_min;
      }
      const float px = X(minute), py = Y(value);
      if (!started) b.moveTo(px, py), started = true;
      else b.lineTo(px, py);
      ex = px, ey = py, ev = value;
      if (minute >= clip_min) break;
    }
    if (!started) continue;
    c->drawPath(b.detach(), Stroke(color, (cnd == cands[0] ? 3.2f : 2.4f) * s_));
    const float pulse = 0.5f + 0.5f * std::sin(time_ * 4);
    c->drawCircle(ex, ey, (6 + 4 * pulse) * s_, P(color, 0.25f));
    c->drawCircle(ex, ey, 4.5f * s_, P(color));
    ends.push_back({ey, cnd, ev});
  }
  c->restore();
  // End labels in a column right of the plot, pushed apart.
  std::sort(ends.begin(), ends.end(), [](const EndLabel& a, const EndLabel& b) { return a.y < b.y; });
  float prev_y = -1e9f;
  for (EndLabel& e : ends) {
    e.y = std::max(e.y, prev_y + 30 * s_);
    prev_y = e.y;
  }
  for (const EndLabel& e : ends) {
    const auto& cand = race->candidates[e.cand];
    const uint32_t color = m.data->party(cand.party).color;
    const float lx = plot.fRight + 12 * s_;
    avatars_->Draw(c, cand, m.data->party(cand.party), lx, e.y - 12 * s_, 24 * s_);
    DrawText(c, Ellipsize(fonts_->Bold(12.5f * s_), L_.CandidateName(cand), 100 * s_), lx + 29 * s_,
             e.y - 1 * s_, fonts_->Bold(12.5f * s_), kText);
    const double shown = Roll("trend_v:" + race->id + std::to_string(e.cand), e.value * 10000) / 10000;
    DrawText(c, Pct(shown, 2), lx + 29 * s_, e.y + 13 * s_, fonts_->Bold(12 * s_), color | 0xFF000000);
  }
}

// ------------------------------------------------------------- seat arc ----

void Dashboard::DrawSeatArc(SkCanvas* c, const DashboardModel& m, SkRect area) {
  const auto& info = m.data->info();
  const SeatCount sc = CountSeats(m, m.office);
  const int n = sc.total;
  const char* title_key = m.office == election::kHouse      ? "seats.title.house"
                          : m.office == election::kGovernor ? "seats.title.governor"
                                                            : "seats.title.senate";
  const std::string title_arg = m.office == election::kGovernor
                                    ? std::to_string(sc.races)
                                    : info.senate.tie_breaker_name;
  DrawText(c, Fmt(L_.T(title_key), {title_arg}), area.fLeft, area.fTop + 20 * s_,
           fonts_->Bold(20 * s_), kText);
  struct Seat {
    std::string party;
    int kind = 0;  // 0 held/called, 1 leading, 2 uncalled
    const Race* race = nullptr;
    bool holdover = false;
  };
  std::vector<Seat> seats;
  std::map<std::string, std::pair<int, int>> by_party;  // solid, leading
  if (m.office == election::kHouse) {
    for (const auto& [party, k] : sc.solid) {
      for (int i = 0; i < k; ++i) seats.push_back({party, 0, nullptr, false});
      by_party[party].first += k;
    }
  } else {
    if (m.office == election::kSenate) {
      for (const auto& [party, k] : info.senate.holdovers) {
        const std::string p = info.senate.CaucusOf(party);
        for (int i = 0; i < k; ++i) seats.push_back({p, 0, nullptr, true});
        by_party[p].first += k;
      }
    } else {
      for (const election::StateInfo& st : m.data->states()) {
        if (st.governor_party.empty() || m.data->RaceForState(st.fips, election::kGovernor)) continue;
        seats.push_back({st.governor_party, 0, nullptr, true});
        by_party[st.governor_party].first++;
      }
    }
    for (const Race* race : m.data->RacesFor(m.office)) {
      const RaceState st = StateOf(m, *race);
      const RaceStatus* rs = StatusOf(m, *race);
      Seat seat;
      seat.race = race;
      seat.kind = 2;
      if (rs && rs->called >= 0) {
        seat.party = race->candidates[rs->called].party;
        seat.kind = 0;
        by_party[seat.party].first++;
      } else if (st.has_votes) {
        seat.party = race->candidates[st.leader].party;
        seat.kind = 1;
        by_party[seat.party].second++;
      }
      seats.push_back(seat);
    }
  }
  while (static_cast<int>(seats.size()) < n) seats.push_back({"", 2, nullptr, false});
  // Left-to-right: DEM, others, uncalled, REP; within a party solid first
  // from the outside in.
  auto rank = [](const Seat& s) {
    if (s.kind == 2) return 50;
    if (s.party == "DEM") return s.kind == 0 ? 0 : 10;
    if (s.party == "REP") return s.kind == 0 ? 100 : 90;
    return s.kind == 0 ? 20 : 30;
  };
  std::stable_sort(seats.begin(), seats.end(),
                   [&](const Seat& a, const Seat& b) { return rank(a) < rank(b); });

  // Hemicycle geometry: rows scale with the seat count, seats per row
  // proportional to the radius; seats are ordered by angle from the left.
  const float cx = area.centerX();
  const float cy = area.fBottom - 70 * s_;
  const float R = std::min(area.width() * 0.42f, (cy - area.fTop - 60 * s_));
  const int nrows = std::max(3, static_cast<int>(std::lround(std::sqrt(n / 3.0))));
  const float inner = 0.42f;
  std::vector<float> radii(nrows);
  float radius_sum = 0;
  for (int row = 0; row < nrows; ++row) {
    radii[row] = R * (inner + (1 - inner) * row / std::max(1, nrows - 1));
    radius_sum += radii[row];
  }
  std::vector<int> per_row(nrows);
  int assigned = 0;
  for (int row = 0; row < nrows; ++row) {
    per_row[row] = static_cast<int>(std::floor(n * radii[row] / radius_sum));
    assigned += per_row[row];
  }
  for (int row = nrows - 1; assigned < n; row = (row + nrows - 1) % nrows) {
    per_row[row]++;
    assigned++;
  }
  struct Pos {
    float angle, x, y;
  };
  std::vector<Pos> pos;
  for (int row = 0; row < nrows; ++row) {
    for (int k = 0; k < per_row[row]; ++k) {
      const float a = 3.14159265f * (1 - (k + 0.5f) / per_row[row]);
      pos.push_back({a, cx + std::cos(a) * radii[row], cy - std::sin(a) * radii[row]});
    }
  }
  std::stable_sort(pos.begin(), pos.end(), [](const Pos& a, const Pos& b) { return a.angle > b.angle; });
  const float seat_r = std::min(R * (1 - inner) / nrows * 0.42f,
                                3.14159265f * R / std::max(1, per_row.back()) * 0.42f);
  for (int i = 0; i < n && i < static_cast<int>(pos.size()); ++i) {
    const Seat& seat = seats[i];
    const uint32_t target = seat.party.empty() ? 0xFF2B3D52 : m.data->party(seat.party).color;
    const std::string key = "seat" + m.office + std::to_string(i);
    // Colour cross-fade + pop when the seat changes hands.
    auto [last, fresh] = seat_color_.try_emplace(key, target);
    if (!fresh && last->second != target) flash_[key] = 1;
    last->second = target;
    const float rr = Approach(anim_, key + "r", ((target >> 16) & 0xFF) / 255.f, 6);
    const float gg = Approach(anim_, key + "g", ((target >> 8) & 0xFF) / 255.f, 6);
    const float bb = Approach(anim_, key + "b", (target & 0xFF) / 255.f, 6);
    const SkColor col = SkColorSetRGB(static_cast<U8CPU>(rr * 255), static_cast<U8CPU>(gg * 255),
                                      static_cast<U8CPU>(bb * 255));
    const float appear = EaseBack((chart_age_ - i * (0.45f / n)) / 0.3f);
    const float flash = flash_.count(key) ? flash_[key] : 0.f;
    const float radius = seat_r * appear * (1 + 0.35f * flash);
    if (radius <= 0.1f) continue;
    if (seat.kind == 1) {
      c->drawCircle(pos[i].x, pos[i].y, radius, P(col, 0.35f));
      c->drawCircle(pos[i].x, pos[i].y, radius - 1.2f * s_, Stroke(col, 2 * s_));
    } else {
      c->drawCircle(pos[i].x, pos[i].y, radius, P(col, seat.kind == 2 ? 0.6f : seat.holdover ? 0.55f : 1));
    }
    if (flash > 0) c->drawCircle(pos[i].x, pos[i].y, radius * (1.2f + 0.8f * (1 - flash)), Stroke(kGold, 2 * s_, flash));
    if (seat.race) {
      if (seat.kind == 0 && radius > 7 * s_) {
        DrawText(c, "✓", pos[i].x, pos[i].y + radius * 0.35f, fonts_->Bold(radius), SK_ColorWHITE, Align::kCenter);
      }
      if (radius > 8 * s_) {
        DrawText(c, seat.race->state_abbr, pos[i].x, pos[i].y + radius + 10 * s_,
                 fonts_->Regular(9 * s_), kText3, Align::kCenter);
      }
      AddHit(SkRect::MakeXYWH(pos[i].x - radius, pos[i].y - radius, radius * 2, radius * 2),
             {DashboardHit::Kind::kRace, ChartKind::kMap, seat.race});
    }
  }
  // Majority marker.
  SkPaint dash = Stroke(kGold, 1.5f * s_, 0.8f);
  const float iv[2] = {5 * s_, 4 * s_};
  dash.setPathEffect(SkDashPathEffect::Make(iv, 0));
  c->drawLine(cx, cy - R * 1.1f, cx, cy - R * inner * 0.8f, dash);
  DrawText(c, Fmt(L_.T("seats.majority"), {std::to_string(sc.majority)}), cx, cy - R * 1.13f,
           fonts_->Bold(12 * s_), kGold, Align::kCenter);
  // Party tallies under the arc, big numbers rolling.
  std::vector<std::pair<std::string, int>> order;
  for (const auto& [p, cnt] : by_party) order.push_back({p, cnt.first + cnt.second});
  std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
  float x = area.fLeft;
  const float ty = cy + 42 * s_;
  for (const auto& [party, unused] : order) {
    const auto& cnt = by_party[party];
    const election::Party& p = m.data->party(party);
    const int total = static_cast<int>(std::lround(Roll("seats:" + m.office + party, cnt.first + cnt.second)));
    c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(x, ty - 26 * s_, 10 * s_, 34 * s_), 3 * s_, 3 * s_), P(p.color));
    x += 16 * s_;
    DrawText(c, std::to_string(total), x, ty, fonts_->Bold(26 * s_), kText);
    const float nw = TextWidth(fonts_->Bold(26 * s_), std::to_string(total)) + 6 * s_;
    DrawText(c, L_.PartyShort(p), x + nw, ty - 12 * s_, fonts_->Bold(12.5f * s_), kText2);
    DrawText(c, Fmt(L_.T("seats.split"), {std::to_string(cnt.first), std::to_string(cnt.second)}), x + nw,
             ty + 3 * s_, fonts_->Regular(10.5f * s_), kText3);
    x += nw + std::max(TextWidth(fonts_->Bold(12.5f * s_), L_.PartyShort(p)),
                       TextWidth(fonts_->Regular(10.5f * s_), Fmt(L_.T("seats.split"), {"000", "00"}))) +
         22 * s_;
  }
  // Legend.
  const float ly = area.fTop + 44 * s_;
  float lx = area.fLeft;
  auto legend = [&](const char* key, int style) {
    if (style == 0) c->drawCircle(lx + 6 * s_, ly - 4 * s_, 6 * s_, P(kText2));
    if (style == 1) {
      c->drawCircle(lx + 6 * s_, ly - 4 * s_, 6 * s_, P(kText2, 0.35f));
      c->drawCircle(lx + 6 * s_, ly - 4 * s_, 5 * s_, Stroke(kText2, 2 * s_));
    }
    if (style == 2) c->drawCircle(lx + 6 * s_, ly - 4 * s_, 6 * s_, P(kText2, 0.45f));
    lx += 18 * s_;
    lx += DrawText(c, L_.T(key), lx, ly, fonts_->Regular(12 * s_), kText2) + 14 * s_;
  };
  legend("seats.projected", 0);
  if (m.office != election::kHouse) {
    legend("seats.leading", 1);
    legend("seats.holdover", 2);
  }
}

// -------------------------------------------------------------- margins ----

void Dashboard::DrawMargins(SkCanvas* c, const DashboardModel& m, SkRect area) {
  const std::vector<const Race*> races = ChartRaces(m);
  const std::string office = races.empty() ? m.office : races.front()->office;
  DrawText(c, Fmt(L_.T("margins.title"), {L_.OfficeName(office)}), area.fLeft, area.fTop + 20 * s_,
           fonts_->Bold(20 * s_), kText);
  DrawText(c, L_.T("margins.hint"), area.fLeft, area.fTop + 40 * s_, fonts_->Regular(12 * s_), kText3);
  std::vector<RaceState> states;
  for (const Race* race : races) states.push_back(StateOf(m, *race));
  std::stable_sort(states.begin(), states.end(), [](const RaceState& a, const RaceState& b) {
    if (a.has_votes != b.has_votes) return a.has_votes;
    return a.margin < b.margin;
  });
  const float top = area.fTop + 58 * s_;
  const float row_h = std::min(28 * s_, (area.fBottom - top) / std::max<size_t>(1, states.size()));
  const float name_w = 40 * s_, who_w = 150 * s_;
  const float bar_x = area.fLeft + name_w + who_w + 12 * s_;
  const float bar_w = area.fRight - bar_x - 150 * s_;
  const float scale = 0.30f;  // full bar = 30 points
  for (size_t rank = 0; rank < states.size(); ++rank) {
    const RaceState& st = states[rank];
    const std::string key = "mrg:" + st.race->id;
    // Enter staggered, then slide to the current rank.
    const float target_y = top + rank * row_h;
    float y = Approach(anim_, key + "y", target_y, 6);
    const float appear = Ease((chart_age_ - rank * 0.025f) / 0.35f);
    y += (1 - appear) * 20 * s_;
    c->saveLayerAlphaf(nullptr, appear);
    const float mid = y + row_h / 2;
    if (rank % 2 == 0) {
      c->drawRect(SkRect::MakeXYWH(area.fLeft - 6 * s_, y, area.width() + 12 * s_, row_h), P(SkColorSetARGB(14, 255, 255, 255)));
    }
    const float fl = flash_.count("row:" + st.race->id) ? flash_["row:" + st.race->id] : 0.f;
    if (fl > 0) {
      c->drawRect(SkRect::MakeXYWH(area.fLeft - 6 * s_, y, area.width() + 12 * s_, row_h),
                  P(SkColorSetARGB(static_cast<U8CPU>(80 * fl), 245, 197, 66)));
    }
    DrawText(c, st.race->state_abbr + (st.race->special ? "*" : ""), area.fLeft, mid + 4.5f * s_,
             fonts_->Bold(12.5f * s_), kText);
    if (st.has_votes) {
      const Candidate& lead = st.race->candidates[st.leader];
      const election::Party& p = m.data->party(lead.party);
      const float av = std::min(row_h - 6 * s_, 20 * s_);
      avatars_->Draw(c, lead, p, area.fLeft + name_w, mid - av / 2, av);
      DrawText(c, Ellipsize(fonts_->Regular(12 * s_), L_.CandidateName(lead), who_w - av - 10 * s_),
               area.fLeft + name_w + av + 5 * s_, mid + 4.5f * s_, fonts_->Regular(12 * s_), kText2);
      const float w = Approach(anim_, key + "w", std::min(1.f, static_cast<float>(st.margin) / scale) * bar_w, 5);
      c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(bar_x, mid - 6 * s_, std::max(3 * s_, w), 12 * s_), 3 * s_, 3 * s_),
                   P(p.color));
      const double shown = Roll(key + "m", st.margin * 10000) / 10000;
      DrawText(c, "+" + Pct(shown, 1), bar_x + std::max(3 * s_, w) + 6 * s_, mid + 4.5f * s_,
               fonts_->Bold(12 * s_), kText);
      // Close-race highlight.
      if (st.margin < 0.02 && st.tally->Progress() > 0.3) {
        c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(bar_x - 3 * s_, mid - 9 * s_, 70 * s_, 18 * s_), 5 * s_, 5 * s_),
                     Stroke(SkColorSetRGB(0, 184, 148), 1.5f * s_, 0.6f + 0.4f * std::sin(time_ * 5)));
      }
      // Progress mini-bar + status.
      const float px = area.fRight - 140 * s_;
      c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(px, mid - 3 * s_, 70 * s_, 6 * s_), 3 * s_, 3 * s_), P(kGrid));
      const float pw = Approach(anim_, key + "p", 70 * s_ * static_cast<float>(st.tally->Progress()), 5);
      c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(px, mid - 3 * s_, pw, 6 * s_), 3 * s_, 3 * s_), P(kGold));
      const RaceStatus* rs = StatusOf(m, *st.race);
      if (rs && rs->called >= 0) {
        DrawStamp(c, area.fRight - 40 * s_, mid, std::min(row_h * 1.1f, 30 * s_), rs->called_age);
      } else {
        DrawText(c, Pct(st.tally->Progress(), 0), area.fRight - 22 * s_, mid + 4 * s_, fonts_->Regular(11 * s_), kText3,
                 Align::kRight);
      }
    } else {
      DrawText(c, L_.T("race.not_started"), area.fLeft + name_w, mid + 4.5f * s_, fonts_->Regular(12 * s_), kText3);
    }
    c->restore();
    AddHit(SkRect::MakeXYWH(area.fLeft - 6 * s_, target_y, area.width() + 12 * s_, row_h),
           {DashboardHit::Kind::kRace, ChartKind::kMap, st.race});
  }
}

// -------------------------------------------------------------- parties ----

void Dashboard::DrawParties(SkCanvas* c, const DashboardModel& m, SkRect area) {
  const std::vector<const Race*> races = ChartRaces(m);
  const std::string office = races.empty() ? m.office : races.front()->office;
  DrawText(c, Fmt(L_.T("parties.title"), {L_.OfficeName(office)}), area.fLeft, area.fTop + 20 * s_,
           fonts_->Bold(20 * s_), kText);
  struct Row {
    std::string party;
    int64_t votes = 0;
    int projected = 0, leading = 0;
  };
  std::map<std::string, Row> rows;
  int64_t grand = 0;
  for (const Race* race_ptr : races) {
    const Race& race = *race_ptr;
    const RaceState st = StateOf(m, race);
    if (!st.tally) continue;
    for (size_t i = 0; i < race.candidates.size() && i < st.tally->votes.size(); ++i) {
      Row& r = rows[race.candidates[i].party];
      r.party = race.candidates[i].party;
      r.votes += st.tally->votes[i];
      grand += st.tally->votes[i];
    }
    const RaceStatus* rs = StatusOf(m, race);
    if (rs && rs->called >= 0) rows[race.candidates[rs->called].party].projected++;
    else if (st.has_votes) rows[race.candidates[st.leader].party].leading++;
  }
  std::vector<Row> sorted;
  for (auto& [k, v] : rows) sorted.push_back(v);
  std::sort(sorted.begin(), sorted.end(), [](const Row& a, const Row& b) { return a.votes > b.votes; });
  DrawText(c, Fmt(L_.T("parties.total"), {FormatThousands(static_cast<int64_t>(Roll("parties:total", static_cast<double>(grand))))}),
           area.fLeft, area.fTop + 40 * s_, fonts_->Regular(12.5f * s_), kText3);
  if (grand <= 0) {
    DrawText(c, L_.T("chart.nodata"), area.centerX(), area.centerY(), fonts_->Regular(16 * s_), kText2, Align::kCenter);
    return;
  }
  const float top = area.fTop + 64 * s_;
  const float row_h = std::min(44 * s_, (area.fBottom - top) / std::max<size_t>(1, sorted.size()));
  const float label_w = 130 * s_;
  const float bar_x = area.fLeft + label_w;
  const float bar_w = area.width() - label_w - 230 * s_;
  const double max_votes = static_cast<double>(sorted.front().votes);
  for (size_t rank = 0; rank < sorted.size(); ++rank) {
    const Row& r = sorted[rank];
    const election::Party& p = m.data->party(r.party);
    const std::string key = "pty:" + r.party;
    const float y = Approach(anim_, key + "y", top + rank * row_h, 6);
    const float appear = Ease((chart_age_ - rank * 0.05f) / 0.4f);
    const float mid = y + row_h / 2;
    c->saveLayerAlphaf(nullptr, appear);
    c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(area.fLeft, mid - 9 * s_, 6 * s_, 18 * s_), 3 * s_, 3 * s_), P(p.color));
    DrawText(c, Ellipsize(fonts_->Bold(14 * s_), L_.PartyName(p), label_w - 20 * s_), area.fLeft + 14 * s_, mid + 5 * s_,
             fonts_->Bold(14 * s_), kText);
    const double shown = Roll(key + "v", static_cast<double>(r.votes));
    const float w = std::max(2.f, static_cast<float>(shown / max_votes) * bar_w * appear);
    const float bh = std::min(row_h * 0.6f, 24 * s_);
    c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(bar_x, mid - bh / 2, w, bh), 4 * s_, 4 * s_), P(p.color));
    // Shine on the growing edge while the number rolls.
    if (std::fabs(shown - r.votes) > 1) {
      c->drawRect(SkRect::MakeXYWH(bar_x + w - 6 * s_, mid - bh / 2, 6 * s_, bh), P(SK_ColorWHITE, 0.35f));
    }
    DrawText(c, FormatThousands(static_cast<int64_t>(shown)), bar_x + w + 8 * s_, mid - 1 * s_, fonts_->Bold(13 * s_), kText);
    DrawText(c, Pct(shown / std::max(1.0, static_cast<double>(grand))), bar_x + w + 8 * s_, mid + 13 * s_,
             fonts_->Regular(11 * s_), kText3);
    DrawText(c, Fmt(L_.T("parties.seats"), {std::to_string(r.projected), std::to_string(r.leading)}),
             area.fRight, mid + 5 * s_, fonts_->Regular(12 * s_), kText2, Align::kRight);
    c->restore();
  }
}

// ----------------------------------------------------------------- grid ----

void Dashboard::DrawRaceGrid(SkCanvas* c, const DashboardModel& m, SkRect area) {
  if (m.office == election::kHouse) {
    DrawHouseGrid(c, m, area);
    return;
  }
  const std::vector<const Race*> races = ChartRaces(m);
  DrawText(c, Fmt(L_.T("grid.title"), {std::to_string(races.size()), L_.OfficeName(m.office)}),
           area.fLeft, area.fTop + 20 * s_, fonts_->Bold(20 * s_), kText);
  const int cols = 6;
  const int nrows = (static_cast<int>(races.size()) + cols - 1) / cols;
  const float top = area.fTop + 34 * s_;
  const float gap = 7 * s_;
  const float tw = (area.width() - gap * (cols - 1)) / cols;
  const float th = (area.fBottom - top - gap * (nrows - 1)) / nrows;
  auto last_name = [](const std::string& name) {
    const size_t sp = name.rfind(' ');
    std::string last = sp == std::string::npos ? name : name.substr(sp + 1);
    if ((last == "Jr." || last == "Sr." || last == "II" || last == "III") && sp != std::string::npos) {
      const size_t sp2 = name.rfind(' ', sp - 1);
      last = name.substr(sp2 == std::string::npos ? 0 : sp2 + 1, sp - (sp2 == std::string::npos ? 0 : sp2 + 1));
    }
    return last;
  };
  for (size_t i = 0; i < races.size(); ++i) {
    const Race& race = *races[i];
    const RaceState st = StateOf(m, race);
    const int col = static_cast<int>(i) % cols, row = static_cast<int>(i) / cols;
    const float appear = EaseBack((chart_age_ - i * 0.012f) / 0.3f);
    const SkRect tile = SkRect::MakeXYWH(area.fLeft + col * (tw + gap), top + row * (th + gap), tw, th);
    c->save();
    c->translate(tile.centerX(), tile.centerY());
    c->scale(std::max(0.01f, appear), std::max(0.01f, appear));
    c->translate(-tile.centerX(), -tile.centerY());
    c->drawRRect(SkRRect::MakeRectXY(tile, 9 * s_, 9 * s_), P(SkColorSetARGB(24, 255, 255, 255)));
    const float fl = flash_.count("row:" + race.id) ? flash_["row:" + race.id] : 0.f;
    if (fl > 0) c->drawRRect(SkRRect::MakeRectXY(tile, 9 * s_, 9 * s_), Stroke(kGold, (1 + 3 * fl) * s_, fl));
    const float pad = 8 * s_;
    DrawText(c, race.state_abbr + (race.special ? "*" : ""), tile.fLeft + pad, tile.fTop + 18 * s_,
             fonts_->Bold(14 * s_), kText);
    // Progress ring (sweeps as counting advances); the WIN seal once called.
    const float ring_r = std::min(th * 0.17f, 12 * s_);
    const float rcx = tile.fRight - pad - ring_r, rcy = tile.fTop + pad + ring_r;
    const double prog = st.tally ? st.tally->Progress() : 0;
    const float sweep = static_cast<float>(Roll("grid_p:" + race.id, prog * 1000) / 1000);
    const RaceStatus* rs = StatusOf(m, race);
    if (rs && rs->called >= 0) {
      DrawStamp(c, rcx, rcy + 2 * s_, ring_r * 2.6f, rs->called_age);
    } else {
      c->drawCircle(rcx, rcy, ring_r, Stroke(kGrid, 3 * s_));
      if (sweep > 0) {
        SkPathBuilder arc;
        arc.addArc(SkRect::MakeXYWH(rcx - ring_r, rcy - ring_r, ring_r * 2, ring_r * 2), -90, 360 * sweep);
        c->drawPath(arc.detach(), Stroke(kGold, 3 * s_));
      }
      DrawText(c, Pct(sweep, 0), rcx - ring_r - 4 * s_, rcy + 4 * s_, fonts_->Regular(9.5f * s_), kText3,
               Align::kRight);
    }
    // Top two: party dot, last name, share and a thin bar.
    float by = tile.fTop + std::max(2 * ring_r + pad + 12 * s_, 38 * s_);
    const float row_h = std::min(21 * s_, (tile.fBottom - by - 2 * s_) / 1.6f);
    if (st.has_votes) {
      for (int k : {st.leader, st.second}) {
        if (k < 0) continue;
        const Candidate& cand = race.candidates[k];
        const election::Party& p = m.data->party(cand.party);
        c->drawCircle(tile.fLeft + pad + 4 * s_, by - 4 * s_, 4 * s_, P(p.color));
        const double share = Roll("grid_s:" + race.id + std::to_string(k), st.tally->Share(k) * 10000) / 10000;
        const std::string pct = Pct(share);
        const float pw = TextWidth(fonts_->Bold(11.5f * s_), pct);
        DrawText(c, Ellipsize(fonts_->Bold(11.5f * s_), last_name(cand.name), tw - pad * 2 - pw - 16 * s_),
                 tile.fLeft + pad + 12 * s_, by, fonts_->Bold(11.5f * s_), k == st.leader ? kText : kText2);
        DrawText(c, pct, tile.fRight - pad, by, fonts_->Bold(11.5f * s_), kText, Align::kRight);
        const float bw = (tw - pad * 2) * static_cast<float>(share);
        c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(tile.fLeft + pad, by + 4 * s_, tw - pad * 2, 3 * s_), 1.5f * s_, 1.5f * s_), P(kGrid));
        c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(tile.fLeft + pad, by + 4 * s_, bw, 3 * s_), 1.5f * s_, 1.5f * s_), P(p.color));
        by += row_h;
      }
      if (!(rs && rs->called >= 0) && rs && rs->declared >= 0 && by + 14 * s_ < tile.fBottom) {
        Chip(c, fonts_, L_.T("chip.declared"), tile.fLeft + pad, tile.fBottom - pad - 14 * s_, 14 * s_,
             0xFF9B59B6, s_);
      }
    } else {
      float ax = tile.fLeft + pad;
      for (const Candidate& cand : race.candidates) {
        avatars_->Draw(c, cand, m.data->party(cand.party), ax, by - 12 * s_, 18 * s_);
        ax += 20 * s_;
        if (ax > tile.fRight - 24 * s_) break;
      }
    }
    c->restore();
    AddHit(tile, {DashboardHit::Kind::kRace, ChartKind::kMap, &race});
  }
}

void Dashboard::DrawHouseGrid(SkCanvas* c, const DashboardModel& m, SkRect area) {
  DrawText(c, L_.T("grid.house_title"), area.fLeft, area.fTop + 20 * s_, fonts_->Bold(20 * s_), kText);
  std::vector<const election::StateInfo*> states;
  for (const election::StateInfo& st : m.data->states()) {
    if (st.house_seats > 0) states.push_back(&st);
  }
  const int cols = 10;
  const int nrows = (static_cast<int>(states.size()) + cols - 1) / cols;
  const float top = area.fTop + 34 * s_;
  const float gap = 6 * s_;
  const float tw = (area.width() - gap * (cols - 1)) / cols;
  const float th = (area.fBottom - top - gap * (nrows - 1)) / nrows;
  const election::Party& D = m.data->party("DEM");
  const election::Party& R = m.data->party("REP");
  for (size_t i = 0; i < states.size(); ++i) {
    const election::StateInfo& st = *states[i];
    const int col = static_cast<int>(i) % cols, row = static_cast<int>(i) / cols;
    const float appear = EaseBack((chart_age_ - i * 0.015f) / 0.35f);
    const SkRect tile = SkRect::MakeXYWH(area.fLeft + col * (tw + gap), top + row * (th + gap), tw, th);
    const int region = m.tree->FindByCode(st.fips);
    const election::HouseTally t = m.results->House(region >= 0 ? region : 0);
    c->save();
    c->translate(tile.centerX(), tile.centerY());
    c->scale(std::max(0.01f, appear), std::max(0.01f, appear));
    c->translate(-tile.centerX(), -tile.centerY());
    // Tile tinted by the party ahead in called seats.
    const int dem = t.Won("DEM"), rep = t.Won("REP");
    const uint32_t tint = dem == rep ? 0xFF2B3D52 : (dem > rep ? D.color : R.color);
    c->drawRRect(SkRRect::MakeRectXY(tile, 8 * s_, 8 * s_),
                 P(tint, 0.18f + 0.5f * t.Called() / std::max(1, st.house_seats)));
    const float pad = 7 * s_;
    DrawText(c, st.abbr, tile.fLeft + pad, tile.fTop + 17 * s_, fonts_->Bold(13 * s_), kText);
    DrawText(c, std::to_string(st.house_seats), tile.fRight - pad, tile.fTop + 16 * s_,
             fonts_->Regular(10.5f * s_), kText3, Align::kRight);
    const SkRect bar = SkRect::MakeXYWH(tile.fLeft + pad, tile.fBottom - pad - 7 * s_, tw - 2 * pad, 7 * s_);
    c->drawRRect(SkRRect::MakeRectXY(bar, 3 * s_, 3 * s_), P(kGrid));
    const float unit = bar.width() / st.house_seats;
    const float dw = Approach(anim_, "hg:d" + st.abbr, unit * dem, 5);
    const float rw = Approach(anim_, "hg:r" + st.abbr, unit * rep, 5);
    c->drawRect(SkRect::MakeXYWH(bar.fLeft, bar.fTop, dw, bar.height()), P(D.color));
    c->drawRect(SkRect::MakeXYWH(bar.fRight - rw, bar.fTop, rw, bar.height()), P(R.color));
    if (th > 52 * s_) {
      DrawText(c, std::to_string(dem) + " – " + std::to_string(rep), tile.centerX(), bar.fTop - 6 * s_,
               fonts_->Bold(12 * s_), kText, Align::kCenter);
    }
    c->restore();
    DashboardHit hit;
    hit.kind = DashboardHit::Kind::kState;
    hit.state = st.fips;
    AddHit(tile, hit);
  }
}

// ------------------------------------------------------------------ PiP ----

void Dashboard::DrawPip(SkCanvas* c, const DashboardModel& m) {
  pip_rect_ = SkRect::MakeEmpty();
  if (!m.pip) return;
  // Queue management: breaking items interrupt, otherwise rotate.
  pip_age_ += dt_;
  pip_swap_ = std::min(1.f, pip_swap_ + dt_ / 0.45f);
  const bool breaking_waiting = !pip_queue_.empty() && pip_queue_.front().breaking;
  const float hold = 6.5f;
  if (!pip_has_current_ || pip_age_ > hold || (breaking_waiting && pip_age_ > 2.5f && !pip_current_.breaking) ||
      (breaking_waiting && pip_age_ > 4.f)) {
    PipItem next;
    bool have = false;
    if (!pip_queue_.empty()) {
      next = pip_queue_.front();
      pip_queue_.pop_front();
      have = true;
    } else if (pip_has_current_ && pip_recent_.size() > 1 && pip_age_ > hold) {
      next = pip_recent_.front();  // rotate the recent items
      pip_recent_.pop_front();
      have = true;
    }
    if (have) {
      if (pip_has_current_) {
        pip_previous_ = pip_current_;
        pip_recent_.push_back(pip_current_);
        while (pip_recent_.size() > 8) pip_recent_.pop_front();
        pip_swap_ = 0;
      }
      pip_current_ = next;
      pip_has_current_ = true;
      pip_age_ = 0;
    } else if (pip_has_current_ && pip_age_ > hold) {
      pip_age_ = hold - 2;  // nothing new: keep showing
    }
  }
  if (!pip_has_current_) return;

  const float panel_x = Layout(m.width, m.height).panel_x;
  const float w = 340 * s_, h = 178 * s_;
  // Bottom-right of the map normally; bottom-left (over the feed) while a
  // chart is open so it never covers the chart.
  const bool chart_open = m.chart != ChartKind::kMap;
  const float tx = chart_open ? 16 * s_ : panel_x - 16 * s_ - w;
  const float ty = chart_open ? m.height - 146 * s_ - h : m.height - 186 * s_ - h;
  const SkRect win = SkRect::MakeXYWH(Approach(anim_, "pip_x", tx, 7), Approach(anim_, "pip_y", ty, 7), w, h);
  pip_rect_ = win;
  // Soft shadow + window.
  c->drawRRect(SkRRect::MakeRectXY(win.makeOffset(0, 4 * s_).makeOutset(3 * s_, 3 * s_), 14 * s_, 14 * s_),
               P(SK_ColorBLACK, 0.35f));
  c->drawRRect(SkRRect::MakeRectXY(win, 12 * s_, 12 * s_), P(SkColorSetARGB(245, 12, 22, 36)));
  c->drawRRect(SkRRect::MakeRectXY(win.makeInset(0.5f, 0.5f), 12 * s_, 12 * s_),
               Stroke(pip_current_.breaking ? kRed : SkColorSetARGB(70, 255, 255, 255), 1.5f * s_));
  // Header.
  const float hh = 26 * s_;
  c->save();
  c->clipRRect(SkRRect::MakeRectXY(win, 12 * s_, 12 * s_), true);
  c->drawRect(SkRect::MakeXYWH(win.fLeft, win.fTop, w, hh), P(SkColorSetARGB(255, 20, 34, 52)));
  c->drawCircle(win.fLeft + 13 * s_, win.fTop + hh / 2, 4.5f * s_, P(kRed, 0.55f + 0.45f * std::sin(time_ * 5)));
  DrawText(c, "LIVE", win.fLeft + 22 * s_, win.fTop + 17.5f * s_, fonts_->Bold(11 * s_), kRed);
  DrawText(c, L_.T("pip.title"), win.fLeft + 58 * s_, win.fTop + 17.5f * s_, fonts_->Bold(12 * s_), kText);
  DrawText(c, "×", win.fRight - 12 * s_, win.fTop + 18 * s_, fonts_->Bold(15 * s_), kText2, Align::kCenter);
  AddHit(win, {DashboardHit::Kind::kPip, ChartKind::kMap, pip_current_.event.race});
  AddHit(SkRect::MakeXYWH(win.fRight - 24 * s_, win.fTop, 24 * s_, hh), {DashboardHit::Kind::kPipClose});

  auto draw_item = [&](const PipItem& it, float dx, float alpha) {
    c->save();
    c->translate(dx, 0);
    c->saveLayerAlphaf(nullptr, alpha);
    const float x = win.fLeft + 12 * s_;
    float y = win.fTop + hh + 20 * s_;
    const float tw = w - 24 * s_;
    if (!it.is_news && !it.event.race) {
      if (it.breaking) {
        const std::string tag = L_.T("breaking");
        const float cw = TextWidth(fonts_->Bold(10.5f * s_), tag) + 8 * s_;
        c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(x, y - 11 * s_, cw, 15 * s_), 3 * s_, 3 * s_), P(kRed));
        DrawText(c, tag, x + 4 * s_, y, fonts_->Bold(10.5f * s_), SK_ColorWHITE);
      }
      DrawText(c, std::string(L_.T(std::string("ev.") + election::EventKey(it.event.type) + ".title")) + " · " +
                      it.event.time_label + " ET",
               x + (it.breaking ? 70 * s_ : 0), y, fonts_->Bold(11.5f * s_), kGold);
      y += 24 * s_;
      const std::vector<std::string> lines = WrapText(fonts_->Bold(16 * s_), EventText(it.event, m), tw);
      for (size_t i = 0; i < lines.size() && i < 3; ++i) {
        DrawText(c, lines[i], x, y, fonts_->Bold(16 * s_), kText);
        y += 21 * s_;
      }
      if (!it.event.party.empty()) {
        const election::Party& p = m.data->party(it.event.party);
        c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(x, win.fBottom - 30 * s_, tw, 10 * s_), 5 * s_, 5 * s_),
                     P(p.color));
      }
    } else if (!it.is_news && it.event.race) {
      const election::Race& race = *it.event.race;
      const std::string tag = std::string(it.breaking ? L_.T("breaking") : "") ;
      float tx = x;
      if (it.breaking) {
        const float cw = TextWidth(fonts_->Bold(10.5f * s_), tag) + 8 * s_;
        c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(tx, y - 11 * s_, cw, 15 * s_), 3 * s_, 3 * s_), P(kRed));
        DrawText(c, tag, tx + 4 * s_, y, fonts_->Bold(10.5f * s_), SK_ColorWHITE);
        tx += cw + 6 * s_;
      }
      DrawText(c, std::string(L_.T(std::string("ev.") + election::EventKey(it.event.type) + ".title")) + " · " +
                      it.event.time_label,
               tx, y, fonts_->Bold(11.5f * s_), kGold);
      y += 22 * s_;
      const std::vector<std::string> lines = WrapText(fonts_->Bold(15 * s_), EventText(it.event, m), tw);
      for (size_t i = 0; i < lines.size() && i < 2; ++i) {
        DrawText(c, lines[i], x, y, fonts_->Bold(15 * s_), kText);
        y += 19 * s_;
      }
      // Live mini-bars for the top two of the race.
      const int county = m.tree->FindByCode(race.state_code);
      if (county >= 0) {
        const Tally& t = m.results->RaceTally(race, county);
        const std::vector<int> rank = t.Ranking();
        y = std::max(y, win.fBottom - 50 * s_);
        for (size_t k = 0; k < std::min<size_t>(2, rank.size()) && t.TotalVotes() > 0; ++k) {
          const Candidate& cand = race.candidates[rank[k]];
          const election::Party& p = m.data->party(cand.party);
          avatars_->Draw(c, cand, p, x, y - 12 * s_, 18 * s_);
          DrawText(c, Ellipsize(fonts_->Regular(11.5f * s_), L_.CandidateName(cand), 90 * s_), x + 23 * s_, y + 1 * s_,
                   fonts_->Regular(11.5f * s_), kText2);
          const double share = Roll("pip:" + race.id + std::to_string(rank[k]), t.Share(rank[k]) * 10000) / 10000;
          const float bx = x + 120 * s_, bw = tw - 175 * s_;
          c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(bx, y - 5 * s_, bw, 6 * s_), 3 * s_, 3 * s_), P(kGrid));
          c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(bx, y - 5 * s_, bw * static_cast<float>(share), 6 * s_), 3 * s_, 3 * s_),
                       P(p.color));
          DrawText(c, Pct(share), x + tw, y + 1 * s_, fonts_->Bold(11.5f * s_), kText, Align::kRight);
          y += 20 * s_;
        }
      }
    } else if (it.is_news && !it.news.assessments.empty()) {
      const store::Assessment& a = it.news.assessments.front();
      const bool good = a.sentiment == store::Sentiment::kGood, bad = a.sentiment == store::Sentiment::kBad;
      const SkColor col = good ? SkColorSetRGB(46, 204, 113) : bad ? SkColorSetRGB(231, 76, 60) : kText3;
      DrawText(c, std::string(L_.T(good ? "news.good" : bad ? "news.bad" : "news.neutral")) + " · " + it.news.article.source,
               x, y, fonts_->Bold(11.5f * s_), col);
      y += 22 * s_;
      std::string title = it.news.digest.empty() ? it.news.article.title : it.news.digest;
      if (it.news.article.simulated && title.rfind("【模擬】", 0) != 0) title = "【模擬】" + title;
      const std::vector<std::string> lines = WrapText(fonts_->Bold(15 * s_), title, tw);
      for (size_t i = 0; i < lines.size() && i < 2; ++i) {
        DrawText(c, lines[i], x, y, fonts_->Bold(15 * s_), kText);
        y += 19 * s_;
      }
      y = std::max(y + 6 * s_, win.fBottom - 36 * s_);
      float cx = x;
      for (const store::Assessment& as : it.news.assessments) {
        const election::Candidate* cand = m.data->CandidateById(as.candidate_id);
        if (!cand) continue;
        const uint32_t cc = as.sentiment == store::Sentiment::kGood  ? 0xFF2ECC71
                            : as.sentiment == store::Sentiment::kBad ? 0xFFE74C3C
                                                                      : 0xFF7F8C8D;
        const std::string label = L_.CandidateName(*cand) + (as.sentiment == store::Sentiment::kGood  ? " ▲"
                                                             : as.sentiment == store::Sentiment::kBad ? " ▼"
                                                                                                       : " ●");
        const float cw = TextWidth(fonts_->Bold(11 * s_), label) + 12 * s_;
        if (cx + cw > x + tw) break;
        c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(cx, y - 12 * s_, cw, 17 * s_), 8 * s_, 8 * s_), P(cc));
        DrawText(c, label, cx + 6 * s_, y + 1 * s_, fonts_->Bold(11 * s_), SK_ColorWHITE);
        cx += cw + 5 * s_;
      }
      DrawText(c, it.news.model, x + tw, win.fBottom - 12 * s_, fonts_->Regular(10 * s_), kText3, Align::kRight);
    }
    c->restore();
    c->restore();
  };
  const float swap = Ease(pip_swap_);
  if (swap < 1) draw_item(pip_previous_, -w * swap, 1 - swap);
  draw_item(pip_current_, w * (1 - swap), swap);
  // Dwell progress along the bottom edge.
  c->drawRect(SkRect::MakeXYWH(win.fLeft, win.fBottom - 3 * s_, w * std::min(1.f, pip_age_ / hold), 3 * s_),
              P(pip_current_.breaking ? kRed : kGold, 0.8f));
  c->restore();
}

}  // namespace usa::ui
