// Turns successive results snapshots into election-night events: first
// returns, lead changes, projected winners and completed counts, plus the
// per-region vote deltas and lead flips the map animates. Works for any
// results source (live file feed or simulation).
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/election/results.h"
#include "src/geo/region_tree.h"

namespace usa::election {

// Election-night events. "Breaking" events change the story of the night and
// get a full-width banner plus a map call-out:
//   kLeadChange        the leader is overtaken
//   kVictoryDeclared   a candidate claims victory, usually before the race is
//                      called and always long before certification
//   kConcession        the runner-up concedes
//   kCalled            the margin exceeds every vote still out (race call)
//   kSeatFlip          a race is called for a party other than the incumbent's
//   kIncumbentTrailing a sitting senator/governor falls behind
//   kChamberControl    a party clinches the Senate or House majority
// Regular updates get a call-out and a feed line only:
//   kPollsClosed       the last polls close in one or more states
//   kFirstReturns      first votes in a race
//   kCloseRace         margin under 1% with 70%+ counted
//   kFinal             every reporting unit counted
enum class EventType {
  kFirstReturns,
  kLeadChange,
  kVictoryDeclared,
  kConcession,
  kCalled,
  kIncumbentTrailing,
  kCloseRace,
  kFinal,
  kSeatFlip,
  kPollsClosed,
  kChamberControl,
};

bool IsBreaking(EventType t);
const char* EventKey(EventType t);  // i18n key suffix, e.g. "lead_change"

struct ElectionEvent {
  EventType type = EventType::kFirstReturns;
  const Race* race = nullptr;
  int leader = -1;    // candidate index
  int previous = -1;  // previous leader (lead change)
  int64_t margin = 0;
  double progress = 0;     // share of units counted in the race
  std::string time_label;  // "18:42" (from the snapshot's updated_at)
  // Lead change against a candidate who had already declared victory.
  bool after_declaration = false;
  // kSeatFlip: party gaining the seat / losing it. kChamberControl: office
  // ("senate"/"house"), party and seats. kPollsClosed: state abbreviations.
  std::string office;
  std::string party;
  std::string previous_party;
  int seats = 0;
  std::vector<std::string> states;
};

struct RegionDelta {
  int region = -1;
  const Race* race = nullptr;
  int64_t votes_added = 0;
};

struct RegionFlip {
  int region = -1;
  const Race* race = nullptr;
  int leader = -1;
  int previous = -1;
};

struct EventBatch {
  std::vector<ElectionEvent> events;
  std::vector<RegionDelta> deltas;  // counties, towns and villages with new votes
  std::vector<RegionFlip> flips;    // regions whose local leader changed
  int64_t votes_added = 0;          // nationwide, all races
  bool baseline = false;            // first snapshot: state recorded, no events
};

class EventTracker {
 public:
  // With `data`, national events (poll closings, chamber control) are
  // reported too.
  explicit EventTracker(const geo::RegionTree* tree, const ElectionData* data = nullptr)
      : tree_(tree), data_(data) {}

  // Compares `view` with the previous snapshot seen.
  EventBatch Update(ResultsView& view, const std::vector<Race>& races);

  // True once a race's winner is projected (or the count is complete).
  bool IsCalled(const std::string& race_id) const;
  // Candidate index a called race was called for, or -1.
  int CalledFor(const std::string& race_id) const;
  void Reset();

  // Projection rule: the trailing candidate would need to win the votes
  // still to come (estimated from the average per counted unit, +15%) by
  // more than 70/30 (60/40 with 75% counted), i.e. margin > share x
  // remaining x 1.15.
  static constexpr double kCallShare = 0.4;
  static constexpr double kCallShareLate = 0.2;
  static bool Decided(const Tally& t);

 private:
  void NationalEvents(ResultsView& view, EventBatch* batch);

  struct RegionState {
    int64_t total = 0;
    int leader = -1;
  };
  struct RaceState {
    bool called = false;
    bool final = false;
    bool close_alerted = false;
    bool incumbent_alerted = false;
    int declared = -1;  // candidate who declared victory
    int leader = -1;    // leader as reported in events (with hysteresis)
    int called_for = -1;
    size_t declarations_seen = 0;
  };
  const geo::RegionTree* tree_;
  const ElectionData* data_;
  bool has_baseline_ = false;
  double last_minute_ = -1e9;
  std::unordered_map<std::string, bool> chamber_called_;
  std::unordered_map<uint64_t, RegionState> regions_;  // (race index, region)
  std::unordered_map<std::string, RaceState> races_;
};

// Election-night clock. Minute 0 is 18:00 Eastern Time on election day
// (2026-11-03, EST = UTC-5 since daylight saving time ended on Nov 1); the
// night runs to 06:00 ET on Nov 4, when the western mail-heavy states are
// still counting.
inline constexpr double kNightMinutes = 720;
inline constexpr int kNightStartHour = 18;
// "21:30" for `minutes` after 18:00 ET (wraps past midnight).
std::string NightClockLabel(double minutes);
// "2026-11-03T21:30:00-05:00" for `minutes` after 18:00 ET.
std::string NightTimestamp(double minutes);

// "18:42" from "2026-11-03T18:42:00-05:00"; "" when absent.
std::string TimeLabel(const std::string& updated_at);
// Minutes after 18:00 ET on election day for a timestamp (times on Nov 4 are
// past midnight, i.e. > 360), or -1 when unparseable.
double MinutesAfterClose(const std::string& updated_at);

}  // namespace usa::election
