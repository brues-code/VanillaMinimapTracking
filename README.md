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
  pipeline to draw extra blips and inject hover-tooltip text. It owns the
  per-character config (lazy-loaded from `WTF\Account\<account>\<realm>\
  <character>\VanillaMinimapTracking.txt` on first API call after login,
  flushed back on UI shutdown — same convention as `AddOns.txt`) and
  exposes a small Lua API + a custom `MINIMAP_UPDATE_TRACKING` event.
- **`MinimapBlips/`** — a companion WoW addon that ships the blip artwork
  (in `MinimapBlips/icons/`), registers it with the DLL on load, draws the
  toggle button next to the minimap, and renders the category menu. It's a
  thin UI on top of the DLL's Lua API — no `SavedVariables`, no path
  knowledge, no tracking state of its own. Without the DLL loaded, the
  addon shows a chat warning and does nothing.

## Categories

The current category menu (managed in
[`MinimapBlips/MinimapBlipsMenu.lua`](MinimapBlips/MinimapBlipsMenu.lua)):

- **Target** — your current target.
- **Focus** — a pseudo-focus unit (vanilla 1.12.1 has no native focus). Set
  with `C_Minimap.SetFocus([unit])` (defaults to `"target"`) or
  `C_Minimap.SetFocusByName(name)`.
- **Auctioneer, Banker, Flight Master, Innkeeper, Repair, Trainer,
  Stable Master, Battlemaster, Vendor** — NPCs matched by their
  `m_npcFlags` field.
- **Mailbox** — game objects of type `MAILBOX`.

Self-targeting and self-focus are filtered out — your own character won't
get a target/focus blip placed on you.

## Build

Install [Visual Studio Build Tools 2019+](https://visualstudio.microsoft.com/visual-cpp-build-tools/) and check **"Desktop development with C++"** in the installer. That workload bundles CMake (via the "C++ CMake tools for Windows" component), so no separate CMake install is needed.

Run the build commands from a **Developer PowerShell for VS** (Start menu → "Developer PowerShell"), so `cmake` and the MSVC toolchain are on PATH.

```powershell
git submodule update --init --recursive
cmake -B build -A Win32
cmake --build build --config Release
```

`-A Win32` is mandatory — WoW 1.12.1 is a 32-bit process. Output is
`build\Release\VanillaMinimapTracking.dll`.

## Install

1. **DLL:** drop `build\Release\VanillaMinimapTracking.dll` into the folder
   VanillaFixes loads DLLs from (typically next to the WoW.exe loaded by
   VanillaFixes).
2. **Addon:** copy the entire `MinimapBlips/` folder to
   `<WoW>\Interface\AddOns\MinimapBlips\`.

Both the DLL and the addon need to be installed — the addon detects the DLL
via the `C_Minimap` namespace and refuses to load if the DLL is missing.

## Lua API

Functions live on the `C_Minimap` namespace table (Blizzard's `C_*` style).
Pass type names from the `Enum.MinimapBlip` table — keys are PascalCase
(`Target`, `FlightMaster`, `StableMaster`, `Battlemaster`, `Mailbox`, …) and
values are the lowercase strings the engine actually uses. Hard-coded strings
work too if you'd rather skip the enum.

| Function                                                              | Returns             | Notes                                                                       |
|-----------------------------------------------------------------------|---------------------|-----------------------------------------------------------------------------|
| `C_Minimap.RegisterIcons({{type, icon, scale}, ...})` | —                   | Bulk-register icons in one call.  |
| `C_Minimap.RegisterIcon(type, icon, scale)`                       | —                   | Single-icon variant.                                                        |                               |
| `C_Minimap.Track(type, 0\|1)`                                      | —                   | Set a category's tracked state. Saved on UI shutdown (logout / `/reload`).  |
| `C_Minimap.Toggle(type)`                                           | —                   | Flip a category's tracked state — caller doesn't need to know current value. |
| `C_Minimap.ClearAllTracking()`                                     | —                   | Disables every currently-tracked category. Fires one `MINIMAP_UPDATE_TRACKING`. |
| `C_Minimap.IsTracked(type)`                                        | `1` or `nil`        | `if C_Minimap.IsTracked(t) then ... end`.                               |
| `C_Minimap.GetTracked()`                                           | `{type=1, ...}` set | All currently-tracked types as a set keyed by lowercase name.               |
| `C_Minimap.ListVisibleGUIDs([type])`                               | array of strings    | GUIDs (hex `"0x%016X"`) for every blip currently on the minimap, optionally filtered by type. |
| `C_Minimap.SetFocus([unit])`                                       | —                   | Pins `unit` (a unit ID like `"target"`/`"party1"`) as focus. Defaults to `"target"`. Errors if not found. |
| `C_Minimap.SetFocusByName(name)`                                   | —                   | Captures a unit by name (silent fail if not found).                         |
| `C_Minimap.ClearFocus()`                                           | —                   | Drops the focus.                                                            |

To detect whether the DLL is loaded, just check the namespace:
`if C_Minimap then ... end`.

A real WoW event fires whenever tracking state changes — register it with
`frame:RegisterEvent("MINIMAP_UPDATE_TRACKING")` and dispatch from the
addon's `OnEvent` like any built-in event. `arg1` is the type that
changed (e.g. `"vendor"`), or `""` from `ClearAllTracking` meaning
"everything changed". Listeners still query the on/off state with
`C_Minimap.IsTracked(type)` / `C_Minimap.GetTracked()` — `arg1` exists
for fast-path dispatch, not as the source of truth.

## Notes for the curious

The DLL hooks a handful of well-known WoW 1.12.1 functions to render extra
blips (see [`src/Blips.cpp`](src/Blips.cpp)) and reads creature subnames
directly out of the in-memory creature cache (`unit + 0xB30 → cache + 0x10`).
Per-character path resolution uses three engine session globals
(`0x00BE1C0C`, `0x00C28130+0x20`, `0x00C27D88`) — the same trio WoW itself
reads to write `AddOns.txt` and other per-character WTF files (see
[ClassicAPI/docs/SessionGlobals.md](https://github.com/brues-code/ClassicAPI/blob/main/docs/SessionGlobals.md)).
The custom `MINIMAP_UPDATE_TRACKING` event is dispatched via the
engine's own event table by claiming an unused slot — addons listen for it
exactly the same way they would for a built-in event.
