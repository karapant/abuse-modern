#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build a self-contained browser for the stock Abuse campaign's original tiles."""

import argparse
import base64
from collections import Counter
import json
from pathlib import Path
import re
import struct
import zlib


TILE_LAYERS = {5: "Foreground", 6: "Background"}
LEVEL_MAPS = {"fgmap": ("Foreground", 0x3FFF), "bgmap": ("Background", 0xFFFF)}
FAMILIES = (
    ("Industrial pavement", "Start here. Approve the straight and rising/falling pieces together, then check caps and ramp transitions.",
     {26: "Straight deck", 47: "Alternate deck", 18: "Down ramp", 19: "Up ramp",
      23: "Down transition", 24: "Up transition", 88: "Down ramp support",
      89: "Up ramp support", 14: "Left cap", 15: "Right cap"}),
    ("Structural supports", "Review after pavement. Columns, corner pieces and horizontal trim meet at the same boundaries.",
     {10: "Left column", 11: "Right column", 43: "Left lower corner", 44: "Right lower corner",
      57: "Right trim end", 58: "Left trim end", 67: "Horizontal trim", 68: "Column center",
      75: "Recessed column"}),
    ("Service wall and conduits", "Pair long spans with returns and vertical breaks. This group can define the industrial wall style.",
     {330: "Panel join", 336: "Panel span", 408: "Upright", 411: "Wall face", 412: "Green wall face",
      418: "Conduit span", 419: "Conduit join", 420: "Conduit bend",
      453: "Lower conduit span", 454: "Lower conduit elbow", 455: "Lower conduit center",
      456: "Lower conduit return"}),
)
TOKENS = re.compile(r'"(?:\\.|[^"\\])*"|;[^\n]*|[()]|[^\s();"\']+')


def spec_entries(path):
    """Yield stable archive entry numbers and their original payloads."""
    contents = path.read_bytes()
    if len(contents) < 10 or contents[:8] != b"SPEC1.0\0":
        raise ValueError(f"{path}: expected an Abuse SPEC archive")
    count, = struct.unpack_from("<H", contents, 8)
    cursor = 10
    for index in range(count):
        if cursor + 2 > len(contents):
            raise ValueError(f"{path}: truncated SPEC directory")
        kind, length = struct.unpack_from("BB", contents, cursor)
        name_end = cursor + 2 + length
        if name_end + 9 > len(contents):
            raise ValueError(f"{path}: truncated SPEC entry {index}")
        name = contents[cursor + 2:name_end].split(b"\0", 1)[0].decode("latin-1")
        _, size, offset = struct.unpack_from("<BII", contents, name_end)
        if offset + size > len(contents):
            raise ValueError(f"{path}: truncated payload for {name}")
        yield index, kind, name, contents[offset:offset + size]
        cursor = name_end + 9


def tile_load_order(root):
    """Follow literal stock script loads in order. Never evaluate game scripts."""
    archives = []
    visited = set()
    missing = set()

    def safe_path(name):
        path = (root / name).resolve()
        return path if path.is_relative_to(root) else None

    def visit(name):
        path = safe_path(name)
        if path is None or path in visited or not path.is_file():
            return
        visited.add(path)
        tokens = [token for token in TOKENS.findall(path.read_text(encoding="latin-1"))
                  if not token.startswith(";")]
        for i, token in enumerate(tokens[:-2]):
            if token != "(":
                continue
            command = tokens[i + 1]
            if command == "load" and tokens[i + 2].startswith('"'):
                visit(tokens[i + 2][1:-1])
            elif command == "load_tiles":
                for argument in tokens[i + 2:]:
                    if not argument.startswith('"'):
                        break
                    archive = argument[1:-1]
                    candidate = safe_path(archive)
                    if candidate is not None and candidate.is_file():
                        archives.append(candidate.relative_to(root).as_posix())
                    else:
                        missing.add(archive)

    visit("abuse.lsp")
    return archives, sorted(missing)


def get_palette(root):
    for _, kind, name, payload in spec_entries(root / "art/back/backgrnd.spe"):
        if kind == 2 and name == "palette":
            count, = struct.unpack_from("<H", payload)
            if count != 256 or len(payload) < 2 + count * 3:
                raise ValueError("The stock palette must contain 256 colors")
            return [payload[2 + i * 3:5 + i * 3] for i in range(count)]
    raise ValueError("Main palette not found in art/back/backgrnd.spe")


def thumbnail(width, height, pixels, palette, transparent):
    def chunk(kind, content):
        return (struct.pack(">I", len(content)) + kind + content
                + struct.pack(">I", zlib.crc32(kind + content)))

    table = [color + bytes([0 if transparent and i == 0 else 255])
             for i, color in enumerate(palette)]
    rows = b"".join(b"\0" + b"".join(table[p] for p in pixels[y:y + width])
                    for y in range(0, len(pixels), width))
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
    return "data:image/png;base64," + base64.b64encode(png).decode("ascii")


def level_contexts(cells, width, height, level):
    """Keep up to two placements per tile with different direct neighbors."""
    examples = {}
    signatures = {}
    crop_width, crop_height = min(9, width), min(7, height)
    for index, tile_id in enumerate(cells):
        choices = examples.setdefault(tile_id, [])
        if len(choices) >= 2:
            continue
        x, y = index % width, index // width
        neighbor = (
            cells[index - 1] if x else -1,
            cells[index + 1] if x + 1 < width else -1,
            cells[index - width] if y else -1,
            cells[index + width] if y + 1 < height else -1,
        )
        if signatures.get(tile_id) == neighbor:
            continue
        signatures[tile_id] = neighbor
        left = max(0, min(x - crop_width // 2, width - crop_width))
        top = max(0, min(y - crop_height // 2, height - crop_height))
        grid = [cells[row * width + col]
                for row in range(top, top + crop_height)
                for col in range(left, left + crop_width)]
        choices.append(dict(level=level, x=x, y=y, left=left, top=top,
                            width=crop_width, height=crop_height, tiles=grid))
    return examples


def campaign_usage(root):
    usage = {}
    contexts = {}
    levels = sorted((root / "levels").glob("level[0-9][0-9].spe"))
    if not levels:
        raise ValueError("No numbered campaign levels found")
    for path in levels:
        for _, _, name, payload in spec_entries(path):
            if name not in LEVEL_MAPS:
                continue
            layer, mask = LEVEL_MAPS[name]
            if len(payload) < 8:
                raise ValueError(f"{path}: truncated {name} header")
            width, height = struct.unpack_from("<II", payload)
            end = 8 + 2 * width * height
            if end > len(payload):
                raise ValueError(f"{path}: truncated {name} cells")
            cells = [value & mask for value, in struct.iter_unpack("<H", payload[8:end])]
            for tile_id, count in Counter(cells).items():
                usage.setdefault((layer, tile_id), {})[path.stem] = count
            for tile_id, examples in level_contexts(cells, width, height, path.stem).items():
                contexts.setdefault((layer, tile_id), []).extend(examples)
    return [path.stem for path in levels], usage, contexts


def build_catalog(root):
    archive_order, missing = tile_load_order(root)
    if not archive_order:
        raise ValueError("No literal tile archive loads found from abuse.lsp")
    palette = get_palette(root)
    levels, usage, contexts = campaign_usage(root)
    tiles = []
    active = {}
    archive_tiles = {}
    family_by_id = {tile_id: (name, role) for name, _, members in FAMILIES
                    for tile_id, role in members.items()}
    for archive in archive_order:
        if archive not in archive_tiles:
            archive_tiles[archive] = []
            for index, kind, name, payload in spec_entries(root / archive):
                match = re.match(r"^\s*(\d+)", name)
                if kind not in TILE_LAYERS or not match:
                    continue
                if len(payload) < 4:
                    raise ValueError(f"{archive}: truncated tile header {name}")
                layer, tile_id = TILE_LAYERS[kind], int(match[1])
                width, height = struct.unpack_from("<HH", payload)
                if not width or not height or len(payload) < 4 + width * height:
                    raise ValueError(f"{archive}: invalid tile dimensions for {name}")
                family, role = family_by_id.get(tile_id, ("", "")) if layer == "Foreground" else ("", "")
                tile = dict(archive=archive, entry=index, name=name, layer=layer, id=tile_id,
                            width=width, height=height, family=family, role=role,
                            thumb=thumbnail(width, height, payload[4:4 + width * height],
                                            palette, layer == "Foreground"),
                            usage={}, contexts=[], placements=0, active=False)
                archive_tiles[archive].append(tile)
                tiles.append(tile)
        for tile in archive_tiles[archive]:
            active[tile["layer"], tile["id"]] = tile

    for key, tile in active.items():
        tile["active"] = True
        tile["usage"] = usage.get(key, {})
        tile["contexts"] = contexts.get(key, [])
        tile["placements"] = sum(tile["usage"].values())
    families = []
    for name, note, _ in FAMILIES:
        members = [tile for tile in active.values() if tile["family"] == name and tile["usage"]]
        covered = {level for tile in members for level in tile["usage"]}
        families.append(dict(name=name, note=note, tiles=len(members), levels=len(covered),
                             placements=sum(tile["placements"] for tile in members)))
    unresolved = [dict(layer=layer, id=tile_id, placements=sum(placements.values()))
                  for (layer, tile_id), placements in usage.items() if (layer, tile_id) not in active]
    return dict(tiles=tiles, levels=levels, families=families, archiveCount=len(archive_tiles),
                missing=missing, unresolved=sorted(unresolved, key=lambda row: -row["placements"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data", type=Path, help="original asset directory containing abuse.lsp")
    parser.add_argument("--output", type=Path, default=Path("build/tile-browser.html"))
    args = parser.parse_args()
    try:
        catalog = build_catalog(args.data.resolve())
        template = Path(__file__).with_suffix(".html").read_text(encoding="utf-8")
        payload = json.dumps(catalog, ensure_ascii=True, separators=(",", ":")).replace("<", "\\u003c")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(template.replace("__TILE_CATALOG__", payload), encoding="utf-8")
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"Tile browser: {error}\n")
    print(f"{args.output}: {len(catalog['tiles'])} tile entries, {len(catalog['levels'])} campaign maps")


if __name__ == "__main__":
    main()
