// This file is part of VanillaMinimapTracking.
//
// VanillaMinimapTracking is free software: you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// VanillaMinimapTracking is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License along with
// VanillaMinimapTracking. If not, see <https://www.gnu.org/licenses/>.

#pragma once

namespace Event::Custom {

// Reserve an event name to be claimed in the engine's event table at
// the next safe opportunity. Place a static instance at file scope:
//
//   static const Event::Custom::AutoReserve _r{"MY_EVENT"};
//
// Static-init chains the name onto an internal list *before* `DllMain`
// runs. After the engine and any other DLLs have finished writing to
// the event table (signaled by the first `Frame::RegisterEvent` call
// from Lua), we walk the table from the END looking for NULL-name
// slots and claim them for our reserved names — engine-owned
// `SStrDup` storage so the engine's reload teardown frees them
// correctly.
//
// We don't hook `RebuildEventTable` directly: chaining with other
// DLLs that hook the same function (SuperWoWhook, ClassicAPI,
// nampower, transmogfix) led to count→buffer-size mismatches and
// crashes. Slot-claim works per-DLL independently.
struct AutoReserve {
    explicit AutoReserve(const char *name);
};

// Returns the slot id currently assigned to `name`, or -1 if not yet
// claimed. Slot indices may change across `/reload`, so call this at
// fire time rather than caching the value.
int Lookup(const char *name);

// Dispatches with `(string, int)` — used by
// `MINIMAP_UPDATE_TRACKING` for `(typeName, enabled)` payloads.
void Fire_SD(int eventID, const char *arg1, int arg2);

// Internal: re-attempts unclaimed reservations. Called from the
// `Frame::RegisterEvent` hook in DllMain.
void RetryClaims();

// Internal: permit writes to the event table. Boot-time writes can
// race with the engine's table init and trigger SMemFree on
// in-flight slots.
void EnableWrites();

// Internal: invalidate cached slot indices before `/reload`. The
// engine rebuilds at a fresh allocation; old slots are stale.
void PrepareForReload();

} // namespace Event::Custom
