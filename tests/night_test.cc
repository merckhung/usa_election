// Election-night simulation and event detection.
#include <cstdio>
#include <map>
#include <set>

#include "gtest/gtest.h"
#include "src/election/events.h"
#include "src/election/model.h"
#include "src/election/results.h"
#include "src/election/results_source.h"
#include "src/geo/region_tree.h"

namespace usa::election {
namespace {

class NightTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    data_ = new ElectionData();
    std::string error;
    ASSERT_TRUE(data_->Load("data/election/2026", &error)) << error;
    std::string json;
    ASSERT_TRUE(ReadFile("data/map/counties-albers-10m.json", &json));
    tree_ = new geo::RegionTree();
    ASSERT_TRUE(tree_->LoadTopoJson(json, &error)) << error;
  }
  static void TearDownTestSuite() {
    delete data_;
    delete tree_;
  }
  static ElectionData* data_;
  static geo::RegionTree* tree_;
};

ElectionData* NightTest::data_ = nullptr;
geo::RegionTree* NightTest::tree_ = nullptr;

TEST_F(NightTest, SpeedSnapsToPowersOfTwo) {
  SimulatedResultsSource sim(data_, tree_, 1, 64);
  EXPECT_EQ(sim.speed(), 64);
  sim.set_speed(100);
  EXPECT_EQ(sim.speed(), 128);
  sim.set_speed(0.3);
  EXPECT_EQ(sim.speed(), 1);
  sim.set_speed(1e9);
  EXPECT_EQ(sim.speed(), 4096);
  sim.SlowDown();
  EXPECT_EQ(sim.speed(), 2048);
  sim.SpeedUp();
  EXPECT_EQ(sim.speed(), 4096);
}

TEST_F(NightTest, ClockAdvancesWithSpeed) {
  SimulatedResultsSource sim(data_, tree_, 1, 60);  // -> x64
  sim.Poll(0);
  sim.Poll(60);  // one real minute at x64 = 64 simulated minutes
  EXPECT_NEAR(sim.clock_minutes(), 64, 1e-6);
  sim.set_paused(true);
  sim.Poll(120);
  EXPECT_NEAR(sim.clock_minutes(), 64, 1e-6);
  EXPECT_EQ(SimulatedResultsSource::ClockLabel(64), "19:04");
  EXPECT_EQ(SimulatedResultsSource::ClockLabel(390), "00:30");
}

TEST_F(NightTest, CountFollowsPollClosingsThroughTheNight) {
  SimulatedResultsSource sim(data_, tree_, 20261103, 64);
  EXPECT_GT(sim.station_count(), 10000);
  // Nothing before the first polls close at 19:00 ET.
  EXPECT_EQ(sim.SnapshotAtClock(55)->status, ResultsStatus::kPreElection);
  EXPECT_EQ(sim.SnapshotAtClock(120)->status, ResultsStatus::kCounting);
  EXPECT_EQ(sim.SnapshotAtClock(120)->updated_at, "2026-11-03T20:00:00-05:00");
  EXPECT_EQ(sim.SnapshotAtClock(400)->updated_at, "2026-11-04T00:40:00-05:00");
  // States only report after their own polls close: California (23:00 ET)
  // has nothing at 22:30, Georgia (19:00) has votes.
  auto snap = sim.SnapshotAtClock(270);
  ResultsView view(data_, tree_, snap);
  EXPECT_EQ(view.RaceTally(*data_->RaceForState("06", kGovernor), tree_->FindByCode("06")).TotalVotes(), 0);
  EXPECT_GT(view.RaceTally(*data_->RaceForState("13", kSenate), tree_->FindByCode("13")).TotalVotes(), 0);
  EXPECT_EQ(view.House(tree_->FindByCode("06")).Called(), 0);
  EXPECT_GT(view.House(tree_->FindByCode("13")).Called(), 0);
  // Still counting at 06:00 (late mail ballots out west).
  EXPECT_EQ(sim.SnapshotAtClock(720)->status, ResultsStatus::kCounting);
  EXPECT_GT(sim.last_report_minute(), 720);
}

TEST_F(NightTest, WholeNightProducesTheExpectedStory) {
  SimulatedResultsSource sim(data_, tree_, 20261103, 64);
  EventTracker tracker(tree_, data_);
  std::map<EventType, int> counts;
  std::map<std::string, int> control;
  std::set<std::string> closed;
  for (int minute = 0; minute <= 720; minute += 2) {
    auto snap = sim.SnapshotAtClock(minute);
    ResultsView view(data_, tree_, snap);
    const EventBatch batch = tracker.Update(view, data_->races());
    EXPECT_EQ(batch.baseline, minute == 0);
    for (const ElectionEvent& e : batch.events) {
      counts[e.type]++;
      if (e.type == EventType::kChamberControl) control[e.office]++;
      if (e.type == EventType::kPollsClosed) closed.insert(e.states.begin(), e.states.end());
      if (e.race) EXPECT_TRUE(e.states.empty());
    }
  }
  const int races = static_cast<int>(data_->races().size());
  std::printf("called %d declared %d lead changes %d flips %d control senate %d house %d\n",
              counts[EventType::kCalled], counts[EventType::kVictoryDeclared],
              counts[EventType::kLeadChange], counts[EventType::kSeatFlip], control["senate"],
              control["house"]);
  EXPECT_EQ(counts[EventType::kFirstReturns], races);
  EXPECT_EQ(closed.size(), 51u);  // every state's polls close during the night
  EXPECT_GE(counts[EventType::kCalled], races / 2);
  EXPECT_LE(counts[EventType::kCalled], races);
  EXPECT_GE(counts[EventType::kVictoryDeclared], races / 2);
  EXPECT_GE(counts[EventType::kLeadChange], 5);
  EXPECT_GE(counts[EventType::kSeatFlip], 1);
  // The House majority is always reached; each chamber is announced once.
  EXPECT_EQ(control["house"], 1);
  EXPECT_LE(control["senate"], 1);
  EXPECT_TRUE(IsBreaking(EventType::kChamberControl));
  EXPECT_FALSE(IsBreaking(EventType::kPollsClosed));
}

TEST_F(NightTest, DeclarationsRoundTripThroughJson) {
  SimulatedResultsSource sim(data_, tree_, 20261128, 64);
  auto snap = sim.SnapshotAtClock(600);
  ASSERT_FALSE(snap->declarations.empty());
  ASSERT_FALSE(snap->house.empty());
  const std::string json = ResultsToJson(*snap, *data_);
  ResultsSnapshot parsed;
  std::string error;
  ASSERT_TRUE(ParseResultsJson(json, *data_, &parsed, &error)) << error;
  ASSERT_EQ(parsed.declarations.size(), snap->declarations.size());
  EXPECT_EQ(parsed.declarations[0].candidate_id, snap->declarations[0].candidate_id);
  EXPECT_EQ(parsed.declarations[0].type, snap->declarations[0].type);
  ResultsView a(data_, tree_, snap), b(data_, tree_, std::make_shared<ResultsSnapshot>(parsed));
  EXPECT_EQ(a.House(0).Called(), b.House(0).Called());
  EXPECT_EQ(a.House(0).Won("DEM"), b.House(0).Won("DEM"));
}

TEST_F(NightTest, InflowHistoryMatchesSnapshots) {
  SimulatedResultsSource sim(data_, tree_, 5, 64);
  const auto buckets = sim.InflowHistory(5, 300);
  int64_t sum = 0;
  for (int64_t b : buckets) sum += b;
  auto snap = sim.SnapshotAtClock(300);
  ResultsView view(data_, tree_, snap);
  int64_t total = 0;
  for (const Race& r : data_->races()) total += view.RaceTally(r, tree_->FindByCode(r.state_code)).TotalVotes();
  EXPECT_EQ(sum, total);
}

// Hand-made snapshots for the tracker rules.
class TrackerTest : public NightTest {
 protected:
  std::shared_ptr<ResultsSnapshot> Snap(int64_t a, int64_t b, int counted, int total,
                                        const std::string& time) {
    auto s = std::make_shared<ResultsSnapshot>();
    const Race& race = *data_->RaceForState("13", kSenate);
    Tally t;
    t.votes.assign(race.candidates.size(), 0);
    t.votes[1] = a;  // Mike Collins (REP)
    t.votes[0] = b;  // Jon Ossoff (DEM, incumbent)
    t.units_counted = counted;
    t.units_total = total;
    t.has_data = true;
    s->races[race.id]["13"] = t;
    s->updated_at = "2026-11-03T" + time + ":00-05:00";
    return s;
  }
  EventBatch Feed(EventTracker& tracker, std::shared_ptr<ResultsSnapshot> s) {
    ResultsView view(data_, tree_, s);
    return tracker.Update(view, data_->races());
  }
};

TEST_F(TrackerTest, LeadChangeNeedsAMargin) {
  EventTracker tracker(tree_);
  Feed(tracker, Snap(0, 0, 0, 100, "18:00"));  // baseline
  auto b = Feed(tracker, Snap(1000, 900, 10, 100, "19:30"));
  ASSERT_EQ(b.events.size(), 1u);
  EXPECT_EQ(b.events[0].type, EventType::kFirstReturns);
  EXPECT_EQ(b.events[0].time_label, "19:30");
  // Overtaken by 10 votes: below the hysteresis margin, no event.
  b = Feed(tracker, Snap(2000, 2010, 20, 100, "19:40"));
  for (const auto& e : b.events) EXPECT_NE(e.type, EventType::kLeadChange);
  // Clear overtake: lead change (plus incumbent no longer trailing is silent).
  b = Feed(tracker, Snap(3000, 3500, 30, 100, "19:50"));
  bool lead_change = false;
  for (const auto& e : b.events) {
    if (e.type == EventType::kLeadChange) {
      lead_change = true;
      EXPECT_EQ(e.leader, 0);
      EXPECT_EQ(e.previous, 1);
      EXPECT_TRUE(IsBreaking(e.type));
    }
  }
  EXPECT_TRUE(lead_change);
}

TEST_F(TrackerTest, IncumbentTrailingCalledAndFinal) {
  EventTracker tracker(tree_);
  Feed(tracker, Snap(0, 0, 0, 100, "18:00"));
  Feed(tracker, Snap(100, 50, 1, 100, "19:20"));
  auto b = Feed(tracker, Snap(40000, 30000, 40, 100, "20:00"));
  std::set<EventType> types;
  for (const auto& e : b.events) types.insert(e.type);
  EXPECT_TRUE(types.count(EventType::kIncumbentTrailing));
  EXPECT_FALSE(types.count(EventType::kCalled));  // 60% still out
  b = Feed(tracker, Snap(90000, 30000, 90, 100, "23:00"));
  types.clear();
  for (const auto& e : b.events) types.insert(e.type);
  EXPECT_TRUE(types.count(EventType::kCalled));
  // Called for the Republican against a Democratic incumbent: a seat flip.
  EXPECT_TRUE(types.count(EventType::kSeatFlip));
  for (const auto& e : b.events) {
    if (e.type == EventType::kSeatFlip) {
      EXPECT_EQ(e.party, "REP");
      EXPECT_EQ(e.previous_party, "DEM");
    }
  }
  EXPECT_TRUE(tracker.IsCalled("2026-GA-senate"));
  EXPECT_EQ(tracker.CalledFor("2026-GA-senate"), 1);
  b = Feed(tracker, Snap(99000, 33000, 100, 100, "23:30"));
  ASSERT_EQ(b.events.size(), 1u);
  EXPECT_EQ(b.events[0].type, EventType::kFinal);
}

TEST_F(TrackerTest, DeclarationsBecomeEvents) {
  EventTracker tracker(tree_);
  Feed(tracker, Snap(0, 0, 0, 100, "18:00"));
  auto s = Snap(5000, 3000, 50, 100, "21:00");
  s->declarations.push_back({"2026-GA-senate", "GA-S-02", Declaration::Type::kVictory,
                             "2026-11-03T21:05:00-05:00"});
  auto b = Feed(tracker, s);
  bool victory = false;
  for (const auto& e : b.events) {
    if (e.type == EventType::kVictoryDeclared) {
      victory = true;
      EXPECT_EQ(e.leader, 1);
      EXPECT_EQ(e.time_label, "21:05");
    }
  }
  EXPECT_TRUE(victory);
  // Same declaration again: no duplicate.
  auto s2 = Snap(6000, 3500, 60, 100, "21:10");
  s2->declarations = s->declarations;
  b = Feed(tracker, s2);
  for (const auto& e : b.events) EXPECT_NE(e.type, EventType::kVictoryDeclared);
}

TEST_F(TrackerTest, PollClosingsAndChamberControl) {
  EventTracker tracker(tree_, data_);
  Feed(tracker, Snap(0, 0, 0, 100, "18:00"));
  auto s = Snap(10, 5, 1, 100, "19:00");
  auto b = Feed(tracker, s);
  bool georgia_closed = false;
  for (const auto& e : b.events) {
    if (e.type == EventType::kPollsClosed) {
      EXPECT_EQ(e.time_label, "19:00");
      for (const auto& st : e.states) georgia_closed |= st == "GA";
    }
  }
  EXPECT_TRUE(georgia_closed);
  // 218 House seats for one party: control.
  auto h = Snap(10, 5, 1, 100, "23:00");
  h->house["06"].won["DEM"] = 52;
  h->house["36"].won["DEM"] = 26;
  h->house["48"].won["DEM"] = 38;
  h->house["12"].won["DEM"] = 28;
  h->house["17"].won["DEM"] = 17;
  h->house["42"].won["DEM"] = 17;
  h->house["39"].won["DEM"] = 15;
  h->house["26"].won["DEM"] = 13;
  h->house["37"].won["DEM"] = 12;  // 218
  b = Feed(tracker, h);
  int control = 0;
  for (const auto& e : b.events) {
    if (e.type == EventType::kChamberControl) {
      ++control;
      EXPECT_EQ(e.office, "house");
      EXPECT_EQ(e.party, "DEM");
      EXPECT_EQ(e.seats, 218);
    }
  }
  EXPECT_EQ(control, 1);
  b = Feed(tracker, h);  // announced once
  for (const auto& e : b.events) EXPECT_NE(e.type, EventType::kChamberControl);
}

TEST(Events, TimeLabels) {
  EXPECT_EQ(TimeLabel("2026-11-03T18:42:00-05:00"), "18:42");
  EXPECT_EQ(TimeLabel(""), "");
  EXPECT_DOUBLE_EQ(MinutesAfterClose("2026-11-03T18:42:00-05:00"), 42);
  EXPECT_DOUBLE_EQ(MinutesAfterClose("2026-11-04T01:30:00-05:00"), 450);
  EXPECT_DOUBLE_EQ(MinutesAfterClose("garbage"), -1);
  EXPECT_EQ(NightClockLabel(0), "18:00");
  EXPECT_EQ(NightClockLabel(360), "00:00");
  EXPECT_EQ(NightTimestamp(90), "2026-11-03T19:30:00-05:00");
  EXPECT_EQ(NightTimestamp(420), "2026-11-04T01:00:00-05:00");
}

}  // namespace
}  // namespace usa::election
