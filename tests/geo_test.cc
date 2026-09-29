#include <cmath>
#include <set>

#include "glm/geometric.hpp"
#include "gtest/gtest.h"
#include "src/election/model.h"
#include "src/geo/mesh_builder.h"
#include "src/geo/projection.h"
#include "src/geo/region_tree.h"
#include "src/geo/topojson.h"

namespace usa::geo {
namespace {

// A 2x1 quantized rectangle split into two unit squares that share the arc
// (1,0)-(1,1). Arc coordinates are delta-encoded.
constexpr char kTinyTopo[] = R"({
  "type": "Topology",
  "transform": {"scale": [1, 1], "translate": [0, 0]},
  "arcs": [
    [[1, 0], [0, 1]],
    [[1, 1], [-1, 0], [0, -1], [1, 0]],
    [[1, 0], [1, 0], [0, 1], [-1, 0]]
  ],
  "objects": {
    "counties": {"type": "GeometryCollection", "geometries": [
      {"type": "Polygon", "arcs": [[0, 1]], "properties": {"COUNTYCODE": "A"}},
      {"type": "MultiPolygon", "arcs": [[[2, -1]]], "properties": {"COUNTYCODE": "B"}}
    ]}
  }
})";

TEST(TopoJson, DecodesQuantizedDeltaArcs) {
  Topology topo;
  std::string error;
  ASSERT_TRUE(ParseTopoJson(kTinyTopo, {}, &topo, &error)) << error;
  const auto& counties = topo.objects["counties"];
  ASSERT_EQ(counties.size(), 2u);
  EXPECT_EQ(counties[0].properties.at("COUNTYCODE"), "A");
  // Square A: arc 0 (1,0)->(1,1) then arc 1 (1,1)->(0,1)->(0,0)->(1,0); the
  // junction point shared by consecutive arcs is not duplicated.
  const Ring& a = counties[0].polygons[0].rings[0];
  ASSERT_EQ(a.size(), 5u);
  EXPECT_DOUBLE_EQ(a[1].lon, 1);
  EXPECT_DOUBLE_EQ(a[1].lat, 1);
  EXPECT_DOUBLE_EQ(a[2].lon, 0);
  EXPECT_DOUBLE_EQ(a[3].lat, 0);
  // Square B: arc 2 then arc 0 reversed (~0 == -1): (1,1)->(1,0).
  ASSERT_EQ(counties[1].polygons.size(), 1u);
  const Ring& b = counties[1].polygons[0].rings[0];
  ASSERT_EQ(b.size(), 5u);
  EXPECT_DOUBLE_EQ(b[2].lon, 2);
  EXPECT_DOUBLE_EQ(b[2].lat, 1);
  EXPECT_DOUBLE_EQ(b.back().lon, 1);
  EXPECT_DOUBLE_EQ(b.back().lat, 0);
}

TEST(TopoJson, RejectsGarbage) {
  Topology topo;
  std::string error;
  EXPECT_FALSE(ParseTopoJson("{not json", {}, &topo, &error));
  EXPECT_FALSE(ParseTopoJson(R"({"type":"FeatureCollection"})", {}, &topo, &error));
}

TEST(Projection, AlbersPixelsToKilometres) {
  // us-atlas pixels: the lower 48 span ~960 px for ~4,600 km.
  const glm::vec2 a = Projection::Project({100, 300});
  const glm::vec2 b = Projection::Project({200, 300});
  EXPECT_NEAR(glm::length(a - b), 100 * Projection::kKmPerPixel, 1e-3);
  // y grows down in pixels and up (north) in km.
  EXPECT_GT(Projection::Project({100, 100}).y, Projection::Project({100, 200}).y);
}

TEST(Projection, AlaskaAndHawaiiAreInsets) {
  EXPECT_NE(Projection::InsetFor("02"), nullptr);
  EXPECT_NE(Projection::InsetFor("15"), nullptr);
  EXPECT_EQ(Projection::InsetFor("06"), nullptr);
  int n = 0;
  Projection::insets(&n);
  EXPECT_EQ(n, 2);
}

class UsAtlasTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    std::string json;
    ASSERT_TRUE(election::ReadFile("data/map/counties-albers-10m.json", &json));
    std::string error;
    tree_ = new RegionTree();
    ASSERT_TRUE(tree_->LoadTopoJson(json, &error)) << error;
    tree_->ApplyStateInfo({{"13", "GA", "喬治亞州", "ジョージア州", "Georgia"}});
  }
  static void TearDownTestSuite() { delete tree_; }
  static RegionTree* tree_;
};

RegionTree* UsAtlasTest::tree_ = nullptr;

TEST_F(UsAtlasTest, Hierarchy) {
  const Region& nation = tree_->nation();
  EXPECT_EQ(nation.code, "US");
  EXPECT_EQ(nation.children.size(), 51u);  // 50 states + DC
  int counties = 0;
  for (int c : nation.children) {
    EXPECT_EQ(tree_->region(c).level, Level::kState);
    counties += static_cast<int>(tree_->region(c).children.size());
    for (int k : tree_->region(c).children) {
      EXPECT_EQ(tree_->region(k).level, Level::kCounty);
      EXPECT_EQ(tree_->region(k).state_code, tree_->region(c).code);
    }
  }
  EXPECT_GT(counties, 3100);
  EXPECT_EQ(tree_->region(tree_->FindByCode("13")).children.size(), 159u);  // Georgia
}

TEST_F(UsAtlasTest, NamesCodesAndAbbreviations) {
  const int ga = tree_->FindByCode("13");
  ASSERT_GE(ga, 0);
  EXPECT_EQ(tree_->region(ga).name_en, "Georgia");
  EXPECT_EQ(tree_->FindState("GA"), ga);
  EXPECT_EQ(tree_->region(ga).abbr, "GA");
  EXPECT_EQ(tree_->region(ga).name_zh, "喬治亞州");
  const int fulton = tree_->FindByCode("13121");
  ASSERT_GE(fulton, 0);
  EXPECT_EQ(tree_->region(fulton).name_en, "Fulton");
  EXPECT_EQ(tree_->region(fulton).parent, ga);
}

TEST_F(UsAtlasTest, DistancesArePlausible) {
  // Manhattan to Los Angeles County: ~3,900 km (Albers USA is not exactly
  // equidistant, so allow 8%).
  const glm::vec2 ny = tree_->region(tree_->FindByCode("36061")).label;
  const glm::vec2 la = tree_->region(tree_->FindByCode("06037")).label;
  EXPECT_NEAR(glm::length(ny - la), 3940, 3940 * 0.08);
}

TEST_F(UsAtlasTest, AreasArePlausible) {
  // Georgia: ~154,000 km^2 (land + water).
  const Region& ga = tree_->region(tree_->FindByCode("13"));
  EXPECT_NEAR(ga.area_km2, 154000, 154000 * 0.12);
}

TEST_F(UsAtlasTest, PickingFindsTheRightRegion) {
  const int fulton = tree_->FindByCode("13121");
  const glm::vec2 p = tree_->region(fulton).label;
  const int state = tree_->ChildAt(0, p);
  ASSERT_GE(state, 0);
  EXPECT_EQ(tree_->region(state).code, "13");
  EXPECT_EQ(tree_->ChildAt(state, p), fulton);
  EXPECT_EQ(tree_->AncestorAt(fulton, Level::kState), state);
  EXPECT_EQ(tree_->Path(fulton).size(), 3u);
}

TEST_F(UsAtlasTest, LabelPointsAreInside) {
  int outside = 0;
  for (int c : tree_->nation().children) {
    if (!RegionTree::Contains(tree_->region(c), tree_->region(c).label)) ++outside;
  }
  EXPECT_EQ(outside, 0);
}

TEST_F(UsAtlasTest, MeshTopAreaMatchesPolygonArea) {
  const Region& state = tree_->region(tree_->FindByCode("13"));
  const MapMesh mesh = BuildMapMesh(*tree_, state.children);
  EXPECT_EQ(mesh.region_ids.size(), state.children.size());
  double top_area = 0;
  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    const MapVertex& a = mesh.vertices[mesh.indices[i]];
    const MapVertex& b = mesh.vertices[mesh.indices[i + 1]];
    const MapVertex& c = mesh.vertices[mesh.indices[i + 2]];
    if (a.nz < 0.5f || b.nz < 0.5f || c.nz < 0.5f) continue;  // walls
    top_area += std::fabs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) * 0.5;
  }
  EXPECT_NEAR(top_area, state.area_km2, state.area_km2 * 0.01);
  EXPECT_FALSE(mesh.line_vertices.empty());
  EXPECT_EQ(mesh.line_vertices.size() % 2, 0u);
}

}  // namespace
}  // namespace usa::geo
