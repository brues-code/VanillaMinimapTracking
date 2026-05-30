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

enum Offsets {
    PATCH_MINIMAP_RENDER_PARTY_LISTING = 0x4ED6BE,
    PATCH_MINIMAP_TRACK_UPDATE_CHANGED_GATE = 0x4EB4CC,
    PATCH_MINIMAP_TRACK_UPDATE_PRE_SHOW_GATE = 0x4EB54C,
    PATCH_MINIMAP_TRACK_UPDATE_APPEND_TO_TOOLTIP_BUFFER = 0x4EB6F4,

    FUN_CGX_TEX_FLAGS_CONSTRUCTOR = 0x58A980,
    FUN_CLNT_OBJ_MGR_ENUM_VISIBLE_OBJECTS = 0x468380,
    FUN_CLNT_OBJ_MGR_OBJECT_PTR = 0x468460,
    FUN_CSTATUS_DESTRUCTOR = 0x419E30,
    FUN_GET_GUID_FROM_NAME = 0x515970,
    FUN_GX_PRIM_DRAW_ELEMENTS = 0x58A2E0,
    FUN_GX_PRIM_LOCK_VERTEX_PTRS = 0x58A2A0,
    FUN_GX_PRIM_UNLOCK_VERTEX_PTRS = 0x58A340,
    FUN_GX_RS_SET = 0x589E80,
    FUN_LOAD_SCRIPT_FUNCTIONS = 0x490250,
    FUN_OBJECT_ENUM_PROC = 0x4EAA90,
    FUN_RENDER_OBJECT_BLIP = 0x4EBC00,
    FUN_TEXTURE_CREATE = 0x449D90,
    FUN_TEXTURE_GET_GX_TEX = 0x44ACF0,
    FUN_WORLD_POS_TO_MINIMAP_FRAME_COORDS = 0x4EAA30,
    FUN_S_STR_PACK = 0x64A760,
    FUN_INVALID_FUNCTION_PTR_CHECK = 0x42A320,
    FUN_CWORLD_QUERY_MAP_OBJ_IDS = 0x670540,
    FUN_FRAME_SCRIPT_INITIALIZE = 0x7039E0,
    FUN_CGGAMEUI_SHUTDOWN = 0X490BD0,
    FUN_CGUNIT_C_CAN_ASSIST = 0x6066F0,
    FUN_CGUNIT_C_UNIT_REACTION = 0x6061E0,

    // Embedded-addon plumbing (see src/addons/Embedded.cpp). Two file
    // pipelines need intercepting because the engine reads text resources
    // (`.lua`, `.toc`) via the high-level `FUN_FILE_READ` (open+read+close
    // in one call) but textures (`.blp`) via the low-level `FUN_FILE_OPEN`
    // → `FUN_FILE_READ_HANDLE` pair. Both pipelines need to serve our
    // baked-in byte arrays when paths fall under
    // `Interface\AddOns\MinimapBlips\`. `FUN_ADDON_INIT` post-hook calls
    // `FUN_TOC_PARSER` with our addon name so the engine treats it as a
    // registered addon. `FUN_STORM_SMEM_*` are the engine's allocators —
    // we allocate through them so the caller's standard `SMemFree`
    // reclaims our buffers cleanly.
    FUN_FILE_READ = 0x00648620,
    FUN_FILE_OPEN = 0x006477C0,
    FUN_FILE_READ_HANDLE = 0x00648460,
    FUN_FILE_SIZE = 0x006487F0,
    FUN_TOC_PARSER = 0x0051C9B0,
    FUN_ADDON_INIT = 0x0051C740,
    FUN_STORM_SMEM_ALLOC = 0x006462E0,
    FUN_STORM_SMEM_FREE = 0x00646430,

    // FrameScript_SignalEvent2 — fires a custom event with a printf-style
    // format string, dispatching arg1, arg2, ... to all frames that have
    // registered the event by name.
    FUN_FIRE_EVENT = 0x00703F50,
    // `Frame::RegisterEvent(name)` — hook point so `Event::Custom::RetryClaims`
    // can grab table slots once Lua starts listening to events.
    FUN_FRAME_REGISTER_EVENT = 0x00702140,
    // Storm SStrDup — `__stdcall char *(const char *src, file, line)`.
    // Wraps `SMemAlloc`, which the engine's reload teardown's `SMemFree`
    // validates against; using it for our event-name copies keeps the
    // teardown safe.
    FUN_STORM_SSTRDUP = 0x0064A620,

    LUA_PUSH_NIL = 0x6F37F0,
    LUA_IS_NUMBER = 0x6F34D0,
    LUA_TO_NUMBER = 0x6F3620,
    LUA_PUSH_NUMBER = 0x6F3810,
    LUA_IS_STRING = 0x6F3510,
    LUA_TO_STRING = 0x6F3690,
    LUA_PUSH_STRING = 0x6F3890,
    LUA_GET_TABLE = 0x6F3A40,
    LUA_SET_TABLE = 0x6F3E20,
    LUA_NEW_TABLE = 0x6F3C90,
    LUA_PUSH_CCLOSURE = 0x6F3920,
    LUA_TYPE = 0x6F3400,
    LUA_NEXT = 0x6F4450,
    LUA_SET_TOP = 0x6F3080,
    LUA_ERROR = 0x6F4940,

    VAR_LUA_STATE = 0x00CEEF74,

    // Object-manager base pointer. NULL on the glue / character-select
    // screen — engine functions like `ClntObjMgrEnumVisibleObjects` and
    // `ClntObjMgrObjectPtr` deref this unconditionally on entry, so any
    // synchronous call to them from a Lua handler must null-check this
    // first or risk an access violation pre-login.
    VAR_OBJECT_MANAGER_PTR = 0x00B41414,

    // Engine globals that the SavedVariables/AddOns.txt path-builder at
    // 0x0051EBE0 uses to format `WTF\Account\<account>\<realm>\<character>`.
    //
    // Account is the value WoW writes the per-character WTF directory under —
    // NOT the saved-credentials CVAR string at 0x008826C8 we tried first.
    VAR_ACCOUNT_NAME_PTR = 0x00BE1C0C,    // *(char**) — account name string
    VAR_CHARACTER_NAME = 0x00C27D88,      // inline char buffer (NUL byte 0 ⇒ no active char)
    VAR_REALM_INFO_PTR = 0x00C28130,      // *(struct**); struct at +0x20 holds realm name char*
    OFF_REALM_INFO_NAME = 0x20,

    // Engine event registry. `[VAR_EVENT_TABLE_BASE_PTR]` dereferences to
    // an array of 16-byte entries; `[VAR_EVENT_TABLE_COUNT]` is the live
    // entry count. Each entry's `+OFF_EVENT_ENTRY_NAME` is a Storm-allocated
    // C-string (NULL means "free slot we can claim for a custom event").
    VAR_EVENT_TABLE_BASE_PTR = 0x00CEEF68,
    VAR_EVENT_TABLE_COUNT = 0x00CEEF64,
    EVENT_ENTRY_STRIDE = 0x10,
    OFF_EVENT_ENTRY_NAME = 0x00,

    CONST_BLIP_VERTICES = 0xBC8230,
    CONST_NORMAL_VEC3 = 0xBC829C,
    CONST_TEX_COORDS = 0xBC77F0,
    CONST_VERT_INDICES = 0x807A2C,
    CONST_BLIP_HALF = 0xBC7630,

    VFTABLE_CSTATUS = 0x7FFA10,
};
