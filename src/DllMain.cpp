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

#include "Blips.h"
#include "Common.h"
#include "Game.h"
#include "MinHook.h"
#include "Offsets.h"
#include "event/Custom.h"

static Game::FrameScript_Initialize_t FrameScript_Initialize_o = nullptr;
static Game::LoadScriptFunctions_t LoadScriptFunctions_o = nullptr;
static Game::CGGameUI_Shutdown_t CGGameUI_Shutdown_o = nullptr;
static Game::FrameRegisterEvent_t FrameRegisterEvent_o = nullptr;

static void __fastcall InvalidFunctionPtrCheck_h() {}

static bool __fastcall FrameScript_Initialize_h() {
    // Invalidate cached event-slot indices BEFORE the engine tears down its
    // event table — the table is rebuilt at a fresh address afterwards.
    Event::Custom::PrepareForReload();
    FrameScript_Initialize_o();
    return true;
}

static void __fastcall LoadScriptFunctions_h() {
    LoadScriptFunctions_o();
    Blips::RegisterLuaFunctions();
    // Now that the engine has finished its own boot-time RegisterEvent
    // calls, it's safe for our writes to land in the event table.
    Event::Custom::EnableWrites();
}

static void __fastcall FrameRegisterEvent_h(void *frame, void *edx, const char *eventName) {
    // Every Lua-side `frame:RegisterEvent(...)` is a chance to claim any
    // custom event slots we couldn't grab at LoadScriptFunctions time —
    // the engine's strcmp scan is about to run, so we need the slot named
    // before it walks the table.
    Event::Custom::RetryAll();
    FrameRegisterEvent_o(frame, edx, eventName);
}

static void __fastcall CGGameUI_Shutdown_h() {
    CGGameUI_Shutdown_o();
    // Match WoW's own behavior (AddOns.txt etc.): flush state on UI teardown
    // — fires on both clean logout and `/reload`. Reset clears the path, so
    // Save must run first.
    Blips::Save();
    Blips::Reset();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        if (MH_Initialize() != MH_OK)
            return FALSE;

        auto *target = reinterpret_cast<LPVOID>(Offsets::FUN_INVALID_FUNCTION_PTR_CHECK);
        if (MH_CreateHook(target, static_cast<LPVOID>(InvalidFunctionPtrCheck_h), nullptr) != MH_OK)
            return FALSE;
        if (MH_EnableHook(target) != MH_OK)
            return FALSE;

        HOOK_FUNCTION(Offsets::FUN_FRAME_SCRIPT_INITIALIZE, FrameScript_Initialize_h,
                      FrameScript_Initialize_o);
        HOOK_FUNCTION(Offsets::FUN_LOAD_SCRIPT_FUNCTIONS, LoadScriptFunctions_h,
                      LoadScriptFunctions_o);
        HOOK_FUNCTION(Offsets::FUN_FRAME_REGISTER_EVENT, FrameRegisterEvent_h,
                      FrameRegisterEvent_o);
        HOOK_FUNCTION(Offsets::FUN_CGGAMEUI_SHUTDOWN, CGGameUI_Shutdown_h, CGGameUI_Shutdown_o);
    } else if (reason == DLL_PROCESS_DETACH) {
        MH_Uninitialize();
    }
    return TRUE;
}
