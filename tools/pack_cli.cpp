#include "rev_pack.h"

#include <cstdio>

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::printf("Usage: pack_cli <cues_path> <output_header> <cache_path> [workspace_root]\n");
        return 2;
    }

    const char* cues_path = argv[1];
    const char* output_header = argv[2];
    const char* cache_path = argv[3];
    const char* workspace_root = (argc >= 5) ? argv[4] : ".";

    rev::pack::PackResult result = rev::pack::PackAssets(
        cues_path,
        output_header,
        cache_path,
        workspace_root
    );

    if (!result.ok) {
        std::printf("Pack failed: %s\n", result.error);
        return 1;
    }

    std::printf("Pack ok: total=%d packed=%d skipped=%d optional_skipped=%d\n",
                result.total, result.packed, result.skipped, result.optional_skipped);
    return 0;
}
