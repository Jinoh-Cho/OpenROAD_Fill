#pragma once

#include <string>
#include <utility>
#include <vector>

#include "odb/geom.h"
#include "polygon.h"

namespace boost::polygon {
template <typename T>
class polygon_90_set_data;
}

namespace fin {

struct TileGridConfig
{
  odb::Rect region;
  odb::Point origin;
  int window_size;
  int resolution;
};

struct DensityWindow
{
  odb::Rect bounds;
  size_t first_tile_x;
  size_t first_tile_y;
  double metal_area = 0.0;
  double density = 0.0;
  double planned_fill_area = 0.0;
  double post_fill_density = 0.0;
};

enum class TileViolationReason
{
  kCapacity,
  kDiscreteCandidate
};

struct TileViolation
{
  size_t tile_index;
  TileViolationReason reason;
  double required_density;
  double available_density;
};

struct WindowViolation
{
  size_t window_index;
  double required_fill_area;
  double fill_budget;
};

// Windows retained by J40's multilevel density analysis.  window_indices
// refer to TileGrid::windows(), whose tiles are at the finest resolution.
struct MultilevelDensityAnalysisResult
{
  std::vector<size_t> window_indices;
  int levels = 0;
  double max_window_area = 0.0;
  double bloat_max_window_area = 0.0;
};

// Return the minimum and maximum densities across all windows.  An empty
// collection has a density range of {0.0, 0.0}.
std::pair<double, double> getWindowDensityRange(
    const std::vector<DensityWindow>& windows);
std::pair<double, double> getWindowPostFillDensityRange(
    const std::vector<DensityWindow>& windows);

class TileGrid
{
 public:
  explicit TileGrid(const TileGridConfig& config);
  const std::vector<odb::Rect>& tiles() const { return tiles_; }
  std::vector<DensityWindow>& windows() { return windows_; }
  const std::vector<DensityWindow>& windows() const { return windows_; }
  const std::vector<double>& metalAreas() const { return metal_areas_; }
  std::vector<std::vector<size_t>> windowTileIndices() const;
  std::vector<std::vector<size_t>> windowTileIndices(
      const std::vector<size_t>& window_indices) const;
  // Run J40's multilevel maximum-density analysis.  resolution must be a
  // power of two; it is the finest r-dissection used by this TileGrid.
  MultilevelDensityAnalysisResult analyzeMultilevelDensity(
      double relative_accuracy) const;
  bool writeSvg(
      const std::string& filename,
      const boost::polygon::polygon_90_set_data<int>& metal_shapes,
      int dbu_per_micron,
      const std::vector<double>* planned_fill_areas = nullptr,
      bool show_tile_values = true,
      const boost::polygon::polygon_90_set_data<int>* placed_fill_shapes
      = nullptr,
      const std::vector<Polygon90>* fillable_polygons = nullptr,
      const std::vector<double>* target_tile_densities = nullptr,
      const std::vector<TileViolation>* tile_violations = nullptr,
      const std::vector<WindowViolation>* window_violations = nullptr,
      const std::vector<double>* bloated_non_fill_areas = nullptr,
      const std::vector<double>* fillable_region_areas = nullptr) const;
  bool writeLpDensityMaps(
      const std::string& filename,
      const boost::polygon::polygon_90_set_data<int>& metal_shapes,
      const std::vector<double>& planned_fill_areas) const;

  // Calculate the metal area and density of every tile and density window.
  // The polygon set is a union, so overlapping shapes are counted once.
  void calculateMetalDensities(
      const boost::polygon::polygon_90_set_data<int>& metal_shapes);
  const std::vector<double>& metalDensities() const { return metal_densities_; }
  double totalMetalArea() const;

 private:
  void calculateWindowDensities();

  std::vector<odb::Rect> tiles_;
  odb::Rect region_;
  std::vector<DensityWindow> windows_;
  size_t tile_columns_ = 0;
  int tiles_per_window_ = 0;
  std::vector<double> metal_densities_;
  std::vector<double> metal_areas_;
};

}  // namespace fin
