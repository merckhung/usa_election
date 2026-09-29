// Administrative hierarchy of the United States: nation -> 50 states + DC ->
// 3,142 counties (county equivalents: parishes, boroughs, census areas,
// independent cities), with projected geometry.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "glm/common.hpp"
#include "glm/vec2.hpp"

namespace usa::geo {

enum class Level : uint8_t { kNation = 0, kState = 1, kCounty = 2, kPrecinct = 3 };

const char* LevelName(Level level);

// A polygon in projected km; rings[0] is the exterior (CCW), others holes (CW).
struct Polygon2D {
  std::vector<std::vector<glm::vec2>> rings;
};

struct Region {
  int id = -1;
  Level level = Level::kNation;
  std::string code;        // "US", state FIPS ("06") or county FIPS ("06037")
  std::string state_code;  // owning state FIPS ("" for the nation)
  std::string abbr;        // postal code for states ("CA"), "" otherwise
  std::string name_en;
  std::string name_zh;     // localised names (fall back to English)
  std::string name_ja;
  std::string name_es;
  int parent = -1;
  std::vector<int> children;
  std::vector<Polygon2D> polygons;
  glm::vec2 min{0}, max{0};  // bounding box (km)
  glm::vec2 label{0};        // good interior point for labels / markers
  float area_km2 = 0;

  glm::vec2 center() const { return (min + max) * 0.5f; }
  glm::vec2 size() const { return max - min; }
};

class RegionTree {
 public:
  // Loads a us-atlas style TopoJSON ("states" and "counties" collections,
  // ids are FIPS codes, pre-projected with Albers USA).
  bool LoadTopoJson(std::string_view json, std::string* error);

  // State metadata: postal abbreviation and localised names by FIPS.
  struct StateInfo {
    std::string fips, abbr, name_zh, name_ja, name_es;
  };
  void ApplyStateInfo(const std::vector<StateInfo>& states);
  // State region id for a postal code ("CA") or FIPS ("06"), or -1.
  int FindState(std::string_view abbr_or_fips) const;

  int size() const { return static_cast<int>(regions_.size()); }
  const Region& region(int id) const { return regions_[id]; }
  const Region& nation() const { return regions_[0]; }

  // Returns the region id for a code, or -1.
  int FindByCode(std::string_view code) const;

  // Returns the child of `parent` whose area contains `p`, or -1.
  int ChildAt(int parent, glm::vec2 p) const;

  // Ids from the nation down to `id` (inclusive).
  std::vector<int> Path(int id) const;

  // Returns the ancestor of `id` at `level` (or `id` itself), or -1.
  int AncestorAt(int id, Level level) const;

  static bool Contains(const Region& r, glm::vec2 p);

 private:
  int AddRegion(Region r);
  void Finalize(Region* r);

  std::vector<Region> regions_;
  std::unordered_map<std::string, int> by_code_;
};

}  // namespace usa::geo
