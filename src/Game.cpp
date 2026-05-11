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
const lua_pushnil_t PushNil = reinterpret_cast<lua_pushnil_t>(Offsets::LUA_PUSH_NIL);
const lua_isnumber_t IsNumber = reinterpret_cast<lua_isnumber_t>(Offsets::LUA_IS_NUMBER);
const lua_tonumber_t ToNumber = reinterpret_cast<lua_tonumber_t>(Offsets::LUA_TO_NUMBER);
const lua_pushnumber_t PushNumber = reinterpret_cast<lua_pushnumber_t>(Offsets::LUA_PUSH_NUMBER);
const lua_isstring_t IsString = reinterpret_cast<lua_isstring_t>(Offsets::LUA_IS_STRING);
const lua_tostring_t ToString = reinterpret_cast<lua_tostring_t>(Offsets::LUA_TO_STRING);
const lua_pushstring_t PushString = reinterpret_cast<lua_pushstring_t>(Offsets::LUA_PUSH_STRING);
const lua_pushcclosure_t PushCClosure =
    reinterpret_cast<lua_pushcclosure_t>(Offsets::LUA_PUSH_CCLOSURE);
const lua_gettable_t GetTable = reinterpret_cast<lua_gettable_t>(Offsets::LUA_GET_TABLE);
const lua_settable_t SetTable = reinterpret_cast<lua_settable_t>(Offsets::LUA_SET_TABLE);
const lua_newtable_t NewTable = reinterpret_cast<lua_newtable_t>(Offsets::LUA_NEW_TABLE);
const lua_type_t Type = reinterpret_cast<lua_type_t>(Offsets::LUA_TYPE);
const lua_next_t Next = reinterpret_cast<lua_next_t>(Offsets::LUA_NEXT);
const lua_settop_t SetTop = reinterpret_cast<lua_settop_t>(Offsets::LUA_SET_TOP);
const lua_error_t Error = reinterpret_cast<lua_error_t>(Offsets::LUA_ERROR);

void *State() {
    return *reinterpret_cast<void **>(static_cast<uintptr_t>(Offsets::VAR_LUA_STATE));
}

void RegisterTableFunction(const char *tableName, const char *methodName, CFunction func) {
    void *L = State();
    if (L == nullptr)
        return;

    // If _G[tableName] doesn't already exist as a table, create it.
    PushString(L, tableName);
    GetTable(L, GLOBALS_INDEX);
    const bool alreadyExists = (Type(L, -1) == TYPE_TABLE);
    SetTop(L, -2); // pop the lookup result
    if (!alreadyExists) {
        PushString(L, tableName);
        NewTable(L);
        SetTable(L, GLOBALS_INDEX);
    }

    // Re-fetch the namespace and set the method on it.
    PushString(L, tableName);
    GetTable(L, GLOBALS_INDEX);   // [tbl]
    PushString(L, methodName);    // [tbl, methodName]
    PushCClosure(L, func, 0);     // [tbl, methodName, closure]
    SetTable(L, -3);              // tbl[methodName] = closure; pops k+v. [tbl]
    SetTop(L, -2);                // pop tbl. []
}

void RegisterStringEnum(const char *parent, const char *sub, const EnumEntry *entries,
                        int count) {
    void *L = State();
    if (L == nullptr)
        return;

    // Ensure _G[parent] exists as a table.
    PushString(L, parent);
    GetTable(L, GLOBALS_INDEX);
    const bool parentExists = (Type(L, -1) == TYPE_TABLE);
    SetTop(L, -2);
    if (!parentExists) {
        PushString(L, parent);
        NewTable(L);
        SetTable(L, GLOBALS_INDEX);
    }

    // _G[parent][sub] = { ...entries }
    PushString(L, parent);
    GetTable(L, GLOBALS_INDEX);   // [parentTbl]
    PushString(L, sub);            // [parentTbl, subName]
    NewTable(L);                   // [parentTbl, subName, subTbl]
    for (int i = 0; i < count; i++) {
        PushString(L, entries[i].key);
        PushString(L, entries[i].value);
        SetTable(L, -3);            // subTbl[key] = value
    }
    SetTable(L, -3);                // parentTbl[sub] = subTbl
    SetTop(L, -2);                  // pop parentTbl
}
} // namespace Lua

const GetGUIDFromName_t GetGUIDFromName =
    reinterpret_cast<GetGUIDFromName_t>(Offsets::FUN_GET_GUID_FROM_NAME);
const ClntObjMgrObjectPtr_t ClntObjMgrObjectPtr =
    reinterpret_cast<ClntObjMgrObjectPtr_t>(Offsets::FUN_CLNT_OBJ_MGR_OBJECT_PTR);
const TextureGetGxTex_t TextureGetGxTex =
    reinterpret_cast<TextureGetGxTex_t>(Offsets::FUN_TEXTURE_GET_GX_TEX);
const GxRsSet_t GxRsSet = reinterpret_cast<GxRsSet_t>(Offsets::FUN_GX_RS_SET);
const GxPrimLockVertexPtrs_t GxPrimLockVertexPtrs =
    reinterpret_cast<GxPrimLockVertexPtrs_t>(Offsets::FUN_GX_PRIM_LOCK_VERTEX_PTRS);
const GxPrimDrawElements_t GxPrimDrawElements =
    reinterpret_cast<GxPrimDrawElements_t>(Offsets::FUN_GX_PRIM_DRAW_ELEMENTS);
const GxPrimUnlockVertexPtrs_t GxPrimUnlockVertexPtrs =
    reinterpret_cast<GxPrimUnlockVertexPtrs_t>(Offsets::FUN_GX_PRIM_UNLOCK_VERTEX_PTRS);
const TextureCreate_t TextureCreate =
    reinterpret_cast<TextureCreate_t>(Offsets::FUN_TEXTURE_CREATE);
const WorldPosToMinimapFrameCoords_t WorldPosToMinimapFrameCoords =
    reinterpret_cast<WorldPosToMinimapFrameCoords_t>(
        Offsets::FUN_WORLD_POS_TO_MINIMAP_FRAME_COORDS);
const SStrPack_t SStrPack = reinterpret_cast<SStrPack_t>(Offsets::FUN_S_STR_PACK);
const CWorld_QueryMapObjIDs_t CWorld_QueryMapObjIDs =
    reinterpret_cast<CWorld_QueryMapObjIDs_t>(Offsets::FUN_CWORLD_QUERY_MAP_OBJ_IDS);
const ClntObjMgrEnumVisibleObjects_t ClntObjMgrEnumVisibleObjects =
    reinterpret_cast<ClntObjMgrEnumVisibleObjects_t>(
        Offsets::FUN_CLNT_OBJ_MGR_ENUM_VISIBLE_OBJECTS);
const CGUnit_C_CanAssist_t CGUnit_C_CanAssist =
    reinterpret_cast<CGUnit_C_CanAssist_t>(Offsets::FUN_CGUNIT_C_CAN_ASSIST);

CStatus::CStatus()
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

void DrawMinimapTexture(HTEXTURE__ *texture, C2Vector minimapPosition, float scale, bool gray) {
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

    CStatus status;
    CGxTex *gxTex = TextureGetGxTex(texture, 1, &status);
    if (!status.ok())
        return;

    GxRsSet(GxRs_Texture0, gxTex);

    GxPrimLockVertexPtrs(4, vertices, 12, &normal, 0, &color, 0, nullptr, 0,
                         reinterpret_cast<C2Vector *>(&texCoords), 8, nullptr, 0);

    GxPrimDrawElements(EGxPrim::GxPrim_TriangleStrip, 4, vertIndices);

    GxPrimUnlockVertexPtrs();
}

} // namespace Game
