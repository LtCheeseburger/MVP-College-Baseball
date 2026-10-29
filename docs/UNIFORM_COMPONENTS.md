# Pregame uniform components

The team-selection uniform step has four rows: **Preset**, **Hat**, **Jersey**, and **Pants**. Up/down chooses a
row; left/right changes its value. Selecting a preset sets all three parts. Editing a part changes the preset
label to **Custom**. Batting helmets follow Hat. Confirm/back continue through the original pregame flow.

Each matchup starts with the game's Home/Away defaults. Choices are separate for each side, including a matchup
between the same team. They live only in memory for that matchup: no setting, roster field or save format is added.
Returning to a part during the same pregame flow keeps the current combination; changing teams resets it.

The existing artwork is a **jersey thumbnail**, not a composite preview. It follows the jersey's donor preset;
added pieces are identified by their labels. A live 3D/composite preview and Workshop selector UI are outside this
change. Home Run Derby/mini-game screens and replacement layouts without the added rows keep the original selector.

## Standalone pieces

Existing full uniform packs continue to work. Their parts become separate choices automatically. The stock menu
has Home/Away; additional native variants are offered when both their `uNNNx.xsh` and `fNNNx.xsh` members exist in
the loose models folder. Native suffix order is `abcdghijklmnop` (there is no `e` or `f` variant).

An independent piece is a small TOML record plus a new XSH member in an ordinary [mod](MVPMOD.md):

```text
mod.json
files/data/uniforms/44/hat-navy.toml
files/data/models_big/uc_44_navy_hat.xsh/xsh.meta
files/data/models_big/uc_44_navy_hat.xsh/<the four hat/helmet image files>
```

```toml
format = 1
id = "navy"
name = "Navy"
team = 44
part = "hat"
donor = "a"
texture = "uc_44_navy_hat.xsh"
```

- `team` is the **engine roster record index**, not the logo/asset ID in a texture filename. The supported engine
  path has an eight-bit team field; this feature does not expand team capacity.
- `part` is `hat`, `jersey` or `pants`. No separate helmet choice is stored.
- `donor` is an available native variant for this team. It supplies original style/fit metadata and fallback art.
  A jersey also inherits its donor's name/number lettering unless `font = "another.xsh"` supplies a complete font.
- `texture` and optional `font` name members in `models.big`, without a path. Use unique, lowercase member names
  (letters, digits, `_`, `-`, up to 40 characters before `.xsh`). The pack can contain just this component's images.
- `id` is a unique lowercase token within the team's component list; `stock-` is reserved. `name` is 1–24 printable
  ASCII characters, fitting the native menu font. One record per file lets unrelated mods add pieces independently.
  Records are limited to 4 KiB, with up to 128 accepted records per team.
- The record's team must match its folder. A missing donor, invalid record or duplicate ID is not added to the list.
  A loaded pack missing any required image falls back as a whole component and logs the reason. Incomplete optional
  lettering retains the donor font. Invalid DDS pixels continue to follow the runtime's existing blank-texture rule.
- Mark these cosmetic mods `affectsSaves: false`. Enable/disable them in the launcher, then restart the game.

Required XSH directory tags:

| Piece | Tags |
| --- | --- |
| Hat (including helmets) | `hat `, `hlm1`, `hlm2`, `hhlm` |
| Jersey | `tors`, `torf`, `merf`, `merk`, `mslv` |
| Pants (including short/sock variants and distant players) | `lega`, `legb`, `legc`, `legd`, `lesa`, `lesd`, `llod` |
| Optional jersey lettering | `A___` through `Z___`, `zig0` through `zig9` |

The directory tag `hat ` has a trailing space; the game's image-name lookup uses `hat`.

### Create an editable piece

`tools/uniform_piece.py` reads a user's installed loose assets and creates a **new** unpacked mod folder. Python
3.9+ is sufficient; it has no third-party dependencies. Only the chosen component's images are copied. Their pixel
data, original image headers and attachments stay exact; the small XSH directory is re-indexed. The input install
is read-only and an existing output is refused.

```sh
python tools/uniform_piece.py --game /path/to/game --out /path/to/mods/navy-hat \
  --team 44 --part hat --donor a --id navy --name "Navy" --source u044a.xsh
```

The indices/filename above are an example: use the roster index and source asset member for the team being edited.
For a jersey, add `--font f044a.xsh` to include independently editable lettering. Edit the output DDS files using the
existing texture workflow, preserving each slot's format. Enable the output folder in the launcher to test it. To
share it, ZIP the folder's **contents**, with `mod.json` at the ZIP root, and name the file `*.mvpmod`.

The tool does not bundle original game files with the repository, modify Workshop's pinned submodule, or add a
second mod loader. Records and textures use the existing merged mod filesystem and load order.

## Integration and ownership

Handwritten hooks target the supported Xbox MVP 06 image, SHA-256
`264e0ecc4e6c33a32670b1f1f4cf1884d7d7fb3de4a5bcfb6c37f00b12fef96b`. No generated code or game assets are committed.

| Original entry | Role |
| --- | --- |
| `0x515C70`, `0x538830`, `0x515C60` | Uniform-control initialization, activation/team changes, destruction |
| `0x516290`, `0x516370`, `0x516050` | Native label refresh, random preset shortcut, commit |
| `0x538970/0x5389A0`, `0x5389D0/0x538A00` | Left/right press and release pairs |
| `0x566BD0`, `0x566C40` | TeamSelect up/down dispatch, scoped to an enhanced uniform control |
| `0x27AAD0`, `0x272FE0` | Uniform resource configuration and reset/destruction |
| `0x8C510`, `0x2673B0`, `0x423B10`, `0x2670E0` | Original descriptor creation, resource load, image lookup, resource release |
| `0x25F140`, `0x25F230` | Release/rebuild derived cap/helmet copies and optional generated front torso |

`src/common/uniform_layout.cpp` patches the two native uniform groups in `fes_teamselect.fel` as the existing
mod-VFS opens it. It clones native text-box styling and uses existing fonts. Both groups must match; otherwise
the patch leaves the entire asset unchanged and the hooks retain the original controls.

The committed native descriptor remains `{team, preset, asset}`. At player-resource load, a temporary descriptor
loads the jersey donor through the original pipeline, retaining its style, name/number font and team branding.
Component sources then replace only their mapped material slots. Extra packs stay owned by that uniform object
until reset. A pack shared by two pieces is loaded once per object. The native resource owner at `+4` is unchanged.

The engine also allocates derived hat/helmet images and sometimes a front-torso image. Their original cleanup runs
before component pointers change, and their original builder runs afterward. This keeps those derived images in
sync and avoids freeing an interior pointer in a component pack. Gear and independently allocated lettering slots
remain under their original owners. Optional font blocks use the original font cleanup path, not the material
resource release function.

## Validation

Portable checks (no game files required):

```sh
cmake -S tests/uniforms -B build/uniform-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/uniform-tests
ctest --test-dir build/uniform-tests --output-on-failure
```

The standalone test project uses the same toml++ v3.4.0 as the runtime. It tests preset/component behavior, same-team
side isolation, wraparound and defaults, catalog rejection, exact component-slot coverage, all-or-nothing FEL
patching, and the production hooks against a controlled guest-memory/API fixture. The hook checks cover argument
cleanup, descriptor preservation, resource sharing/release and whole-component fallback. Python checks cover
component-only export, source preservation, lettering, missing images, output refusal and cross-team/part paths.

These checks are **not native gameplay acceptance**. Before marking the feature ready for merge, run the Windows
build and verify the rendered pregame controls with keyboard/controller input, home/away and same-team choices,
hat/helmet and pants behavior during batting/fielding (including distant players), jersey lettering, a standalone
piece mod, backing out/changing teams, and a second matchup returning to defaults. No full-game or dynasty-season
simulation is needed for this cosmetic feature. Native validation is currently pending.
