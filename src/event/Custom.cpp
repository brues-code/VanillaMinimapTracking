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

#include "Custom.h"

#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace Event::Custom {

namespace {

// Cache keyed by eventName pointer (callers always pass a static literal,
// so pointer equality is a valid identity check). A failed claim caches
// `slot = -1`; subsequent `Register`/`RetryAll` retries until it succeeds.
struct CacheEntry {
    const char *name;
    int slot;
};
constexpr int MAX_CACHE = 8;
CacheEntry g_cache[MAX_CACHE];
int g_cacheCount = 0;
bool g_writesEnabled = false;

// Storm-allocate a copy of `s`. The engine's event table treats every
// `entry.name` as a Storm-owned pointer — its reload teardown loop calls
// `SMemFree(entry.name)` and validates the block came from `SMemAlloc`.
// Static literals in the DLL would crash that free, so we must hand it a
// real Storm allocation.
char *AllocStormCopy(const char *s) {
    if (s == nullptr)
        return nullptr;
    using SMemAlloc_t = void *(__stdcall *)(size_t size, const char *file,
                                            int line, int flags);
    auto fn = reinterpret_cast<SMemAlloc_t>(Offsets::FUN_STORM_SMEM_ALLOC);
    const size_t len = std::strlen(s);
    char *buf = static_cast<char *>(fn(len + 1, __FILE__, __LINE__, 0));
    if (buf == nullptr)
        return nullptr;
    std::memcpy(buf, s, len + 1);
    return buf;
}

int TryClaim(const char *eventName) {
    if (!g_writesEnabled)
        return -1;
    auto *base = *reinterpret_cast<uint8_t **>(Offsets::VAR_EVENT_TABLE_BASE_PTR);
    const int count = *reinterpret_cast<int *>(Offsets::VAR_EVENT_TABLE_COUNT);
    if (base == nullptr || count <= 0)
        return -1;
    for (int i = 0; i < count; i++) {
        auto **namePtr = reinterpret_cast<const char **>(
            base + i * Offsets::EVENT_ENTRY_STRIDE + Offsets::OFF_EVENT_ENTRY_NAME);
        if (*namePtr == nullptr) {
            char *stormName = AllocStormCopy(eventName);
            if (stormName == nullptr)
                return -1;
            *namePtr = stormName;
            return i;
        }
    }
    return -1;
}

} // namespace

int Register(const char *eventName) {
    if (eventName == nullptr)
        return -1;
    for (int i = 0; i < g_cacheCount; i++) {
        if (g_cache[i].name == eventName) {
            if (g_cache[i].slot < 0)
                g_cache[i].slot = TryClaim(eventName);
            return g_cache[i].slot;
        }
    }
    if (g_cacheCount >= MAX_CACHE)
        return -1;
    g_cache[g_cacheCount].name = eventName;
    g_cache[g_cacheCount].slot = TryClaim(eventName);
    return g_cache[g_cacheCount++].slot;
}

void RetryAll() {
    for (int i = 0; i < g_cacheCount; i++) {
        if (g_cache[i].slot < 0)
            g_cache[i].slot = TryClaim(g_cache[i].name);
    }
}

void EnableWrites() { g_writesEnabled = true; }

void PrepareForReload() {
    for (int i = 0; i < g_cacheCount; i++)
        g_cache[i].slot = -1;
    g_writesEnabled = false;
}

void Fire_SD(int eventID, const char *arg1, int arg2) {
    if (eventID < 0)
        return;
    using FireEvent_SD_t = void(__cdecl *)(int eventID, const char *format,
                                           const char *a, int b);
    auto fn = reinterpret_cast<FireEvent_SD_t>(Offsets::FUN_FIRE_EVENT);
    fn(eventID, "%s%d", arg1, arg2);
}

} // namespace Event::Custom
