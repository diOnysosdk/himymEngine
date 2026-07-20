#include "rev_pack.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <windows.h>

namespace rev {
namespace pack {

// ---------------------------------------------------------------------------
// CRC32 (ISO 3309 table)
// ---------------------------------------------------------------------------
static uint32_t s_crc_table[256];
static std::once_flag s_crc_init_once;

static void InitCRCTable() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        s_crc_table[i] = c;
    }
}

uint32_t CRC32(const unsigned char* data, size_t size) {
    std::call_once(s_crc_init_once, InitCRCTable);
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

static bool IsAbsolutePath(const char* path) {
    if (!path || !path[0]) return false;
    // Windows drive path: C:\... or C:/...
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
        return true;
    }
    // UNC path: \\server\share
    if (path[0] == '\\' && path[1] == '\\') return true;
    // Network/posix-like absolute path //server/... or /...
    if (path[0] == '/') return true;
    return false;
}

static void GetDirectoryOfPath(const char* path, char* out_dir, size_t out_size) {
    if (!out_dir || out_size == 0) return;
    out_dir[0] = '\0';
    if (!path || !path[0]) return;

    strncpy_s(out_dir, out_size, path, _TRUNCATE);
    char* last_slash = strrchr(out_dir, '\\');
    char* last_fslash = strrchr(out_dir, '/');
    char* cut = last_slash;
    if (last_fslash && (!cut || last_fslash > cut)) cut = last_fslash;
    if (cut) *cut = '\0';
}

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
    if (out_size) *out_size = 0;

    // Freshly baked files can be briefly unavailable due to writer/AV sharing.
    for (int attempt = 0; attempt < 12; ++attempt) {
        FILE* f = nullptr;
        fopen_s(&f, path, "rb");
        if (!f) {
            Sleep(15);
            continue;
        }

        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0) {
            fclose(f);
            Sleep(15);
            continue;
        }

        unsigned char* buf = (unsigned char*)malloc((size_t)sz);
        if (!buf) {
            fclose(f);
            return nullptr;
        }

        size_t read_sz = fread(buf, 1, (size_t)sz, f);
        fclose(f);
        if (read_sz != (size_t)sz) {
            free(buf);
            Sleep(15);
            continue;
        }

        if (out_size) *out_size = (size_t)sz;
        return buf;
    }

    return nullptr;
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

// Parse a [mesh_cues] line: asset_key|asset_path|mesh_type|...
// Returns true only for mesh_type == 4 (external glTF/GLB).
static bool ParseMeshAssetLine(const char* line, char* key_out, char* path_out) {
    char tmp[1024];
    strncpy_s(tmp, line, sizeof(tmp) - 1);

    char* p1 = strchr(tmp, '|');          // end of asset_key
    if (!p1) return false;
    *p1 = '\0';

    char* p2 = strchr(p1 + 1, '|');       // end of asset_path
    if (!p2) return false;
    *p2 = '\0';

    char* p3 = strchr(p2 + 1, '|');       // end of mesh_type
    char mesh_type_str[8] = {};
    if (p3) {
        size_t len = (size_t)(p3 - (p2 + 1));
        if (len > 7) len = 7;
        memcpy(mesh_type_str, p2 + 1, len);
    } else {
        strncpy_s(mesh_type_str, p2 + 1, sizeof(mesh_type_str) - 1);
    }
    if (atoi(mesh_type_str) != 4) return false;  // only pack external glTF/GLB

    // Trim path
    char* path_start = p1 + 1;
    size_t plen = strlen(path_start);
    while (plen > 0 && (path_start[plen-1] == '\r' || path_start[plen-1] == '\n' || path_start[plen-1] == ' '))
        path_start[--plen] = '\0';

    strncpy_s(key_out,  128, tmp,        _TRUNCATE);
    strncpy_s(path_out, 512, path_start, _TRUNCATE);
    return key_out[0] != '\0' && path_out[0] != '\0';
}

// Parse [text_cues] line and read baked and optional glyph atlas asset pairs.
static bool ParseTextAssetLine(const char* line, char* key_out, char* path_out,
                               char* atlas_key_out, char* atlas_path_out,
                               char* meta_key_out, char* meta_path_out) {
    char tmp[1024];
    strncpy_s(tmp, line, sizeof(tmp) - 1);

    // Strip trailing newline/CR from the whole line first.
    size_t tlen = strlen(tmp);
    while (tlen > 0 && (tmp[tlen-1] == '\r' || tmp[tlen-1] == '\n' || tmp[tlen-1] == ' '))
        tmp[--tlen] = '\0';

    char* last = strrchr(tmp, '|');
    if (!last) return false;
    *last = '\0';
    char* path = last + 1;

    char* prev = strrchr(tmp, '|');
    if (!prev) return false;
    *prev = '\0';
    char* key = prev + 1;

    if (atlas_key_out) atlas_key_out[0] = '\0';
    if (atlas_path_out) atlas_path_out[0] = '\0';
    if (meta_key_out) meta_key_out[0] = '\0';
    if (meta_path_out) meta_path_out[0] = '\0';
    char source[8192] = {};
    char fields[64][512] = {};
    strncpy_s(source, sizeof(source), line, _TRUNCATE);
    int field_count = 0;
    char* context = nullptr;
    char* token = strtok_s(source, "|\r\n", &context);
    while (token && field_count < 64) {
        strncpy_s(fields[field_count], sizeof(fields[field_count]), token, _TRUNCATE);
        ++field_count;
        token = strtok_s(nullptr, "|\r\n", &context);
    }
    for (int i = 0; i + 3 < field_count; ++i) {
        if (!strstr(fields[i], "glyph") ||
            (!strstr(fields[i + 1], ".png") && !strstr(fields[i + 1], ".webp")) ||
            !strstr(fields[i + 2], "glyph") || !strstr(fields[i + 3], ".txt")) {
            continue;
        }
        if (i >= 2) {
            strncpy_s(key_out, 128, fields[i - 2], _TRUNCATE);
            strncpy_s(path_out, 512, fields[i - 1], _TRUNCATE);
        }
        if (atlas_key_out) strncpy_s(atlas_key_out, 128, fields[i], _TRUNCATE);
        if (atlas_path_out) strncpy_s(atlas_path_out, 512, fields[i + 1], _TRUNCATE);
        if (meta_key_out) strncpy_s(meta_key_out, 128, fields[i + 2], _TRUNCATE);
        if (meta_path_out) strncpy_s(meta_path_out, 512, fields[i + 3], _TRUNCATE);
        return true;
    }

    // Legacy text rows without glyph assets use the final key/path pair.
    if (key[0] == '\0' || path[0] == '\0') return false;
    if (!strchr(path, '/') && !strchr(path, '\\') &&
        !strstr(path, ".png") && !strstr(path, ".webp") && !strstr(path, ".dds")) return false;
    strncpy_s(key_out, 128, key, _TRUNCATE);
    strncpy_s(path_out, 512, path, _TRUNCATE);
    return true;
}

static bool IsOptionalBakedTextAsset(const char* key, const char* path) {
    if (!key || !path) return false;
    if (strncmp(key, "text_s", 6) != 0 && strncmp(key, "scroll_s", 8) != 0) return false;
    if (!strstr(path, ".png") && !strstr(path, ".webp")) return false;
    if (!strstr(path, "project_assets")) return false;
    return true;
}

static int ParseAnimatedSpriteAssetLine(const char* line, AssetRef* out_refs, int max_refs) {
    if (!line || !out_refs || max_refs <= 0) return 0;

    char tmp[8192] = {};
    strncpy_s(tmp, line, sizeof(tmp) - 1);

    char* p1 = strchr(tmp, '|');
    if (!p1) return 0;
    *p1 = '\0';
    char* frame_keys = p1 + 1;

    char* p2 = strchr(frame_keys, '|');
    if (!p2) return 0;
    *p2 = '\0';
    char* frame_paths = p2 + 1;

    char* p3 = strchr(frame_paths, '|');
    if (p3) *p3 = '\0';

    int count = 0;
    char* key_ctx = nullptr;
    char* path_ctx = nullptr;
    char* key_tok = strtok_s(frame_keys, ";", &key_ctx);
    char* path_tok = strtok_s(frame_paths, ";", &path_ctx);
    while (key_tok && count < max_refs) {
        if (key_tok[0] != '\0') {
            strncpy_s(out_refs[count].key, sizeof(out_refs[count].key), key_tok, _TRUNCATE);
            if (path_tok && path_tok[0] != '\0') {
                strncpy_s(out_refs[count].path, sizeof(out_refs[count].path), path_tok, _TRUNCATE);
            } else {
                strncpy_s(out_refs[count].path, sizeof(out_refs[count].path), key_tok, _TRUNCATE);
            }
            ++count;
        }
        key_tok = strtok_s(nullptr, ";", &key_ctx);
        if (path_tok) path_tok = strtok_s(nullptr, ";", &path_ctx);
    }
    return count;
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

    char cues_dir[640] = {};
    char cues_leaf[128] = {};
    char cues_leaf_assets[192] = {};
    {
        char full_cues_path[640] = {};
        if (IsAbsolutePath(cues_path)) {
            strncpy_s(full_cues_path, sizeof(full_cues_path), cues_path, _TRUNCATE);
        } else if (workspace_root && workspace_root[0]) {
            snprintf(full_cues_path, sizeof(full_cues_path), "%s\\%s", workspace_root, cues_path);
        } else {
            strncpy_s(full_cues_path, sizeof(full_cues_path), cues_path, _TRUNCATE);
        }
        GetDirectoryOfPath(full_cues_path, cues_dir, sizeof(cues_dir));
        const char* leaf = strrchr(cues_dir, '\\');
        if (!leaf) leaf = strrchr(cues_dir, '/');
        leaf = leaf ? leaf + 1 : cues_dir;
        strncpy_s(cues_leaf, sizeof(cues_leaf), leaf, _TRUNCATE);
        if (cues_leaf[0]) {
            snprintf(cues_leaf_assets, sizeof(cues_leaf_assets), "%s_assets", cues_leaf);
        }
    }

    AssetRef refs[kMaxAssets];
    int ref_count = 0;
    bool in_image = false, in_music = false, in_mesh = false, in_text = false, in_scroll_text = false, in_animated_sprite = false, in_pixel = false, in_pixel_emitter = false;
    char line[1024];

    while (fgets(line, sizeof(line), cues)) {
        char* s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (strstr(s, "[image_cues]"))       { in_image = true;  in_music = false; in_mesh = false; in_text = false; in_scroll_text = false; in_animated_sprite = false; in_pixel = false; in_pixel_emitter = false; continue; }
        if (strstr(s, "[animated_sprite_cues]")) { in_image = false; in_music = false; in_mesh = false; in_text = false; in_scroll_text = false; in_animated_sprite = true; in_pixel = false; in_pixel_emitter = false; continue; }
        if (strstr(s, "[pixel_cues]"))       { in_image = false; in_music = false; in_mesh = false; in_text = false; in_scroll_text = false; in_animated_sprite = false; in_pixel = true; in_pixel_emitter = false; continue; }
        if (strstr(s, "[pixel_emitter_cues]")) { in_image = false; in_music = false; in_mesh = false; in_text = false; in_scroll_text = false; in_animated_sprite = false; in_pixel = false; in_pixel_emitter = true; continue; }
        if (strstr(s, "[music_cues]"))       { in_image = false; in_music = true;  in_mesh = false; in_text = false; in_scroll_text = false; in_animated_sprite = false; in_pixel = false; in_pixel_emitter = false; continue; }
        if (strstr(s, "[mesh_cues]"))        { in_image = false; in_music = false; in_mesh = true;  in_text = false; in_scroll_text = false; in_animated_sprite = false; in_pixel = false; in_pixel_emitter = false; continue; }
        if (strstr(s, "[text_cues]"))        { in_image = false; in_music = false; in_mesh = false; in_text = true;  in_scroll_text = false; in_animated_sprite = false; in_pixel = false; in_pixel_emitter = false; continue; }
        if (strstr(s, "[scroll_text_cues]")) { in_image = false; in_music = false; in_mesh = false; in_text = false; in_scroll_text = true;  in_animated_sprite = false; in_pixel = false; in_pixel_emitter = false; continue; }
        if (s[0] == '[') { in_image = false; in_music = false; in_mesh = false; in_text = false; in_scroll_text = false; in_animated_sprite = false; in_pixel = false; in_pixel_emitter = false; continue; }
        if (s[0] == '#' || s[0] == '\r' || s[0] == '\n' || s[0] == '\0') continue;

        if ((in_image || in_music) && ref_count < kMaxAssets) {
            if (ParseAssetLine(s, refs[ref_count].key, refs[ref_count].path))
                ref_count++;
        } else if (in_mesh && ref_count < kMaxAssets) {
            if (ParseMeshAssetLine(s, refs[ref_count].key, refs[ref_count].path))
                ref_count++;
        } else if ((in_text || in_scroll_text) && ref_count < kMaxAssets) {
            char atlas_key[128] = {}, atlas_path[512] = {};
            char meta_key[128] = {}, meta_path[512] = {};
            if (ParseTextAssetLine(s, refs[ref_count].key, refs[ref_count].path,
                                   atlas_key, atlas_path, meta_key, meta_path)) {
                ref_count++;
                if (atlas_key[0] && ref_count < kMaxAssets) {
                    strncpy_s(refs[ref_count].key, atlas_key, _TRUNCATE);
                    strncpy_s(refs[ref_count].path, atlas_path, _TRUNCATE);
                    ref_count++;
                }
                if (meta_key[0] && ref_count < kMaxAssets) {
                    strncpy_s(refs[ref_count].key, meta_key, _TRUNCATE);
                    strncpy_s(refs[ref_count].path, meta_path, _TRUNCATE);
                    ref_count++;
                }
            }
        } else if (in_animated_sprite && ref_count < kMaxAssets) {
            AssetRef parsed[64] = {};
            int parsed_count = ParseAnimatedSpriteAssetLine(s, parsed, 64);
            for (int i = 0; i < parsed_count && ref_count < kMaxAssets; ++i) {
                refs[ref_count++] = parsed[i];
            }
        } else if (in_pixel && ref_count < kMaxAssets) {
            if (ParseAssetLine(s, refs[ref_count].key, refs[ref_count].path))
                ref_count++;
        } else if (in_pixel_emitter && ref_count < kMaxAssets) {
            char key[128] = {}, path[512] = {};
            if (ParseAssetLine(s, key, path)) {
                const char* first_pipe = strchr(s, '|');
                const char* second_pipe = first_pipe ? strchr(first_pipe + 1, '|') : nullptr;
                int visual_source = 1;
                if (second_pipe) visual_source = atoi(second_pipe + 1);
                if (visual_source == 0) {
                    strncpy_s(refs[ref_count].key, key, _TRUNCATE);
                    strncpy_s(refs[ref_count].path, path, _TRUNCATE);
                    ref_count++;
                }
            }
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
            fprintf(hdr, "// format: rev_pack_v2_indexed_asset_symbols\n");
            fprintf(hdr, "#pragma once\n");
            fprintf(hdr, "#include \"rev_pack.h\"\n\n");
            fprintf(hdr, "#define HIMYM_PACKED_ASSET_FORMAT_VERSION 2\n\n");
            fprintf(hdr, "static const char* PACKED_CUES_PATH = \"%s\";\n\n", cues_fwd);
            fprintf(hdr, "static const rev::pack::PackedAsset kPackedAssets[] = { { nullptr, nullptr, 0, 0 } };\n");
            fprintf(hdr, "static const int kPackedAssetCount = 0;\n");
            // Embed cues.txt content so the packed exe needs no disk access.
            {
                char full_cues[640] = {};
                if (IsAbsolutePath(cues_path))
                    strncpy_s(full_cues, cues_path, sizeof(full_cues) - 1);
                else if (workspace_root && workspace_root[0])
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
        int               data_owner;
    };
    LoadedAsset* loaded = (LoadedAsset*)malloc(ref_count * sizeof(LoadedAsset));
    if (!loaded) { snprintf(result.error, sizeof(result.error), "OOM"); return result; }
    memset(loaded, 0, ref_count * sizeof(LoadedAsset));
    int loaded_count = 0;

    for (int i = 0; i < ref_count; ++i) {
        char full_path[640];
        if (IsAbsolutePath(refs[i].path))
            snprintf(full_path, sizeof(full_path), "%s", refs[i].path);
        else if (workspace_root && workspace_root[0])
            snprintf(full_path, sizeof(full_path), "%s\\%s", workspace_root, refs[i].path);
        else
            snprintf(full_path, sizeof(full_path), "%s", refs[i].path);
        for (char* p = full_path; *p; ++p) if (*p == '/') *p = '\\';

        size_t sz = 0;
        unsigned char* data = ReadFile(full_path, &sz);
        if (!data && !IsAbsolutePath(refs[i].path)) {
            // Fallbacks for relative asset paths (common for frame filenames in animated sprites).
            char try_path[640] = {};

            if (cues_dir[0]) {
                snprintf(try_path, sizeof(try_path), "%s\\%s", cues_dir, refs[i].path);
                for (char* p = try_path; *p; ++p) if (*p == '/') *p = '\\';
                data = ReadFile(try_path, &sz);
                if (data) {
                    strncpy_s(full_path, sizeof(full_path), try_path, _TRUNCATE);
                }
            }

            if (!data && cues_dir[0] && !strchr(refs[i].path, '\\') && !strchr(refs[i].path, '/')) {
                snprintf(try_path, sizeof(try_path), "%s\\project_assets\\%s", cues_dir, refs[i].path);
                data = ReadFile(try_path, &sz);
                if (data) {
                    strncpy_s(full_path, sizeof(full_path), try_path, _TRUNCATE);
                }
            }

            if (!data && cues_dir[0] && cues_leaf_assets[0] && !strchr(refs[i].path, '\\') && !strchr(refs[i].path, '/')) {
                snprintf(try_path, sizeof(try_path), "%s\\%s\\%s", cues_dir, cues_leaf_assets, refs[i].path);
                data = ReadFile(try_path, &sz);
                if (data) {
                    strncpy_s(full_path, sizeof(full_path), try_path, _TRUNCATE);
                }
            }

            if (!data && workspace_root && workspace_root[0] && !strchr(refs[i].path, '\\') && !strchr(refs[i].path, '/')) {
                snprintf(try_path, sizeof(try_path), "%s\\project_assets\\%s", workspace_root, refs[i].path);
                data = ReadFile(try_path, &sz);
                if (data) {
                    strncpy_s(full_path, sizeof(full_path), try_path, _TRUNCATE);
                }
            }

            if (!data && workspace_root && workspace_root[0] && cues_leaf_assets[0] && !strchr(refs[i].path, '\\') && !strchr(refs[i].path, '/')) {
                snprintf(try_path, sizeof(try_path), "%s\\%s\\%s", workspace_root, cues_leaf_assets, refs[i].path);
                data = ReadFile(try_path, &sz);
                if (data) {
                    strncpy_s(full_path, sizeof(full_path), try_path, _TRUNCATE);
                }
            }
        }
        if (!data) {
            if (IsOptionalBakedTextAsset(refs[i].key, refs[i].path)) {
                printf("[rev_pack] Warning: skipping missing baked text asset: %s\n", full_path);
                result.skipped++;
                result.optional_skipped++;
                continue;
            }
            snprintf(result.error, sizeof(result.error), "Cannot read asset: %s", full_path);
            for (int j = 0; j < loaded_count; ++j) free(loaded[j].data);
            free(loaded);
            return result;
        }
        uint32_t crc        = CRC32(data, sz);
        uint32_t cached_crc = CacheFind(cache, cache_count, refs[i].key);

        strncpy_s(loaded[loaded_count].key,      refs[i].key,  _TRUNCATE);
        strncpy_s(loaded[loaded_count].rel_path, refs[i].path, _TRUNCATE);
        KeyToIdent(refs[i].key, loaded[loaded_count].ident, sizeof(loaded[loaded_count].ident));
        loaded[loaded_count].data    = data;
        loaded[loaded_count].size    = sz;
        loaded[loaded_count].crc32   = crc;
        loaded[loaded_count].changed = (crc != cached_crc);
        loaded[loaded_count].data_owner = loaded_count;

        // Multiple cues may reference the same file, or different keys may
        // resolve to byte-identical files. Keep the keys as lookup aliases,
        // but emit and own the payload only once.
        for (int prior = 0; prior < loaded_count; ++prior) {
            if (loaded[prior].size == sz && loaded[prior].crc32 == crc &&
                memcmp(loaded[prior].data, data, sz) == 0) {
                free(data);
                loaded[loaded_count].data = loaded[prior].data;
                loaded[loaded_count].data_owner = loaded[prior].data_owner;
                break;
            }
        }
        loaded_count++;

        if (loaded[loaded_count - 1].changed) result.packed++;
        else result.skipped++;
    }

    // --- Step 4: write packed_assets.h ---
    FILE* hdr = nullptr;
    fopen_s(&hdr, output_header, "w");
    if (!hdr) {
        snprintf(result.error, sizeof(result.error), "Cannot write: %s", output_header);
        for (int i = 0; i < loaded_count; ++i) free(loaded[i].data);
        free(loaded);
        return result;
    }

    fprintf(hdr, "// packed_assets.h -- generated by rev_pack. DO NOT EDIT.\n");
    fprintf(hdr, "// format: rev_pack_v2_indexed_asset_symbols\n");
    fprintf(hdr, "#pragma once\n");
    fprintf(hdr, "#include \"rev_pack.h\"\n\n");
    fprintf(hdr, "#define HIMYM_PACKED_ASSET_FORMAT_VERSION 2\n\n");

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
        if (IsAbsolutePath(cues_path))
            strncpy_s(full_cues, cues_path, sizeof(full_cues) - 1);
        else if (workspace_root && workspace_root[0])
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

    for (int i = 0; i < loaded_count; ++i) {
        if (loaded[i].data_owner != i) continue;
        fprintf(hdr, "// %s  CRC32=0x%08X  size=%zu\n", loaded[i].key, loaded[i].crc32, loaded[i].size);
        fprintf(hdr, "static const unsigned char kAsset_%d[] = {\n", i);
        WriteHexArray(hdr, loaded[i].data, loaded[i].size);
        fprintf(hdr, "};\n\n");
    }

    fprintf(hdr, "static const rev::pack::PackedAsset kPackedAssets[] = {\n");
    if (loaded_count == 0) {
        // Empty initializer lists produce a zero-sized array in C++, which is ill-formed.
        fprintf(hdr, "    { nullptr, nullptr, 0, 0 },\n");
    } else {
        for (int i = 0; i < loaded_count; ++i) {
            fprintf(hdr, "    { \"%s\", kAsset_%d, sizeof(kAsset_%d), 0x%08Xu },\n",
                loaded[i].key, loaded[i].data_owner, loaded[i].data_owner, loaded[i].crc32);
        }
    }
    fprintf(hdr, "};\n");
    fprintf(hdr, "static const int kPackedAssetCount = %d;\n", loaded_count);
    fclose(hdr);

    // --- Step 5: update checksum cache ---
    FILE* cache_f = nullptr;
    fopen_s(&cache_f, cache_path, "w");
    if (cache_f) {
        for (int i = 0; i < loaded_count; ++i)
            fprintf(cache_f, "%s|%s|%08X\n", loaded[i].key, loaded[i].rel_path, loaded[i].crc32);
        fclose(cache_f);
    }

    for (int i = 0; i < loaded_count; ++i)
        if (loaded[i].data_owner == i) free(loaded[i].data);
    free(loaded);

    result.ok = true;
    return result;
}

}  // namespace pack
}  // namespace rev
