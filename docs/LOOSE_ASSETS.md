# Loose-asset layout (spec, format version 1)

Phase 1 of `ROADMAP.md`. This is the contract between setup (writes the layout), the runtime (reads it) and
MVP Workshop (edits it). It covers the BASE build only. Nothing here is implemented yet.

## 1. Terms
- **Install root**: the folder holding `MVP06.exe`. Game files sit under `game/` as they do on the disc
  (`game/default.xbe`, `game/data/...`).
- **Archive**: an EA BIGF file (`.big`), 128 of them on the disc.
- **Member**: one directory entry in an archive. Its name can contain `\` sub-paths.
- **Loose file**: a member written out as a plain file.

## 2. Folders
- An archive `X/name.big` becomes the folder `X/name_big/`. The `.big` file itself is **not** installed.
- A member `a\b\c.ext` becomes `X/name_big/a/b/c.ext`. Separators are normalised to `/` on disk.
- A member that is itself a BIGF archive (checked by its magic, whatever its extension) becomes a nested folder named
  the same way: `foo.big` → `foo_big/`, and `foo.viv` → `foo_viv/`. This repeats at any depth.
- Names are compared case-insensitively, as the game does. Setup writes them in lower case.
- A member name repeated with **identical** bytes is written once and listed again in `order`; the runtime points both
  directory entries at that file. The disc does this in 10 frontend texture archives (`awards`, `bkgnds`, `bpsetup`,
  `chalnges`, `portrait`, `smstad`, `stadiums`, `uniforms`, `wbkgnds`, `wstad`): each repeats one 1-byte placeholder.
- A repeated name with *different* bytes, or a member colliding with a nested folder, cannot be laid out as files.
  Such an archive stays packed and setup logs it (none on this disc).
- Loose files that were never archives on the disc (`.xsh`, `.vp6`, `.abk` and the rest) stay where they are.

Example:
```
game/data/models.big                 -> game/data/models_big/a000i/<tex>.xsh/   (see 4)
game/data/datafile/datafile.big      -> game/data/datafile/datafile_big/datafile.txt
game/data/audio/proj0/proj0dat.big   -> game/data/audio/proj0/proj0dat_big/...
```

### Archive order and directory data
The game sometimes depends on member order or on the BIG header (for example offsets it reads itself). Each
extracted archive therefore keeps a `_big.json` beside its members:
```json
{ "magic": "BIGF", "headerSize": 1234, "order": ["a000i\\x.xsh", "..."], "align": 16, "refpack": ["a000i\\x.xsh"] }
```
- `order`: original member names, exact spelling, in directory order. The runtime builds its virtual directory in this order.
  New files added by a mod go at the end.
- `refpack`: members that were RefPack-compressed on the disc (see 3).
- `align` / `headerSize`: so a repack (Workshop export, verify) can rebuild the original archive byte-identical.

## 3. RefPack
- Members are stored **decompressed**. A member listed in `refpack` was compressed on the disc.
- The runtime does not decompress loose members. `modvfs::AddPatch` receives the same unpacked bytes it gets today.
- Members listed in `refpack` are RefPack-compressed again (real matches, not literal runs) when the archive is first
  opened. This is required: the game loads some archives whole into its 40 MB heap (`datafile.big` is 2.8 MB packed,
  12.8 MB unpacked), and with uncompressed members it stalls on the first loading screen. Members are packed on all
  cores: `datafile` takes about 80 ms, and all 26 archives opened up to the title screen add about 0.5 s together.
- Image data that is RefPack-compressed *inside* an `.xsh` (type flag `0x80`) is a texture matter (see 4), not this rule.

## 4. Textures (`.xsh`, SHPX)
An `.xsh` is a container: a 16-byte header (`SHPX`, size, count, version), a directory of `(tag[4], offset)`,
an optional group name, then one block per image. Each block has a 16-byte header plus pixels, followed by attachments
(name `0x70`, metal bin `0x69`, hotspots `0x7C`, palette `0x2A`, comment `0x6F`).

What the disc holds (survey of all 11,903 SHPX files, 39,744 images):
| SHPX type | Count | Storage | Loose form |
|---|---|---|---|
| 0x7D BGRA8888 | 19,203 | always swizzled, each mip level on its own | DDS 32-bit A8R8G8B8 (legacy masks), unswizzled |
| 0x60 DXT1 | 14,657 | linear blocks | DDS `DXT1`, blocks copied as they are |
| 0x61 DXT3 | 5,300 | linear blocks | DDS `DXT3`, blocks copied as they are |
| 0x7B PAL8 | 584 | palette in a `0x2A` attachment | **raw** `.bin` (the block's bytes after its header) |

Mip levels: the count is the top 4 bits of the header's last u16 (`RawY >> 12`); levels follow level 0 back to back.
Some DXT blocks end with 16 zero bytes after the pixels (kept as "pad"). Nothing else (other types, RefPack-compressed
pixels, shared or out-of-order blocks) occurs, and anything like that would stay raw or keep the file whole.
12 files named `.xsh` are not SHPX and stay plain files.

Each `.xsh` becomes a folder with the **same name, extension included**:
```
<tex>.xsh/
  xsh.meta         every byte that isn't pixels (binary, below)
  00_<tag>.dds     one per image, numbered in file order
  03_<tag>.bin     an image kept raw
```

### `xsh.meta` (binary, little endian)
`"XSHM"`, u32 version (1), u32 head length + head (file start up to the first block: header, directory, group name),
u32 image count, then per image in file order: u32 directory index, u8 kind (0 DDS, 1 raw), u16 name length + name,
the 16-byte block header, u32 pad length, u32 rest length + rest (attachments up to the next block).
It is binary on purpose: nothing in it is meant to be edited, and both readers (C# and C++) stay tiny.

### Rebuild rule and the test
- Rebuild: head, then per image its header (width, height, mip count and block size updated from the DDS), pixels
  (BGRA re-swizzled per level), pad and rest; then the declared size and the directory offsets are patched.
- **Passed (2026-10-28):** all 11,903 SHPX files split and rebuild byte-identical, through the written DDS files, in
  MVP Workshop (`XshFolder`, test `tests/LooseTextures`) and in the runtime (`sdk/runtime/kernel/xsh.cpp`). Size-only
  planning (DDS headers only) agrees with the built length for every file.
- Edited images: a DDS in the slot's format is used at any size and mip count (BGRA needs power-of-two sides).
  MVP Workshop also converts other formats (DXT5, BGRA into a DXT slot and the reverse, rebuilding the mip chain).
  The runtime does not convert: a DDS it can't use is logged (`textures: ... shown blank`) and replaced by blank pixels
  in the original size, so the game never gets a malformed file.
- In archives, texture folders are built when the archive opens and RefPack-compressed like other members
  (11,153 of the 11,903 were compressed on the disc).

## 5. Manifest (`game/manifest.json`)
```json
{
  "format": 1,
  "source": { "image": "<file name>", "xbeSha1": "<kImageSha1>", "release": "MVP 06 NCAA Baseball", "setup": "0.1.0" },
  "created": "2026-10-28T00:00:00Z",
  "files": {
    "data/models_big/a000i/x.xsh/00_tex0.dds": {
      "sha1": "...", "size": 0,
      "origin": { "disc": "data/models.big", "member": "a000i\\x.xsh", "part": "00_tex0.dds" }
    },
    "data/movies/intro.vp6": { "sha1": "...", "size": 0, "origin": { "disc": "data/movies/intro.vp6" } }
  }
}
```
- Written by `LooseInstaller` (MVP Workshop.Core), which setup uses. Every installed game file is listed, `_big.json`
  and `xsh.meta` included. Paths are relative to `game/`, with `/`.
- `origin` says where repair gets it again: the disc file, plus the archive member (nested ones joined with `\`) and
  the part of a split `.xsh`. Repair re-extracts that disc file and writes only the files that failed.
- The image is named, not hashed: hashing a whole dump would add minutes to setup, and `xbeSha1` already fixes the release.
- **Modded?** A file is modded when its hash differs from the manifest. Mods should go in mod folders (6) rather than
  editing these files, but direct edits are allowed and verify reports them as "changed" and offers to restore them, never silently.
- The `format` number goes up with any incompatible layout change. The runtime refuses a higher number than it knows and says so.

## 6. Lookup order
When the game opens a path (a disc file or an archive member), the runtime checks in order and the first hit wins:
1. Enabled mods, in launcher order (`.mvpmod`, M0).
2. ~~DEV overhaul overlay~~ — not in BASE.
3. The loose file under `game/`.
4. The archive member, if the `.big` is still installed (archive mode, kept as a fallback for installs without loose files).

The game's own `modvfs::AddPatch` patches (main menu, feflow) run on the winner's bytes, after RefPack unpacking,
the same as today. A mod that replaces a patched member gets patched too. That is intended: the patches are fixes, not content.

## 7. Open questions for Phase 2/3 (checked against the code, not guessed)
1. ~~Audio banks~~: served from loose folders like the rest; menus, a match and a dynasty load ran that way (Phase 2).
2. ~~Raw RefPack readers~~: moot, members are served compressed (see 3).
3. ~~SHPX mip counts and block order~~ (answered in 4).
4. How long it takes to rebuild an `.xsh` while a match loads, to decide whether a cache (`%LOCALAPPDATA%`, keyed by DDS hash) is needed.
5. How much disk space the full loose install takes, measured once, for the setup space check.
