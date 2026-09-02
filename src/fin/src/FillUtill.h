#pragma once

#include <string>
#include <utility>
#include <vector>

#include "odb/geom.h"

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
  bool writeSvg(const std::string& filename,
                const boost::polygon::polygon_90_set_data<int>& metal_shapes,
                int dbu_per_micron,
                const std::vector<double>* planned_fill_areas = nullptr,
                bool show_tile_values = true) const;
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
