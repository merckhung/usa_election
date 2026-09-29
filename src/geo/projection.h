// Map projection for the US map.
//
// The map data (us-atlas counties-albers-10m.json) is already projected with
// d3.geoAlbersUsa into a 975x610 pixel frame, with Alaska and Hawaii moved
// into insets at the lower left. We convert those pixels to approximate
// kilometres (x east, y north, origin near the centre of the lower 48) so the
// rest of the app can keep working in km.
#pragma once

#include <string_view>

#include "glm/vec2.hpp"
#include "src/geo/topojson.h"

namespace usa::geo {

// Screen-space frame around an inset (Alaska, Hawaii), in projected km.
struct Inset {
  const char* state_fips;
  const char* label;  // i18n key suffix
};

class Projection {
 public:
  // Albers pixels -> km. 1 px of the 975-wide frame is ~4.8 km in the lower 48.
  static constexpr double kKmPerPixel = 4.8;
  static constexpr double kCenterX = 487.5;
  static constexpr double kCenterY = 305.0;

  // `p.lon`/`p.lat` hold the pre-projected pixel x/y.
  static glm::vec2 Project(const LonLat& p, std::string_view state_fips = {});
  static glm::vec2 ProjectRaw(const LonLat& p) { return Project(p); }

  // States drawn as insets (not to scale / position).
  static const Inset* InsetFor(std::string_view state_fips);
  static const Inset* insets(int* count);
};

}  // namespace usa::geo
