// SPDX-License-Identifier: BSD-3-Clause
// Direct flat OpenDB replication. No Tcl/SWIG calls in the replication loops.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "odb/db.h"
#include "odb/dbWireCodec.h"
#include "odb/geom.h"
#include "utl/Logger.h"

namespace {
using namespace odb;
using Clock = std::chrono::steady_clock;

double seconds(Clock::time_point start)
{
  return std::chrono::duration<double>(Clock::now() - start).count();
}

int shift(int value, int64_t offset)
{
  const int64_t result = value + offset;
  if (result < std::numeric_limits<int>::min()
      || result > std::numeric_limits<int>::max()) {
    throw std::runtime_error(
        "Translated coordinate exceeds signed 32-bit range");
  }
  return static_cast<int>(result);
}

template <class T>
T* checked(T* value, const char* description)
{
  if (!value) {
    throw std::runtime_error(std::string("Cannot create ") + description
                             + " (duplicate name or unsupported geometry)");
  }
  return value;
}

template <class T>
std::vector<T*> snapshot(dbSet<T> objects)
{
  std::vector<T*> result;
  result.reserve(objects.size());
  for (auto* object : objects) {
    result.push_back(object);
  }
  return result;
}

template <class T>
size_t mappingSize(const std::vector<T*>& objects)
{
  size_t size = 1;
  for (auto* object : objects) {
    size = std::max(size, static_cast<size_t>(object->getId()) + 1);
  }
  return size;
}

void checkCapacity(size_t source_count, int64_t copies, const char* name)
{
  if (source_count
      && static_cast<uint64_t>(copies)
             > (std::numeric_limits<uint32_t>::max() - 1) / source_count) {
    throw std::runtime_error(std::string("Replicated ") + name
                             + " exceed the 32-bit OpenDB object ID capacity");
  }
}

struct NetSource
{
  dbNet* net;
  bool shared;
  dbWire* wire;
  std::vector<dbITerm*> terms;
  std::vector<dbSWire*> swires;
};

// Decode native routing opcodes, translate absolute points, and remap terminal
// references. RECT deltas, extensions, NDRs, layers and via definitions stay
// local.
int mappedJunction(const std::vector<int>& junctions, int id)
{
  if (id < 0 || static_cast<size_t>(id) >= junctions.size()
      || junctions[id] < 0) {
    throw std::runtime_error("Wire references an unmapped junction");
  }
  return junctions[id];
}

void copyColors(dbWireDecoder& decoder,
                dbWireEncoder& encoder,
                std::optional<uint8_t>& color,
                std::optional<dbWireDecoder::ViaColor>& via_color)
{
  const auto next = decoder.getColor();
  if (next != color) {
    if (next) {
      encoder.setColor(*next);
    } else {
      encoder.clearColor();
    }
    color = next;
  }
  const auto next_via = decoder.getViaColor();
  const bool changed = next_via.has_value() != via_color.has_value()
                       || (next_via && via_color
                           && (next_via->bottom_color != via_color->bottom_color
                               || next_via->cut_color != via_color->cut_color
                               || next_via->top_color != via_color->top_color));
  if (changed) {
    if (next_via) {
      encoder.setViaColor(
          next_via->bottom_color, next_via->cut_color, next_via->top_color);
    } else {
      encoder.clearViaColor();
    }
    via_color = next_via;
  }
}

void copyPath(dbWireDecoder& decoder,
              dbWireEncoder& encoder,
              std::vector<int>& junctions,
              int& point_count,
              dbWireDecoder::OpCode opcode)
{
  auto* layer = decoder.getLayer();
  const auto type = decoder.getWireType();
  const int ref = opcode == dbWireDecoder::PATH
                      ? -1
                      : mappedJunction(junctions, decoder.getJunctionValue());
  dbTechLayerRule* rule = nullptr;
  if (decoder.peek() == dbWireDecoder::RULE) {
    decoder.next();
    rule = decoder.getRule();
  }
  point_count = 0;
  if (opcode == dbWireDecoder::PATH) {
    if (rule) {
      encoder.newPath(layer, type, rule);
    } else {
      encoder.newPath(layer, type);
    }
  } else if (opcode == dbWireDecoder::JUNCTION) {
    const auto point = decoder.next();
    if (point == dbWireDecoder::POINT_EXT) {
      int x, y, ext;
      decoder.getPoint(x, y, ext);
      if (rule) {
        encoder.newPathExt(ref, ext, type, rule);
      } else {
        encoder.newPathExt(ref, ext, type);
      }
    } else if (point == dbWireDecoder::POINT) {
      if (rule) {
        encoder.newPath(ref, type, rule);
      } else {
        encoder.newPath(ref, type);
      }
    } else {
      throw std::runtime_error("Missing point after junction");
    }
    junctions.at(decoder.getJunctionId()) = ref;
    point_count = 1;
  } else if (opcode == dbWireDecoder::SHORT) {
    if (rule) {
      encoder.newPathShort(ref, layer, type, rule);
    } else {
      encoder.newPathShort(ref, layer, type);
    }
  } else {
    if (rule) {
      encoder.newPathVirtualWire(ref, layer, type, rule);
    } else {
      encoder.newPathVirtualWire(ref, layer, type);
    }
  }
}

void copyPoint(dbWireDecoder& decoder,
               dbWireEncoder& encoder,
               std::vector<int>& junctions,
               int& point_count,
               dbWireDecoder::OpCode opcode,
               int64_t dx,
               int64_t dy)
{
  const uint32_t property = point_count ? decoder.getProperty() : 0;
  int x, y, id;
  if (opcode == dbWireDecoder::POINT_EXT) {
    int ext;
    decoder.getPoint(x, y, ext);
    id = encoder.addPoint(shift(x, dx), shift(y, dy), ext, property);
  } else {
    decoder.getPoint(x, y);
    id = encoder.addPoint(shift(x, dx), shift(y, dy), property);
  }
  junctions.at(decoder.getJunctionId()) = id;
  ++point_count;
}

void copyWire(dbWire* source,
              dbNet* target,
              int64_t dx,
              int64_t dy,
              const std::vector<dbInst*>& instances,
              const std::vector<dbBTerm*>& pins,
              dbWireEncoder& encoder)
{
  if (!source) {
    return;
  }
  auto* wire = target->getWire();
  if (wire) {
    encoder.append(wire);
    encoder.clearColor();
    encoder.clearViaColor();
  } else {
    wire = checked(dbWire::create(target), "wire");
    encoder.begin(wire);
  }
  dbWireDecoder
      decoder;  // Fresh optional color state for each independent net.
  decoder.begin(source);
  std::vector<int> junctions(source->length(), -1);
  std::optional<uint8_t> color;
  std::optional<dbWireDecoder::ViaColor> via_color;
  int point_count = 0;
  while (true) {
    const auto opcode = decoder.next();
    if (opcode == dbWireDecoder::END_DECODE) {
      break;
    }
    switch (opcode) {
      case dbWireDecoder::PATH:
      case dbWireDecoder::JUNCTION:
      case dbWireDecoder::SHORT:
      case dbWireDecoder::VWIRE: {
        copyPath(decoder, encoder, junctions, point_count, opcode);
        break;
      }
      case dbWireDecoder::POINT:
      case dbWireDecoder::POINT_EXT: {
        copyColors(decoder, encoder, color, via_color);
        copyPoint(decoder, encoder, junctions, point_count, opcode, dx, dy);
        break;
      }
      case dbWireDecoder::VIA:
      case dbWireDecoder::TECH_VIA: {
        copyColors(decoder, encoder, color, via_color);
        const int id = opcode == dbWireDecoder::VIA
                           ? encoder.addVia(decoder.getVia())
                           : encoder.addTechVia(decoder.getTechVia());
        junctions.at(decoder.getJunctionId()) = id;
        break;
      }
      case dbWireDecoder::RECT: {
        int x0, y0, x1, y1;
        decoder.getRect(x0, y0, x1, y1);
        encoder.addRect(x0, y0, x1, y1);
        break;
      }
      case dbWireDecoder::ITERM: {
        auto* term = decoder.getITerm();
        encoder.addITerm(
            instances.at(term->getInst()->getId())->getITerm(term->getMTerm()));
        break;
      }
      case dbWireDecoder::BTERM:
        encoder.addBTerm(pins.at(decoder.getBTerm()->getId()));
        break;
      default:
        throw std::runtime_error("Unexpected wire opcode");
    }
  }
  encoder.end();
}

void copySpecial(const NetSource& source,
                 dbNet* target,
                 const std::vector<dbNet*>& nets,
                 int64_t dx,
                 int64_t dy)
{
  for (auto* old_wire : source.swires) {
    auto* shield = old_wire->getShield();
    auto* wire
        = checked(dbSWire::create(target,
                                  old_wire->getWireType(),
                                  shield ? nets.at(shield->getId()) : nullptr),
                  "special wire");
    for (auto* old : old_wire->getWires()) {
      dbSBox* box;
      if (old->isVia()) {
        const auto xy = old->getViaXY();
        if (auto* via = old->getTechVia()) {
          box = dbSBox::create(wire,
                               via,
                               shift(xy.x(), dx),
                               shift(xy.y(), dy),
                               old->getWireShapeType());
        } else {
          box = dbSBox::create(wire,
                               old->getBlockVia(),
                               shift(xy.x(), dx),
                               shift(xy.y(), dy),
                               old->getWireShapeType());
        }
        checked(box, "special via");
        if (old->hasViaLayerMasks()) {
          box->setViaLayerMask(old->getViaBottomLayerMask(),
                               old->getViaCutLayerMask(),
                               old->getViaTopLayerMask());
        }
      } else {
        if (old->getDirection() == dbSBox::OCTILINEAR) {
          throw std::runtime_error("Octilinear special wires are unsupported");
        }
        box = checked(dbSBox::create(wire,
                                     old->getTechLayer(),
                                     shift(old->xMin(), dx),
                                     shift(old->yMin(), dy),
                                     shift(old->xMax(), dx),
                                     shift(old->yMax(), dy),
                                     old->getWireShapeType(),
                                     old->getDirection()),
                      "special rectangle");
        box->setLayerMask(old->getLayerMask());
      }
    }
  }
}

struct Timings
{
  double instances = 0;
  double nets = 0;
  double pins = 0;
  double routing = 0;
  double rows = 0;
};

struct Counts
{
  uint64_t instances;
  uint64_t nets;
  uint64_t pins;
  uint64_t rows;
};

struct Sources
{
  std::vector<dbInst*> originals;
  std::vector<dbNet*> original_nets;
  std::vector<dbBTerm*> original_pins;
  std::vector<dbRow*> original_rows;
  std::vector<NetSource> sources;
  std::vector<dbWire*> detached;
  uint64_t shared_count;
};

Sources captureSources(dbBlock* block, int64_t copies, bool share)
{
  auto originals = snapshot(block->getInsts());
  auto original_nets = snapshot(block->getNets());
  auto original_pins = snapshot(block->getBTerms());
  auto original_rows = snapshot(block->getRows());
  checkCapacity(originals.size(), copies, "instances");
  checkCapacity(original_nets.size(), copies, "nets");
  checkCapacity(original_pins.size(), copies, "boundary terminals");
  size_t iterm_count = 0;
  for (auto* inst : originals) {
    iterm_count += inst->getMaster()->getMTermCount();
  }
  checkCapacity(iterm_count, copies, "instance terminals");
  std::vector<NetSource> sources;
  std::vector<dbWire*> detached;
  uint64_t shared_count = 0;
  for (auto* net : original_nets) {
    const auto type = net->getSigType();
    const bool shared
        = share && net->isSpecial()
          && (type == dbSigType::POWER || type == dbSigType::GROUND);
    auto* wire = net->getWire();
    if (shared && wire) {
      // Shared regular routing needs an immutable source while target grows.
      auto* saved = checked(dbWire::create(block), "routing snapshot");
      saved->append(wire);
      detached.push_back(saved);
      wire = saved;
    }
    if (!shared && net->isWildConnected()) {
      net->clearWildConnected();
    }
    shared_count += shared;
    sources.push_back({net,
                       shared,
                       wire,
                       snapshot(net->getITerms()),
                       snapshot(net->getSWires())});
  }
  return {std::move(originals),
          std::move(original_nets),
          std::move(original_pins),
          std::move(original_rows),
          std::move(sources),
          std::move(detached),
          shared_count};
}

void copyInstances(dbBlock* block,
                   const std::vector<dbInst*>& originals,
                   std::vector<dbInst*>& instances,
                   const std::string& prefix,
                   int64_t dx,
                   int64_t dy)
{
  for (auto* old : originals) {
    auto* inst = checked(dbInst::create(block,
                                        old->getMaster(),
                                        (prefix + old->getName()).c_str(),
                                        old->isPhysicalOnly()),
                         "instance");
    const auto origin = old->getOrigin();
    inst->setOrient(old->getOrient());
    inst->setOrigin(shift(origin.x(), dx), shift(origin.y(), dy));
    inst->setPlacementStatus(old->getPlacementStatus());
    inst->setSourceType(old->getSourceType());
    inst->setWeight(old->getWeight());
    instances[old->getId()] = inst;
  }
}

void copyNets(dbBlock* block,
              const std::vector<NetSource>& sources,
              std::vector<dbNet*>& nets,
              const std::string& prefix)
{
  for (const auto& source : sources) {
    auto* old = source.net;
    auto* net = old;
    if (!source.shared) {
      net = checked(dbNet::create(block, (prefix + old->getName()).c_str()),
                    "net");
      net->setSigType(old->getSigType());
      net->setWeight(old->getWeight());
      net->setSourceType(old->getSourceType());
      if (old->isSpecial()) {
        net->setSpecial();
      }
      if (auto* rule = old->getNonDefaultRule()) {
        net->setNonDefaultRule(rule);
      }
    }
    nets[old->getId()] = net;
  }
}

void copyPins(dbBlock* block,
              const std::vector<dbBTerm*>& original_pins,
              const std::vector<dbNet*>& nets,
              std::vector<dbBTerm*>& pins,
              const std::string& prefix,
              int64_t dx,
              int64_t dy)
{
  for (auto* old : original_pins) {
    auto* pin = checked(dbBTerm::create(nets.at(old->getNet()->getId()),
                                        (prefix + old->getName()).c_str()),
                        "boundary terminal");
    pin->setIoType(old->getIoType());
    pin->setSigType(old->getSigType());
    if (old->isSpecial()) {
      pin->setSpecial();
    }
    for (auto* old_pin : old->getBPins()) {
      auto* bpin = checked(dbBPin::create(pin), "boundary pin");
      bpin->setPlacementStatus(old_pin->getPlacementStatus());
      for (auto* box : old_pin->getBoxes()) {
        if (box->isVia()) {
          throw std::runtime_error("Boundary pin vias are unsupported");
        }
        auto* copied = checked(dbBox::create(bpin,
                                             box->getTechLayer(),
                                             shift(box->xMin(), dx),
                                             shift(box->yMin(), dy),
                                             shift(box->xMax(), dx),
                                             shift(box->yMax(), dy)),
                               "boundary pin rectangle");
        copied->setLayerMask(box->getLayerMask());
      }
    }
    pins[old->getId()] = pin;
  }
}

void copyRouting(const std::vector<NetSource>& sources,
                 const std::vector<dbNet*>& nets,
                 const std::vector<dbInst*>& instances,
                 const std::vector<dbBTerm*>& pins,
                 dbWireEncoder& encoder,
                 int64_t dx,
                 int64_t dy)
{
  for (const auto& source : sources) {
    auto* net = nets[source.net->getId()];
    for (auto* old : source.terms) {
      auto* term
          = instances[old->getInst()->getId()]->getITerm(old->getMTerm());
      term->connect(net);
      if (old->isSpecial()) {
        term->setSpecial();
      }
    }
    copyWire(source.wire, net, dx, dy, instances, pins, encoder);
    copySpecial(source, net, nets, dx, dy);
  }
}

void copyRows(dbBlock* block,
              const std::vector<dbRow*>& original_rows,
              const std::string& prefix,
              int64_t dx,
              int64_t dy)
{
  for (auto* old : original_rows) {
    const auto origin = old->getOrigin();
    checked(dbRow::create(block,
                          (prefix + old->getName()).c_str(),
                          old->getSite(),
                          shift(origin.x(), dx),
                          shift(origin.y(), dy),
                          old->getOrient(),
                          old->getDirection(),
                          old->getSiteCount(),
                          old->getSpacing()),
            "row");
  }
}

void copyGrids(dbBlock* block, int64_t nx, int64_t ny, int64_t px, int64_t py)
{
  for (auto* grid : block->getTrackGrids()) {
    for (bool x : {true, false}) {
      const int n
          = x ? grid->getNumGridPatternsX() : grid->getNumGridPatternsY();
      for (int k = 0; k < n; ++k) {
        int origin, count, step, mask;
        bool same;
        if (x) {
          grid->getGridPatternX(k, origin, count, step, mask, same);
        } else {
          grid->getGridPatternY(k, origin, count, step, mask, same);
        }
        for (int64_t i = 1; i < (x ? nx : ny); ++i) {
          const int translated = shift(origin, i * (x ? px : py));
          if (x) {
            grid->addGridPatternX(translated, count, step, mask, same);
          } else {
            grid->addGridPatternY(translated, count, step, mask, same);
          }
        }
      }
    }
  }
  if (auto* grid = block->getGCellGrid()) {
    for (bool x : {true, false}) {
      const int n
          = x ? grid->getNumGridPatternsX() : grid->getNumGridPatternsY();
      for (int k = 0; k < n; ++k) {
        int origin, count, step;
        if (x) {
          grid->getGridPatternX(k, origin, count, step);
        } else {
          grid->getGridPatternY(k, origin, count, step);
        }
        for (int64_t i = 1; i < (x ? nx : ny); ++i) {
          const int translated = shift(origin, i * (x ? px : py));
          if (x) {
            grid->addGridPatternX(translated, count, step);
          } else {
            grid->addGridPatternY(translated, count, step);
          }
        }
      }
    }
  }
}

Counts replicate(dbBlock* block,
                 int64_t nx,
                 int64_t ny,
                 int64_t gap_x,
                 int64_t gap_y,
                 bool share,
                 Timings& timings)
{
  if (!block->getChildren().empty() || !block->getRegions().empty()
      || !block->getGroups().empty() || !block->getBlockages().empty()
      || !block->getObstructions().empty() || !block->getFills().empty()) {
    throw std::runtime_error(
        "Expected a flat unfilled benchmark without "
        "children, regions, groups, or blockages");
  }
  const auto die = block->getDieArea();
  const int64_t px = static_cast<int64_t>(die.dx()) + gap_x;
  const int64_t py = static_cast<int64_t>(die.dy()) + gap_y;
  if (px <= 0 || py <= 0 || px > std::numeric_limits<int>::max()
      || py > std::numeric_limits<int>::max()) {
    throw std::runtime_error("Invalid die size or pitch");
  }
  const int max_x = shift(die.xMax(), (nx - 1) * px);
  const int max_y = shift(die.yMax(), (ny - 1) * py);
  const int64_t copies = nx * ny;
  auto captured = captureSources(block, copies, share);
  const auto& originals = captured.originals;
  const auto& original_nets = captured.original_nets;
  const auto& original_pins = captured.original_pins;
  const auto& original_rows = captured.original_rows;
  const auto& sources = captured.sources;
  const auto& detached = captured.detached;
  const auto shared_count = captured.shared_count;
  std::vector<dbInst*> instances(mappingSize(originals));
  std::vector<dbNet*> nets(mappingSize(original_nets));
  std::vector<dbBTerm*> pins(mappingSize(original_pins));
  dbWireEncoder encoder;
  const auto start = Clock::now();
  auto last = start;
  for (int64_t tile = 1; tile < copies; ++tile) {
    const auto col = tile % nx;
    const auto row = tile / nx;
    const auto dx = col * px;
    const auto dy = row * py;
    const std::string prefix
        = "t" + std::to_string(col) + "_" + std::to_string(row) + "__";
    auto phase = Clock::now();
    copyInstances(block, originals, instances, prefix, dx, dy);
    timings.instances += seconds(phase);
    phase = Clock::now();
    copyNets(block, sources, nets, prefix);
    timings.nets += seconds(phase);
    phase = Clock::now();
    copyPins(block, original_pins, nets, pins, prefix, dx, dy);
    timings.pins += seconds(phase);
    phase = Clock::now();
    copyRouting(sources, nets, instances, pins, encoder, dx, dy);
    timings.routing += seconds(phase);
    phase = Clock::now();
    copyRows(block, original_rows, prefix, dx, dy);
    timings.rows += seconds(phase);
    const auto now = Clock::now();
    if (now - last >= std::chrono::seconds(1) || tile + 1 == copies) {
      const double elapsed = seconds(start);
      const int filled = static_cast<int>(24 * (tile + 1) / copies);
      std::cout << "COPY [" << std::string(filled, '#')
                << std::string(24 - filled, '-') << "] " << tile + 1 << '/'
                << copies << " tiles, " << std::fixed << std::setprecision(1)
                << 100.0 * (tile + 1) / copies << "%, elapsed " << elapsed
                << "s, ETA " << elapsed * (copies - tile - 1) / tile << "s\n"
                << std::flush;
      last = now;
    }
  }
  for (auto* saved : detached) {
    dbWire::destroy(saved);
  }
  copyGrids(block, nx, ny, px, py);
  block->setDieArea(Rect(die.xMin(), die.yMin(), max_x, max_y));
  block->setCoreArea(block->computeCoreArea());
  return {originals.size() * copies,
          (original_nets.size() - shared_count) * copies + shared_count,
          original_pins.size() * copies,
          original_rows.size() * copies};
}

int64_t integer(const char* text)
{
  size_t consumed = 0;
  const auto value = std::stoll(text, &consumed);
  if (consumed != std::string(text).size() || value < 0
      || value > std::numeric_limits<int>::max()) {
    throw std::runtime_error("Arguments must be nonnegative 32-bit integers");
  }
  return value;
}
}  // namespace

int main(int argc, char** argv)
{
  try {
    if (argc != 9) {
      throw std::runtime_error(
          "Internal usage: input.odb output.odb nx ny gap_x "
          "gap_y share stats.txt");
    }
    const int64_t nx = integer(argv[3]);
    const int64_t ny = integer(argv[4]);
    const int64_t gap_x = integer(argv[5]);
    const int64_t gap_y = integer(argv[6]);
    const bool share = integer(argv[7]) != 0;
    if (!nx || !ny) {
      throw std::runtime_error("Copy counts must be positive");
    }
    if (std::filesystem::exists(argv[2]) || std::filesystem::exists(argv[8])) {
      throw std::runtime_error("Output or statistics file already exists");
    }
    utl::Logger logger;
    std::unique_ptr<dbDatabase, decltype(&dbDatabase::destroy)> db(
        dbDatabase::create(), dbDatabase::destroy);
    db->setLogger(&logger);
    const auto start = Clock::now();
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
      throw std::runtime_error("Cannot open source ODB");
    }
    db->read(input);
    if (!db->getChip() || !db->getChip()->getBlock()) {
      throw std::runtime_error("Source ODB has no top block");
    }
    const double load = seconds(start);
    auto phase = Clock::now();
    Timings timings;
    const Counts counts = replicate(
        db->getChip()->getBlock(), nx, ny, gap_x, gap_y, share, timings);
    const double copied = seconds(phase);
    phase = Clock::now();
    std::cout << "Saving ODB: " << argv[2] << '\n' << std::flush;
    std::ofstream output(argv[2], std::ios::binary);
    if (!output) {
      throw std::runtime_error("Cannot open output ODB");
    }
    db->write(output);
    output.close();
    if (!output) {
      throw std::runtime_error("Failed writing ODB (check disk space)");
    }
    const double written = seconds(phase);
    phase = Clock::now();
    db.reset();
    const double cleanup = seconds(phase);
    std::ofstream stats(argv[8]);
    stats << std::setprecision(12) << "load_odb_seconds=" << load
          << "\nreplication_seconds=" << copied
          << "\ninstances_seconds=" << timings.instances
          << "\nnets_seconds=" << timings.nets
          << "\npins_seconds=" << timings.pins
          << "\nrouting_seconds=" << timings.routing
          << "\nrows_seconds=" << timings.rows
          << "\nwrite_odb_seconds=" << written
          << "\ncleanup_seconds=" << cleanup
          << "\ninstances=" << counts.instances << "\nnets=" << counts.nets
          << "\nbterms=" << counts.pins << "\nrows=" << counts.rows << '\n';
    stats.close();
    if (!stats) {
      throw std::runtime_error("Failed writing statistics");
    }
    std::cout << "Native replication " << copied << "s; write " << written
              << "s; cleanup " << cleanup << "s\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Native ODB replication failed: " << error.what() << '\n';
    return 1;
  }
}
