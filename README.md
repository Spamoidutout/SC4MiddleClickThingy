# SC4 Middle Click Thingy 1.0.0

by **Spamoidutout** - https://community.simtropolis.com/profile/740965-spamoidutout/

A tiny DLL plugin for SimCity 4 that lets the middle mouse button reach the most used tools without touching the keyboard.

Each middle click on the city does the next step:

| Open tool | Middle click |
|---|---|
| any other tool (road, zoning…) | Escape (the game's "Cancel" command) |
| none | Query tool |
| Query tool | Bulldozer, if this click quickly follows the middle click that opened the Query tool (within 3 s, with no left or right click in between); Escape otherwise |
| Bulldozer | Escape, and the cycle starts again |

So, from no tool:
- **one click** opens the Query tool;
- **two quick clicks** open the Bulldozer;
- **one more click** always closes whatever is open.

The next step depends on the tool that is really open. The cycle stays right even if you use the keyboard or the toolbar in between.

## Requirements

- SimCity 4 Deluxe / Rush Hour, **version 641** (Steam, GOG, EA app), on Windows.
- Nothing else: no dependency, no other plugin needed.

## Installation

Copy `SC4MiddleClickThingy.dll` into the **root** of `Documents\SimCity 4\Plugins`, not into a subfolder.

To uninstall, delete the DLL.

## What it leaves alone

- **Clicks on the interface.** Only a middle click on the city counts. A middle click on a game panel, or on a window added by another mod, does nothing.
- **Middle-button drags and long presses.** The mouse must move less than 8 pixels and the button must be released within 0.8 s. Camera mods that use a middle-button drag keep working.
- **Clicks while a dialog is open**, such as the building window of the Query tool. Closing the tool at that moment would crash the game.
- **The region view.** The mod only works in a city.

The mod never blocks any input: every click still reaches the game and the other mods.

## Compatibility

Checked in game with other plugins that use the middle mouse button:

- [Region View Census UI DLL Plugin](https://community.simtropolis.com/files/file/36145-region-view-census-ui-dll-plugin/): no conflict, it is only active in the region view.
- [SC4 Modern Camera](https://community.simtropolis.com/files/file/37462-sc4-modern-camera/): no conflict, it only uses middle-button drags, which this mod ignores.
- SC4 Advisor Control: no conflict, its middle click on the advisor portraits is a click on the interface, which this mod ignores.

## Log

Normally the mod writes no file at all. Only if something goes wrong does it create `SC4MiddleClickThingy.log` next to the DLL, with the error. For example, if the game raises an exception during a call made by the mod, the log notes it and the mod turns itself off until the game is restarted.

## How it works

- **Catching the middle button.** The game never passes the middle button on: its mouse message `0xFA0C6390` carries neither event 9 nor event 12, even though the SDK declares them. The mod therefore catches the Windows messages of the button (`WM_MBUTTONDOWN`, `WM_MBUTTONDBLCLK`, `WM_MBUTTONUP`) with a `WH_MOUSE` hook, limited to the game's own thread. The hook runs when the game's message loop takes the message out of the queue, where the game also handles its keyboard shortcuts. The tool changes at once, and no click is lost. The click position is the last one given by the game's mouse message.
- **City or interface.** On release, the mod walks through the game windows under the mouse, from the main window down:
  - A window that has children (a container) only blocks the click through a visible child under the mouse; its empty parts are transparent. For example, the game's bottom panel `0xAA32BCE6` is 428 px high, but only its lower part is drawn. The city view `0x9A47B417` is a container too.
  - A window without children (a button, a picture, a panel background) blocks the click, unless it covers the whole screen: then it is a transparent overlay on the city.
- **Changing tools.** The mod sends the same commands as the keyboard shortcuts, through `cISC4View3DWin::ProcessCommand`:
  - `Cancel`: `0x6A935CF1`;
  - `QueryTool`: `0x6A935CF3`;
  - `DemolishTool`: `0x6A935D36`.

  It reads the open tool with `GetCurrentViewInputControl()->GetID()` (Query tool: `0xC7AF928E`). Before acting, it checks `cIGZWinMgr::GetModalNestCount() == 0`.
- **The 3-second rule.** Left and right clicks (events 7 and 8 of the game's mouse message) cancel the chaining from the Query tool to the Bulldozer.

## Building

The DLL is built with clang and lld, without Visual Studio: a 32-bit DLL with the MSVC ABI, and no C runtime. Run `build/build.sh`; it needs `clang`, `lld-link` and `llvm-dlltool` (LLVM 15 or newer). The plugin is written to `build/out/`.

The script also builds `test/out/`: a test build of the DLL and `harness.exe`, a fake game that loads it and checks its behaviour. Run it with Wine:

```sh
cd test/out && WINEDEBUG=-all wine harness.exe
```

Source files:

| File | Contents |
|---|---|
| `src/MiddleClick.cpp` | the mod |
| `src/GZ.h` | the game interfaces it uses, with their vtable slots (derived from gzcom-dll) |
| `src/Runtime.cpp`, `src/Runtime.h` | a tiny replacement for the C runtime (memory functions, log file) |
| `src/Win32.h` | the few Win32 declarations it needs |
| `test/Harness.cpp` | the fake game |
| `build/build.sh`, `build/*.def` | the build script and the imported Windows functions |

## Credits

- Game interfaces from [gzcom-dll](https://github.com/nsgomez/gzcom-dll) by Nelson Gomez, Nicholas Hayes and contributors.
- Command IDs, tool IDs and window behaviour from the [OpenSC4](https://github.com/tloriato/OpenSC4) reverse-engineering specs.

## License

GNU Lesser General Public License, version 2.1 or later. See `LICENSE.txt`.

## Changelog

**1.0.0**: first release.
