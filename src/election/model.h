// Static election data for the US midterm elections of 2026-11-03:
// parties, Senate and governor races with candidates, chamber rules
// (Senate holdovers and tie-breaker, House majority), House delegations and
// per-state poll-closing times. Loaded from data/election/2026/*.json.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace usa::election {

// Offices shown on the map / dashboard.
inline constexpr const char* kSenate = "senate";
inline constexpr const char* kGovernor = "governor";
inline constexpr const char* kHouse = "house";

struct Party {
  std::string code;  // "DEM", "REP", "IND", "LIB", "GRN", "OTH"
  std::string name_en, name_es, name_zh, name_ja;
  std::string short_en, short_es, short_zh, short_ja;
  uint32_t color = 0xFF8C8C8C;  // ARGB
};

struct Candidate {
  std::string id;  // "<state>-<S|G>-<nn>", e.g. "GA-S-01"
  std::string name;
  std::string party;                     // party code
  std::string party_label;               // minor party's own name when party is OTH
  std::vector<std::string> endorsed_by;  // e.g. an independent backed by a party
  std::optional<int> ballot_no;          // unused in the US (kept for the UI)
  bool incumbent = false;
  std::string photo;     // path relative to the data root
  std::string bioguide;  // Congress bioguide id when known
  std::string note;
};

struct Incumbent {
  std::string name;
  std::string party;
  std::string status;  // "running", "retiring", "term-limited", "appointed", ...
  bool term_limited = false;
  std::string note;
};

struct Race {
  std::string id;          // "2026-GA-senate"
  std::string office;      // kSenate or kGovernor
  bool special = false;    // special election (e.g. Ohio, Florida Senate)
  std::string state_code;  // FIPS "13"
  std::string state_abbr;  // "GA"
  std::string state_name;  // "Georgia"
  std::string state_name_zh, state_name_ja, state_name_es;
  Incumbent incumbent;
  std::vector<Candidate> candidates;
  std::string status_note;  // e.g. "runoff pending"

  int CandidateIndex(std::string_view candidate_id) const;
  // "Georgia Senate", "Ohio Senate (special)", "Arizona Governor".
  std::string Title() const;
};

struct StateInfo {
  std::string fips, abbr, name, name_zh, name_ja, name_es;
  double poll_close_minutes = 60;  // minutes after 18:00 ET when the last polls close
  int house_seats = 0;
  int house_dem = 0, house_rep = 0, house_other = 0;  // delegation before the election
  std::string poll_close_label;                        // "20:00" (ET)
  std::string governor_party;        // current governor's party
};

struct Chamber {
  std::string office;  // kSenate / kHouse
  int total = 0;
  int majority = 0;  // seats needed for outright control
  std::map<std::string, int> before;     // seats by party before the election
  std::map<std::string, int> holdovers;  // Senate seats not up this year
  std::string tie_breaker_party;         // Vice President's party (Senate)
  std::string tie_breaker_name;
  std::map<std::string, std::string> caucus;  // e.g. IND -> DEM
  // Party a seat won by `party` is counted with for control.
  std::string CaucusOf(const std::string& party) const {
    const auto it = caucus.find(party);
    return it == caucus.end() ? party : it->second;
  }
};

struct OfficeSummary {
  std::string office;
  int seats = 0;  // seats / races on the ballot
};

struct Referendum {  // kept for API compatibility (no national referendum)
  std::string id;
  int case_no = 0;
  std::string question_zh, question_en, question_ja;
};

struct ElectionInfo {
  std::string id;
  std::string date;       // "2026-11-03"
  std::string next_date;  // "2026-11-04" (counting continues past midnight)
  std::string name;
  std::string status_note;
  Chamber senate;
  Chamber house;
  std::map<std::string, int> governors_before;
  std::vector<OfficeSummary> offices;
  std::vector<Referendum> referendums;  // empty
};

class ElectionData {
 public:
  bool Load(const std::string& dir, std::string* error);
  bool LoadFromJson(std::string_view parties_json, std::string_view candidates_json,
                    std::string_view election_json, std::string_view states_json,
                    std::string* error);

  const ElectionInfo& info() const { return info_; }
  const std::vector<Party>& parties() const { return parties_; }
  const std::vector<Race>& races() const { return races_; }
  const std::vector<StateInfo>& states() const { return states_; }

  const Party& party(std::string_view code) const;  // falls back to OTH/IND
  const Race* RaceForState(std::string_view state_fips, std::string_view office) const;
  const Race* RaceById(std::string_view id) const;
  const Candidate* CandidateById(std::string_view id, const Race** race = nullptr) const;
  const StateInfo* State(std::string_view fips_or_abbr) const;
  // Races for one office, in state order.
  std::vector<const Race*> RacesFor(std::string_view office) const;

 private:
  ElectionInfo info_;
  std::vector<Party> parties_;
  std::vector<Race> races_;
  std::vector<StateInfo> states_;
  Party unknown_party_;
};

bool ReadFile(const std::string& path, std::string* out);
uint32_t ParseColor(std::string_view hex, uint32_t fallback);

}  // namespace usa::election
