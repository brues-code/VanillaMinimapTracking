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

// `Frame::RegisterEvent` is `__thiscall(this, eventName)`. MSVC can't emit
// __thiscall on free functions, but a __fastcall with a dummy EDX arg
// matches the register layout (ECX=this, EDX unused, stack=name).
using FrameRegisterEvent_t = void(__fastcall *)(void *frame, void *edx,
                                                 const char *eventName);
static FrameRegisterEvent_t FrameRegisterEvent_o = nullptr;

// No-op replacement for the engine's "is this function pointer valid" check.
// The stock check crashes us out under MinHook's trampolines because the
// trampoline page isn't inside the engine's expected text range. Returning
// "valid" (do-nothing body) lets our hooks coexist with the check.
static void __fastcall InvalidFunctionPtrCheck_h() {}

static bool __fastcall FrameScript_Initialize_h() {
    // Invalidate cached slot indices BEFORE the engine tears down the
    // event table — the table is rebuilt at a fresh allocation.
    Event::Custom::PrepareForReload();
    // FrameScript init fires on both initial load and after /reload, so
    // clear the logout gate here so our hooks resume work in the new
    // session. (Set by `CGGameUI_Shutdown_h` on logout/reload.)
    Blips::g_inLogout = false;
    FrameScript_Initialize_o();
    return true;
}

static void __fastcall LoadScriptFunctions_h() {
    LoadScriptFunctions_o();
    // Each module self-registers via a `Game::ModuleAutoRegister` static at
    // file scope; we just walk the list here.
    Game::RunModuleRegistrations();
    // Permit writes from this point on. Earlier writes can race with
    // the engine's table init and crash in `SMemFree`.
    Event::Custom::EnableWrites();
}

// Every Lua-side `frame:RegisterEvent(...)` is a chance to claim a
// slot for any unclaimed custom event. By this point the engine's
// table is fully populated and other DLLs' post-rebuild writes are
// done, so the table is settled and our backwards walk finds genuine
// NULL slots near the tail.
static void __fastcall FrameRegisterEvent_h(void *frame, void *edx,
                                            const char *eventName) {
    // Once logout has started, skip both our bookkeeping AND the
    // engine trampoline. Frame registrations during teardown aren't
    // needed for our state and the trampoline path has been seen to
    // crash inside the object-manager deref during this window.
    if (Blips::g_inLogout)
        return;
    Event::Custom::RetryClaims();
    FrameRegisterEvent_o(frame, edx, eventName);
}

static void __fastcall CGGameUI_Shutdown_h() {
    // Flip the logout flag BEFORE invoking the trampoline so any other
    // hook the engine fires during its own shutdown sequence sees the
    // flag set and bails immediately. Then save state, uninstall blip
    // hooks, and run the engine teardown.
    Blips::g_inLogout = true;
    Blips::Save();
    Blips::Reset();
    CGGameUI_Shutdown_o();
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
