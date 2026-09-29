#include "src/geo/projection.h"

namespace usa::geo {
namespace {

constexpr Inset kInsets[] = {
    {"02", "AK"},  // Alaska
    {"15", "HI"},  // Hawaii
};

}  // namespace

glm::vec2 Projection::Project(const LonLat& p, std::string_view) {
  // Pixel y grows downwards; km y grows north.
  return glm::vec2(static_cast<float>((p.lon - kCenterX) * kKmPerPixel),
                   static_cast<float>(-(p.lat - kCenterY) * kKmPerPixel));
}

const Inset* Projection::InsetFor(std::string_view fips) {
  for (const Inset& inset : kInsets) {
    if (fips == inset.state_fips) return &inset;
  }
  return nullptr;
}

const Inset* Projection::insets(int* count) {
  *count = static_cast<int>(sizeof(kInsets) / sizeof(kInsets[0]));
  return kInsets;
}

}  // namespace usa::geo
