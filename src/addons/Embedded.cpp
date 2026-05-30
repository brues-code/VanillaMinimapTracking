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

// Embedded `MinimapBlips` addon fallback. When the user doesn't have the
// addon installed on disk, this module makes the engine think it does —
// without writing anything to the filesystem.
//
// How:
//
//   1. The CMake build embeds every file under `MinimapBlips/` (`.lua`,
//      `.toc`, `icons/*.blp`) into a generated header
//      (`embedded_minimapblips.h`) as a byte array per file plus a
//      `{path, data, size}` manifest.
//
//   2. We hook two file-pipeline entry points to serve embedded paths:
//      - `FUN_FILE_READ` — the engine's high-level open-read-close
//        wrapper used for text resources (`.lua`, `.toc`). We allocate
//        a Storm buffer, copy the embedded content in, and hand it back
//        in one call. The caller's normal `SMemFree` reclaims it.
//      - `FUN_FILE_OPEN` + `FUN_FILE_READ_HANDLE` — the low-level open
//        and read-from-handle pair used by the BLP texture pipeline
//        (`TextureCreate` → BLP parser → these two). Our open hook
//        synthesizes a handle whose `*handle` slot holds a magic type
//        code; our read hook recognizes the magic and serves bytes
//        sequentially from the embedded buffer. The engine's close
//        function tears the handle down for free as long as we
//        zero-init the engine's "free-if-non-NULL" pointer slots and
//        initialize the critical section at +0x24.
//
//   3. We post-hook the engine's `FUN_ADDON_INIT`. After the engine's own
//      `Interface\AddOns\` directory scan finishes, we call the TOC
//      parser directly with `"MinimapBlips"`. The parser opens its TOC
//      via our hooked read function and registers the addon as a normal
//      entry. Dedup-safe: if the user already has it on disk, the
//      engine's scan picked it up and our injected call is a hash-table
//      no-op.
//
// Pattern reverse-engineered from ClassicAPI's Embedded.cpp (which in
// turn lifted it from WeirdUtils.dll at `0x10015A74`). The disk-vs-
// embedded version comparison lets a customized on-disk MinimapBlips
// override the embedded fallback when newer.

#include "Embedded.h"

#include "Common.h"
#include "Game.h"
#include "MinHook.h"
#include "Offsets.h"
#include "embedded_minimapblips.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Addons::Embedded {

namespace {

constexpr const char *kAddonName = "MinimapBlips";
constexpr const char *kAddonTocFile = "MinimapBlips.toc";

// `Interface\AddOns\MinimapBlips\` — the prefix the engine constructs for
// any of our addon's files. Comparing case-insensitively because Windows
// file paths are case-insensitive and the engine doesn't normalize.
constexpr const char *kAddonPathPrefix = "Interface\\AddOns\\MinimapBlips\\";

// Magic value placed at handle+0x00 so our FileReadHandle_h hook can
// recognize handles we synthesized vs. engine-allocated ones. Must NOT
// collide with the engine's source-type codes (0..4 from FUN_006477c0).
// `'MBlp'` little-endian = 0x706c424d.
constexpr uint32_t kEmbeddedTypeCode = 0x706c424dU;

// Engine handle struct layout (from FUN_006472d0 close + FUN_00648460 read):
//   +0x00  source type code (0..4 engine-defined; we use kEmbeddedTypeCode)
//   +0x0c  pointer field — engine close frees if non-zero (we keep 0)
//   +0x10  pointer field — engine close frees if non-zero (we keep 0)
//   +0x18  pointer field — engine close frees if non-zero (we keep 0)
//   +0x1c  pointer field — engine close frees if non-zero (we keep 0)
//   +0x24  CRITICAL_SECTION (24 bytes on x86 Win32) — read fn locks here
//   +0x44  our embedded buffer pointer (engine close ignores)
//   +0x48  our buffer size (uint32)
//   +0x4c  our read position (uint32)
// Allocating the full 0x60 byte struct (engine's size for cases 0/1/3)
// lets the engine's standard FUN_006472d0 + SMemFree close path tear us
// down cleanly without an extra close hook — all the ptr fields it
// inspects are NULL, and DeleteCriticalSection works on our initialized
// CS at +0x24.
constexpr size_t kHandleSize = 0x60;
constexpr size_t kHandleOffType = 0x00;
constexpr size_t kHandleOffCS = 0x24;
constexpr size_t kHandleOffData = 0x44;
constexpr size_t kHandleOffSize = 0x48;
constexpr size_t kHandleOffPos = 0x4c;

char NormalizeChar(char c) {
    if (c == '/') return '\\';
    if (c >= 'A' && c <= 'Z') return static_cast<char>(c + 32);
    return c;
}

bool PathEqualsCI(const char *a, const char *b) {
    while (*a && *b) {
        if (NormalizeChar(*a) != NormalizeChar(*b)) return false;
        ++a; ++b;
    }
    return *a == *b;
}

// Strip the `Interface\AddOns\MinimapBlips\` prefix and return the suffix
// (e.g. `icons\Auctioneer.blp`) on a match, NULL otherwise.
const char *StripAddonPrefix(const char *path) {
    if (path == nullptr) return nullptr;
    const char *p = path;
    const char *q = kAddonPathPrefix;
    while (*q) {
        if (*p == '\0') return nullptr;
        if (NormalizeChar(*p) != NormalizeChar(*q)) return nullptr;
        ++p; ++q;
    }
    return p;
}

const MinimapBlipsFiles::File *LookupEmbedded(const char *suffix) {
    for (size_t i = 0; i < MinimapBlipsFiles::kFileCount; ++i) {
        if (PathEqualsCI(suffix, MinimapBlipsFiles::kFiles[i].path))
            return &MinimapBlipsFiles::kFiles[i];
    }
    return nullptr;
}

// Storm allocator — same one `FUN_FILE_READ` uses internally. Buffer
// allocated here is freed cleanly by the caller's standard `SMemFree`.
// `__stdcall` per the function's `RET 0x10` epilogue (4 args × 4 bytes).
using SMemAlloc_t = void *(__stdcall *)(size_t size, const char *file, int line, int flags);

// `FUN_FILE_READ` — `__stdcall` (callee cleans 28 bytes via `RET 0x1C`),
// not `__cdecl`. Getting this wrong silently corrupts the caller's stack.
using FileRead_t = int(__stdcall *)(int unused, const char *path, void **outBuf,
                                    size_t *outSize, size_t extraBytes,
                                    int flag1, int flag2);
FileRead_t FileRead_o = nullptr;

using SMemFree_t = void(__stdcall *)(void *buf, const char *file, int line, int flags);

// Extracts the value of the `## Version: X` line from a TOC byte buffer.
// Writes into `out` (size `outSize`) and returns true on success. TOC
// format is `## Key: Value`, one per line, case-sensitive on the key.
bool ExtractTocVersion(const char *content, size_t size,
                       char *out, size_t outSize) {
    static const char kKey[] = "## Version:";
    constexpr size_t kKeyLen = sizeof(kKey) - 1;
    for (size_t i = 0; i + kKeyLen <= size; ++i) {
        const bool atLineStart = (i == 0) || content[i - 1] == '\n';
        if (!atLineStart) continue;
        if (std::memcmp(content + i, kKey, kKeyLen) != 0) continue;
        const char *p = content + i + kKeyLen;
        const char *end = content + size;
        while (p < end && (*p == ' ' || *p == '\t')) ++p;
        size_t j = 0;
        while (p < end && *p != '\r' && *p != '\n' && j + 1 < outSize) {
            out[j++] = *p++;
        }
        while (j > 0 && (out[j - 1] == ' ' || out[j - 1] == '\t')) --j;
        out[j] = '\0';
        return j > 0;
    }
    if (outSize > 0) out[0] = '\0';
    return false;
}

// Returns -1/0/+1 for `a < b` / `a == b` / `a > b`. Walks the two
// strings as dot-separated numeric components (`1.2` < `1.10`). The
// CMake-time stamping always produces a real numeric version, so no
// "DEV" sentinel is needed here.
int CompareVersions(const char *a, const char *b) {
    while (*a || *b) {
        int va = 0, vb = 0;
        while (*a >= '0' && *a <= '9') { va = va * 10 + (*a - '0'); ++a; }
        while (*b >= '0' && *b <= '9') { vb = vb * 10 + (*b - '0'); ++b; }
        if (va != vb) return va < vb ? -1 : 1;
        if (*a == '.') ++a;
        if (*b == '.') ++b;
        if (!*a && !*b) break;
        if (!*a) return -1;
        if (!*b) return 1;
    }
    return 0;
}

// Pre-extracted embedded TOC version, populated lazily on first
// `FileRead_h` call under our prefix. Empty until populated.
char g_embeddedVersion[64] = "";

// Which source wins the embed-vs-disk comparison. Decided once on the
// first relevant file read.
enum class Source { Undecided, Disk, Embedded };
Source g_source = Source::Undecided;

// Re-entry guard. The disk-version probe inside `DecideSource` calls
// `FileRead_o` (the engine's high-level read trampoline), which internally
// dispatches through `FUN_006477c0` — which we also hook. Without this
// guard, the inner `FileOpen_h` would re-trigger `DecideSource` (still
// Undecided), recursing until stack overflow → silent process exit.
// Holding the guard means "we're inside the disk probe — bypass embed
// hooks entirely so the disk read sees the real disk state."
bool g_decidingSource = false;

void EnsureEmbeddedVersionExtracted() {
    if (g_embeddedVersion[0] != '\0') return;
    for (size_t i = 0; i < MinimapBlipsFiles::kFileCount; ++i) {
        if (PathEqualsCI(kAddonTocFile, MinimapBlipsFiles::kFiles[i].path)) {
            ExtractTocVersion(
                reinterpret_cast<const char *>(MinimapBlipsFiles::kFiles[i].data),
                MinimapBlipsFiles::kFiles[i].size,
                g_embeddedVersion, sizeof(g_embeddedVersion));
            return;
        }
    }
}

// Decide whether disk or embedded should serve all reads for this addon.
// Called on the first FileRead_h call with a matching prefix. Reads the
// on-disk TOC via the original FUN_FILE_READ (bypassing our hook), parses
// its version, compares to the embedded version, caches the winner.
void DecideSource() {
    if (g_source != Source::Undecided) return;
    EnsureEmbeddedVersionExtracted();

    char fullPath[256];
    std::snprintf(fullPath, sizeof(fullPath), "%s%s",
                  kAddonPathPrefix, kAddonTocFile);

    void *diskBuf = nullptr;
    size_t diskSize = 0;
    g_decidingSource = true;
    const int ok = FileRead_o(0, fullPath, &diskBuf, &diskSize, 1, 1, 0);
    g_decidingSource = false;
    if (ok == 0 || diskBuf == nullptr) {
        g_source = Source::Embedded;
        return;
    }

    char diskVersion[64] = "";
    ExtractTocVersion(static_cast<const char *>(diskBuf), diskSize,
                      diskVersion, sizeof(diskVersion));

    auto SMemFree = reinterpret_cast<SMemFree_t>(Offsets::FUN_STORM_SMEM_FREE);
    SMemFree(diskBuf, __FILE__, __LINE__, 0);

    if (diskVersion[0] == '\0') {
        g_source = Source::Embedded;
        return;
    }

    const int cmp = CompareVersions(g_embeddedVersion, diskVersion);
    g_source = (cmp > 0) ? Source::Embedded : Source::Disk;
}

int __stdcall FileRead_h(int unused, const char *path, void **outBuf,
                         size_t *outSize, size_t extraBytes,
                         int flag1, int flag2) {
    if (g_decidingSource) {
        // DecideSource's own disk probe is in flight — let it see real disk.
        return FileRead_o(unused, path, outBuf, outSize, extraBytes,
                          flag1, flag2);
    }
    const char *suffix = StripAddonPrefix(path);
    if (suffix == nullptr) {
        // Path isn't under `Interface\AddOns\MinimapBlips\` — pass through.
        return FileRead_o(unused, path, outBuf, outSize, extraBytes,
                          flag1, flag2);
    }

    DecideSource();

    if (g_source == Source::Disk) {
        // Disk version is at least as new — serve disk. If disk doesn't
        // have this specific file (e.g. embedded has files disk doesn't),
        // fall through to embedded.
        const int diskResult = FileRead_o(unused, path, outBuf, outSize,
                                           extraBytes, flag1, flag2);
        if (diskResult != 0) return diskResult;
    }

    const auto *entry = LookupEmbedded(suffix);
    if (entry == nullptr) {
        // Not in our embedded set either — let the engine try disk one
        // more time (returns 0 if that also fails).
        return FileRead_o(unused, path, outBuf, outSize, extraBytes,
                          flag1, flag2);
    }

    auto SMemAlloc = reinterpret_cast<SMemAlloc_t>(Offsets::FUN_STORM_SMEM_ALLOC);
    const size_t totalSize = entry->size + extraBytes;
    void *buf = SMemAlloc(totalSize, __FILE__, __LINE__, 0);
    if (buf == nullptr) return 0;
    std::memcpy(buf, entry->data, entry->size);
    if (extraBytes > 0) {
        std::memset(static_cast<uint8_t *>(buf) + entry->size, 0, extraBytes);
    }
    if (outBuf != nullptr) *outBuf = buf;
    if (outSize != nullptr) *outSize = entry->size;
    return 1;
}

// Low-level file open / handle-read used by the BLP texture pipeline.
// `TextureCreate` → BLP parser → `FUN_006477c0` (open) → `FUN_00648460`
// (read). Doesn't flow through `FUN_FILE_READ`, so the text-file hook
// above misses it — that's why icon BLPs need their own hook pair.
//
// Open: `__stdcall(int *src, char *path, uint flags, int **outHandle)`
// returns a non-zero source-type code on success. Engine allocates a
// 0x60-byte handle struct via SMemAlloc.
// Read: `__stdcall(void *handle, void *buf, uint size, uint *outBytes,
//                  uint flag1, uint flag2)` returns bool. Switches on
// `*handle` (source type) to dispatch to backend-specific read funcs.
// Default case (unknown type) returns false — so we MUST hook read to
// add a case for our magic type.
using FileOpen_t = int(__stdcall *)(int *src, const char *path, unsigned flags,
                                     int **outHandle);
FileOpen_t FileOpen_o = nullptr;

using FileReadHandle_t = bool(__stdcall *)(void *handle, void *buf, unsigned size,
                                            unsigned *outBytes, unsigned f1, unsigned f2);
FileReadHandle_t FileReadHandle_o = nullptr;

// `FUN_006487f0(handle, outFlag)` — file-size query. Dispatches on
// `*handle`. Texture loader calls this to size the texture heap slot;
// returning 0 makes the engine allocate 0 bytes for the texture and
// render a blank/missing placeholder. RET 0x8 = 2 args, __stdcall.
using FileSize_t = unsigned(__stdcall *)(void *handle, unsigned *outFlag);
FileSize_t FileSize_o = nullptr;

static void *AllocEmbeddedHandle(const MinimapBlipsFiles::File *entry) {
    auto SMemAlloc = reinterpret_cast<SMemAlloc_t>(Offsets::FUN_STORM_SMEM_ALLOC);
    auto *h = static_cast<uint8_t *>(SMemAlloc(kHandleSize, __FILE__, __LINE__, 0));
    if (h == nullptr) return nullptr;
    std::memset(h, 0, kHandleSize);
    *reinterpret_cast<uint32_t *>(h + kHandleOffType) = kEmbeddedTypeCode;
    InitializeCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(h + kHandleOffCS));
    *reinterpret_cast<const unsigned char **>(h + kHandleOffData) = entry->data;
    *reinterpret_cast<uint32_t *>(h + kHandleOffSize) = static_cast<uint32_t>(entry->size);
    *reinterpret_cast<uint32_t *>(h + kHandleOffPos) = 0;
    return h;
}

int __stdcall FileOpen_h(int *src, const char *path, unsigned flags, int **outHandle) {
    if (g_decidingSource) {
        // DecideSource's disk probe → engine's FUN_FILE_READ → here.
        // Bypass our logic so the probe sees real disk state. Without
        // this, DecideSource recurses indefinitely.
        return FileOpen_o(src, path, flags, outHandle);
    }
    const char *suffix = StripAddonPrefix(path);
    if (suffix == nullptr) {
        return FileOpen_o(src, path, flags, outHandle);
    }
    DecideSource();

    if (g_source == Source::Disk) {
        // Disk version is at least as new — try disk. If it doesn't have
        // this specific file, fall through to embedded.
        const int r = FileOpen_o(src, path, flags, outHandle);
        if (r != 0) return r;
    }

    const auto *entry = LookupEmbedded(suffix);
    if (entry == nullptr) {
        // Not embedded either — let the engine try disk one more time.
        return FileOpen_o(src, path, flags, outHandle);
    }

    void *h = AllocEmbeddedHandle(entry);
    if (h == nullptr) return 0;
    *outHandle = static_cast<int *>(h);
    // Non-zero return = success. The specific value is irrelevant —
    // FUN_005a3660 (the BLP-pipeline caller) only checks `iVar2 != 0`.
    return 1;
}

bool __stdcall FileReadHandle_h(void *handle, void *buf, unsigned size,
                                 unsigned *outBytes, unsigned f1, unsigned f2) {
    if (g_decidingSource || handle == nullptr ||
        *reinterpret_cast<uint32_t *>(handle) != kEmbeddedTypeCode) {
        return FileReadHandle_o(handle, buf, size, outBytes, f1, f2);
    }
    // Match the engine's behavior for size==0: return true with
    // outBytes=0 (FUN_00648460's early-out path on `param_3 == 0`).
    if (size == 0) {
        if (outBytes != nullptr) *outBytes = 0;
        return true;
    }

    auto *h = reinterpret_cast<uint8_t *>(handle);
    auto *cs = reinterpret_cast<LPCRITICAL_SECTION>(h + kHandleOffCS);
    EnterCriticalSection(cs);

    const auto *data = *reinterpret_cast<const unsigned char **>(h + kHandleOffData);
    const auto totalSize = *reinterpret_cast<uint32_t *>(h + kHandleOffSize);
    auto &pos = *reinterpret_cast<uint32_t *>(h + kHandleOffPos);

    const uint32_t available = (pos < totalSize) ? (totalSize - pos) : 0u;
    const uint32_t toRead = (size < available) ? size : available;

    if (buf != nullptr && toRead > 0) {
        std::memcpy(buf, data + pos, toRead);
    }
    pos += toRead;
    if (outBytes != nullptr) *outBytes = toRead;

    LeaveCriticalSection(cs);
    // Match engine's `return 0 < bytesRead;` — false on EOF (toRead==0).
    return toRead > 0;
}

unsigned __stdcall FileSize_h(void *handle, unsigned *outFlag) {
    if (g_decidingSource || handle == nullptr ||
        *reinterpret_cast<uint32_t *>(handle) != kEmbeddedTypeCode) {
        return FileSize_o(handle, outFlag);
    }
    // The engine's case-0 path zeroes `*outFlag` when present — mirror it
    // so callers that read `*outFlag` see a defined value.
    if (outFlag != nullptr) *outFlag = 0;
    return *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(handle) + kHandleOffSize);
}

// `FUN_ADDON_INIT` — `__fastcall(accountName)`. Post-hook: engine's
// normal `Interface\AddOns\` directory scan runs first, then we call the
// TOC parser with `"MinimapBlips"`. Dedup-safe — if the user already has
// it on disk, the engine's scan picked it up and our call is a no-op.
using AddonInit_t = void(__fastcall *)(const char *accountName);
AddonInit_t AddonInit_o = nullptr;

using TocParser_t = void(__fastcall *)(const char *name);

void __fastcall AddonInit_h(const char *accountName) {
    AddonInit_o(accountName);
    auto TocParser = reinterpret_cast<TocParser_t>(Offsets::FUN_TOC_PARSER);
    TocParser(kAddonName);
}

} // namespace

BOOL InstallHooks() {
    HOOK_FUNCTION(Offsets::FUN_FILE_READ, FileRead_h, FileRead_o);
    HOOK_FUNCTION(Offsets::FUN_FILE_OPEN, FileOpen_h, FileOpen_o);
    HOOK_FUNCTION(Offsets::FUN_FILE_READ_HANDLE, FileReadHandle_h, FileReadHandle_o);
    HOOK_FUNCTION(Offsets::FUN_FILE_SIZE, FileSize_h, FileSize_o);
    HOOK_FUNCTION(Offsets::FUN_ADDON_INIT, AddonInit_h, AddonInit_o);
    return TRUE;
}

} // namespace Addons::Embedded
