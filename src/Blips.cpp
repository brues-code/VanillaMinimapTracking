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

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace Blips {

static Game::RenderObjectBlips_t RenderObjectBlips_o = nullptr;
static Game::ObjectEnumProc_t ObjectEnumProc_o = nullptr;
static Game::ClntObjMgrEnumVisibleObjects_t ClntObjMgrEnumVisibleObjects_o = nullptr;
static void *MinimapRender_PartyListing_o = nullptr;
static void *OnLayerTrackUpdate_ChangedGate_o = nullptr;
static void *OnLayerTrackUpdate_PreShowGate_o = nullptr;
static void *OnLayerTrackUpdate_AppendToTooltipBuffer_o = nullptr;

struct Blip {
    Game::HTEXTURE__ *texture;
    float scale;
};

struct TrackedObjectData {
    uint64_t guid;
    Game::C2Vector minimapPos;
    bool isInDifferentArea;
    std::string name;
    Blip blip;
};

struct BlipHoverEntry {
    uint64_t guid;
    std::string name;
    bool gray;
};

struct BlipHoverState {
    bool changed = false;
    bool nonEmpty = false;
    uint64_t hash = 0;
    std::vector<BlipHoverEntry> hits;
};

static bool g_hooksInstalled = false;
static std::unordered_map<std::string, Game::HTEXTURE__ *> g_textureCache;
static std::unordered_map<std::string, Blip> g_registeredIcons;
static bool g_targetTracking = false;
static uint64_t g_currentTargetGUID = 0;
static bool g_focusTracking = false;
static uint64_t g_focusGUID = 0;
static Blip g_targetHostileBlip = {nullptr, 1.0F};
static std::unordered_map<uint32_t, Blip> g_trackedUnitFlagsBlips;
static std::unordered_map<uint32_t, Blip> g_trackedGameObjectTypesBlips;
static std::vector<TrackedObjectData> g_trackedObjectsData;
static BlipHoverState g_blipHoverState;

static const std::unordered_map<std::string, uint32_t> g_stringToFlag = {
    {"auctioneer", Game::UNIT_NPC_FLAG_AUCTIONEER},
    {"banker", Game::UNIT_NPC_FLAG_BANKER},
    {"battlemaster", Game::UNIT_NPC_FLAG_BATTLEMASTER},
    {"flight master", Game::UNIT_NPC_FLAG_FLIGHTMASTER},
    {"innkeeper", Game::UNIT_NPC_FLAG_INNKEEPER},
    {"repair", Game::UNIT_NPC_FLAG_REPAIR},
    {"stable master", Game::UNIT_NPC_FLAG_STABLEMASTER},
    {"summoning ritual unit", Game::UNIT_NPC_FLAG_SUMMONING_RITUAL},
    {"trainer", Game::UNIT_NPC_FLAG_TRAINER},
    {"vendor", Game::UNIT_NPC_FLAG_VENDOR}};

static const std::unordered_map<std::string, uint32_t> g_stringToGameObjectType = {
    {"brainwashing", Game::GAMEOBJECT_TYPE_QUESTGIVER}, // Will be further filtered
    {"mailbox", Game::GAMEOBJECT_TYPE_MAILBOX},
    {"summoning ritual object", Game::GAMEOBJECT_TYPE_SUMMONING_RITUAL},
};

static void TrackObject(Game::MINIMAPINFO *info, Game::CGObject_C *objectptr, uint64_t guid,
                        Blip blip) {
    Game::C2Vector minimapPos;
    Game::C3Vector unitPos;

    uint32_t wmoID = 0;
    uint32_t mapObjID = 0;
    uint32_t groupNum = 0;
    Game::CWorld_QueryMapObjIDs(objectptr->m_worldData, &wmoID, &mapObjID, &groupNum);

    // Hide outside blip when inside, replicating original function
    if (info->wmoID && (wmoID != info->wmoID || mapObjID != info->mapObjID))
        return;

    objectptr->vftable->GetPosition(objectptr, &unitPos);
    float unkScale = info->minimapFrame->FrameScriptPart.vftable->GetUnkScale(
        &info->minimapFrame->FrameScriptPart);

    Game::WorldPosToMinimapFrameCoords(&minimapPos, nullptr, info->currentPos, info->radius,
                                       unitPos.x, unitPos.y, info->layoutScale, unkScale);

    g_trackedObjectsData.push_back(
        {guid, minimapPos, wmoID != info->wmoID, objectptr->vftable->GetName(objectptr), blip});
}

static bool IsTargetHostile(Game::CGUnit_C *unitptr) {
    const uint64_t playerGUID = Game::GetGUIDFromName("player");
    if (playerGUID == 0)
        return false;

    auto *playerPtr = reinterpret_cast<Game::CGUnit_C *>(Game::ClntObjMgrObjectPtr(
        Game::TYPE_MASK::TYPEMASK_UNIT | Game::TYPE_MASK::TYPEMASK_PLAYER, nullptr,
        playerGUID, 0));
    if (playerPtr == nullptr)
        return false;

    return !Game::CGUnit_C_CanAssist(playerPtr, unitptr);
}

static bool IsAnyTrackingActive() {
    return g_targetTracking || g_focusTracking || !g_trackedUnitFlagsBlips.empty() ||
           !g_trackedGameObjectTypesBlips.empty();
}

static bool CheckObject(Game::MINIMAPINFO *info, uint64_t guid) {
    if (g_targetTracking && g_currentTargetGUID != 0 && guid == g_currentTargetGUID) {
        const auto iconIt = g_registeredIcons.find("target");
        if (iconIt != g_registeredIcons.end()) {
            auto *unitptr = reinterpret_cast<Game::CGUnit_C *>(
                Game::ClntObjMgrObjectPtr(Game::TYPE_MASK::TYPEMASK_UNIT, nullptr, guid, 0));
            if (unitptr != nullptr) {
                Blip blip = iconIt->second;
                if (g_targetHostileBlip.texture != nullptr && IsTargetHostile(unitptr)) {
                    blip = g_targetHostileBlip;
                }
                TrackObject(info, reinterpret_cast<Game::CGObject_C *>(unitptr), guid, blip);
                return true;
            }
        }
        return false;
    }

    if (g_focusTracking && g_focusGUID != 0 && guid == g_focusGUID) {
        const auto iconIt = g_registeredIcons.find("focus");
        if (iconIt != g_registeredIcons.end()) {
            auto *unitptr = reinterpret_cast<Game::CGUnit_C *>(
                Game::ClntObjMgrObjectPtr(Game::TYPE_MASK::TYPEMASK_UNIT, nullptr, guid, 0));
            if (unitptr != nullptr) {
                TrackObject(info, reinterpret_cast<Game::CGObject_C *>(unitptr), guid,
                            iconIt->second);
                return true;
            }
        }
        return false;
    }

    uint32_t typemask = 0;
    if (!g_trackedUnitFlagsBlips.empty()) {
        typemask = typemask | Game::TYPE_MASK::TYPEMASK_UNIT;
    }
    if (!g_trackedGameObjectTypesBlips.empty()) {
        typemask = typemask | Game::TYPE_MASK::TYPEMASK_GAMEOBJECT;
    }
    if (typemask == 0)
        return false;

    Game::CGObject_C *objptr = Game::ClntObjMgrObjectPtr(typemask, nullptr, guid, 0);
    if (objptr == nullptr)
        return false;

    if (objptr->m_objectType == Game::OBJECT_TYPE::UNIT) {
        const auto *unitData = reinterpret_cast<Game::CGUnitData *>(objptr->m_data);
        uint32_t matchedFlag = 0;
        Blip blip;

        // Units can have multiple flags (ex: repair and vendor), we find the strongest one
        for (const auto &[flag, tracked] : g_trackedUnitFlagsBlips) {
            if (unitData->m_npcFlags & flag) {
                if (flag > matchedFlag) {
                    matchedFlag = flag;
                    blip = tracked;
                }
            }
        }
        if (matchedFlag != 0) {
            TrackObject(info, objptr, guid, blip);
            return true;
        }
    } else if (objptr->m_objectType == Game::OBJECT_TYPE::GAMEOBJECT) {
        const auto *gameObjectData = reinterpret_cast<Game::CGGameObjectData *>(objptr->m_data);

        const auto it = g_trackedGameObjectTypesBlips.find(gameObjectData->m_type);
        if (it != g_trackedGameObjectTypesBlips.end()) {
            // Goblin Brainwashing Device filtering
            if (gameObjectData->m_type != Game::GAMEOBJECT_TYPES::GAMEOBJECT_TYPE_QUESTGIVER ||
                gameObjectData->m_displayID == 6424) {
                TrackObject(info, objptr, guid, it->second);
                return true;
            }
        }
    }
    return false;
}

static void DrawTrackedBlips(Game::CGMinimapFrame *minimapPtr, Game::DNInfo *dnInfo) {
    // We are gathering the position in ObjectEnumProc because it is rate limited to avoid spamming
    // expensive calls. To do it in RenderObjectBlips, we can use DNInfo for current position,
    // MinimapGetWorldRadius() for world radius and minimapPtr +0x7C for layout scale.
    for (const auto &objData : g_trackedObjectsData) {
        Game::DrawMinimapTexture(objData.blip.texture, objData.minimapPos, objData.blip.scale,
                                 objData.isInDifferentArea);
    }
}

static bool IsUnitTracked(uint64_t guid) {
    if (guid == 0)
        return false;
    if (g_targetTracking && guid == g_currentTargetGUID)
        return true;
    if (g_focusTracking && guid == g_focusGUID)
        return true;
    return false;
}

static void UpdateCustomHover(Game::C2Vector mouse, Game::C2Vector offset) {
    std::vector<BlipHoverEntry> now;
    now.reserve(g_trackedObjectsData.size());

    for (const auto &objData : g_trackedObjectsData) {
        const float half = Game::BLIP_HALF * objData.blip.scale;
        const float px = objData.minimapPos.x + offset.x;
        const float py = objData.minimapPos.y + offset.y;

        if (fabsf(mouse.x - px) <= half && fabsf(mouse.y - py) <= half) {
            now.push_back({objData.guid, objData.name, objData.isInDifferentArea});
        }
    }

    std::sort(now.begin(), now.end(), [](const auto &a, const auto &b) { return a.guid < b.guid; });

    // FNV-1a hash
    uint64_t h = 14695981039346656037ULL;
    auto mix = [&](uint64_t v) {
        h ^= v;
        h *= 1099511628211ULL;
    };
    for (const auto &hit : now) {
        mix(hit.guid);
        mix(hit.gray ? 1ULL : 0ULL);
        mix(std::hash<std::string_view>{}(hit.name));
    }

    g_blipHoverState.nonEmpty = !now.empty();
    const bool changed = (h != g_blipHoverState.hash);
    g_blipHoverState.changed = changed;
    if (changed) {
        g_blipHoverState.hash = h;
        g_blipHoverState.hits.swap(now);
    }
}

static void WriteToMinimapTooltip(char *tooltipText) {
    if (g_blipHoverState.hits.empty())
        return;

    for (const auto &hit : g_blipHoverState.hits) {
        if (hit.gray) {
            Game::SStrPack(tooltipText, "|cffb0b0b0", 0x400);
            Game::SStrPack(tooltipText, hit.name.c_str(), 0x400);
            Game::SStrPack(tooltipText, "|r\n", 0x400);
        } else {
            Game::SStrPack(tooltipText, hit.name.c_str(), 0x400);
            Game::SStrPack(tooltipText, "\n", 0x400);
        }
    }
}

static int __fastcall
ClntObjMgrEnumVisibleObjects_h(Game::ClntObjMgrEnumVisibleObjectsCallback_t callback,
                               void *context) {
    if (reinterpret_cast<uintptr_t>(callback) == Offsets::FUN_OBJECT_ENUM_PROC) {
        g_trackedObjectsData.clear();
        if (g_targetTracking) {
            g_currentTargetGUID = Game::GetGUIDFromName("target");
        }
    }
    return ClntObjMgrEnumVisibleObjects_o(callback, context);
}

static int __fastcall ObjectEnumProc_h(Game::MINIMAPINFO *info, uint64_t guid) {
    if (!CheckObject(info, guid))
        ObjectEnumProc_o(info, guid);
    return 1; // The original function always seems to return 1
}

static void __fastcall RenderObjectBlips_h(Game::CGMinimapFrame *thisptr, void * /*edx*/,
                                           Game::DNInfo *dnInfo) {
    RenderObjectBlips_o(thisptr, dnInfo);
    DrawTrackedBlips(thisptr, dnInfo);
}

constexpr uintptr_t minimapSkipPartyUnitAddress = 0x4ed79e;

static void __declspec(naked) MinimapRender_PartyListing_h() {
    __asm {
        pushad
        mov  eax, [edi + 0xbc7660] // GUID low
        mov  edx, [edi + 0xbc7664] // GUID high
        push edx
        push eax
        call IsUnitTracked
        add  esp, 8
        test al, al
        jnz  skip

        popad
        jmp  dword ptr [MinimapRender_PartyListing_o]
    skip:
        popad
        jmp  minimapSkipPartyUnitAddress // skip processing this unit
    }
}

// Right before the early-out check uses ECX to decide if anything changed
static void __declspec(naked) OnLayerTrackUpdate_ChangedGate_h() {
    __asm {
        pushad

        mov  eax, [ebx+0x8] // param pointer
        push dword ptr [ebp-0x40] // offsetY
        push dword ptr [ebp-0x3C] // offsetX
        push dword ptr [eax+0x28] // mouseY
        push dword ptr [eax+0x24] // mouseX
        call UpdateCustomHover
        add  esp, 16

        popad

            // If our set changed, force ECX=1 (stock "changed" flag)
        cmp  byte ptr [g_blipHoverState.changed], 0
        je   short no_custom_change
        mov  ecx, 1
    no_custom_change:
        jmp  dword ptr [OnLayerTrackUpdate_ChangedGate_o]
    }
}

// Right before the branch that decides whether to build/update the tooltip text
static void __declspec(naked) OnLayerTrackUpdate_PreShowGate_h() {
    __asm {
        // If we have any changes, force EDX=1 so the stock code builds the tooltip
        cmp  byte ptr [g_blipHoverState.changed], 0
        je   short no_set_edx
        mov  edx, 1
    no_set_edx:
        jmp  dword ptr [OnLayerTrackUpdate_PreShowGate_o]
    }
}

// After the scratch tooltip buffer is initialized, before original blips text are appended
static void __declspec(naked) OnLayerTrackUpdate_AppendToTooltipBuffer_h() {
    __asm {
        // Append into stock scratch buffer [EBP-0x468]
        pushad
        lea  eax, [ebp-0x468]
        push eax
        call WriteToMinimapTooltip
        add  esp, 4
        popad

        jmp  dword ptr [OnLayerTrackUpdate_AppendToTooltipBuffer_o]
    }
}

static bool InstallHooks() {
    if (g_hooksInstalled)
        return TRUE;

    HOOK_FUNCTION(Offsets::FUN_CLNT_OBJ_MGR_ENUM_VISIBLE_OBJECTS, ClntObjMgrEnumVisibleObjects_h,
                  ClntObjMgrEnumVisibleObjects_o);
    HOOK_FUNCTION(Offsets::FUN_OBJECT_ENUM_PROC, ObjectEnumProc_h, ObjectEnumProc_o);
    HOOK_FUNCTION(Offsets::FUN_RENDER_OBJECT_BLIP, RenderObjectBlips_h, RenderObjectBlips_o);
    HOOK_FUNCTION(Offsets::PATCH_MINIMAP_RENDER_PARTY_LISTING, MinimapRender_PartyListing_h,
                  MinimapRender_PartyListing_o);
    HOOK_FUNCTION(Offsets::PATCH_MINIMAP_TRACK_UPDATE_CHANGED_GATE,
                  OnLayerTrackUpdate_ChangedGate_h, OnLayerTrackUpdate_ChangedGate_o);
    HOOK_FUNCTION(Offsets::PATCH_MINIMAP_TRACK_UPDATE_PRE_SHOW_GATE,
                  OnLayerTrackUpdate_PreShowGate_h, OnLayerTrackUpdate_PreShowGate_o);
    HOOK_FUNCTION(Offsets::PATCH_MINIMAP_TRACK_UPDATE_APPEND_TO_TOOLTIP_BUFFER,
                  OnLayerTrackUpdate_AppendToTooltipBuffer_h,
                  OnLayerTrackUpdate_AppendToTooltipBuffer_o);

    g_hooksInstalled = true;
    return TRUE;
}

static bool UninstallHooks() {
    if (!g_hooksInstalled)
        return TRUE;

    UNHOOK_FUNCTION(Offsets::FUN_CLNT_OBJ_MGR_ENUM_VISIBLE_OBJECTS, ClntObjMgrEnumVisibleObjects_o);
    UNHOOK_FUNCTION(Offsets::FUN_OBJECT_ENUM_PROC, ObjectEnumProc_o);
    UNHOOK_FUNCTION(Offsets::FUN_RENDER_OBJECT_BLIP, RenderObjectBlips_o);
    UNHOOK_FUNCTION(Offsets::PATCH_MINIMAP_RENDER_PARTY_LISTING, MinimapRender_PartyListing_o);
    UNHOOK_FUNCTION(Offsets::PATCH_MINIMAP_TRACK_UPDATE_CHANGED_GATE,
                    OnLayerTrackUpdate_ChangedGate_o);
    UNHOOK_FUNCTION(Offsets::PATCH_MINIMAP_TRACK_UPDATE_PRE_SHOW_GATE,
                    OnLayerTrackUpdate_PreShowGate_o);
    UNHOOK_FUNCTION(Offsets::PATCH_MINIMAP_TRACK_UPDATE_APPEND_TO_TOOLTIP_BUFFER,
                    OnLayerTrackUpdate_AppendToTooltipBuffer_o);

    g_hooksInstalled = false;
    return TRUE;
}

static Game::HTEXTURE__ *LoadTextureCached(const std::string &texturePathLower) {
    if (const auto it = g_textureCache.find(texturePathLower); it != g_textureCache.end()) {
        return it->second;
    }

    Game::CGxTexFlags texFlags(Game::EGxTexFilter::GxTex_Nearest, 0, 0, 0, 0, 0, 0, 1);
    Game::CStatus status;
    Game::HTEXTURE__ *texture =
        Game::TextureCreate(texturePathLower.c_str(), &status, texFlags, 0, 1);
    if (!status.ok()) {
        return nullptr;
    }
    g_textureCache[texturePathLower] = texture;
    return texture;
}

static int __fastcall Script_MinimapBlip_RegisterIcon(void *L) {
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: MinimapBlip_RegisterIcon(trackingType, icon [, scale])");
        return 0;
    }

    std::string typeName = Game::Lua::ToString(L, 1);
    std::transform(typeName.begin(), typeName.end(), typeName.begin(), ::tolower);

    std::string texturePath = Game::Lua::ToString(L, 2);
    std::transform(texturePath.begin(), texturePath.end(), texturePath.begin(), ::tolower);

    float scale = 1.0F;
    if (Game::Lua::IsNumber(L, 3)) {
        scale = static_cast<float>(Game::Lua::ToNumber(L, 3));
    }

    Game::HTEXTURE__ *texture = LoadTextureCached(texturePath);
    if (!texture) {
        Game::Lua::Error(L, "Couldn't load texture.");
        return 0;
    }

    g_registeredIcons[typeName] = {texture, scale};
    return 0;
}

static int __fastcall Script_MinimapBlip_RegisterHostileIcon(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: MinimapBlip_RegisterHostileIcon(icon [, scale])");
        return 0;
    }

    std::string texturePath = Game::Lua::ToString(L, 1);
    std::transform(texturePath.begin(), texturePath.end(), texturePath.begin(), ::tolower);

    float scale = 1.0F;
    if (Game::Lua::IsNumber(L, 2)) {
        scale = static_cast<float>(Game::Lua::ToNumber(L, 2));
    }

    Game::HTEXTURE__ *texture = LoadTextureCached(texturePath);
    if (!texture) {
        Game::Lua::Error(L, "Couldn't load texture.");
        return 0;
    }

    g_targetHostileBlip = {texture, scale};
    return 0;
}

static int __fastcall Script_MinimapBlip_Track(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: MinimapBlip_Track(trackingType [, enabled])");
        return 0;
    }

    std::string typeName = Game::Lua::ToString(L, 1);
    std::transform(typeName.begin(), typeName.end(), typeName.begin(), ::tolower);

    bool enabled = true;
    if (Game::Lua::IsNumber(L, 2)) {
        enabled = (Game::Lua::ToNumber(L, 2) != 0.0);
    }

    if (typeName == "target") {
        if (enabled) {
            if (g_registeredIcons.find("target") == g_registeredIcons.end()) {
                Game::Lua::Error(
                    L, "No icon registered for target. Call MinimapBlip_RegisterIcon first.");
                return 0;
            }
            g_targetTracking = true;
            InstallHooks();
        } else {
            g_targetTracking = false;
            if (!IsAnyTrackingActive())
                UninstallHooks();
        }
        return 0;
    }

    if (typeName == "focus") {
        if (enabled) {
            if (g_registeredIcons.find("focus") == g_registeredIcons.end()) {
                Game::Lua::Error(
                    L, "No icon registered for focus. Call MinimapBlip_RegisterIcon first.");
                return 0;
            }
            g_focusTracking = true;
            InstallHooks();
        } else {
            g_focusTracking = false;
            if (!IsAnyTrackingActive())
                UninstallHooks();
        }
        return 0;
    }

    const auto itFlag = g_stringToFlag.find(typeName);
    if (itFlag != g_stringToFlag.end()) {
        if (enabled) {
            const auto iconIt = g_registeredIcons.find(typeName);
            if (iconIt == g_registeredIcons.end()) {
                Game::Lua::Error(
                    L,
                    "No icon registered for this type. Call MinimapBlip_RegisterIcon first.");
                return 0;
            }
            InstallHooks();
            g_trackedUnitFlagsBlips[itFlag->second] = iconIt->second;
        } else {
            g_trackedUnitFlagsBlips.erase(itFlag->second);
            if (!IsAnyTrackingActive())
                UninstallHooks();
        }
        return 0;
    }

    const auto itType = g_stringToGameObjectType.find(typeName);
    if (itType != g_stringToGameObjectType.end()) {
        if (enabled) {
            const auto iconIt = g_registeredIcons.find(typeName);
            if (iconIt == g_registeredIcons.end()) {
                Game::Lua::Error(
                    L,
                    "No icon registered for this type. Call MinimapBlip_RegisterIcon first.");
                return 0;
            }
            InstallHooks();
            g_trackedGameObjectTypesBlips[itType->second] = iconIt->second;
        } else {
            g_trackedGameObjectTypesBlips.erase(itType->second);
            if (!IsAnyTrackingActive())
                UninstallHooks();
        }
        return 0;
    }

    Game::Lua::Error(L, "Unknown tracking type. Supported types: target, Auctioneer, Banker, "
                        "Battlemaster, Brainwashing, Flight Master, Innkeeper, Mailbox, Repair, "
                        "Stable Master, Summoning Ritual Object, Summoning Ritual Unit, Trainer, "
                        "Vendor.");
    return 0;
}

static int __fastcall Script_MinimapBlip_SetFocus(void *L) {
    const uint64_t guid = Game::GetGUIDFromName("target");
    if (guid == 0) {
        Game::Lua::Error(L, "No target to set as focus.");
        return 0;
    }
    g_focusGUID = guid;
    return 0;
}

static int __fastcall Script_MinimapBlip_SetFocusByName(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: MinimapBlip_SetFocusByName(name)");
        return 0;
    }
    const std::string name = Game::Lua::ToString(L, 1);
    const uint64_t guid = Game::GetGUIDFromName(name.c_str());
    if (guid != 0)
        g_focusGUID = guid;
    return 0;
}

static int __fastcall Script_MinimapBlip_ClearFocus(void * /*L*/) {
    g_focusGUID = 0;
    return 0;
}

void RegisterLuaFunctions() {
    Game::FrameScript_RegisterFunction(
        "MinimapBlip_RegisterIcon",
        reinterpret_cast<uintptr_t>(&Script_MinimapBlip_RegisterIcon));
    Game::FrameScript_RegisterFunction(
        "MinimapBlip_RegisterHostileIcon",
        reinterpret_cast<uintptr_t>(&Script_MinimapBlip_RegisterHostileIcon));
    Game::FrameScript_Execute("MINIMAP_BLIP_VERSION = \"1.0.0\"", "VanillaMinimapTracking");
    Game::FrameScript_RegisterFunction("MinimapBlip_Track",
                                       reinterpret_cast<uintptr_t>(&Script_MinimapBlip_Track));
    Game::FrameScript_RegisterFunction(
        "MinimapBlip_SetFocus", reinterpret_cast<uintptr_t>(&Script_MinimapBlip_SetFocus));
    Game::FrameScript_RegisterFunction(
        "MinimapBlip_SetFocusByName",
        reinterpret_cast<uintptr_t>(&Script_MinimapBlip_SetFocusByName));
    Game::FrameScript_RegisterFunction(
        "MinimapBlip_ClearFocus", reinterpret_cast<uintptr_t>(&Script_MinimapBlip_ClearFocus));
}

void Reset() {
    g_registeredIcons.clear();
    g_targetHostileBlip = {nullptr, 1.0F};
    g_trackedUnitFlagsBlips.clear();
    g_trackedGameObjectTypesBlips.clear();
    g_trackedObjectsData.clear();
    g_targetTracking = false;
    g_currentTargetGUID = 0;
    g_focusTracking = false;
    g_focusGUID = 0;
    g_blipHoverState = BlipHoverState();
    UninstallHooks();
}

} // namespace Blips
