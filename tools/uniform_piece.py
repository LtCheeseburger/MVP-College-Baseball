#!/usr/bin/env python3
"""Create an editable, independent uniform-piece mod from a user's loose install.

Only this component's images are copied. Source assets are read-only. The output is
the existing unpacked .mvpmod development format; edit its DDS images, then enable
the folder in the launcher or zip its contents to a .mvpmod for distribution.
"""
import argparse
import json
import re
import struct
from pathlib import Path

TAGS = {
    "hat": {b"hat ", b"hlm1", b"hlm2", b"hhlm"},
    "jersey": {b"tors", b"torf", b"merf", b"merk", b"mslv"},
    "pants": {b"lega", b"legb", b"legc", b"legd", b"lesa", b"lesd", b"llod"},
}
FONT_TAGS = {bytes([c]) + b"___" for c in range(65, 91)} | {f"zig{i}".encode() for i in range(10)}


def component_files(source, wanted):
    """Re-index xsh.meta; keep selected image headers, attachments and pixels exact."""
    data = (source / "xsh.meta").read_bytes()
    pos = 0

    def take(count):
        nonlocal pos
        if count < 0 or pos + count > len(data):
            raise ValueError("truncated xsh.meta")
        result = data[pos:pos + count]
        pos += count
        return result

    def u32():
        return struct.unpack("<I", take(4))[0]

    if take(4) != b"XSHM" or u32() != 1:
        raise ValueError("expected xsh.meta version 1")
    head = take(u32())
    if len(head) < 16 or head[:4] != b"SHPX":
        raise ValueError("expected an SHPX texture")
    count = struct.unpack_from("<I", head, 8)[0]
    if not 0 < count <= 4096 or 16 + count * 8 > len(head) or u32() != count:
        raise ValueError("invalid texture directory")
    images, seen, files = [], set(), {}
    for _ in range(count):
        index = u32()
        start = pos
        kind = take(1)[0]
        name = take(struct.unpack("<H", take(2))[0]).decode("utf-8")
        if index >= count or kind not in (0, 1) or not re.fullmatch(r"[A-Za-z0-9_. -]+\.(dds|bin)", name):
            raise ValueError("invalid image index, kind or filename")
        take(16)  # image header
        u32()     # pixel padding
        take(u32())  # attachments, kept byte for byte
        tag = head[16 + index * 8:20 + index * 8]
        if tag in seen:
            raise ValueError("duplicate texture tag")
        seen.add(tag)
        if tag in wanted:
            if name in files:
                raise ValueError("duplicate image filename")
            files[name] = (source / name).read_bytes()
            images.append((tag, data[start:pos]))
    if pos != len(data) or not wanted.issubset(seen):
        raise ValueError("unexpected metadata or missing component images: " + repr(wanted - seen))
    new_head = bytearray(head[:16])
    struct.pack_into("<II", new_head, 4, 0, len(images))  # runtime computes final file size/offsets
    for tag, _ in images:
        new_head.extend(tag + bytes(4))
    new_head.extend(head[16 + count * 8:])  # optional group name
    meta = bytearray(b"XSHM" + struct.pack("<II", 1, len(new_head)) + new_head + struct.pack("<I", len(images)))
    for index, (_, record) in enumerate(images):
        meta.extend(struct.pack("<I", index) + record)
    files["xsh.meta"] = bytes(meta)
    return files


def create(game, output, team, part, donor, identity, name, source, font=None):
    game, output = game.resolve(), output.resolve()
    if output == game or game in output.parents:
        raise ValueError("output must be outside the source game folder")
    if not 0 <= team <= 255 or donor not in "abcdghijklmnop" or len(donor) != 1:
        raise ValueError("invalid roster index or donor variant")
    if not re.fullmatch(r"[a-z0-9-]{1,24}", identity) or identity.startswith("stock-"):
        raise ValueError("id must be 1-24 lowercase letters, digits or hyphens, without stock- prefix")
    if not 1 <= len(name) <= 24 or any(ord(c) < 32 or ord(c) > 126 for c in name):
        raise ValueError("name must be 1-24 printable ASCII characters")
    for member in (source, font):
        if member is not None and not re.fullmatch(r"[a-z0-9_-]{1,40}\.xsh", member):
            raise ValueError("source/font must be a lowercase .xsh member name without a path")
    if font and part != "jersey":
        raise ValueError("only a jersey can supply lettering")
    manifest = json.loads((game / "manifest.json").read_text(encoding="utf-8"))
    sha1 = manifest.get("source", {}).get("xbeSha1", "")
    if manifest.get("format") != 1 or not re.fullmatch(r"[0-9a-fA-F]{40}", sha1):
        raise ValueError("expected a version 1 loose install manifest with an XBE SHA-1")
    texture = f"uc_{team}_{identity}_{part}.xsh"
    all_files = {}
    for member, target, tags in ((source, texture, TAGS[part]), (font, f"uc_{team}_{identity}_font.xsh", FONT_TAGS)):
        if member is None:
            continue
        for filename, contents in component_files(game / "data/models_big" / member, tags).items():
            all_files[f"files/data/models_big/{target}/{filename}"] = contents
    record = {"format": 1, "id": identity, "name": name, "team": team, "part": part,
              "donor": donor, "texture": texture}
    if font:
        record["font"] = f"uc_{team}_{identity}_font.xsh"
    # JSON quoted strings are valid TOML basic strings for this printable ASCII schema.
    toml = "".join(f"{key} = {json.dumps(value)}\n" for key, value in record.items())
    all_files[f"files/data/uniforms/{team}/{part}-{identity}.toml"] = toml.encode()
    mod = {"format": 1, "id": f"uniform-{team}-{part}-{identity}", "name": f"{name} {part.title()}",
           "version": "1.0.0", "game": {"xbeSha1": sha1}, "layout": 1, "affectsSaves": False,
           "description": f"Independent {part} for roster team {team}; helmets follow hats."}
    all_files["mod.json"] = (json.dumps(mod, indent=2) + "\n").encode()
    # Validate/read everything before creating output; never overwrite an existing mod or the source install.
    output.mkdir(parents=False, exist_ok=False)
    for relative, contents in all_files.items():
        dest = output / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        with dest.open("xb") as stream:
            stream.write(contents)
    return len(all_files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True, help="installed game/ folder (contains manifest.json)")
    parser.add_argument("--out", type=Path, required=True, help="new mod folder; its parent must exist")
    parser.add_argument("--team", type=int, required=True, help="engine roster record index, not logo ID")
    parser.add_argument("--part", choices=TAGS, required=True)
    parser.add_argument("--donor", default="a", help="native variant supplying style/lettering metadata")
    parser.add_argument("--id", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--source", required=True, help="source member, e.g. u001a.xsh")
    parser.add_argument("--font", help="optional complete jersey lettering member, e.g. f001a.xsh")
    args = parser.parse_args()
    try:
        count = create(args.game, args.out, args.team, args.part, args.donor, args.id, args.name, args.source, args.font)
    except (OSError, ValueError, KeyError, struct.error) as exc:
        parser.error(str(exc))
    print(f"Created {count} files in {args.out}. Edit the component DDS files, then enable this mod folder.")


if __name__ == "__main__":
    main()
