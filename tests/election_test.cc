#include <filesystem>
#include <map>
#include <set>

#include "gtest/gtest.h"
#include "src/election/model.h"
#include "src/election/results.h"
#include "src/election/results_source.h"
#include "src/geo/region_tree.h"

namespace usa::election {
namespace {

class ElectionDataTest : public ::testing::Test {
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

ElectionData* ElectionDataTest::data_ = nullptr;
geo::RegionTree* ElectionDataTest::tree_ = nullptr;

TEST_F(ElectionDataTest, SenateAndGovernorRaces) {
  EXPECT_EQ(data_->info().date, "2026-11-03");
  const auto senate = data_->RacesFor(kSenate);
  const auto governors = data_->RacesFor(kGovernor);
  // 33 Class II seats + specials in Ohio and Florida; 36 governorships.
  EXPECT_EQ(senate.size(), 35u);
  EXPECT_EQ(governors.size(), 36u);
  int specials = 0;
  std::set<std::string> ids, senate_states;
  for (const Race& r : data_->races()) {
    EXPECT_GE(r.candidates.size(), 2u) << r.id;
    EXPECT_EQ(r.id, "2026-" + r.state_abbr + "-" + r.office);
    specials += r.special;
    if (r.office == kSenate) senate_states.insert(r.state_abbr);
    for (const Candidate& c : r.candidates) {
      EXPECT_TRUE(ids.insert(c.id).second) << "duplicate id " << c.id;
      EXPECT_EQ(c.id.substr(0, 2), r.state_abbr);
      EXPECT_FALSE(c.name.empty());
    }
  }
  EXPECT_EQ(specials, 2);
  EXPECT_EQ(senate_states.size(), 35u);  // one Senate race per state this cycle
  EXPECT_EQ(data_->RaceForState("39", kSenate)->special, true);  // Ohio
}

TEST_F(ElectionDataTest, ChamberRules) {
  const Chamber& senate = data_->info().senate;
  EXPECT_EQ(senate.total, 100);
  EXPECT_EQ(senate.majority, 51);
  int holdovers = 0;
  for (const auto& [p, n] : senate.holdovers) holdovers += n;
  EXPECT_EQ(holdovers + static_cast<int>(data_->RacesFor(kSenate).size()), 100);
  EXPECT_EQ(senate.tie_breaker_party, "REP");
  EXPECT_EQ(senate.CaucusOf("IND"), "DEM");
  const Chamber& house = data_->info().house;
  EXPECT_EQ(house.total, 435);
  EXPECT_EQ(house.majority, 218);
  int seats = 0;
  for (const StateInfo& st : data_->states()) seats += st.house_seats;
  EXPECT_EQ(seats, 435);
  int governors = 0;
  for (const auto& [p, n] : data_->info().governors_before) governors += n;
  EXPECT_EQ(governors, 50);
}

TEST_F(ElectionDataTest, StatesAndPollClosings) {
  EXPECT_EQ(data_->states().size(), 51u);
  const StateInfo* ga = data_->State("GA");
  ASSERT_NE(ga, nullptr);
  EXPECT_EQ(ga, data_->State("13"));
  EXPECT_EQ(ga->name, "Georgia");
  EXPECT_DOUBLE_EQ(ga->poll_close_minutes, 60);  // 19:00 ET
  EXPECT_DOUBLE_EQ(data_->State("AK")->poll_close_minutes, 420);  // 01:00 ET
  for (const StateInfo& st : data_->states()) {
    EXPECT_FALSE(st.name_zh.empty()) << st.abbr;
    EXPECT_FALSE(st.name_ja.empty()) << st.abbr;
    EXPECT_FALSE(st.name_es.empty()) << st.abbr;
    EXPECT_GE(st.poll_close_minutes, 60) << st.abbr;
    EXPECT_LE(st.poll_close_minutes, 420) << st.abbr;
  }
}

TEST_F(ElectionDataTest, EveryPartyIsKnownAndColoured) {
  std::set<std::string> codes;
  for (const Party& p : data_->parties()) {
    codes.insert(p.code);
    EXPECT_NE(p.color & 0x00FFFFFF, 0u) << p.code;
    EXPECT_FALSE(p.name_ja.empty()) << p.code;
    EXPECT_FALSE(p.name_zh.empty()) << p.code;
    EXPECT_FALSE(p.name_es.empty()) << p.code;
  }
  for (const Race& r : data_->races()) {
    EXPECT_TRUE(codes.count(r.incumbent.party)) << r.id << " " << r.incumbent.party;
    for (const Candidate& c : r.candidates) {
      EXPECT_TRUE(codes.count(c.party)) << c.name << " " << c.party;
    }
  }
  EXPECT_EQ(data_->party("DEM").name_en, "Democratic Party");
  EXPECT_EQ(data_->party("REP").name_zh, "共和黨");
}

TEST_F(ElectionDataTest, EveryRaceMapsToAState) {
  for (const Race& r : data_->races()) {
    const int id = tree_->FindByCode(r.state_code);
    ASSERT_GE(id, 0) << r.state_code;
    EXPECT_EQ(tree_->region(id).name_en, r.state_name);
    EXPECT_EQ(data_->RaceForState(r.state_code, r.office), &r);
  }
}

TEST_F(ElectionDataTest, PhotosReferencedExistOrAreOptional) {
  int present = 0;
  for (const Race& r : data_->races()) {
    for (const Candidate& c : r.candidates) {
      present += std::filesystem::exists(c.photo);
    }
  }
  EXPECT_GE(present, 40);
}

TEST_F(ElectionDataTest, ResultsAggregateUpTheHierarchy) {
  const Race& georgia = *data_->RaceForState("13", kSenate);
  const int state = tree_->FindByCode("13");
  const int fulton = tree_->FindByCode("13121");
  const int dekalb = tree_->FindByCode("13089");
  ASSERT_GE(fulton, 0);
  ASSERT_GE(dekalb, 0);
  const std::string a = georgia.candidates[0].id, b = georgia.candidates[1].id;
  std::string json = R"({"status": "counting", "races": {"2026-GA-senate": {"regions": {)";
  json += R"("13121": {"votes": {")" + a + R"(": 100, ")" + b + R"(": 300}, "eligible": 1000,
                 "ballots_cast": 420, "units_counted": 10, "units_total": 12},)";
  json += R"("13089": {"votes": {")" + a + R"(": 50, ")" + b + R"(": 10, "bogus": 99},
                 "units_counted": 1, "units_total": 1}}}},
             "house": {"GA": {"seats": 14, "won": {"DEM": 3, "REP": 5}}}})";
  auto snap = std::make_shared<ResultsSnapshot>();
  std::string error;
  ASSERT_TRUE(ParseResultsJson(json, *data_, snap.get(), &error)) << error;
  EXPECT_EQ(snap->status, ResultsStatus::kCounting);
  ResultsView view(data_, tree_, snap);
  const Tally& t = view.RaceTally(georgia, state);
  EXPECT_EQ(t.votes[0], 150);
  EXPECT_EQ(t.votes[1], 310);
  EXPECT_EQ(t.TotalVotes(), 460);
  EXPECT_EQ(t.Leader(), 1);
  EXPECT_EQ(t.units_counted, 11);
  EXPECT_EQ(t.units_total, 13);
  EXPECT_NEAR(t.Share(1), 310.0 / 460.0, 1e-9);
  EXPECT_EQ(view.RaceTally(georgia, dekalb).Leader(), 0);
  EXPECT_EQ(view.RaceForRegion(fulton), &georgia);
  view.set_office(kGovernor);
  EXPECT_EQ(view.RaceForRegion(fulton), data_->RaceForState("13", kGovernor));
  view.set_office(kHouse);
  EXPECT_EQ(view.RaceForRegion(fulton), nullptr);
  // House: per state and nationwide.
  const HouseTally ga = view.House(fulton);
  EXPECT_EQ(ga.seats, 14);
  EXPECT_EQ(ga.Won("DEM"), 3);
  EXPECT_EQ(ga.Called(), 8);
  const HouseTally us = view.House(0);
  EXPECT_EQ(us.seats, 435);
  EXPECT_EQ(us.Called(), 8);
}

TEST_F(ElectionDataTest, ResultsJsonRoundTrip) {
  SimulatedResultsSource sim(data_, tree_, 7, 60);
  auto snap = sim.SnapshotAt(0.6);
  const std::string json = ResultsToJson(*snap, *data_);
  auto parsed = std::make_shared<ResultsSnapshot>();
  std::string error;
  ASSERT_TRUE(ParseResultsJson(json, *data_, parsed.get(), &error)) << error;
  ResultsView a(data_, tree_, snap), b(data_, tree_, parsed);
  for (const Race& r : data_->races()) {
    const int county = tree_->FindByCode(r.state_code);
    EXPECT_EQ(a.RaceTally(r, county).votes, b.RaceTally(r, county).votes) << r.id;
  }
  EXPECT_TRUE(parsed->simulated);
}

TEST_F(ElectionDataTest, SimulationIsDeterministicAndMonotonic) {
  SimulatedResultsSource sim(data_, tree_, 42, 60);
  auto early = sim.SnapshotAt(0.3);
  auto late = sim.SnapshotAt(0.9);
  auto again = sim.SnapshotAt(0.9);
  ResultsView ve(data_, tree_, early), vl(data_, tree_, late), va(data_, tree_, again);
  for (const Race& r : data_->races()) {
    const int state = tree_->FindByCode(r.state_code);
    EXPECT_LE(ve.RaceTally(r, state).units_counted, vl.RaceTally(r, state).units_counted);
    EXPECT_EQ(vl.RaceTally(r, state).votes, va.RaceTally(r, state).votes);
  }
  EXPECT_LE(ve.House(0).Called(), vl.House(0).Called());
  // At 06:00 ET mail-heavy states are still counting: not final.
  auto morning = sim.SnapshotAt(1.0);
  EXPECT_EQ(morning->status, ResultsStatus::kCounting);
  ResultsView vm(data_, tree_, morning);
  const Race& ca = *data_->RaceForState("06", kGovernor);
  EXPECT_LT(vm.RaceTally(ca, tree_->FindByCode("06")).Progress(), 0.9);
  EXPECT_LT(vm.House(tree_->FindByCode("06")).Called(), 52);
  EXPECT_TRUE(morning->simulated);
}

TEST(Color, ParsesHex) {
  EXPECT_EQ(ParseColor("#1A48B5", 0), 0xFF1A48B5u);
  EXPECT_EQ(ParseColor("1A48B5", 7), 7u);
  EXPECT_EQ(ParseColor("#GG0000", 7), 7u);
}

}  // namespace
}  // namespace usa::election
