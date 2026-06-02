#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

// rev_pack — asset packing and embedded loading
//
// Pack side (editor):
//   PackAssets()  — reads cues.txt, finds referenced files, writes packed_assets.h
//                   with CRC32 checksums; skips files unchanged since last pack.
//
// Load side (runtime, when HIMYM_PACKED_ASSETS is defined):
//   Use the PackedAsset table generated in packed_assets.h.
//   GetPackedAsset(key) returns the raw bytes + size for a given asset_key.

namespace rev {
namespace pack {

// A single packed asset entry (runtime side — used in packed_assets.h)
struct PackedAsset {
    const char*          key;    // asset_key matching cues.txt (e.g. "logo.png")
    const unsigned char* data;
    size_t               size;
    uint32_t             crc32;
};

// Look up a packed asset by key. Returns nullptr if not found.
// Only meaningful when compiled against a packed_assets.h that populates the table.
inline const PackedAsset* GetPackedAsset(const char* key, const PackedAsset* table, int count) {
    if (!key || !table) return nullptr;
    for (int i = 0; i < count; ++i)
        if (table[i].key && strcmp(table[i].key, key) == 0)
            return &table[i];
    return nullptr;
}

// --- Editor / packer side ---

// Result of PackAssets
struct PackResult {
    int total;     // total assets found
    int packed;    // assets written (new or changed)
    int skipped;   // assets unchanged (checksum matched cache)
    int optional_skipped; // optional assets skipped due to read failures
    bool ok;       // false if a critical error occurred
    char error[256];
};

// Pack all assets referenced in cues_path into output_header.
// cache_path stores checksum state between runs (e.g. "assets/pack_cache.txt").
// workspace_root is the base path for resolving asset_path entries.
PackResult PackAssets(const char* cues_path,
                      const char* output_header,
                      const char* cache_path,
                      const char* workspace_root);

// CRC32 (ISO 3309) of a byte buffer
uint32_t CRC32(const unsigned char* data, size_t size);

}  // namespace pack
}  // namespace rev
