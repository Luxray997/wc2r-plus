# WarCraft 2 Remastered+

A mod for **Warcraft II: Remastered** that adds missing features from other versions of the game along with some of its own.

Players with and without the mod can still play together.

## Features

- **Chat**
  - Scrollable history
  - Visible while paused
  - Customizable & color-coded channels: `[Team]`, `[All]` and `[Allies]` (cycle with `Tab`)
  - Colored usernames matching player color
  - Timestamps
- **Revamped UIs and Screens**
  - Enhanced Custom Scenario Screen
  - Enhanced Create Multiplayer Game Screen
  - Enhanced Multiplayer Lobby
  - Map preview
  - Map selector with search
  - Enhanced settings menu with search
  - Host controlled lobbies (host can lock/unlock teams and slots)
  - Organized Alliances menu
- **Map sharing**
  - Players can download maps from hosts running the mod. See [PROTOCOL.md](PROTOCOL.md) for implementation details.
- **Accessibility**
  - Modern "Black2Pink" mod -- Changes the black player's color (pink/cyan) for increased visibility. Purely cosmetic and on your screen only

The main menu shows "WarCraft 2 Remastered+ Version …" when the mod is running.

## Compatibility

The mod is built for **WC2 Remastered version 1.0.2.2818**. The mod will not load on any other version and will leave the game vanilla. If a new version of WC2 Remastered is released, the mod will remain inactive until the mod is updated.

## Install

⚠️ Although no bans have been observed, this mod is not official and may be flagged by Blizzard's anti-cheat. Proceed at your own risk.

1. Download **`wc2r-plus-setup-<version>.exe`** from the [latest release](../../releases/latest) and run it
2. Select your WC2 Remastered installation location. If it is in `C:\Program Files (x86)` or `C:\Program Files`, it will be automatically detected.
3. Click `Install` (or `Update` to update). You may be asked for administrator privileges

The game must be closed in order for the files to be installed correctly. The installer can also be used to uninstall or reinstall the mod.

For security purposes, you may want to build from source and run the installer yourself. 

## Building from source

Requirements: 
- Windows
- Visual Studio 2022 (Desktop development with C++)
- CMake 3.20 or newer
- Git (CMake downloads [MinHook](https://github.com/TsudaKageyu/minhook) at a pinned commit).

Then run:
```
cmake -S . -B build -A Win32
cmake --build build --config Release
```

The results are in `build\bin\Release`. You can either run the installer or move `version.dll` (the file that the game loads, which loads the mod) into the game's directory (`...\Warcraft II Remastered\x86\`) manually.

Releases are built from this repository by GitHub Actions, so each release can be traced to its
source.

## Stored Files
The mod stores its config, logs, and other resources in

```
C:\Users\<user>\Saved Games\Warcraft2Remastered\wc2r-plus\
```

If map downloading is used, the maps will be saved into the game's maps folder at `x86\Maps\Download`. Maps will be placed in the `Download\V1` folder, unless there's a different map with the same name, then it will go into the `Download\V2` folder, and so on.

## License

[MIT](LICENSE). The mod and its installer include [MinHook](https://github.com/TsudaKageyu/minhook)
(BSD-2-Clause), which includes the Hacker Disassembler Engine (BSD-2-Clause); their notices are in
[THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt), which ships with every release.

Not affiliated with or endorsed by Blizzard Entertainment. Warcraft is a trademark of Blizzard
Entertainment, Inc.
