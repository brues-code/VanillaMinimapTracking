# VanillaMinimapTracking

Adds custom blip icons to the minimap in WoW 1.12.1 (Turtle WoW / VanillaFixes).
Lets you see your target, focus, vendors, auctioneers, mailboxes, and other
NPC types as recognizable icons on the minimap, hover them for tooltips
(including unit subtitles like `<Innkeeper>`), and toggle each category from
a small button next to the minimap.

![In-game screenshot](ingame.png)

## What's in this repo

- **`src/`** — a C++ DLL that injects into WoW.exe via the VanillaFixes loader.
  It hooks WoW's per-frame visible-object enumeration and the minimap render
  pipeline to draw extra blips and inject hover-tooltip text. It also
  registers a small Lua API (`MinimapBlip_Track`, `MinimapBlip_SetFocus`,
  etc.) that the addon uses.
- **`MinimapBlips/`** — a companion WoW addon that puts the toggle button on
  the minimap, shows the category menu, and persists which categories you
  have enabled. The addon is just a UI on top of the DLL's Lua API; without
  the DLL, the addon does nothing and shows a chat warning.

The two pieces ship together: the DLL provides the rendering and per-object
tracking, the addon provides the UI.

## Categories

The current category menu (managed in
[`MinimapBlips/MinimapBlipsMenu.lua`](MinimapBlips/MinimapBlipsMenu.lua)):

- **Target** — your current target. A separate hostile-target icon is used
  if you can't assist the target (uses `CGUnit_C::CanAssist`).
- **Focus** — a pseudo-focus unit (vanilla 1.12.1 has no native focus). Set
  with `MinimapBlip_SetFocus()`/`MinimapBlip_SetFocusByName(name)`.
- **Auctioneer, Banker, Flight Master, Innkeeper, Repair, Trainer,
  Stable Master, Battlemaster, Vendor** — NPCs matched by their
  `m_npcFlags` field.
- **Mailbox** — game objects of type `MAILBOX`.

Self-targeting and self-focus are filtered out — your own character won't
get a target/focus blip placed on you.

## Build

Requirements: Windows, MSVC (Visual Studio 2019+ Build Tools), CMake 3.10+,
Git.

```powershell
cd C:\Git\VanillaMinimapTracking
git submodule update --init --recursive
cmake -B build -A Win32
cmake --build build --config Release
```

`-A Win32` is mandatory — WoW 1.12.1 is a 32-bit process. Output is
`build\Release\VanillaMinimapTracking.dll`.

To produce a tagged release build with a baked-in version value:

```powershell
cmake -B build -A Win32 -DVANILLAMINIMAPTRACKING_TAG=v1.2.3
cmake --build build --config Release
```

## Install

1. **DLL:** drop `build\Release\VanillaMinimapTracking.dll` into the folder
   VanillaFixes loads DLLs from (typically next to the WoW.exe loaded by
   VanillaFixes).
2. **Addon:** copy the entire `MinimapBlips/` folder to
   `<WoW>\Interface\AddOns\MinimapBlips\`.

Both the DLL and the addon need to be installed — the addon detects the DLL
via the `MINIMAP_BLIP_VERSION` global and refuses to load if the DLL is
missing.

## Notes for the curious

The DLL hooks a handful of well-known WoW 1.12.1 functions to render extra
blips (see [`src/Blips.cpp`](src/Blips.cpp)) and reads creature subnames
directly out of the in-memory creature cache (`unit + 0xB30 → cache + 0x10`).
Detailed offset and architecture notes for future hacking live in
agent-targeted memory under `~/.claude/projects/.../memory/`.
