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
#include "addons/Embedded.h"
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

// ---------------------------------------------------------------------------
// Hook install — kept OFF the Windows loader lock.
//
// Every MH_EnableHook freezes all process threads (CreateToolhelp32Snapshot +
// SuspendThread/GetThreadContext each). Doing that from DllMain, under the
// loader lock, is the pattern MinHook documents as unsafe: on machines whose
// security stack intercepts thread suspension it can stall the remote
// LoadLibrary thread past VanillaFixes' 10-second injection deadline, so VF
// reads the still-running thread as STILL_ACTIVE (259) and shows its generic
// "compatible client" error — a false diagnosis of a slow load.
//
// So DllMain installs nothing. The install runs later, on a thread that does
// NOT hold the loader lock, via one of two triggers that both funnel through
// the latched EnsureInitialized():
//   * VanillaFixes' `Load` export, called on the game's main thread after
//     injection (the sanctioned VF extension point), or
//   * a fallback worker thread we spawn from DllMain, ONLY when VanillaFixes
//     is not the loader (no VfPatcher.dll in the process), for injectors that
//     never call `Load`.
//
// The worker MUST stand down under VanillaFixes. VF creates the process
// suspended and injects every dlls.txt DLL sequentially via remote threads;
// a worker spawned from our DllMain starts the moment our own LoadLibrary
// releases the loader lock — i.e. while VF is still injecting the NEXT
// dlls.txt DLL. Its install (plus the MH_ApplyQueued thread-freeze) would
// race that DLL's DllMain patching the same engine functions. `Load` has no
// such race: VfPatcher calls it on the game's main thread after the process
// resumes, when every DllMain in the injection chain has already completed,
// serialized in dlls.txt order — and still before the engine's first read of
// any AddOns file, so the embedded-addon file hooks are up in time.
// ---------------------------------------------------------------------------

static volatile LONG g_initClaimed = 0;    // 0 until a thread takes the installer role
static volatile LONG g_initDone = 0;       // 0 until the install has finished
static volatile LONG g_initResult = 1;     // 0 = success, 1 = failure (until proven)
static volatile LONG g_mhInitialized = 0;  // MH_Initialize succeeded (gates detach teardown)

static bool InstallHooks() {
    if (MH_Initialize() != MH_OK)
        return false;
    InterlockedExchange(&g_mhInitialized, 1);

    auto *target = reinterpret_cast<LPVOID>(Offsets::FUN_INVALID_FUNCTION_PTR_CHECK);
    if (MH_CreateHook(target, static_cast<LPVOID>(InvalidFunctionPtrCheck_h), nullptr) != MH_OK)
        return false;
    if (MH_QueueEnableHook(target) != MH_OK)
        return false;

    QUEUE_HOOK_FUNCTION(Offsets::FUN_FRAME_SCRIPT_INITIALIZE, FrameScript_Initialize_h,
                        FrameScript_Initialize_o);
    QUEUE_HOOK_FUNCTION(Offsets::FUN_LOAD_SCRIPT_FUNCTIONS, LoadScriptFunctions_h,
                        LoadScriptFunctions_o);
    QUEUE_HOOK_FUNCTION(Offsets::FUN_FRAME_REGISTER_EVENT, FrameRegisterEvent_h,
                        FrameRegisterEvent_o);
    QUEUE_HOOK_FUNCTION(Offsets::FUN_CGGAMEUI_SHUTDOWN, CGGameUI_Shutdown_h, CGGameUI_Shutdown_o);

    // Embedded MinimapBlips addon: serves the addon's files from baked-in
    // byte arrays when the user doesn't have it installed on disk.
    if (!Addons::Embedded::QueueHooks())
        return false;

    // One thread-freeze that activates every queued hook at once. On the
    // worker path the game's main thread may already be running, so the
    // single apply also makes the set come up atomically — the file hooks
    // in particular only make sense together.
    return MH_ApplyQueued() == MH_OK;
}

// Runs InstallHooks exactly once. Returns 0 on success, 1 on failure. If a
// second caller arrives while the install is in flight, it blocks briefly so
// both callers observe the real result.
static DWORD EnsureInitialized() {
    if (InterlockedCompareExchange(&g_initClaimed, 1, 0) == 0) {
        g_initResult = InstallHooks() ? 0 : 1;
        InterlockedExchange(&g_initDone, 1);
    } else {
        while (InterlockedCompareExchange(&g_initDone, 0, 0) == 0)
            Sleep(1);
    }
    return static_cast<DWORD>(g_initResult);
}

static DWORD WINAPI InitWorker(LPVOID) {
    EnsureInitialized();
    return 0;
}

// VanillaFixes calls this on the game's MAIN thread after injection
// (InitAdditionalDLLs -> GetProcAddress(module, "Load")), outside the loader
// lock and with no timeout — the sanctioned VF extension point. Returns 0 on
// success; VF reports any non-zero result to the user. Exported undecorated
// as "Load" via src/VanillaMinimapTracking.def.
extern "C" DWORD Load() { return EnsureInitialized(); }

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // Install nothing here (loader lock — see the block comment above).
        //
        // Under VanillaFixes, `Load` is the ONLY install trigger. VfPatcher.dll
        // is injected before any dlls.txt DLL, so its presence identifies VF as
        // the loader; and the `Load` mechanism shipped in the same VF release
        // (v1.4) as dlls.txt support itself, so any VF that loaded us will call
        // it. Spawning the worker here would race the DllMains of the dlls.txt
        // DLLs VF injects after us (see the block comment above).
        if (GetModuleHandleW(L"VfPatcher.dll") == nullptr) {
            // Any other injector: no `Load` caller exists, so install from a
            // worker off the lock. The new thread cannot run its body until
            // DllMain returns and the loader lock releases, and we never wait
            // on it. DisableThreadLibraryCalls suppressed its THREAD_ATTACH.
            HANDLE worker = CreateThread(nullptr, 0, InitWorker, nullptr, 0, nullptr);
            if (worker != nullptr)
                CloseHandle(worker);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        // Clean /quit path — only tear down if we actually initialized.
        if (g_mhInitialized)
            MH_Uninitialize();
    }
    return TRUE;
}
