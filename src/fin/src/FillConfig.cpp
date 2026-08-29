// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "FillConfig.h"

#include <algorithm>
#include <iterator>

#include "boost/lexical_cast.hpp"
#include "boost/property_tree/json_parser.hpp"

namespace fin {

using utl::FIN;
namespace pt = boost::property_tree;

namespace {

double getValue(const pt::ptree& tree)
{
  return boost::lexical_cast<double>(tree.data());
}

double getValue(const char* key, const pt::ptree& tree)
{
  return getValue(tree.get_child(key));
}

FillLayerConfig readLayerConfig(const pt::ptree& layer, int dbu)
{
  FillLayerConfig config;
  config.space_to_outline = getValue("space_to_outline", layer) * dbu;
  const auto& non_opc = layer.get_child("non-opc");
  auto& non_opc_config = config.non_opc;
  non_opc_config.space_to_fill = getValue("space_to_fill", non_opc) * dbu;
  non_opc_config.space_to_non_fill
      = getValue("space_to_non_fill", non_opc) * dbu;
  non_opc_config.space_line_end = 0;
  config.num_masks = non_opc.get_child("datatype").size();
  const auto widths = non_opc.get_child("width");
  const auto heights = non_opc.get_child("height");
  std::ranges::transform(widths,
                         heights,
                         std::back_inserter(non_opc_config.shapes),
                         [dbu](const auto& width, const auto& height) {
                           return std::make_pair(getValue(width.second) * dbu,
                                                 getValue(height.second) * dbu);
                         });

  config.has_opc = layer.find("opc") != layer.not_found();
  if (!config.has_opc) {
    return config;
  }
  const auto& opc = layer.get_child("opc");
  auto& opc_config = config.opc;
  config.opc_halo = getValue("halo", opc) * dbu;
  opc_config.space_to_fill = getValue("space_to_fill", opc) * dbu;
  opc_config.space_to_non_fill = getValue("space_to_non_fill", opc) * dbu;
  opc_config.space_line_end = opc.find("space_line_end") != opc.not_found()
                                  ? getValue("space_line_end", opc) * dbu
                                  : 0;
  const auto opc_widths = opc.get_child("width");
  const auto opc_heights = opc.get_child("height");
  std::ranges::transform(opc_widths,
                         opc_heights,
                         std::back_inserter(opc_config.shapes),
                         [dbu](const auto& width, const auto& height) {
                           return std::make_pair(getValue(width.second) * dbu,
                                                 getValue(height.second) * dbu);
                         });
  return config;
}

}  // namespace

FillLayerConfigs loadFillLayerConfigs(const char* filename,
                                      odb::dbTech* tech,
                                      utl::Logger* logger)
{
  pt::ptree tree;
  pt::json_parser::read_json(filename, tree);
  FillLayerConfigs layers;
  const int dbu = tech->getDbUnitsPerMicron();
  for (const auto& [unused_name, layer] : tree.get_child("layers")) {
    const FillLayerConfig config = readLayerConfig(layer, dbu);
    const auto names = layer.get_child_optional("names");
    if (names) {
      for (const auto& [unused, layer_name] : *names) {
        auto* tech_layer = tech->findLayer(layer_name.data().c_str());
        if (tech_layer == nullptr) {
          logger->error(
              FIN, 1, "Layer {} in names was not found.", layer_name.data());
        }
        layers[tech_layer] = config;
      }
      continue;
    }
    const auto layer_name = layer.get<std::string>("name");
    auto* tech_layer = tech->findLayer(layer_name.c_str());
    if (tech_layer == nullptr) {
      logger->error(FIN, 2, "Layer {} not found.", layer_name);
    }
    layers[tech_layer] = config;
  }
  return layers;
}

}  // namespace fin
