// Vote tallies: snapshot parsing, and aggregation up the region hierarchy.
//
// Results file format (usa_election.results/v1), all counts optional:
// {
//   "schema": "usa_election.results/v1",
//   "status": "pre-election" | "counting" | "final",
//   "source": "free text", "updated_at": "ISO-8601",
//   "races": {
//     "2026-GA-senate": { "regions": {
//        "<region code>": { "votes": {"<candidate id>": 123, ...},
//                           "eligible": 0, "ballots_cast": 0,
//                           "units_counted": 0, "units_total": 0 } } } },
//   "house": {   // House of Representatives, called seats per state
//     "GA": { "seats": 14, "won": {"DEM": 5, "REP": 8} } },
//   "declarations": [   // campaign statements, independent of the count
//     { "race": "2026-GA-senate", "candidate": "GA-S-01",
//       "type": "victory" | "concede", "time": "2026-11-03T22:40:00-05:00" } ]
// }
// Region codes are the us-atlas FIPS codes: "US", state (2 digits) or county
// (5 digits). A region without an explicit entry is the sum of its children,
// so feeds may report at any granularity.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "src/election/model.h"
#include "src/geo/region_tree.h"

namespace usa::election {

struct Tally {
  std::vector<int64_t> votes;  // indexed like Race::candidates
  int64_t eligible = 0;
  int64_t ballots_cast = 0;
  int units_counted = 0;  // polling stations (or villages) counted
  int units_total = 0;
  bool has_data = false;

  int64_t TotalVotes() const;
  // Candidate index with most votes, or -1 when no votes.
  int Leader() const;
  // Candidate indices sorted by votes (desc), ties by index.
  std::vector<int> Ranking() const;
  double Share(int candidate) const;
  double Turnout() const;   // ballots_cast / eligible, or 0
  double Progress() const;  // units_counted / units_total, or 0
  void Add(const Tally& other);
};

// House seats called in one state (or nationwide when aggregated).
struct HouseTally {
  int seats = 0;
  std::map<std::string, int> won;  // party code -> seats called
  int Called() const;
  int Won(const std::string& party) const;
  void Add(const HouseTally& other);
};

enum class ResultsStatus { kPreElection, kCounting, kFinal };

const char* StatusLabelEn(ResultsStatus s);

// A candidate declaring victory or conceding. Campaigns do this once their
// own numbers look safe, often before the networks call the race and always
// long before states certify the results.
struct Declaration {
  enum class Type { kVictory, kConcede };
  std::string race_id;
  std::string candidate_id;
  Type type = Type::kVictory;
  std::string time;  // ISO-8601
};

struct ResultsSnapshot {
  ResultsStatus status = ResultsStatus::kPreElection;
  std::string source;
  std::string updated_at;
  bool simulated = false;
  uint64_t version = 0;
  // race id -> region code -> tally
  std::unordered_map<std::string, std::unordered_map<std::string, Tally>> races;
  // state FIPS -> House seats called
  std::unordered_map<std::string, HouseTally> house;
  std::vector<Declaration> declarations;
};

bool ParseResultsJson(std::string_view json, const ElectionData& data, ResultsSnapshot* out,
                      std::string* error);

std::string ResultsToJson(const ResultsSnapshot& snap, const ElectionData& data);

// Read-only aggregated view over a snapshot (memoised).
class ResultsView {
 public:
  ResultsView(const ElectionData* data, const geo::RegionTree* tree,
              std::shared_ptr<const ResultsSnapshot> snapshot, std::string office = kSenate);

  const ResultsSnapshot& snapshot() const { return *snapshot_; }

  // Office whose races the map shows (kSenate, kGovernor or kHouse).
  const std::string& office() const { return office_; }
  void set_office(std::string office) { office_ = std::move(office); }

  // Tally of `race` restricted to `region_id` (which must lie in the race's
  // state, or be the state itself).
  const Tally& RaceTally(const Race& race, int region_id);
  // House seats called in the state containing `region_id` (nation: all).
  HouseTally House(int region_id) const;

  // The current office's race in the state that contains `region_id`, or
  // nullptr (no race there, the nation, or the House view).
  const Race* RaceForRegion(int region_id) const;

 private:
  const ElectionData* data_;
  const geo::RegionTree* tree_;
  std::shared_ptr<const ResultsSnapshot> snapshot_;
  std::unordered_map<std::string, std::unordered_map<int, Tally>> race_memo_;
  std::string office_;
};

}  // namespace usa::election
