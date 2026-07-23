/* Author: MikeEviscerate <cpe.bach03@proton.me> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#define _CRT_SECURE_NO_WARNINGS
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xm.h>

#define WAVE_SAMPLE_RATE 48000

struct RiffHeader {
    char ChunkID[4];
    uint32_t ChunkSize;
    char Format[4];

    struct waveFmtHeader {
        char Subchunk1ID[4];
        uint32_t Subchunk1Size;
        uint16_t AudioFormat;
        uint16_t NumChannels;
        uint32_t SampleRate;
        uint32_t ByteRate;
        uint16_t BlockAlign;
        uint16_t BitsPerSample;
    } fmt;
    struct waveDataHeader {
        char Subchunk2ID[4];
        uint32_t Subchunk2Size;
    } data;
};

void processWavHeader(struct RiffHeader * wavHeader, size_t sampleCount) {
    memcpy(wavHeader->ChunkID, "RIFF", 4);
    memcpy(wavHeader->Format, "WAVE", 4);
    memcpy(wavHeader->fmt.Subchunk1ID, "fmt ", 4);
    memcpy(wavHeader->data.Subchunk2ID, "data", 4);

    wavHeader->fmt.Subchunk1Size = 16; //for 16 bit PCM
    wavHeader->fmt.AudioFormat = 1; //for PCM
    wavHeader->fmt.NumChannels = 1; //for mono
    wavHeader->fmt.BitsPerSample = 16;


    wavHeader->fmt.SampleRate = WAVE_SAMPLE_RATE;
    wavHeader->fmt.ByteRate = WAVE_SAMPLE_RATE << 1;
    wavHeader->fmt.BlockAlign = 2;
    wavHeader->data.Subchunk2Size = sampleCount << 1;
    wavHeader->ChunkSize = (uint32_t)(sizeof(struct RiffHeader) + (wavHeader->data.Subchunk2Size) - 8);
}



int main(int argc, char** argv) {
    /* no real way to find the exact duration, doing what libxmtoau does */
    FILE * fptr = NULL;
    struct RiffHeader wavHeader = {0};

    char * xm_data = NULL;
    char* ctx_buf = NULL;
    unsigned int xm_length = 0;
    unsigned int sampleCount = 0;
    short i = 0;

    float float_buffer[30];
    short short_buffer[30];
    char * name = NULL;

    xm_prescan_data_t* p;
    xm_context_t* ctx;

    int PCMsample;

    if (argc != 3) {
        name = strrchr(argv[0], '\\');
        if (name == NULL) {
            name = strrchr(argv[0], '/');
        }
        if (name == NULL) {
            name = " xmwave";
        }

        fprintf(stderr, "Usage: %s path\\to\\file.xm  path\\to\\output.wav\n", name + 1);
        return 1;
    }



    fptr = fopen(argv[1], "rb");
    if (fptr == NULL) {
        perror("fopen");
        return 1;
    }

    if (fseek(fptr, 0, SEEK_END)) {
        perror("fseek");
        return 1;
    }

    xm_length = ftell(fptr);
    if (xm_length == -1) {
        perror("ftell");
        return 1;
    }

    if (fseek(fptr, 0, SEEK_SET)) {
        perror("fseek");
        return 1;
    }

    xm_data = malloc(xm_length);
    if (xm_data == NULL) {
        perror("malloc");
        return 1;
    }

    if (!fread(xm_data, xm_length, 1, fptr)) {
        perror("fread");
        return 1;
    }
    fclose(fptr);

    p = malloc(XM_PRESCAN_DATA_SIZE);
    if (p == NULL) {
        perror("malloc");
        return 1;
    }

    if (!xm_prescan_module(xm_data, xm_length, p)) {
        fprintf(stderr, "xm_prescan_module() failed\n");
        return 1;
    }

    ctx_buf = malloc(xm_size_for_context(p));
    if (ctx_buf == NULL) {
        perror("malloc");
        return 1;
    }


    ctx = xm_create_context(ctx_buf, p, xm_data, xm_length);
    free(xm_data);
    free(p);
    
    xm_set_sample_rate(ctx, WAVE_SAMPLE_RATE);


    fptr = fopen(argv[2], "wb");
    if (fptr == NULL) {
        perror("fopen");
        return 1;
    }
    
    if (fwrite(&wavHeader, sizeof(struct RiffHeader), 1, fptr) < 1) {
        perror("fwrite");
        return 1;
    }

    while(!xm_get_loop_count(ctx)) {
        xm_generate_samples_noninterleaved(ctx, float_buffer, float_buffer, 30);
        for (i = 0; i < 30; i++) {
            /* clamping to avoid clipping or glitches on Win98 fast math */
            PCMsample = (float_buffer[i] * 32768);
            if (PCMsample > 32767) {PCMsample = 32767;}
            else if (PCMsample < -32768) {PCMsample = -32768;}

            short_buffer[i] = (short)PCMsample;
        }

        if(fwrite(short_buffer, sizeof(short), 30, fptr) < 30) {
            perror("fwrite");
            return 1;
        }
        sampleCount += 30;
    }


    if (fseek(fptr, 0, SEEK_SET)) {
        perror("fseek");
        return 1;
    }

    processWavHeader(&wavHeader, sampleCount);

    if (fwrite(&wavHeader, sizeof(struct RiffHeader), 1, fptr) < 1) {
        perror("fwrite");
        return 1;
    }
    fclose(fptr);
    
    return 0;
}
