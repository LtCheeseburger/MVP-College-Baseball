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
(palette `0x2x`, comment `0x6F`, name `0x70`, hotspots `0x7C`, metal bin `0x69`, and so on).

Each `.xsh` becomes a folder with the **same name, extension included**:
```
<tex>.xsh/
  xsh.json         everything that isn't pixels
  00_<tag>.dds     one per image, in directory order
  01_<tag>.dds
  03_<tag>.bin     an image kept native (see below)
```

### Which formats become DDS
| SHPX type | Name | Loose form |
|---|---|---|
| 0x60 / 0x61 / 0x62 | DXT1 / 3 / 5 | DDS BC1 / BC2 / BC3, blocks copied as they are (Xbox DXT blocks are not swizzled) |
| 0x7D | BGRA8888 | DDS `B8G8R8A8_UNORM`, unswizzled when the swizzle flag is set |
| 0x7F, 0x78 | RGB565 | DDS `B5G6R5_UNORM`, unswizzled |
| 0x7E | ARGB1555 | DDS `B5G5R5A1_UNORM`, unswizzled |
| 0x64 | A8 | DDS `A8_UNORM`, unswizzled |
| 0x7B | PAL8 | **native** `.bin` (raw block bytes); the palette stays in `xsh.json` attachments |
| type with flag 0x80 (RefPack pixels) | any | **native** `.bin` until a byte-identical round trip is proven |
| anything else | | **native** `.bin` |

DDS files use the `DX10` header extension only for formats with no legacy FourCC/mask. Mip levels are written as DDS mips
when the block carries them (the mip count and where it's stored must be confirmed against the data in Phase 3 before code is written).

### `xsh.json`
```json
{
  "version": 1,
  "magic": "SHPX", "declaredSize": 0, "shpxVersion": "G357",
  "groupName": "...",                              // null when absent
  "images": [
    { "file": "00_tex0.dds", "tag": "tex0", "offset": 64,
      "header": "base64 of the 16-byte block header",
      "swizzled": true, "pad": "base64 of any bytes between the pixels and the next block",
      "attachments": [ { "header": "base64", "data": "base64" } ] }
  ],
  "tail": "base64 of bytes after the last block, if any"
}
```
The point is that **every byte of the original is either in a DDS or in this file**. Offsets are kept so the rebuild
puts blocks where they were. An edited image that changes size relays the blocks out and fixes up the directory offsets.

### Rebuild rule and the test
- The runtime rebuilds the `.xsh` bytes when the game opens it: DDS pixels → re-swizzle where it applies → block header
  (width/height updated only if the DDS changed size) → attachments → padding.
- **Acceptance test (Phase 3):** for every `.xsh` on the disc, loose or inside an archive, extract → rebuild gives
  **byte-identical** output. The C# (Workshop.Core) and C++ (sdk) rebuilds must both pass.
- Edited DDS in another format or size: re-encoded to the original SHPX type. Bad input (wrong dimensions for a
  power-of-two-only type, unreadable DDS) logs a warning and falls back to the archive member if the `.big` is still installed.
  Otherwise the game gets a 1×1 placeholder of the right type, and verify flags the file so repair can restore it from the ISO.

## 5. Manifest (`game/manifest.json`)
```json
{
  "format": 1,
  "source": { "isoSha1": "...", "xbeSha1": "<kImageSha1>", "release": "MVP 06 NCAA Baseball (NTSC)" },
  "created": "2026-10-28T00:00:00Z", "setup": "0.1.0",
  "files": {
    "data/models_big/a000i/x.xsh/00_tex0.dds": {
      "sha1": "...", "size": 0,
      "origin": { "archive": "data/models.big", "member": "a000i\\x.xsh", "image": 0, "refpack": false }
    },
    "data/movies/intro.vp6": { "sha1": "...", "size": 0, "origin": { "disc": "data/movies/intro.vp6" } }
  }
}
```
- Every installed game file is listed. Paths are relative to `game/`, with `/`.
- `origin` says where repair gets it again: a plain disc file, an archive member, or one image of an `.xsh`.
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
1. Audio banks (`proj*dat.big` / `proj*hdr.big`): does the audio code stream from the archive by offset? If so, those
   archives stay packed (listed in the manifest as whole files) until the dsound path is understood.
2. Does any caller read a member's compressed RefPack bytes directly (see 3)?
3. Where SHPX stores mip counts, and whether any `.xsh` has blocks overlapping or out of directory order.
4. How long it takes to rebuild an `.xsh` while a match loads, to decide whether a cache (`%LOCALAPPDATA%`, keyed by DDS hash) is needed.
5. How much disk space the full loose install takes, measured once, for the setup space check.
