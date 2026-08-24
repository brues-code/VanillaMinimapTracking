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

// Both macros propagate `return FALSE;` to the caller on failure — caller
// must be in a context where that's a valid early-out (DllMain or our
// Install/UninstallHooks helpers).
#define HOOK_FUNCTION(offset, hook, original)                                                      \
    {                                                                                              \
        auto *target = reinterpret_cast<LPVOID>(offset);                                           \
        if (MH_CreateHook(target, static_cast<LPVOID>(hook),                                       \
                          reinterpret_cast<LPVOID *>(&original)) != MH_OK)                         \
            return FALSE;                                                                          \
        if (MH_EnableHook(target) != MH_OK)                                                        \
            return FALSE;                                                                          \
    }

// Queue variant of HOOK_FUNCTION: creates the hook and queues its enable so
// a single MH_ApplyQueued in the caller activates the whole batch in one
// thread-freeze. Used by the deferred install path in DllMain.cpp, where the
// game's main thread may already be running — the single apply makes the
// hook set come up atomically instead of one prologue at a time.
#define QUEUE_HOOK_FUNCTION(offset, hook, original)                                                \
    {                                                                                              \
        auto *target = reinterpret_cast<LPVOID>(offset);                                           \
        if (MH_CreateHook(target, static_cast<LPVOID>(hook),                                       \
                          reinterpret_cast<LPVOID *>(&original)) != MH_OK)                         \
            return FALSE;                                                                          \
        if (MH_QueueEnableHook(target) != MH_OK)                                                   \
            return FALSE;                                                                          \
    }

#define UNHOOK_FUNCTION(offset, original)                                                          \
    {                                                                                              \
        auto *target = reinterpret_cast<LPVOID>(offset);                                           \
        if (MH_DisableHook(target) != MH_OK)                                                       \
            return FALSE;                                                                          \
        if (MH_RemoveHook(target) != MH_OK)                                                        \
            return FALSE;                                                                          \
        (original) = nullptr;                                                                      \
    }
