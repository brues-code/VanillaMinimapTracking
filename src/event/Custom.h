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

// Claims an unused (`name == NULL`) slot in the engine's event-registration
// table at `[VAR_EVENT_TABLE_BASE_PTR]`, sets its name to `eventName`, and
// returns the slot index. After a successful registration, addons can
// `frame:RegisterEvent(eventName)` and the engine treats it like any
// built-in event; a matching `Fire*` call dispatches to those frames.
//
// `eventName` must outlive the engine — pass a static string literal. Returns
// -1 if the table isn't initialised yet, or if no NULL slot was found in the
// live entry range. Cached on failure so a follow-up `RetryAll()` call (e.g.
// from the FrameRegisterEvent hook) can succeed once the table is ready.
int Register(const char *eventName);

// Re-attempts every cached registration that hasn't succeeded yet. The
// engine's event table is populated AFTER our LoadScriptFunctions hook
// fires, so boot-time `Register` calls usually return -1; firing this from
// every Frame::RegisterEvent call catches them up the moment Lua tries to
// listen.
void RetryAll();

// Permits `TryClaim` to actually write to the event table. Until called,
// `Register`/`RetryAll` cache the name only. Boot-time `RegisterEvent`
// calls (engine and other DLLs) can race with the engine's own table init
// and trigger SMemFree on slots they expected to still be NULL — writing
// during that window crashes the engine. DllMain flips this after
// `LoadScriptFunctions_h` returns, so all our writes happen after the
// engine has finished its own setup.
void EnableWrites();

// Call this BEFORE `FrameScript_Initialize_o` runs (i.e. before each
// /reload). The engine reuses our Storm-allocated names through its
// SMemFree teardown, so we don't touch the table — but the table is
// reallocated at a fresh address afterwards, so we drop the writes gate
// and reset cached slot indices.
void PrepareForReload();

// Dispatches an event registered via `Register` with one string + one int
// arg (format `"%s%d"`). Use for `(typeName, enabled)`-style payloads;
// pass booleans as 0/1 since the engine has no native bool format code.
void Fire_SD(int eventID, const char *arg1, int arg2);

} // namespace Event::Custom
