#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors
"""Tile a rectangular DEF using only the Python standard library.

Example: python3 src/fin/test/replicate_def.py input.def output.def --nx 2 --ny 3

Offsets and --gap-x/--gap-y use DEF database units, not microns. Each tile
gets a t<column>_<row>__ name prefix. Orientations are preserved (no mirroring).
Use --share-power to connect all tiles to the same POWER/GROUND special nets.
Without it, wildcard special-net connections are rejected: expanding those
correctly requires LEF knowledge of which cells have the referenced pin.

This is a deliberately limited DEF transformer, not a complete DEF parser.
Supported sections: COMPONENTS, PINS, NETS, SPECIALNETS, REGIONS, GROUPS,
BLOCKAGES, FILLS, VIAS, NONDEFAULTRULES, PROPERTYDEFINITIONS; also ROW,
TRACKS and GCELLGRID. Unsupported sections/clauses fail before output is
replaced. Properties and technology definitions are preserved as written.
Pin geometry and via definitions remain local; route points, placement and
block-level geometry move with the tile. Comments/formatting are not retained.
The input is held in memory; replicated output is streamed to a temporary file.
"""

import argparse
import gzip
import os
from pathlib import Path
import re
import tempfile


TOKEN = re.compile(r'#[^\n]*|"(?:\\.|[^"\\])*"|(?:\\.|[^\s();])+|[();]')
INTEGER = re.compile(r"-?\d+\Z")
SECTIONS = {
    "COMPONENTS", "PINS", "NETS", "SPECIALNETS", "REGIONS", "GROUPS",
    "BLOCKAGES", "FILLS", "VIAS", "NONDEFAULTRULES",
}
STATIC = {"VIAS", "NONDEFAULTRULES", "PROPERTYDEFINITIONS"}
HEADERS = {"VERSION", "DIVIDERCHAR", "BUSBITCHARS", "DESIGN", "UNITS"}
CLAUSES = {
    "COMPONENTS": {
        "SOURCE", "EEQMASTER", "GENERATE", "WEIGHT", "PROPERTY", "REGION",
        "UNPLACED", "PLACED", "FIXED", "COVER", "HALO", "ROUTEHALO", "MASKSHIFT",
    },
    "PINS": {
        "NET", "SPECIAL", "DIRECTION", "NETEXPR", "SUPPLYSENSITIVITY",
        "GROUNDSENSITIVITY", "USE", "PORT", "LAYER", "POLYGON", "VIA",
        "PLACED", "FIXED", "COVER", "PROPERTY", "ANTENNAMODEL",
        "ANTENNAPINPARTIALMETALAREA", "ANTENNAPINPARTIALMETALSIDEAREA",
        "ANTENNAPINDIFFAREA", "ANTENNAPINPARTIALCUTAREA",
        "ANTENNAPINGATEAREA", "ANTENNAPINMAXAREACAR", "ANTENNAPINMAXSIDEAREACAR",
        "ANTENNAPINMAXCUTCAR",
    },
    "NETS": {
        "ROUTED", "FIXED", "COVER", "SOURCE", "USE", "NONDEFAULTRULE",
        "WEIGHT", "ESTCAP", "XTALK", "FREQUENCY", "PATTERN", "PROPERTY",
        "SHIELDNET", "NOSHIELD", "FIXEDBUMP",
    },
    "SPECIALNETS": {
        "ROUTED", "FIXED", "COVER", "SHIELD", "SHAPE", "MASK", "RECT",
        "POLYGON", "SOURCE", "USE", "VOLTAGE", "WEIGHT", "ESTCAP",
        "FREQUENCY", "PATTERN", "PROPERTY", "FIXEDBUMP", "WIDTH", "SPACING",
    },
    "REGIONS": {"TYPE", "PROPERTY"},
    "GROUPS": {"REGION", "PROPERTY", "SOFT"},
    "BLOCKAGES": {
        "COMPONENT", "SLOTS", "FILLS", "PUSHDOWN", "EXCEPTPGNET", "SOFT",
        "PARTIAL", "SPACING", "DESIGNRULEWIDTH", "MASK",
    },
    "FILLS": {"MASK", "OPC"},
}


class Section:
    def __init__(self, name, entries):
        self.name = name
        self.entries = entries


def read_def(path):
    opener = gzip.open if path.suffix == ".gz" else open
    with opener(path, "rt", encoding="utf-8") as stream:
        tokens = [t for t in TOKEN.findall(stream.read()) if not t.startswith("#")]
    items = []
    pos = 0
    while pos < len(tokens):
        name = tokens[pos]
        if name == "END":
            if tokens[pos:] != ["END", "DESIGN"]:
                raise ValueError("Expected END DESIGN at end of input")
            items.append(["END", "DESIGN"])
            break
        if name == "PROPERTYDEFINITIONS":
            end = pos + 1
            while tokens[end:end + 2] != ["END", name]:
                if end >= len(tokens):
                    raise ValueError(f"Missing END {name}")
                end += 1
            items.append(Section(name, [tokens[pos + 1:end]]))
            pos = end + 2
            continue
        if name not in SECTIONS | HEADERS | {"DIEAREA", "ROW", "TRACKS", "GCELLGRID"}:
            raise ValueError(f"Unsupported DEF statement/section: {name}")
        end = tokens.index(";", pos) + 1
        statement = tokens[pos:end]
        pos = end
        if name not in SECTIONS:
            items.append(statement)
            continue
        if len(statement) != 3 or not statement[1].isdigit():
            raise ValueError(f"Invalid {name} section header")
        entries = []
        while tokens[pos:pos + 2] != ["END", name]:
            if pos >= len(tokens) or tokens[pos] != "-":
                raise ValueError(f"Invalid entry or missing END {name}")
            end = tokens.index(";", pos) + 1
            entries.append(tokens[pos:end])
            pos = end
        if len(entries) != int(statement[1]):
            raise ValueError(f"{name}: declared count differs from actual entries")
        items.append(Section(name, entries))
        pos += 2
    if not items or items[-1] != ["END", "DESIGN"]:
        raise ValueError("Missing END DESIGN")
    return items


def clauses(tokens):
    """Yield clause positions, excluding quoted strings containing '+'."""
    return [i for i, token in enumerate(tokens[:-1]) if token == "+"]


def shift_points(tokens, dx, dy):
    """Move coordinate pairs/triples; retain '*' and route extension lengths.

    Routed RECT offsets have four values in one pair of parentheses. Those
    are relative to the preceding point and must remain unchanged.
    """
    for i, token in enumerate(tokens):
        if token != "(":
            continue
        end = tokens.index(")", i)
        values = tokens[i + 1:end]
        if len(values) not in (2, 3):
            continue
        if not all(value == "*" or INTEGER.fullmatch(value) for value in values):
            continue
        for j, offset in ((i + 1, dx), (i + 2, dy)):
            if tokens[j] != "*":
                tokens[j] = str(int(tokens[j]) + offset)


def placed_points(tokens, dx, dy):
    for i in clauses(tokens):
        if tokens[i + 1] in {"PLACED", "FIXED", "COVER"}:
            if tokens[i + 2] != "(":
                raise ValueError("Invalid placement coordinate")
            tokens[i + 3] = str(int(tokens[i + 3]) + dx)
            tokens[i + 4] = str(int(tokens[i + 4]) + dy)


def prefix_name(name, prefix):
    if name.startswith('"'):
        return '"' + prefix + name[1:]
    return prefix + name


def shared_power_names(items, enabled):
    names = set()
    if enabled:
        for item in items:
            if isinstance(item, Section) and item.name == "SPECIALNETS":
                for entry in item.entries:
                    if any(entry[i + 1] == "USE" and entry[i + 2] in {"POWER", "GROUND"}
                           for i in clauses(entry)):
                        names.add(entry[1])
    return names


def transform(entry, section, prefix, dx, dy, shared):
    if section in {"NETS", "SPECIALNETS"} and entry[1] == "MUSTJOIN":
        raise ValueError("MUSTJOIN nets are not supported")
    result = entry.copy()
    positions = clauses(result)
    for i in positions:
        if result[i + 1] not in CLAUSES[section]:
            raise ValueError(f"Unsupported {section} clause: + {result[i + 1]}")

    def rename(name):
        return prefix_name(name, prefix)

    def net_name(name):
        return name if name in shared else rename(name)

    if section not in {"BLOCKAGES", "FILLS"}:
        result[1] = (net_name if section in {"NETS", "SPECIALNETS"} else rename)(result[1])
    if section in {"COMPONENTS", "PINS"}:
        placed_points(result, dx, dy)
    elif section in {"NETS", "SPECIALNETS"}:
        # Numeric instance/pin names are not coordinates.
        start = positions[0] if positions else len(result) - 1
        routing = result[start:]
        shift_points(routing, dx, dy)
        result[start:] = routing
    elif section != "GROUPS":
        shift_points(result, dx, dy)

    for i in positions:
        key = result[i + 1]
        if section == "PINS" and key == "NET":
            result[i + 2] = net_name(result[i + 2])
        if section == "PINS" and key in {"SUPPLYSENSITIVITY", "GROUNDSENSITIVITY"}:
            result[i + 2] = rename(result[i + 2])
        if section == "PINS" and key == "NETEXPR":
            raise ValueError("PINS + NETEXPR cannot be safely renamed")
        if section in {"COMPONENTS", "GROUPS"} and key == "REGION":
            if result[i + 2] == "(":
                end = next((p for p in positions if p > i), len(result) - 1)
                points = result[i + 2:end]
                shift_points(points, dx, dy)
                result[i + 2:end] = points
            else:
                result[i + 2] = rename(result[i + 2])
        if section == "BLOCKAGES" and key == "COMPONENT":
            result[i + 2] = rename(result[i + 2])
        if section in {"NETS", "SPECIALNETS"} and key in {"SHIELD", "SHIELDNET"}:
            result[i + 2] = net_name(result[i + 2])

    if section == "GROUPS":
        end = positions[0] if positions else len(result) - 1
        result[2:end] = [rename(name) for name in result[2:end]]
    if section in {"NETS", "SPECIALNETS"}:
        # Connections occur before routing/options; never rename route tokens.
        end = positions[0] if positions else len(result) - 1
        i = 2
        while i < end:
            if result[i] != "(" or i + 3 >= end or result[i + 3] != ")":
                raise ValueError(f"Unsupported {section} connection in {entry[1]}")
            instance, pin = result[i + 1:i + 3]
            if instance == "PIN":
                result[i + 2] = rename(pin)
            elif instance == "*":
                if entry[1] not in shared:
                    raise ValueError(
                        f"Wildcard connection in {entry[1]} requires shared POWER/GROUND "
                        "special nets (--share-power), or explicit input connections"
                    )
            else:
                result[i + 1] = rename(instance)
            i += 4
    return result


def die_bounds(items):
    dies = [item for item in items if isinstance(item, list) and item[0] == "DIEAREA"]
    if len(dies) != 1:
        raise ValueError("Exactly one DIEAREA is required")
    die = dies[0]
    if len(die) != 10 or die[1] != "(" or die[5] != "(":
        raise ValueError("Only rectangular DIEAREA is supported")
    x0, y0, x1, y1 = (int(die[i]) for i in (2, 3, 6, 7))
    if x1 <= x0 or y1 <= y0:
        raise ValueError("DIEAREA must have positive width and height")
    return x0, y0, x1, y1


def write_tiles(items, stream, args):
    x0, y0, x1, y1 = die_bounds(items)
    pitch_x = x1 - x0 + args.gap_x
    pitch_y = y1 - y0 + args.gap_y
    shared = shared_power_names(items, args.share_power)

    def tiles():
        for row in range(args.ny):
            for column in range(args.nx):
                yield f"t{column}_{row}__", column * pitch_x, row * pitch_y

    def emit(tokens):
        stream.write(" ".join(tokens) + "\n")

    for item in items:
        if isinstance(item, Section):
            section = item.name
            if section == "PROPERTYDEFINITIONS":
                emit([section])
                emit(item.entries[0])
            elif section in STATIC:
                emit([section, str(len(item.entries)), ";"])
                for entry in item.entries:
                    emit(entry)
            else:
                shared_count = sum(entry[1] in shared for entry in item.entries) if section == "SPECIALNETS" else 0
                count = (len(item.entries) - shared_count) * args.nx * args.ny + shared_count
                emit([section, str(count), ";"])
                for entry in item.entries:
                    if section == "SPECIALNETS" and entry[1] in shared:
                        # Retain metadata once; merge each tile's connections/routes.
                        metadata_keys = {"USE", "SOURCE", "VOLTAGE", "WEIGHT", "ESTCAP",
                                         "FREQUENCY", "PATTERN", "PROPERTY", "FIXEDBUMP",
                                         "WIDTH", "SPACING"}
                        route_start = next((i for i in clauses(entry)
                                            if entry[i + 1] in {"ROUTED", "FIXED", "COVER", "SHIELD", "RECT", "POLYGON"}), len(entry) - 1)
                        if any(entry[i + 1] in metadata_keys for i in clauses(entry) if i > route_start):
                            raise ValueError("Shared special-net metadata must precede routing")
                        # DEF connections must all precede the first '+' clause.
                        first_clause = next(iter(clauses(entry)), len(entry) - 1)
                        emit(["-", entry[1]])
                        seen_wildcards = set()
                        for prefix, dx, dy in tiles():
                            converted = transform(entry, section, prefix, dx, dy, shared)
                            for i in range(2, first_clause, 4):
                                connection = converted[i:i + 4]
                                if connection[1] == "*":
                                    pin = connection[2]
                                    if pin in seen_wildcards:
                                        continue
                                    seen_wildcards.add(pin)
                                emit(connection)
                        emit(entry[first_clause:route_start])
                        for prefix, dx, dy in tiles():
                            converted = transform(entry, section, prefix, dx, dy, shared)
                            emit(converted[route_start:-1])
                        emit([";"])
                    else:
                        for prefix, dx, dy in tiles():
                            emit(transform(entry, section, prefix, dx, dy, shared))
            emit(["END", section])
            continue
        key = item[0]
        if key == "DIEAREA":
            emit([key, "(", str(x0), str(y0), ")", "(",
                  str(x1 + (args.nx - 1) * pitch_x),
                  str(y1 + (args.ny - 1) * pitch_y), ")", ";"])
        elif key == "DESIGN":
            emit([key, prefix_name(item[1], f"tile{args.nx}x{args.ny}__"), ";"])
        elif key == "ROW":
            for prefix, dx, dy in tiles():
                row = item.copy()
                row[1] = prefix_name(row[1], prefix)
                row[3] = str(int(row[3]) + dx)
                row[4] = str(int(row[4]) + dy)
                emit(row)
        elif key in {"TRACKS", "GCELLGRID"}:
            if item[1] not in {"X", "Y"}:
                raise ValueError(f"Invalid {key} axis")
            count, pitch = (args.nx, pitch_x) if item[1] == "X" else (args.ny, pitch_y)
            for index in range(count):
                grid = item.copy()
                grid[2] = str(int(grid[2]) + index * pitch)
                emit(grid)
        else:
            emit(item)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", type=Path, help="Input DEF or DEF.gz")
    parser.add_argument("output", type=Path, help="Output DEF or DEF.gz (must not exist)")
    parser.add_argument("--nx", type=int, required=True, help="Number of columns")
    parser.add_argument("--ny", type=int, required=True, help="Number of rows")
    parser.add_argument("--gap-x", type=int, default=0, help="Horizontal gap in DEF DBU")
    parser.add_argument("--gap-y", type=int, default=0, help="Vertical gap in DEF DBU")
    parser.add_argument("--share-power", action="store_true", help="Share POWER/GROUND special nets across all tiles")
    args = parser.parse_args()
    if args.nx < 1 or args.ny < 1 or args.gap_x < 0 or args.gap_y < 0:
        parser.error("nx/ny must be positive; gaps must be nonnegative")
    temporary = None
    try:
        if args.output.exists() or args.input.resolve() == args.output.resolve():
            raise ValueError("Output must be a new file, different from input")
        items = read_def(args.input)
        with tempfile.NamedTemporaryFile(dir=args.output.parent, prefix=".replicate_def_", delete=False) as stream:
            temporary = Path(stream.name)
        opener = gzip.open if args.output.suffix == ".gz" else open
        with opener(temporary, "wt", encoding="utf-8") as stream:
            write_tiles(items, stream, args)
        # Atomic no-overwrite publication, including concurrent invocations.
        os.link(temporary, args.output)
    except (OSError, ValueError, IndexError) as error:
        parser.exit(1, f"error: {error}\n")
    finally:
        if temporary is not None:
            temporary.unlink()
    print(f"Created {args.output} ({args.nx} x {args.ny} tiles)")


if __name__ == "__main__":
    main()
