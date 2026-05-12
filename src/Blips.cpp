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

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>

namespace Blips {

static Game::RenderObjectBlips_t RenderObjectBlips_o = nullptr;
static Game::ObjectEnumProc_t ObjectEnumProc_o = nullptr;
static Game::ClntObjMgrEnumVisibleObjects_t ClntObjMgrEnumVisibleObjects_o = nullptr;
static void *MinimapRender_PartyListing_o = nullptr;
static void *OnLayerTrackUpdate_ChangedGate_o = nullptr;
static void *OnLayerTrackUpdate_PreShowGate_o = nullptr;
static void *OnLayerTrackUpdate_AppendToTooltipBuffer_o = nullptr;

// Registered-texture record. `gxTex` is resolved eagerly at load time so
// per-frame draws skip the `TextureGetGxTex` + `CStatus` round-trip we'd
// otherwise pay for every blip. `texture` is kept so we can re-resolve if
// `gxTex` ever comes back null (e.g. engine not yet ready at load time).
struct TextureCacheEntry {
    Game::HTEXTURE__ *texture;
    mutable Game::CGxTex *gxTex;
};

struct Blip {
    // Pointer into `g_textureCache` — stable across map inserts (unordered_map
    // pointer-stability guarantee) and never erased.
    const TextureCacheEntry *cacheEntry;
    float scale;
};

struct TrackedObjectData {
    uint64_t guid;
    Game::C2Vector minimapPos;
    bool isInDifferentArea;
    std::string name;
    std::string subName;
    std::string typeName; // matches kBlipTypes[i].typeName (e.g. "vendor", "target")
    Blip blip;
};

struct BlipHoverEntry {
    uint64_t guid;
    std::string name;
    std::string subName;
    bool gray;
};

struct BlipHoverState {
    bool changed = false;
    bool nonEmpty = false;
    uint64_t hash = 0;
    std::vector<BlipHoverEntry> hits;
};

static bool g_hooksInstalled = false;
static std::unordered_map<std::string, TextureCacheEntry> g_textureCache;
static std::unordered_map<std::string, Blip> g_registeredIcons;
static bool g_targetTracking = false;
static uint64_t g_currentTargetGUID = 0;
static bool g_focusTracking = false;
static uint64_t g_focusGUID = 0;
static uint64_t g_playerGUID = 0;
static std::unordered_set<std::string> g_enabledTypes;
static std::string g_configPath;
static bool g_configLoaded = false;
static Blip g_targetHostileBlip = {nullptr, 1.0F};
// Tracked-NPC flag list, sorted by flag value descending. The minimap
// "highest flag wins" rule lets us break on the first match instead of
// scanning every entry. N is tiny (≤ ~10), so linear insert/erase in
// ApplyTrack is cheaper than maintaining a sorted+hash dual structure.
static std::vector<std::pair<uint32_t, Blip>> g_trackedUnitFlagsBlips;
static std::unordered_map<uint32_t, Blip> g_trackedGameObjectTypesBlips;
// OR of every flag in g_trackedUnitFlagsBlips; (unit.m_npcFlags & this) == 0
// is the fast-path bail in CheckObject. Bitmask of (1 << type) for every type
// in g_trackedGameObjectTypesBlips; GO types are 0–30 so fit in 32 bits.
static uint32_t g_combinedNpcFlagMask = 0;
static uint32_t g_combinedGameObjectTypeBits = 0;
static std::vector<TrackedObjectData> g_trackedObjectsData;
static BlipHoverState g_blipHoverState;

// Single source of truth for every tracking type. Drives:
//   1. ApplyTrack (lookup by `typeName`, dispatch on `kind`/`engineValue`)
//   2. The Enum.MinimapBlip Lua table (each row's `enumKey` → `typeName`)
// Adding a new type is one row here.
enum class BlipKind {
    Special,    // "target" / "focus" — own per-type boolean flag
    NpcFlag,    // unit's m_npcFlags & engineValue
    GameObject, // gameobject's m_type == engineValue
};

struct BlipTypeDef {
    const char *enumKey;   // PascalCase, exposed as Enum.MinimapBlip.<key>
    const char *typeName;  // lowercase, the value addons actually pass to C_Minimap.*
    BlipKind kind;
    uint32_t engineValue;  // unused for Special
};

static constexpr BlipTypeDef kBlipTypes[] = {
    {"Target",                "target",                  BlipKind::Special,    0},
    {"Focus",                 "focus",                   BlipKind::Special,    0},
    {"Auctioneer",            "auctioneer",              BlipKind::NpcFlag,    Game::UNIT_NPC_FLAG_AUCTIONEER},
    {"Banker",                "banker",                  BlipKind::NpcFlag,    Game::UNIT_NPC_FLAG_BANKER},
    {"Battlemaster",          "battlemaster",            BlipKind::NpcFlag,    Game::UNIT_NPC_FLAG_BATTLEMASTER},
    {"FlightMaster",          "flight master",           BlipKind::NpcFlag,    Game::UNIT_NPC_FLAG_FLIGHTMASTER},
    {"Innkeeper",             "innkeeper",               BlipKind::NpcFlag,    Game::UNIT_NPC_FLAG_INNKEEPER},
    {"Repair",                "repair",                  BlipKind::NpcFlag,    Game::UNIT_NPC_FLAG_REPAIR},
    {"StableMaster",          "stable master",           BlipKind::NpcFlag,    Game::UNIT_NPC_FLAG_STABLEMASTER},
    {"SummoningRitualUnit",   "summoning ritual unit",   BlipKind::NpcFlag,    Game::UNIT_NPC_FLAG_SUMMONING_RITUAL},
    {"Trainer",               "trainer",                 BlipKind::NpcFlag,    Game::UNIT_NPC_FLAG_TRAINER},
    {"Vendor",                "vendor",                  BlipKind::NpcFlag,    Game::UNIT_NPC_FLAG_VENDOR},
    {"Mailbox",               "mailbox",                 BlipKind::GameObject, Game::GAMEOBJECT_TYPE_MAILBOX},
    {"SummoningRitualObject", "summoning ritual object", BlipKind::GameObject, Game::GAMEOBJECT_TYPE_SUMMONING_RITUAL},
};

static const BlipTypeDef *FindBlipType(const std::string &typeName) {
    for (const auto &d : kBlipTypes) {
        if (typeName == d.typeName)
            return &d;
    }
    return nullptr;
}

static const char *FindBlipTypeNameByEngine(BlipKind kind, uint32_t engineValue) {
    for (const auto &d : kBlipTypes) {
        if (d.kind == kind && d.engineValue == engineValue)
            return d.typeName;
    }
    return nullptr;
}

static void TrackObject(Game::MINIMAPINFO *info, Game::CGObject_C *objectptr, uint64_t guid,
                        Blip blip, const char *typeName) {
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

    std::string subName;
    if (objectptr->m_objectType == Game::OBJECT_TYPE::UNIT) {
        // Walk unit → creature-cache row → subname string. Either hop can be
        // NULL (uncached / unnamed); SafeDeref short-circuits if so.
        const auto *unitBytes = reinterpret_cast<const uint8_t *>(objectptr);
        const auto *cacheRow = Game::SafeDeref(unitBytes, 0xB30);
        const auto *raw = reinterpret_cast<const char *>(Game::SafeDeref(cacheRow, 0x10));
        if (raw != nullptr && raw[0] != '\0')
            subName = raw;
    }

    g_trackedObjectsData.push_back(
        {guid, minimapPos, wmoID != info->wmoID, objectptr->vftable->GetName(objectptr), subName,
         typeName ? typeName : "", blip});
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

static bool CheckObject(Game::MINIMAPINFO *info, uint64_t guid) {
    if (g_targetTracking && g_currentTargetGUID != 0 && guid == g_currentTargetGUID &&
        guid != g_playerGUID) {
        const auto iconIt = g_registeredIcons.find("target");
        if (iconIt != g_registeredIcons.end()) {
            auto *unitptr = reinterpret_cast<Game::CGUnit_C *>(
                Game::ClntObjMgrObjectPtr(Game::TYPE_MASK::TYPEMASK_UNIT, nullptr, guid, 0));
            if (unitptr != nullptr) {
                Blip blip = iconIt->second;
                if (g_targetHostileBlip.cacheEntry != nullptr && IsTargetHostile(unitptr)) {
                    blip = g_targetHostileBlip;
                }
                TrackObject(info, reinterpret_cast<Game::CGObject_C *>(unitptr), guid, blip,
                            "target");
                return true;
            }
        }
        return false;
    }

    if (g_focusTracking && g_focusGUID != 0 && guid == g_focusGUID && guid != g_playerGUID) {
        const auto iconIt = g_registeredIcons.find("focus");
        if (iconIt != g_registeredIcons.end()) {
            auto *unitptr = reinterpret_cast<Game::CGUnit_C *>(
                Game::ClntObjMgrObjectPtr(Game::TYPE_MASK::TYPEMASK_UNIT, nullptr, guid, 0));
            if (unitptr != nullptr) {
                TrackObject(info, reinterpret_cast<Game::CGObject_C *>(unitptr), guid,
                            iconIt->second, "focus");
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
        // Fast bail before scanning the per-flag list. Skips the vast
        // majority of city NPCs (anyone not flagged for something tracked).
        if ((unitData->m_npcFlags & g_combinedNpcFlagMask) == 0)
            return false;

        // Vector is sorted by flag-value descending — first match wins,
        // matching the engine's "stable master (0x2000) beats vendor (0x4)" rule.
        for (const auto &[flag, tracked] : g_trackedUnitFlagsBlips) {
            if (unitData->m_npcFlags & flag) {
                TrackObject(info, objptr, guid, tracked,
                            FindBlipTypeNameByEngine(BlipKind::NpcFlag, flag));
                return true;
            }
        }
    } else if (objptr->m_objectType == Game::OBJECT_TYPE::GAMEOBJECT) {
        const auto *gameObjectData = reinterpret_cast<Game::CGGameObjectData *>(objptr->m_data);
        // 1.12 GO types go up to AURA_GENERATOR (30); the > 31 guard keeps
        // the shift defined in case a future patch widens the enum.
        if (gameObjectData->m_type > 31 ||
            (g_combinedGameObjectTypeBits & (1u << gameObjectData->m_type)) == 0)
            return false;

        const auto it = g_trackedGameObjectTypesBlips.find(gameObjectData->m_type);
        if (it != g_trackedGameObjectTypesBlips.end()) {
            TrackObject(info, objptr, guid, it->second,
                        FindBlipTypeNameByEngine(BlipKind::GameObject, gameObjectData->m_type));
            return true;
        }
    }
    return false;
}

static void DrawTrackedBlips(Game::CGMinimapFrame *minimapPtr, Game::DNInfo *dnInfo) {
    // We are gathering the position in ObjectEnumProc because it is rate limited to avoid spamming
    // expensive calls. To do it in RenderObjectBlips, we can use DNInfo for current position,
    // MinimapGetWorldRadius() for world radius and minimapPtr +0x7C for layout scale.
    for (const auto &objData : g_trackedObjectsData) {
        if (objData.blip.cacheEntry == nullptr)
            continue;
        Game::DrawMinimapTexture(objData.blip.cacheEntry->gxTex, objData.minimapPos,
                                 objData.blip.scale, objData.isInDifferentArea);
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
            bool dup = false;
            for (const auto &existing : now) {
                if (existing.name == objData.name && existing.subName == objData.subName) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                now.push_back(
                    {objData.guid, objData.name, objData.subName, objData.isInDifferentArea});
            }
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
        mix(std::hash<std::string_view>{}(hit.subName));
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
        if (hit.gray)
            Game::SStrPack(tooltipText, "|cffb0b0b0", 0x400);
        Game::SStrPack(tooltipText, hit.name.c_str(), 0x400);
        if (!hit.subName.empty()) {
            Game::SStrPack(tooltipText, "\n<", 0x400);
            Game::SStrPack(tooltipText, hit.subName.c_str(), 0x400);
            Game::SStrPack(tooltipText, ">", 0x400);
        }
        if (hit.gray)
            Game::SStrPack(tooltipText, "|r", 0x400);
        Game::SStrPack(tooltipText, "\n", 0x400);
    }
}

static int __fastcall
ClntObjMgrEnumVisibleObjects_h(Game::ClntObjMgrEnumVisibleObjectsCallback_t callback,
                               void *context) {
    if (reinterpret_cast<uintptr_t>(callback) == Offsets::FUN_OBJECT_ENUM_PROC) {
        g_trackedObjectsData.clear();
        g_playerGUID = Game::GetGUIDFromName("player");
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

    // The render hook is about to go away; drop the last enum pass's blip
    // list so a future re-install can't draw from stale data before the
    // first refill of `g_trackedObjectsData` lands. Same for hover hits —
    // they cache whichever blips the cursor was last over.
    g_trackedObjectsData.clear();
    g_blipHoverState = BlipHoverState();

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

// Resolves and caches the engine-side `CGxTex *` for an entry. Called once
// at load and again only if the eager resolve returned null (engine wasn't
// ready). Returning the same null is fine — the per-frame draw path bails
// silently when `gxTex` is null.
static void ResolveGxTex(const TextureCacheEntry &entry) {
    if (entry.gxTex != nullptr || entry.texture == nullptr)
        return;
    Game::CStatus status;
    Game::CGxTex *gxTex = Game::TextureGetGxTex(entry.texture, 1, &status);
    if (status.ok())
        entry.gxTex = gxTex;
}

static const TextureCacheEntry *LoadTextureCached(const std::string &texturePathLower) {
    if (const auto it = g_textureCache.find(texturePathLower); it != g_textureCache.end()) {
        // Retry the GxTex resolve in case the first attempt happened before
        // the engine was ready. Cheap when already resolved (early return).
        ResolveGxTex(it->second);
        return &it->second;
    }

    Game::CGxTexFlags texFlags(Game::EGxTexFilter::GxTex_Nearest, 0, 0, 0, 0, 0, 0, 1);
    Game::CStatus status;
    Game::HTEXTURE__ *texture =
        Game::TextureCreate(texturePathLower.c_str(), &status, texFlags, 0, 1);
    if (!status.ok()) {
        return nullptr;
    }
    auto [it, inserted] = g_textureCache.emplace(texturePathLower, TextureCacheEntry{texture, nullptr});
    ResolveGxTex(it->second);
    return &it->second;
}

static bool EnsureConfigLoaded(); // defined below; forward-declared so the
                                  // public Script_* entry points can call it
static void SyncRuntimeFromIntent();

static int __fastcall Script_MinimapBlip_RegisterIcon(void *L) {
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: C_Minimap.RegisterIcon(trackingType, icon [, scale])");
        return 0;
    }

    const std::string typeName = Game::Lua::ToString(L, 1);

    std::string texturePath = Game::Lua::ToString(L, 2);
    std::transform(texturePath.begin(), texturePath.end(), texturePath.begin(), ::tolower);

    float scale = 1.0F;
    if (Game::Lua::IsNumber(L, 3)) {
        scale = static_cast<float>(Game::Lua::ToNumber(L, 3));
    }

    const TextureCacheEntry *entry = LoadTextureCached(texturePath);
    if (entry == nullptr) {
        Game::Lua::Error(L, "Couldn't load texture.");
        return 0;
    }

    g_registeredIcons[typeName] = {entry, scale};

    // Pull saved intent from disk (if not already), then bring this type's
    // runtime state up if the addon previously had it tracked. Order matters:
    // the icon must be in g_registeredIcons before SyncRuntimeFromIntent so
    // ApplyTrack can find it.
    EnsureConfigLoaded();
    SyncRuntimeFromIntent();
    return 0;
}

// Bulk-register icons from a Lua array of `{ type, icon, scale, hostileIcon? }`
// entries. Equivalent to a loop of C_Minimap.RegisterIcon (and
// C_Minimap.RegisterHostileIcon when `hostileIcon` is present), but with
// only one Lua→C transition for the whole batch.
static int __fastcall Script_MinimapBlip_RegisterIcons(void *L) {
    if (!Game::Lua::IsTable(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Minimap.RegisterIcons({ {type, icon, scale, "
                            "[hostileIcon]}, ... })");
        return 0;
    }

    // Helpers that read a field off the entry-table at the given relative index.
    // Pushing the key shifts negative indices by one, so we adjust before
    // calling lua_gettable. The string variant copies into `out` BEFORE the
    // pop so we don't hand back a pointer into Lua-managed memory that GC
    // could free.
    auto adjust = [](int idx) { return idx < 0 ? idx - 1 : idx; };
    auto readField = [&](int tableStackIdx, const char *field, std::string &out) -> bool {
        Game::Lua::PushString(L, field);
        Game::Lua::GetTable(L, adjust(tableStackIdx));
        bool ok = false;
        if (Game::Lua::IsString(L, -1)) {
            if (const char *s = Game::Lua::ToString(L, -1)) {
                out = s;
                ok = true;
            }
        }
        Game::Lua::SetTop(L, -2); // pop looked-up value
        return ok;
    };
    auto readScale = [&](int tableStackIdx) -> float {
        Game::Lua::PushString(L, "scale");
        Game::Lua::GetTable(L, adjust(tableStackIdx));
        const float v = Game::Lua::IsNumber(L, -1)
                            ? static_cast<float>(Game::Lua::ToNumber(L, -1))
                            : 1.0F;
        Game::Lua::SetTop(L, -2);
        return v;
    };

    Game::Lua::PushNil(L);
    while (Game::Lua::Next(L, 1) != 0) {
        // Stack: [array, key, entry]
        if (Game::Lua::IsTable(L, -1)) {
            std::string typeName, iconPath, hostileIcon;
            const bool hasType = readField(-1, "type", typeName);
            const bool hasIcon = readField(-1, "icon", iconPath);
            const float scale = readScale(-1);
            const bool hasHostile = readField(-1, "hostileIcon", hostileIcon);

            if (hasType && hasIcon) {
                std::transform(iconPath.begin(), iconPath.end(), iconPath.begin(),
                               ::tolower);
                if (const TextureCacheEntry *entry = LoadTextureCached(iconPath)) {
                    g_registeredIcons[typeName] = {entry, scale};
                }
            }

            if (hasHostile) {
                std::transform(hostileIcon.begin(), hostileIcon.end(), hostileIcon.begin(),
                               ::tolower);
                if (const TextureCacheEntry *entry = LoadTextureCached(hostileIcon)) {
                    g_targetHostileBlip = {entry, scale};
                }
            }
        }
        Game::Lua::SetTop(L, -2); // pop value, keep key for next iteration
    }

    // Now that every icon in this batch is in g_registeredIcons, load the
    // saved intent (if not yet) and bring up runtime state for any tracked
    // types whose icons were just supplied.
    EnsureConfigLoaded();
    SyncRuntimeFromIntent();
    return 0;
}

static int __fastcall Script_MinimapBlip_RegisterHostileIcon(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Minimap.RegisterHostileIcon(icon [, scale])");
        return 0;
    }

    std::string texturePath = Game::Lua::ToString(L, 1);
    std::transform(texturePath.begin(), texturePath.end(), texturePath.begin(), ::tolower);

    float scale = 1.0F;
    if (Game::Lua::IsNumber(L, 2)) {
        scale = static_cast<float>(Game::Lua::ToNumber(L, 2));
    }

    const TextureCacheEntry *entry = LoadTextureCached(texturePath);
    if (entry == nullptr) {
        Game::Lua::Error(L, "Couldn't load texture.");
        return 0;
    }

    g_targetHostileBlip = {entry, scale};
    return 0;
}

enum class ApplyResult { Applied, NoChange, UnknownType, IconMissing };

// Toggles tracking for a single type. `typeName` must be lowercase — this is
// the canonical key form used throughout (`g_stringToFlag`, the
// `"target"`/`"focus"` literal compares below, and `g_enabledTypes`).
static ApplyResult ApplyTrack(const std::string &typeName, bool enabled) {
    auto recordEnabled = [&](bool on) {
        if (on)
            g_enabledTypes.insert(typeName);
        else
            g_enabledTypes.erase(typeName);
    };

    const BlipTypeDef *def = FindBlipType(typeName);
    if (def == nullptr)
        return ApplyResult::UnknownType;

    auto needIcon = [&]() -> const Blip * {
        const auto it = g_registeredIcons.find(typeName);
        return (it == g_registeredIcons.end()) ? nullptr : &it->second;
    };

    if (def->kind == BlipKind::Special) {
        bool *flag = (typeName == "target") ? &g_targetTracking : &g_focusTracking;
        if (enabled == *flag)
            return ApplyResult::NoChange;
        if (enabled && needIcon() == nullptr)
            return ApplyResult::IconMissing;
        *flag = enabled;
        recordEnabled(enabled);
        return ApplyResult::Applied;
    }

    if (def->kind == BlipKind::NpcFlag) {
        const auto it = std::find_if(
            g_trackedUnitFlagsBlips.begin(), g_trackedUnitFlagsBlips.end(),
            [&](const auto &p) { return p.first == def->engineValue; });
        const bool currently = it != g_trackedUnitFlagsBlips.end();
        if (enabled == currently)
            return ApplyResult::NoChange;
        if (enabled) {
            const Blip *icon = needIcon();
            if (icon == nullptr)
                return ApplyResult::IconMissing;
            // Keep descending order so the CheckObject loop breaks on the
            // highest-priority flag (the "stable master beats vendor" rule).
            const auto pos = std::lower_bound(
                g_trackedUnitFlagsBlips.begin(), g_trackedUnitFlagsBlips.end(), def->engineValue,
                [](const auto &p, uint32_t f) { return p.first > f; });
            g_trackedUnitFlagsBlips.insert(pos, {def->engineValue, *icon});
            g_combinedNpcFlagMask |= def->engineValue;
        } else {
            g_trackedUnitFlagsBlips.erase(it);
            g_combinedNpcFlagMask &= ~def->engineValue;
        }
    } else {
        // BlipKind::GameObject
        const bool currently =
            g_trackedGameObjectTypesBlips.find(def->engineValue) !=
            g_trackedGameObjectTypesBlips.end();
        if (enabled == currently)
            return ApplyResult::NoChange;
        if (enabled) {
            const Blip *icon = needIcon();
            if (icon == nullptr)
                return ApplyResult::IconMissing;
            g_trackedGameObjectTypesBlips[def->engineValue] = *icon;
            g_combinedGameObjectTypeBits |= (1u << def->engineValue);
        } else {
            g_trackedGameObjectTypesBlips.erase(def->engineValue);
            g_combinedGameObjectTypeBits &= ~(1u << def->engineValue);
        }
    }
    recordEnabled(enabled);
    return ApplyResult::Applied;
}

static void EnsureParentDir(const std::string &filePath) {
    const size_t lastSlash = filePath.find_last_of("/\\");
    if (lastSlash == std::string::npos)
        return;
    const std::string dir = filePath.substr(0, lastSlash);
    size_t pos = 0;
    while (true) {
        pos = dir.find_first_of("/\\", pos + 1);
        const std::string sub = (pos == std::string::npos) ? dir : dir.substr(0, pos);
        if (!sub.empty())
            CreateDirectoryA(sub.c_str(), nullptr);
        if (pos == std::string::npos)
            break;
    }
}

// Flushes the in-memory `g_enabledTypes` set to disk. Mirrors how WoW writes
// AddOns.txt — only on UI shutdown (clean logout / `/reload`), not on every
// toggle. Called from `CGGameUI_Shutdown_h` before `Reset()`.
void Save() {
    if (g_configPath.empty())
        return;
    EnsureParentDir(g_configPath);
    std::ofstream file(g_configPath, std::ios::trunc);
    if (!file.is_open())
        return;
    file << "# VanillaMinimapTracking — enabled tracking categories (one per line)\n";
    for (const auto &name : g_enabledTypes)
        file << name << "\n";
}

static const char *const kTrackingChangedEvent = "MINIMAP_UPDATE_TRACKING";
static const Event::Custom::AutoReserve _reserveTrackingChanged{kTrackingChangedEvent};

// Notifies listeners that the tracked-set changed. `arg1` is the type
// name that changed (so a row keyed on its own type can fast-path past
// other rows' updates), or `""` for `ClearAllTracking` — a sentinel for
// "everything changed, re-query whatever you care about." Listeners
// still call back into `C_Minimap.IsTracked` for the on/off state;
// `arg1` exists for dispatch, not as the source of truth.
static void FireTrackingChanged(const char *typeName) {
    const int eventID = Event::Custom::Lookup(kTrackingChangedEvent);
    Event::Custom::Fire_S(eventID, typeName != nullptr ? typeName : "");
}
static const char *ReadActiveAccountName();
static const char *ReadActiveRealmName();
static const char *ReadActiveCharacterName();

static int __fastcall Script_MinimapBlip_Track(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Minimap.Track(trackingType [, enabled])");
        return 0;
    }

    const std::string typeName = Game::Lua::ToString(L, 1);

    bool enabled = true;
    if (Game::Lua::IsNumber(L, 2))
        enabled = (Game::Lua::ToNumber(L, 2) != 0.0);

    EnsureConfigLoaded();

    const ApplyResult r = ApplyTrack(typeName, enabled);
    if (r == ApplyResult::UnknownType) {
        Game::Lua::Error(
            L, "Unknown tracking type. Supported types: target, focus, auctioneer, banker, "
               "battlemaster, flight master, innkeeper, item restore, mailbox, outdoor pvp, "
               "repair, stable master, trainer, transmog, vendor.");
        return 0;
    }
    if (r == ApplyResult::IconMissing) {
        Game::Lua::Error(L, "No icon registered for this type. Call "
                            "C_Minimap.RegisterIcon first.");
        return 0;
    }
    if (r == ApplyResult::Applied) {
        FireTrackingChanged(typeName.c_str());
    }
    return 0;
}

// Flips the tracked state of `type` without the caller having to know its
// current value — UI handlers just call `C_Minimap.Toggle(t)`.
static int __fastcall Script_MinimapBlip_Toggle(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Minimap.Toggle(trackingType)");
        return 0;
    }
    const std::string typeName = Game::Lua::ToString(L, 1);
    EnsureConfigLoaded();

    const bool nextState = (g_enabledTypes.count(typeName) == 0);
    const ApplyResult r = ApplyTrack(typeName, nextState);
    if (r == ApplyResult::UnknownType) {
        Game::Lua::Error(L, "Unknown tracking type.");
        return 0;
    }
    if (r == ApplyResult::IconMissing) {
        Game::Lua::Error(L, "No icon registered for this type. Call "
                            "C_Minimap.RegisterIcon first.");
        return 0;
    }
    if (r == ApplyResult::Applied) {
        FireTrackingChanged(typeName.c_str());
    }
    return 0;
}

// Drops every currently-tracked category in one shot. Fires the event a
// single time at the end with `arg1=""` (the "everything changed"
// sentinel) so the menu can do one refresh instead of N.
static int __fastcall Script_MinimapBlip_ClearAllTracking(void * /*L*/) {
    EnsureConfigLoaded();
    // ApplyTrack mutates g_enabledTypes, so iterate a snapshot.
    std::vector<std::string> snapshot(g_enabledTypes.begin(), g_enabledTypes.end());
    bool anyChanged = false;
    for (const auto &name : snapshot) {
        if (ApplyTrack(name, false) == ApplyResult::Applied)
            anyChanged = true;
    }
    if (anyChanged)
        FireTrackingChanged("");
    return 0;
}

static int __fastcall Script_MinimapBlip_IsTracked(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Minimap.IsTracked(trackingType)");
        return 0;
    }
    const std::string typeName = Game::Lua::ToString(L, 1);
    EnsureConfigLoaded();
    if (g_enabledTypes.count(typeName) == 0)
        return 0; // nil in Lua → falsy
    Game::Lua::PushNumber(L, 1.0);
    return 1;
}

// Returns a Lua array of GUID hex strings for every tracked object currently
// rendered on the minimap. Optional first arg filters by type (e.g.
// `C_Minimap.ListVisibleGUIDs("vendor")`).
static int __fastcall Script_MinimapBlip_ListVisibleGUIDs(void *L) {
    std::string filter;
    bool hasFilter = false;
    if (Game::Lua::IsString(L, 1)) {
        filter = Game::Lua::ToString(L, 1);
        hasFilter = true;
    }

    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    int idx = 1;
    char buf[24];
    for (const auto &obj : g_trackedObjectsData) {
        if (hasFilter && obj.typeName != filter)
            continue;
        snprintf(buf, sizeof(buf), "0x%016llX", static_cast<unsigned long long>(obj.guid));
        Game::Lua::PushNumber(L, static_cast<double>(idx));
        Game::Lua::PushString(L, buf);
        Game::Lua::SetTable(L, -3);
        idx++;
    }
    return 1;
}

// Returns a Lua set keyed by lowercase type name (`{ target = 1, vendor = 1 }`).
// Lets callers do a direct `tracked[name]` lookup without building their own
// set from a list.
static int __fastcall Script_MinimapBlip_GetTracked(void *L) {
    EnsureConfigLoaded();
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    for (const auto &name : g_enabledTypes) {
        Game::Lua::PushString(L, name.c_str());
        Game::Lua::PushNumber(L, 1.0);
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

static const char *ReadActiveAccountName() {
    return *reinterpret_cast<const char *const *>(Offsets::VAR_ACCOUNT_NAME_PTR);
}

static const char *ReadActiveCharacterName() {
    auto *p = reinterpret_cast<const char *>(Offsets::VAR_CHARACTER_NAME);
    return (p[0] == '\0') ? nullptr : p;
}

static const char *ReadActiveRealmName() {
    auto *info = *reinterpret_cast<uint8_t **>(Offsets::VAR_REALM_INFO_PTR);
    if (info == nullptr)
        return nullptr;
    return *reinterpret_cast<const char *const *>(info + Offsets::OFF_REALM_INFO_NAME);
}

// Reads the config file into `g_enabledTypes` (intent only). Doesn't touch
// runtime state — that's `SyncRuntimeFromIntent`'s job once icons are
// available. Splitting these two concerns means a config entry for a type
// whose icon hasn't been registered yet is preserved instead of dropped.
static void LoadConfigFromFile() {
    if (g_configPath.empty())
        return;
    std::ifstream file(g_configPath);
    if (!file.is_open())
        return;

    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() &&
               (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        std::transform(line.begin(), line.end(), line.begin(), ::tolower);
        if (FindBlipType(line) != nullptr)
            g_enabledTypes.insert(line);
    }
}

// Activates runtime tracking for every type in `g_enabledTypes` whose icon
// is registered. ApplyTrack returns NoChange for types already up and
// IconMissing (silently) for types still waiting on a texture, so this is
// safe to call repeatedly — e.g. after every RegisterIcon[s] batch.
static void SyncRuntimeFromIntent() {
    // ApplyTrack mutates g_enabledTypes, so iterate a snapshot.
    std::vector<std::string> snapshot(g_enabledTypes.begin(), g_enabledTypes.end());
    for (const auto &name : snapshot) {
        ApplyTrack(name, true);
    }
}

// Lazy: resolves account/realm/character via the engine's session globals on
// first call after the player's in world, builds the per-character path, and
// reads whatever was saved last session. No-op if the file doesn't exist
// (first run on this character).
static bool EnsureConfigLoaded() {
    if (g_configLoaded)
        return true;

    const char *account = ReadActiveAccountName();
    if (account == nullptr || account[0] == '\0')
        return false;

    const char *realm = ReadActiveRealmName();
    if (realm == nullptr || realm[0] == '\0')
        return false;

    const char *player = ReadActiveCharacterName();
    if (player == nullptr)
        return false;

    g_configPath = std::string("WTF\\Account\\") + account + "\\" + realm + "\\" + player +
                   "\\VanillaMinimapTracking.txt";
    LoadConfigFromFile();
    g_configLoaded = true;
    return true;
}

// Resolves `unit` (a unit ID like "target"/"party1", or a unit name) and
// pins it as the focus. With no argument, defaults to "target".
static int __fastcall Script_MinimapBlip_SetFocus(void *L) {
    const char *unit = "target";
    if (Game::Lua::IsString(L, 1))
        unit = Game::Lua::ToString(L, 1);
    const uint64_t guid = Game::GetGUIDFromName(unit);
    if (guid == 0) {
        Game::Lua::Error(L, "No unit to set as focus.");
        return 0;
    }
    g_focusGUID = guid;
    return 0;
}

// Walks visible objects to find a unit or player matching `target` by name
// (case-insensitive). Sets `foundGUID` and stops iteration on first hit.
// Calling convention mirrors `ObjectEnumProc_h`: __fastcall puts the context
// in ECX, and the engine pushes guid_low/guid_high on the stack — uint64_t
// as the second __fastcall arg gets handled stack-side by MSVC, matching.
struct NameLookupContext {
    const char *target;
    uint64_t foundGUID;
};

static int __fastcall NameLookupCallback(NameLookupContext *ctx, uint64_t guid) {
    Game::CGObject_C *obj = Game::ClntObjMgrObjectPtr(
        Game::TYPE_MASK::TYPEMASK_UNIT | Game::TYPE_MASK::TYPEMASK_PLAYER, nullptr, guid, 0);
    if (obj == nullptr)
        return 1;

    const char *name = obj->vftable->GetName(obj);
    if (name != nullptr && _stricmp(name, ctx->target) == 0) {
        ctx->foundGUID = guid;
        return 0; // stop iteration
    }
    return 1;
}

static int __fastcall Script_MinimapBlip_SetFocusByName(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Minimap.SetFocusByName(name)");
        return 0;
    }
    const char *name = Game::Lua::ToString(L, 1);
    if (name == nullptr || name[0] == '\0') {
        Game::Lua::Error(L, "Usage: C_Minimap.SetFocusByName(name)");
        return 0;
    }

    NameLookupContext ctx = {name, 0};
    Game::ClntObjMgrEnumVisibleObjects(
        reinterpret_cast<Game::ClntObjMgrEnumVisibleObjectsCallback_t>(NameLookupCallback), &ctx);

    if (ctx.foundGUID == 0) {
        Game::Lua::Error(L, "No visible unit by that name.");
        return 0;
    }
    g_focusGUID = ctx.foundGUID;
    return 0;
}

static int __fastcall Script_MinimapBlip_ClearFocus(void * /*L*/) {
    g_focusGUID = 0;
    return 0;
}

static void RegisterLuaFunctions() {
    // Install our hooks once at module-register time and leave them up for
    // the DLL's lifetime. MinHook's enable/disable suspends every thread in
    // the process while it rewrites prologue bytes — with 7 hooks each,
    // toggling them across 0↔1-tracked transitions caused a visible
    // stutter. The hooks' fast paths are cheap (empty draws, early-returns
    // when nothing is tracked), so always-on is essentially free per frame.
    // `Reset()` still uninstalls on `CGGameUI_Shutdown` so /reload comes up
    // clean.
    InstallHooks();

    constexpr const char *NS = "C_Minimap";
    Game::Lua::RegisterTableFunction(NS, "RegisterIcon", &Script_MinimapBlip_RegisterIcon);
    Game::Lua::RegisterTableFunction(NS, "RegisterIcons", &Script_MinimapBlip_RegisterIcons);
    Game::Lua::RegisterTableFunction(NS, "RegisterHostileIcon",
                                     &Script_MinimapBlip_RegisterHostileIcon);
    Game::Lua::RegisterTableFunction(NS, "Track", &Script_MinimapBlip_Track);
    Game::Lua::RegisterTableFunction(NS, "Toggle", &Script_MinimapBlip_Toggle);
    Game::Lua::RegisterTableFunction(NS, "ClearAllTracking",
                                     &Script_MinimapBlip_ClearAllTracking);
    Game::Lua::RegisterTableFunction(NS, "IsTracked", &Script_MinimapBlip_IsTracked);
    Game::Lua::RegisterTableFunction(NS, "GetTracked", &Script_MinimapBlip_GetTracked);
    Game::Lua::RegisterTableFunction(NS, "ListVisibleGUIDs",
                                     &Script_MinimapBlip_ListVisibleGUIDs);
    Game::Lua::RegisterTableFunction(NS, "SetFocus", &Script_MinimapBlip_SetFocus);
    Game::Lua::RegisterTableFunction(NS, "SetFocusByName", &Script_MinimapBlip_SetFocusByName);
    Game::Lua::RegisterTableFunction(NS, "ClearFocus", &Script_MinimapBlip_ClearFocus);

    constexpr int kBlipTypeCount = static_cast<int>(sizeof(kBlipTypes) / sizeof(kBlipTypes[0]));
    Game::Lua::EnumEntry enumEntries[kBlipTypeCount];
    for (int i = 0; i < kBlipTypeCount; i++)
        enumEntries[i] = {kBlipTypes[i].enumKey, kBlipTypes[i].typeName};
    Game::Lua::RegisterStringEnum("Enum", "MinimapBlip", enumEntries, kBlipTypeCount);
    // Event name is reserved at file scope via `AutoReserve`; the
    // `RebuildEventTable` hook injects it into the engine's input array.
}

// Self-register with the module list so `Game::RunModuleRegistrations()`
// (invoked from the `LoadScriptFunctions` hook in DllMain) picks us up
// without DllMain having to know about Blips specifically.
static const Game::ModuleAutoRegister kBlipsAutoRegister{&RegisterLuaFunctions};

void Reset() {
    g_registeredIcons.clear();
    g_targetHostileBlip = {nullptr, 1.0F};
    g_trackedUnitFlagsBlips.clear();
    g_trackedGameObjectTypesBlips.clear();
    g_combinedNpcFlagMask = 0;
    g_combinedGameObjectTypeBits = 0;
    g_trackedObjectsData.clear();
    g_targetTracking = false;
    g_currentTargetGUID = 0;
    g_focusTracking = false;
    g_focusGUID = 0;
    g_playerGUID = 0;
    g_enabledTypes.clear();
    g_configPath.clear();
    g_configLoaded = false;
    g_blipHoverState = BlipHoverState();
    UninstallHooks();
}

} // namespace Blips
