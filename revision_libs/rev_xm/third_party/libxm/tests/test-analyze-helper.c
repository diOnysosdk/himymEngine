/* Fork maintainer: MikeEviscerate <cpe.bach03@proton.me>   */
/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "common.h"
#include <string.h>

/* Load module in path, then dump unmixed (2 floats per channel per frame) audio
   frames to standard output. */
static void generate_unmixed_f32ne(const char* path, bool recreate) {
    xm_prescan_data_t* p = NULL;
    xm_context_t* ctx = NULL;
    uint32_t saved_sz;
    char *saved = NULL, *buf_temp = NULL;

    float buf[4096];
    uint16_t numsamples = 0;
    uint16_t used_elems = 0;


    ctx = load_module(path);

    if(recreate) {
        saved_sz = xm_save_size(ctx);
        saved = malloc((size_t)saved_sz);
        if(saved == NULL) {
            perror("malloc");
            exit(1);
        }
        xm_save_context(ctx, saved);
        free(ctx);

        p = malloc((size_t)XM_PRESCAN_DATA_SIZE);
        if(p == NULL) {
            perror("malloc");
            exit(1);
        }
        if(!xm_prescan_module(saved, saved_sz, p)) {
            exit(1);
        }
        buf_temp = malloc((size_t)xm_size_for_context(p));
        if(buf_temp == NULL) {
            perror("malloc");
            exit(1);
        }
        ctx = xm_create_context(buf_temp, p, saved, saved_sz);
        free(p);
        free(saved);
    }

    xm_set_sample_rate(ctx, 44100);

    if(xm_get_number_of_channels(ctx) * 2 > 4096) {
        fprintf(stderr, "too many channels\n");
        exit(1);
    }
    numsamples = (uint16_t) (4096 / 2 / xm_get_number_of_channels(ctx));
    used_elems = (uint16_t) (numsamples * 2u * xm_get_number_of_channels(ctx));

    while(xm_get_loop_count(ctx) == 0) {
        xm_generate_samples_unmixed(ctx, buf, numsamples);
        if(fwrite((char*)buf, used_elems * sizeof(float), 1, stdout) != 1) {
            exit(1);
        }
    }
}

/* Compare two float32 (native-endian) bitstreams. */
static int compare_f32ne_streams(const char* path1, const char* path2) {
    float buf1[2048];
    float buf2[2048];
    float x1, x2, y1, y2, vol1, vol2, pan1, pan2;

    size_t x, y, i;
    FILE* a;    FILE* b;


    a = fopen(path1, "rb");
    b = fopen(path2, "rb");
    if(a == NULL || b == NULL) {
        return 1;
    }

    while(!feof(a) && !feof(b)) {
        x = fread(buf1, sizeof(float), 2048, a);
        y = fread(buf2, sizeof(float), 2048, b);
        if(x != y || (x & 1) || (y & 1)) return 1;
        for(i = 0; i < x; i += 2) {
            x1 = buf1[i] * buf1[i]; 
            y1 = buf1[i+1] * buf1[i+1];

            x2 = buf2[i] * buf2[i]; 
            y2 = buf2[i+1] * buf2[i+1];

            vol1 = sqrt(x1 + y1);
            vol2 = sqrt(x2 + y2);
            pan1 = y1 / (x1 + y1);
            pan2 = y2 / (x2 + y2);

            if(fabs(vol1 - vol2) > .004f || fabs(pan1 - pan2) > .004f) {
                return 1;
            }
        }
    }
    if(!feof(a) || !feof(b)) {
        return 1;
    }
    return 0;
}

int main(int argc, char** argv) {
    char* analyze_out = NULL;
    char* name = NULL;
    xm_context_t* ctx = NULL;

    if(argc < 2) {
    usage:
      name = strrchr(argv[0], '\\');
      if (name == NULL) {
        name = strrchr(argv[0], '/');
      }
      if (name != NULL) {
        name += 1;
      }
      else {
        name = argv[0];
      }
        fprintf(stderr,
                "Usage: \n"
                "\t%s analyze foo.xm\n"
                "\t%s compare <(build-generic/%s play foo.xm)"
                " <(build-analyzed/%s play foo.xm)\n",
                name, name, name, name);
        return 1;
    }

    if(!strcmp(argv[1], "analyze")) {
        if(argc != 3) goto usage;

        analyze_out = malloc((size_t)XM_ANALYZE_OUTPUT_SIZE);
        if(analyze_out == NULL) {
            perror("malloc");
            return 1;
        }
        ctx = load_module(argv[2]);
        xm_analyze(ctx, analyze_out);
        fprintf(stdout, "%s\n", analyze_out);
        return 0;
    }

    if(!strcmp(argv[1], "compare")) {
        if(argc != 4) goto usage;
        return compare_f32ne_streams(argv[2], argv[3]);
    }

    if(!strcmp(argv[1], "play") || !strcmp(argv[1], "play2")) {
        if(argc != 3) goto usage;
        generate_unmixed_f32ne(argv[2], !strcmp(argv[1], "play2"));
        return 0;
    }

    goto usage;
}
