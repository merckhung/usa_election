#include "src/election/model.h"

#include <fstream>
#include <sstream>

#include "nlohmann/json.hpp"

namespace usa::election {

using nlohmann::json;

bool ReadFile(const std::string& path, std::string* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  *out = ss.str();
  return true;
}

uint32_t ParseColor(std::string_view hex, uint32_t fallback) {
  if (hex.size() != 7 || hex[0] != '#') return fallback;
  uint32_t v = 0;
  for (size_t i = 1; i < 7; ++i) {
    const char c = hex[i];
    v <<= 4;
    if (c >= '0' && c <= '9') v |= c - '0';
    else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
    else return fallback;
  }
  return 0xFF000000u | v;
}

int Race::CandidateIndex(std::string_view candidate_id) const {
  for (size_t i = 0; i < candidates.size(); ++i) {
    if (candidates[i].id == candidate_id) return static_cast<int>(i);
  }
  return -1;
}

std::string Race::Title() const {
  if (office == kGovernor) return state_name + " Governor";
  return state_name + " Senate" + (special ? " (special)" : "");
}

bool ElectionData::Load(const std::string& dir, std::string* error) {
  std::string parties, candidates, election, states;
  for (auto [name, dst] : {std::pair{"parties.json", &parties},
                           std::pair{"candidates.json", &candidates},
                           std::pair{"election.json", &election},
                           std::pair{"states.json", &states}}) {
    if (!ReadFile(dir + "/" + name, dst)) {
      *error = "cannot read " + dir + "/" + name;
      return false;
    }
  }
  return LoadFromJson(parties, candidates, election, states, error);
}

bool ElectionData::LoadFromJson(std::string_view parties_json, std::string_view candidates_json,
                                std::string_view election_json, std::string_view states_json,
                                std::string* error) {
  const json pj = json::parse(parties_json, nullptr, false);
  const json cj = json::parse(candidates_json, nullptr, false);
  const json ej = json::parse(election_json, nullptr, false);
  const json sj = json::parse(states_json, nullptr, false);
  if (pj.is_discarded() || cj.is_discarded() || ej.is_discarded() || sj.is_discarded()) {
    *error = "invalid election JSON";
    return false;
  }
  try {
    parties_.clear();
    for (const json& p : pj.at("parties")) {
      Party party;
      party.code = p.at("code").get<std::string>();
      party.name_en = p.value("name_en", party.code);
      party.name_es = p.value("name_es", party.name_en);
      party.name_zh = p.value("name_zh", party.name_en);
      party.name_ja = p.value("name_ja", party.name_en);
      party.short_en = p.value("short_en", party.code);
      party.short_es = p.value("short_es", party.short_en);
      party.short_zh = p.value("short_zh", party.short_en);
      party.short_ja = p.value("short_ja", party.short_en);
      party.color = ParseColor(p.value("color", ""), 0xFF8C8C8C);
      parties_.push_back(std::move(party));
    }
    unknown_party_ = Party{"OTH", "Other", "Otro", "其他", "その他", "Oth", "Otro", "其他", "他",
                           0xFF8C8C8C};

    states_.clear();
    for (const json& s : sj.at("states")) {
      StateInfo st;
      st.fips = s.at("fips").get<std::string>();
      st.abbr = s.at("abbr").get<std::string>();
      st.name = s.value("name", st.abbr);
      st.name_zh = s.value("name_zh", st.name);
      st.name_ja = s.value("name_ja", st.name);
      st.name_es = s.value("name_es", st.name);
      st.poll_close_minutes = s.value("poll_close_minutes", 60.0);
      st.house_seats = s.value("house_seats", 0);
      st.house_dem = s.value("house_dem", 0);
      st.house_rep = s.value("house_rep", 0);
      st.house_other = s.value("house_other", 0);
      st.poll_close_label = s.value("poll_close_et", "");
      st.governor_party = s.value("governor_party", "");
      states_.push_back(std::move(st));
    }

    info_.id = ej.value("id", "");
    info_.date = ej.value("date", "2026-11-03");
    info_.next_date = ej.value("next_date", "2026-11-04");
    info_.name = ej.value("name", "");
    info_.status_note = ej.value("status_note", "");

    races_.clear();
    for (const json& r : cj.at("races")) {
      Race race;
      race.id = r.at("id").get<std::string>();
      race.office = r.value("office", kSenate);
      race.special = r.value("special", false);
      race.state_abbr = r.at("state").get<std::string>();
      const StateInfo* st = State(race.state_abbr);
      if (!st) {
        *error = "unknown state " + race.state_abbr + " in race " + race.id;
        return false;
      }
      race.state_code = st->fips;
      race.state_name = st->name;
      race.state_name_zh = st->name_zh;
      race.state_name_ja = st->name_ja;
      race.state_name_es = st->name_es;
      race.status_note = r.value("status_note", "");
      if (r.contains("incumbent")) {
        const json& inc = r["incumbent"];
        race.incumbent.name = inc.value("name", "");
        race.incumbent.party = inc.value("party", "");
        race.incumbent.status = inc.value("status", "");
        race.incumbent.term_limited = race.incumbent.status == "term-limited";
        race.incumbent.note = inc.value("note", "");
      }
      for (const json& c : r.at("candidates")) {
        Candidate cand;
        cand.id = c.at("id").get<std::string>();
        cand.name = c.value("name", "");
        cand.party = c.value("party", "OTH");
        cand.party_label = c.value("party_label", "");
        if (c.contains("endorsed_by")) {
          cand.endorsed_by = c["endorsed_by"].get<std::vector<std::string>>();
        }
        cand.incumbent = c.value("incumbent", false);
        cand.photo = c.value("photo", "");
        cand.bioguide = c.value("bioguide", "");
        cand.note = c.value("note", "");
        race.candidates.push_back(std::move(cand));
      }
      races_.push_back(std::move(race));
    }

    auto chamber = [](const json& j, const char* office) {
      Chamber c;
      c.office = office;
      c.total = j.value("total", 0);
      c.majority = j.value("majority", 0);
      c.before = j.value("before", std::map<std::string, int>{});
      c.holdovers = j.value("holdovers", std::map<std::string, int>{});
      const json tb = j.value("tie_breaker", json::object());
      c.tie_breaker_party = tb.value("party", "");
      c.tie_breaker_name = tb.value("name", "");
      c.caucus = j.value("caucus", std::map<std::string, std::string>{});
      return c;
    };
    info_.senate = chamber(ej.at("senate"), kSenate);
    info_.house = chamber(ej.at("house"), kHouse);
    info_.governors_before = ej.value("governors_before", std::map<std::string, int>{});
    info_.offices.clear();
    for (const json& o : ej.value("offices", json::array())) {
      info_.offices.push_back({o.value("office", ""), o.value("seats", 0)});
    }
  } catch (const json::exception& ex) {
    *error = std::string("election JSON schema error: ") + ex.what();
    return false;
  }
  return true;
}

const Party& ElectionData::party(std::string_view code) const {
  for (const Party& p : parties_) {
    if (p.code == code) return p;
  }
  return unknown_party_;
}

const Race* ElectionData::RaceForState(std::string_view fips, std::string_view office) const {
  for (const Race& r : races_) {
    if (r.state_code == fips && r.office == office) return &r;
  }
  return nullptr;
}

const Race* ElectionData::RaceById(std::string_view id) const {
  for (const Race& r : races_) {
    if (r.id == id) return &r;
  }
  return nullptr;
}

const Candidate* ElectionData::CandidateById(std::string_view id, const Race** race) const {
  for (const Race& r : races_) {
    for (const Candidate& c : r.candidates) {
      if (c.id == id) {
        if (race) *race = &r;
        return &c;
      }
    }
  }
  return nullptr;
}

const StateInfo* ElectionData::State(std::string_view key) const {
  for (const StateInfo& s : states_) {
    if (s.fips == key || s.abbr == key) return &s;
  }
  return nullptr;
}

std::vector<const Race*> ElectionData::RacesFor(std::string_view office) const {
  std::vector<const Race*> out;
  for (const Race& r : races_) {
    if (r.office == office) out.push_back(&r);
  }
  return out;
}

}  // namespace usa::election
