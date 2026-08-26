#pragma once

#include <string>
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
  std::vector<size_t> tile_indices;
};

class TileGrid
{
 public:
  explicit TileGrid(const TileGridConfig& config);
  const std::vector<odb::Rect>& tiles() const { return tiles_; }
  const std::vector<DensityWindow>& windows() const { return windows_; }
  size_t tileWindow(size_t tile_index) const
  {
    return tile_to_window_.at(tile_index);
  }
  bool writeSvg(
      const std::string& filename,
      const boost::polygon::polygon_90_set_data<int>& metal_shapes,
      int dbu_per_micron) const;

  // Calculate the fraction of each tile covered by the supplied metal shapes.
  // The polygon set is a union, so overlapping shapes are counted once.
  void calculateMetalDensities(
      const boost::polygon::polygon_90_set_data<int>& metal_shapes);
  const std::vector<double>& metalDensities() const { return metal_densities_; }
  double totalMetalArea() const;

 private:
  std::vector<odb::Rect> tiles_;
  odb::Rect region_;
  std::vector<DensityWindow> windows_;
  std::vector<size_t> tile_to_window_;
  std::vector<double> metal_densities_;
};

}  // namespace fin
