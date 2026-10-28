# MVP College Baseball (Beta)

Beta builds of the base-game PC port of MVP 06 NCAA Baseball (original UI). Version: see `VERSION`.

An unofficial native PC port of the original Xbox version of MVP 06 NCAA Baseball.

This port was created using the [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) tool.

> [!NOTE]
> No game files are included. Use the files from your own copy of the game.

## How to use

1. Unzip everything into a folder of its own.
2. Put your game in the `assets` folder next to `MVP06.exe`: either the ISO, or the extracted files with
   `default.xbe` directly inside `assets`.
3. Run `MVP06.exe`. The first start builds the game's code for your PC (a few minutes, only once). Don't press any
   keys until the game appears.

## Playing

- **Controllers:** Xbox, PlayStation and Switch pads, plugged in before or during play. Keyboard works too.
- **PC Options:** main menu > Options > PC Options for display mode, resolution, frame limit, effects, keyboard
  keys, vibration and save backups. Changes save straight away.
- **Quit Game:** the last row of the main menu.
- **Screenshots:** F12, saved to `screenshots`.

## Where your files go

Saves, settings (`mvp06-settings.toml`), logs, screenshots and save backups are kept in `Documents\MVP06 Recomp`.
To keep them next to the exe instead, put an empty file named `portable.txt` beside `MVP06.exe`.

Settings only in the settings file: `deadzone_left` / `deadzone_right` (0-50) and `nintendo_buttons`
(`"label"` or `"position"`).

## System Requirements

- Windows 10 64-bit
- A GPU with Direct3D 12 support

## Building

### 1. Clone the Repository
Clone this repository using [Git](https://git-scm.com/).

### 2. Add the Required Game Files
Place your extracted game data files in the `assets` folder. `default.xbe` must be there, with the game's folders beside it.

### 3. Install Dependencies
Install Visual Studio 2022 with **Desktop development with C++**, **C++ Clang tools for Windows** and **C++ CMake tools for Windows** (`build.bat` offers to add the last two).

### 4. Build the Project
Run `build.bat`. The first build recompiles the game and takes a while. The exe is written to `out\build\release`.

## Credits

- [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp), the recompiler and runtime this is built on.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for the overridable function layout and instruction hooks.
- [Zydis](https://github.com/zyantific/zydis) for decoding x86 instructions, [toml++](https://github.com/marzer/tomlplusplus) for the configs, [Dear ImGui](https://github.com/ocornut/imgui) for the settings overlay and [SDL](https://github.com/libsdl-org/SDL) for the window, input and controllers.
