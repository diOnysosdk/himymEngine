#include "rev_pack.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace rev {
namespace pack {

// ---------------------------------------------------------------------------
// CRC32 (ISO 3309 table)
// ---------------------------------------------------------------------------
static uint32_t s_crc_table[256];
static bool     s_crc_init = false;

static void InitCRCTable() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        s_crc_table[i] = c;
    }
    s_crc_init = true;
}

uint32_t CRC32(const unsigned char* data, size_t size) {
    if (!s_crc_init) InitCRCTable();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i)
        crc = s_crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
struct CacheEntry {
    char     key[128];
    char     path[512];
    uint32_t crc32;
};

static const int kMaxCacheEntries = 256;

static int LoadCache(const char* cache_path, CacheEntry* entries) {
    FILE* f = nullptr;
    fopen_s(&f, cache_path, "r");
    if (!f) return 0;
    int count = 0;
    char line[700];
    while (fgets(line, sizeof(line), f) && count < kMaxCacheEntries) {
        // Format: key|path|crc32hex
        char* p1 = strchr(line, '|');
        if (!p1) continue;
        *p1 = '\0';
        char* p2 = strchr(p1 + 1, '|');
        if (!p2) continue;
        *p2 = '\0';
        char crc_str[16] = {};
        strncpy_s(crc_str, p2 + 1, sizeof(crc_str) - 1);
        for (char* c = crc_str; *c; ++c)
            if (*c == '\r' || *c == '\n') { *c = '\0'; break; }

        strncpy_s(entries[count].key,  line,      sizeof(entries[count].key)  - 1);
        strncpy_s(entries[count].path, p1 + 1,    sizeof(entries[count].path) - 1);
        entries[count].crc32 = (uint32_t)strtoul(crc_str, nullptr, 16);
        count++;
    }
    fclose(f);
    return count;
}

static uint32_t CacheFind(const CacheEntry* entries, int count, const char* key) {
    for (int i = 0; i < count; ++i)
        if (strcmp(entries[i].key, key) == 0)
            return entries[i].crc32;
    return 0;
}

static unsigned char* ReadFile(const char* path, size_t* out_size) {
    FILE* f = nullptr;
    fopen_s(&f, path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return nullptr; }
    unsigned char* buf = (unsigned char*)malloc((size_t)sz);
    if (!buf) { fclose(f); return nullptr; }
    fread(buf, 1, (size_t)sz, f);
    fclose(f);
    *out_size = (size_t)sz;
    return buf;
}

static void WriteHexArray(FILE* f, const unsigned char* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (i % 16 == 0) fprintf(f, "    ");
        fprintf(f, "0x%02X", data[i]);
        if (i + 1 < size) fprintf(f, ",");
        if ((i + 1) % 16 == 0 || i + 1 == size) fprintf(f, "\n");
    }
}

static void KeyToIdent(const char* key, char* out, size_t out_size) {
    size_t j = 0;
    for (size_t i = 0; key[i] && j < out_size - 1; ++i) {
        char c = key[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            out[j++] = c;
        else
            out[j++] = '_';
    }
    out[j] = '\0';
}

// ---------------------------------------------------------------------------
// PackAssets — main entry point
// ---------------------------------------------------------------------------

struct AssetRef {
    char key[128];
    char path[512];
};
static const int kMaxAssets = 128;

static bool ParseAssetLine(const char* line, char* key_out, char* path_out) {
    char tmp[640];
    strncpy_s(tmp, line, sizeof(tmp) - 1);
    char* p1 = strchr(tmp, '|');
    if (!p1) return false;
    *p1 = '\0';
    char* path_start = p1 + 1;
    char* p2 = strchr(path_start, '|');
    if (p2) *p2 = '\0';
    size_t plen = strlen(path_start);
    while (plen > 0 && (path_start[plen-1] == '\r' || path_start[plen-1] == '\n' || path_start[plen-1] == ' '))
        path_start[--plen] = '\0';

    strncpy_s(key_out,  128, tmp,        _TRUNCATE);
    strncpy_s(path_out, 512, path_start, _TRUNCATE);
    return key_out[0] != '\0' && path_out[0] != '\0';
}

PackResult PackAssets(const char* cues_path,
                      const char* output_header,
                      const char* cache_path,
                      const char* workspace_root) {
    PackResult result = {};

    // --- Step 1: collect asset refs from cues.txt ---
    FILE* cues = nullptr;
    fopen_s(&cues, cues_path, "r");
    if (!cues) {
        snprintf(result.error, sizeof(result.error), "Cannot open cues.txt: %s", cues_path);
        return result;
    }

    AssetRef refs[kMaxAssets];
    int ref_count = 0;
    bool in_image = false, in_music = false;
    char line[1024];

    while (fgets(line, sizeof(line), cues)) {
        char* s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (strstr(s, "[image_cues]")) { in_image = true; in_music = false; continue; }
        if (strstr(s, "[music_cues]")) { in_image = false; in_music = true; continue; }
        if (s[0] == '[') { in_image = false; in_music = false; continue; }
        if (s[0] == '#' || s[0] == '\r' || s[0] == '\n' || s[0] == '\0') continue;

        if ((in_image || in_music) && ref_count < kMaxAssets) {
            if (ParseAssetLine(s, refs[ref_count].key, refs[ref_count].path))
                ref_count++;
        }
    }
    fclose(cues);
    result.total = ref_count;

    if (ref_count == 0) {
        FILE* hdr = nullptr;
        fopen_s(&hdr, output_header, "w");
        if (hdr) {
            // Normalise cues_path to forward slashes for embedding
            char cues_fwd[512] = {};
            strncpy_s(cues_fwd, cues_path, sizeof(cues_fwd) - 1);
            for (char* p = cues_fwd; *p; ++p) if (*p == '\\') *p = '/';

            fprintf(hdr, "// packed_assets.h -- generated by rev_pack (no assets)\n");
            fprintf(hdr, "#pragma once\n");
            fprintf(hdr, "#include \"rev_pack.h\"\n\n");
            fprintf(hdr, "static const char* PACKED_CUES_PATH = \"%s\";\n\n", cues_fwd);
            fprintf(hdr, "static const rev::pack::PackedAsset kPackedAssets[] = { { nullptr, nullptr, 0, 0 } };\n");
            fprintf(hdr, "static const int kPackedAssetCount = 0;\n");
            // Embed cues.txt content so the packed exe needs no disk access.
            {
                char full_cues[640] = {};
                if (workspace_root && workspace_root[0])
                    snprintf(full_cues, sizeof(full_cues), "%s\\%s", workspace_root, cues_path);
                else
                    strncpy_s(full_cues, cues_path, sizeof(full_cues) - 1);
                size_t csz = 0;
                unsigned char* cdata = ReadFile(full_cues, &csz);
                if (cdata && csz > 0) {
                    fprintf(hdr, "#define HIMYM_HAS_PACKED_CUES\n");
                    fprintf(hdr, "static const unsigned char kPackedCuesContent[] = {\n");
                    WriteHexArray(hdr, cdata, csz);
                    fprintf(hdr, "};\n");
                    fprintf(hdr, "static const size_t kPackedCuesSize = %zu;\n", csz);
                    free(cdata);
                }
            }
            fclose(hdr);
        }
        result.ok = true;
        return result;
    }

    // --- Step 2: load checksum cache ---
    CacheEntry cache[kMaxCacheEntries];
    int cache_count = LoadCache(cache_path, cache);

    // --- Step 3: load + checksum each asset ---
    struct LoadedAsset {
        char              key[128];
        char              ident[128];
        char              rel_path[512];
        unsigned char*    data;
        size_t            size;
        uint32_t          crc32;
        bool              changed;
    };
    LoadedAsset* loaded = (LoadedAsset*)malloc(ref_count * sizeof(LoadedAsset));
    if (!loaded) { snprintf(result.error, sizeof(result.error), "OOM"); return result; }
    memset(loaded, 0, ref_count * sizeof(LoadedAsset));

    for (int i = 0; i < ref_count; ++i) {
        char full_path[640];
        if (workspace_root && workspace_root[0])
            snprintf(full_path, sizeof(full_path), "%s\\%s", workspace_root, refs[i].path);
        else
            snprintf(full_path, sizeof(full_path), "%s", refs[i].path);
        for (char* p = full_path; *p; ++p) if (*p == '/') *p = '\\';

        size_t sz = 0;
        unsigned char* data = ReadFile(full_path, &sz);
        if (!data) {
            snprintf(result.error, sizeof(result.error), "Cannot read asset: %s", full_path);
            for (int j = 0; j < i; ++j) free(loaded[j].data);
            free(loaded);
            return result;
        }
        uint32_t crc        = CRC32(data, sz);
        uint32_t cached_crc = CacheFind(cache, cache_count, refs[i].key);

        strncpy_s(loaded[i].key,      refs[i].key,  _TRUNCATE);
        strncpy_s(loaded[i].rel_path, refs[i].path, _TRUNCATE);
        KeyToIdent(refs[i].key, loaded[i].ident, sizeof(loaded[i].ident));
        loaded[i].data    = data;
        loaded[i].size    = sz;
        loaded[i].crc32   = crc;
        loaded[i].changed = (crc != cached_crc);

        if (loaded[i].changed) result.packed++;
        else result.skipped++;
    }

    // --- Step 4: write packed_assets.h ---
    FILE* hdr = nullptr;
    fopen_s(&hdr, output_header, "w");
    if (!hdr) {
        snprintf(result.error, sizeof(result.error), "Cannot write: %s", output_header);
        for (int i = 0; i < ref_count; ++i) free(loaded[i].data);
        free(loaded);
        return result;
    }

    fprintf(hdr, "// packed_assets.h -- generated by rev_pack. DO NOT EDIT.\n");
    fprintf(hdr, "#pragma once\n");
    fprintf(hdr, "#include \"rev_pack.h\"\n\n");

    // Embed the cues path (kept for backward compat) and the full cues.txt content.
    {
        char cues_fwd[512] = {};
        strncpy_s(cues_fwd, cues_path, sizeof(cues_fwd) - 1);
        for (char* p = cues_fwd; *p; ++p) if (*p == '\\') *p = '/';
        fprintf(hdr, "static const char* PACKED_CUES_PATH = \"%s\";\n\n", cues_fwd);
    }

    // Embed cues.txt content so the packed exe is fully standalone (no disk access needed).
    {
        char full_cues[640] = {};
        if (workspace_root && workspace_root[0])
            snprintf(full_cues, sizeof(full_cues), "%s\\%s", workspace_root, cues_path);
        else
            strncpy_s(full_cues, cues_path, sizeof(full_cues) - 1);
        size_t csz = 0;
        unsigned char* cdata = ReadFile(full_cues, &csz);
        if (cdata && csz > 0) {
            fprintf(hdr, "#define HIMYM_HAS_PACKED_CUES\n");
            fprintf(hdr, "static const unsigned char kPackedCuesContent[] = {\n");
            WriteHexArray(hdr, cdata, csz);
            fprintf(hdr, "};\n");
            fprintf(hdr, "static const size_t kPackedCuesSize = %zu;\n\n", csz);
            free(cdata);
        }
    }

    for (int i = 0; i < ref_count; ++i) {
        fprintf(hdr, "// %s  CRC32=0x%08X  size=%zu\n", loaded[i].key, loaded[i].crc32, loaded[i].size);
        fprintf(hdr, "static const unsigned char kAsset_%s[] = {\n", loaded[i].ident);
        WriteHexArray(hdr, loaded[i].data, loaded[i].size);
        fprintf(hdr, "};\n\n");
    }

    fprintf(hdr, "static const rev::pack::PackedAsset kPackedAssets[] = {\n");
    for (int i = 0; i < ref_count; ++i) {
        fprintf(hdr, "    { \"%s\", kAsset_%s, sizeof(kAsset_%s), 0x%08Xu },\n",
                loaded[i].key, loaded[i].ident, loaded[i].ident, loaded[i].crc32);
    }
    fprintf(hdr, "};\n");
    fprintf(hdr, "static const int kPackedAssetCount = %d;\n", ref_count);
    fclose(hdr);

    // --- Step 5: update checksum cache ---
    FILE* cache_f = nullptr;
    fopen_s(&cache_f, cache_path, "w");
    if (cache_f) {
        for (int i = 0; i < ref_count; ++i)
            fprintf(cache_f, "%s|%s|%08X\n", loaded[i].key, loaded[i].rel_path, loaded[i].crc32);
        fclose(cache_f);
    }

    for (int i = 0; i < ref_count; ++i) free(loaded[i].data);
    free(loaded);

    result.ok = true;
    return result;
}

}  // namespace pack
}  // namespace rev
