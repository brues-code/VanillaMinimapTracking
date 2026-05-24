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

#include "Game.h"
#include "Offsets.h"

// Binds an engine function pointer constant. Each call is
// `const <typedef> <name> = reinterpret_cast<<typedef>>(Offsets::<offset>);`
// — boilerplate that gets repeated for every engine function we wire up.
// `#undef`'d at end-of-file so it doesn't leak.
#define BIND(typedef_t, name, offset_const)                                                        \
    const typedef_t name = reinterpret_cast<typedef_t>(Offsets::offset_const)

namespace Game {

namespace {
// Singly-linked list of pending module registrations. Each
// `ModuleAutoRegister` static instance prepends itself here at DLL load
// time; `RunModuleRegistrations()` walks the list once Lua is ready.
ModuleAutoRegister *g_moduleHead = nullptr;
} // namespace

ModuleAutoRegister::ModuleAutoRegister(Fn f) : fn(f), next(g_moduleHead) {
    g_moduleHead = this;
}

void RunModuleRegistrations() {
    for (auto *node = g_moduleHead; node != nullptr; node = node->next) {
        node->fn();
    }
}

namespace Lua {
BIND(lua_pushnil_t,       PushNil,      LUA_PUSH_NIL);
BIND(lua_isnumber_t,      IsNumber,     LUA_IS_NUMBER);
BIND(lua_tonumber_t,      ToNumber,     LUA_TO_NUMBER);
BIND(lua_pushnumber_t,    PushNumber,   LUA_PUSH_NUMBER);
BIND(lua_isstring_t,      IsString,     LUA_IS_STRING);
BIND(lua_tostring_t,      ToString,     LUA_TO_STRING);
BIND(lua_pushstring_t,    PushString,   LUA_PUSH_STRING);
BIND(lua_pushcclosure_t,  PushCClosure, LUA_PUSH_CCLOSURE);
BIND(lua_gettable_t,      GetTable,     LUA_GET_TABLE);
BIND(lua_settable_t,      SetTable,     LUA_SET_TABLE);
BIND(lua_newtable_t,      NewTable,     LUA_NEW_TABLE);
BIND(lua_type_t,          Type,         LUA_TYPE);
BIND(lua_next_t,          Next,         LUA_NEXT);
BIND(lua_settop_t,        SetTop,       LUA_SET_TOP);
BIND(lua_error_t,         Error,        LUA_ERROR);

void *State() {
    return *reinterpret_cast<void **>(static_cast<uintptr_t>(Offsets::VAR_LUA_STATE));
}

// Looks up `_G[name]`. If absent, creates a fresh table and binds it.
// Leaves the resulting table on top of the stack. Used by both
// `RegisterTableFunction` and `RegisterStringEnum` to spell out the same
// namespace dance the same way.
static void EnsureGlobalTable(void *L, const char *name) {
    PushString(L, name);
    GetTable(L, GLOBALS_INDEX);
    if (Type(L, -1) == TYPE_TABLE)
        return; // already there, leave on stack

    SetTop(L, -2); // pop the non-table
    PushString(L, name);
    NewTable(L);
    SetTable(L, GLOBALS_INDEX);
    // Re-fetch so the table is left on top of the stack for the caller.
    PushString(L, name);
    GetTable(L, GLOBALS_INDEX);
}

void RegisterTableFunction(const char *tableName, const char *methodName, CFunction func) {
    void *L = State();
    if (L == nullptr)
        return;

    EnsureGlobalTable(L, tableName); // [tbl]
    PushString(L, methodName);       // [tbl, methodName]
    PushCClosure(L, func, 0);        // [tbl, methodName, closure]
    SetTable(L, -3);                 // tbl[methodName] = closure; pops k+v. [tbl]
    SetTop(L, -2);                   // pop tbl. []
}

void RegisterStringEnum(const char *parent, const char *sub, const EnumEntry *entries,
                        int count) {
    void *L = State();
    if (L == nullptr)
        return;

    EnsureGlobalTable(L, parent); // [parentTbl]
    PushString(L, sub);           // [parentTbl, subName]
    NewTable(L);                  // [parentTbl, subName, subTbl]
    for (int i = 0; i < count; i++) {
        PushString(L, entries[i].key);
        PushString(L, entries[i].value);
        SetTable(L, -3); // subTbl[key] = value
    }
    SetTable(L, -3); // parentTbl[sub] = subTbl
    SetTop(L, -2);   // pop parentTbl
}
} // namespace Lua

BIND(GetGUIDFromName_t,              GetGUIDFromName,              FUN_GET_GUID_FROM_NAME);
BIND(ClntObjMgrObjectPtr_t,          ClntObjMgrObjectPtr,          FUN_CLNT_OBJ_MGR_OBJECT_PTR);
BIND(TextureGetGxTex_t,              TextureGetGxTex,              FUN_TEXTURE_GET_GX_TEX);
BIND(GxRsSet_t,                      GxRsSet,                      FUN_GX_RS_SET);
BIND(GxPrimLockVertexPtrs_t,         GxPrimLockVertexPtrs,         FUN_GX_PRIM_LOCK_VERTEX_PTRS);
BIND(GxPrimDrawElements_t,           GxPrimDrawElements,           FUN_GX_PRIM_DRAW_ELEMENTS);
BIND(GxPrimUnlockVertexPtrs_t,       GxPrimUnlockVertexPtrs,       FUN_GX_PRIM_UNLOCK_VERTEX_PTRS);
BIND(TextureCreate_t,                TextureCreate,                FUN_TEXTURE_CREATE);
BIND(WorldPosToMinimapFrameCoords_t, WorldPosToMinimapFrameCoords, FUN_WORLD_POS_TO_MINIMAP_FRAME_COORDS);
BIND(SStrPack_t,                     SStrPack,                     FUN_S_STR_PACK);
BIND(CWorld_QueryMapObjIDs_t,        CWorld_QueryMapObjIDs,        FUN_CWORLD_QUERY_MAP_OBJ_IDS);
BIND(ClntObjMgrEnumVisibleObjects_t, ClntObjMgrEnumVisibleObjects, FUN_CLNT_OBJ_MGR_ENUM_VISIBLE_OBJECTS);
BIND(CGUnit_C_CanAssist_t,           CGUnit_C_CanAssist,           FUN_CGUNIT_C_CAN_ASSIST);
BIND(CGUnit_C_UnitReaction_t,        CGUnit_C_UnitReaction,        FUN_CGUNIT_C_UNIT_REACTION);

CStatus::CStatus()
    // `m_head` is a TSLink intrusive-list head pointing at itself, with the
    // low bit of `next` set as the "empty list" sentinel — the engine's
    // CStatus destructor walks this list and the tag tells it the list is
    // empty. Same layout the engine uses when it constructs CStatus itself.
    : vftable(reinterpret_cast<void *>(Offsets::VFTABLE_CSTATUS)), m_unk(8),
      m_head{&m_head, reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(&m_head) | 1U)},
      m_maxSeverity(STATUS_TYPE::STATUS_INFO) {}

CStatus::~CStatus() {
    reinterpret_cast<CStatus_Destructor_t>(Offsets::FUN_CSTATUS_DESTRUCTOR)(this);
}

CGxTexFlags::CGxTexFlags(EGxTexFilter filter, uint32_t wrapU, uint32_t wrapV,
                         uint32_t forceMipTracking, uint32_t generateMipMaps, uint32_t renderTarget,
                         uint32_t maxAnisotropy, uint32_t unknownFlag) {
    reinterpret_cast<CGxTexFlags_Constructor_t>(Offsets::FUN_CGX_TEX_FLAGS_CONSTRUCTOR)(
        this, filter, wrapU, wrapV, forceMipTracking, generateMipMaps, renderTarget, maxAnisotropy,
        unknownFlag);
}

C3Vector *s_blipVertices = reinterpret_cast<C3Vector *>(Offsets::CONST_BLIP_VERTICES);
TexCoord &texCoords = *reinterpret_cast<TexCoord *>(Offsets::CONST_TEX_COORDS);
C3Vector &normal = *reinterpret_cast<C3Vector *>(Offsets::CONST_NORMAL_VEC3);
unsigned short *vertIndices = reinterpret_cast<unsigned short *>(Offsets::CONST_VERT_INDICES);
const float &BLIP_HALF = *reinterpret_cast<float *>(Offsets::CONST_BLIP_HALF);

void DrawMinimapTexture(CGxTex *gxTex, C2Vector minimapPosition, float scale, bool gray) {
    if (gxTex == nullptr)
        return;

    CImVector color = {0xFF, 0xFF, 0xFF, 0xFF};
    if (gray) {
        color = {0xFF, 0xB0, 0xB0, 0xB0};
    }

    C3Vector vertices[4];
    for (auto i = 0; i < 4; i++) {
        vertices[i].x = minimapPosition.x + scale * s_blipVertices[i].x;
        vertices[i].y = minimapPosition.y + scale * s_blipVertices[i].y;
        vertices[i].z = scale * s_blipVertices[i].z;
    }

    GxRsSet(GxRs_Texture0, gxTex);

    GxPrimLockVertexPtrs(4, vertices, 12, &normal, 0, &color, 0, nullptr, 0,
                         reinterpret_cast<C2Vector *>(&texCoords), 8, nullptr, 0);

    GxPrimDrawElements(EGxPrim::GxPrim_TriangleStrip, 4, vertIndices);

    GxPrimUnlockVertexPtrs();
}

} // namespace Game

#undef BIND
