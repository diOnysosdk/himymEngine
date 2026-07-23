/* Fork maintainer: MikeEviscerate <cpe.bach03@proton.me>    */
/* Author: Romain "Artefact2" Dal Maso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

/* modified libxmize */
#define _CRT_SECURE_NO_WARNINGS
#include <xm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* XXX: implement per-waveform zapping */
static void zero_waveforms(xm_context_t* ctx) {
	xm_sample_point_t* sample_data;
	uint32_t sample_length;
	uint16_t s = 0;

	for (s = 0; s < xm_get_number_of_samples(ctx); ++s) {
		sample_data = xm_get_sample_waveform(ctx, s, &sample_length);
		if (sample_data == NULL) {
			continue;
		}
		memset(sample_data, 0, sample_length * sizeof(xm_sample_point_t));
	}
}

int main(int argc, char** argv) {
	FILE* in;
	int i;
	uint32_t ctx_size;
	size_t in_length, sz;
	char *xmdata, *buf, *action, *output, *name = NULL;
	xm_prescan_data_t* p;
	xm_context_t* ctx;

	if (argc < 3) {
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
			"Usage: %s [--zero-all-waveforms] analyze|save|dump <in.xm|in.mod|in.s3m>\n",
			name);
		return 1;
	}

	in = fopen(argv[argc - 1], "rb"); 
	if (in == NULL) {
		perror("fopen");
		return 1;
	}

	if (fseek(in, 0, SEEK_END)) {
		perror("fseek");
		return 1;
	}

	in_length = ftell(in);
	if (in_length == -1) {
		perror("ftell");
		return 1;
	}
	if (in_length > UINT32_MAX) {
		fprintf(stderr, "input file too large\n");
		return 1;
	}

	if (fseek(in, 0, SEEK_SET)) {
		perror("fseek");
		return 1;
	}

	xmdata = malloc((size_t)in_length);
	if (xmdata == NULL) {
		perror("malloc");
		return 1;
	}

	if (!fread(xmdata, (size_t)in_length, 1, in)) {
		perror("fread");
		return 1;
	}

	p = malloc(XM_PRESCAN_DATA_SIZE);
	if (!xm_prescan_module(xmdata, (uint32_t)in_length, p)) {
		fprintf(stderr, "xm_prescan_module() failed\n");
		return 1;
	}

	ctx_size = xm_size_for_context(p);
	buf = malloc(ctx_size);
	if (buf == NULL) {
		perror("malloc");
		return 1;
	}

	ctx = xm_create_context(buf, p, xmdata, (uint32_t)in_length);
	free(xmdata);

	for (i = 1; i < argc - 2; ++i) {
		if (!strcmp("--zero-all-waveforms", argv[i])) {
			zero_waveforms(ctx);
		}
		else {
			fprintf(stderr, "unknown command-line argument: %s\n", argv[i]);
			return 1;
		}
	}

	action = argv[argc - 2];
	if (!strcmp("analyze", action)) {
		output = malloc((size_t)XM_ANALYZE_OUTPUT_SIZE);
		if (output == NULL) {
			perror("malloc");
			return 1;
		}
		xm_analyze(ctx, output);
		fprintf(stdout, "%s\n", output);
		return 0;
	}
	else if (!strcmp("save", action)) {
		sz = xm_save_size(ctx);
		output = malloc(sz);
		if (output == NULL) {
			perror("malloc");
			return 1;
		}
		xm_save_context(ctx, output);
		if (!fwrite(output, sz, 1, stdout)) {
			perror("fwrite");
			return 1;
		}
		return 0;
	}
	else if (!strcmp("dump", action)) {
		output = malloc(xm_dump_size(ctx));
		if (output == NULL) {
			perror("malloc");
			return 1;
		}
		xm_dump_context(ctx, output);
		if (!fwrite(output, ctx_size, 1, stdout)) {
			perror("fwrite");
			return 1;
		}
		return 0;
	}

	fprintf(stderr, "unknown action %s, expected analyze, save or dump\n", action);
	return 1;
}