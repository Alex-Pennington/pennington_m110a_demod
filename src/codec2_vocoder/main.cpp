/**
 * @file main.cpp
 * @brief Codec2 Vocoder CLI - Open Source Voice Codec for HF Radio
 * 
 * Codec2 by David Rowe VK5DGR - LGPL licensed
 * https://github.com/drowe67/codec2
 * 
 * Usage:
 *   codec2_vocoder -e -m 1300 input.raw output.c2    (encode)
 *   codec2_vocoder -d -m 1300 input.c2 output.raw    (decode)
 *   codec2_vocoder -l -m 1300 input.raw output.raw   (loopback)
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include "codec2.h"
}

void print_usage(const char* prog) {
    fprintf(stderr, "Codec2 Vocoder - Open Source Voice Codec for HF Radio\n");
    fprintf(stderr, "By David Rowe VK5DGR (LGPL)\n\n");
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s -e -m <mode> <input.raw> <output.c2>   Encode\n", prog);
    fprintf(stderr, "  %s -d -m <mode> <input.c2> <output.raw>   Decode\n", prog);
    fprintf(stderr, "  %s -l -m <mode> <input.raw> <output.raw>  Loopback (encode+decode)\n", prog);
    fprintf(stderr, "\nModes:\n");
    fprintf(stderr, "  3200  - 3200 bps (highest quality)\n");
    fprintf(stderr, "  2400  - 2400 bps\n");
    fprintf(stderr, "  1600  - 1600 bps\n");
    fprintf(stderr, "  1400  - 1400 bps\n");
    fprintf(stderr, "  1300  - 1300 bps (default)\n");
    fprintf(stderr, "  1200  - 1200 bps\n");
    fprintf(stderr, "  700C  - 700 bps (best for HF)\n");
    fprintf(stderr, "\nAudio format: 8000 Hz, 16-bit signed, mono (raw PCM)\n");
}

int get_mode(const char* mode_str) {
    if (strcmp(mode_str, "3200") == 0) return CODEC2_MODE_3200;
    if (strcmp(mode_str, "2400") == 0) return CODEC2_MODE_2400;
    if (strcmp(mode_str, "1600") == 0) return CODEC2_MODE_1600;
    if (strcmp(mode_str, "1400") == 0) return CODEC2_MODE_1400;
    if (strcmp(mode_str, "1300") == 0) return CODEC2_MODE_1300;
    if (strcmp(mode_str, "1200") == 0) return CODEC2_MODE_1200;
    if (strcmp(mode_str, "700C") == 0 || strcmp(mode_str, "700c") == 0) return CODEC2_MODE_700C;
    return -1;
}

const char* mode_name(int mode) {
    switch(mode) {
        case CODEC2_MODE_3200: return "3200";
        case CODEC2_MODE_2400: return "2400";
        case CODEC2_MODE_1600: return "1600";
        case CODEC2_MODE_1400: return "1400";
        case CODEC2_MODE_1300: return "1300";
        case CODEC2_MODE_1200: return "1200";
        case CODEC2_MODE_700C: return "700C";
        default: return "unknown";
    }
}

int main(int argc, char *argv[]) {
    int mode = CODEC2_MODE_1300;  // default
    char operation = 0;  // 'e', 'd', or 'l'
    const char* input_file = nullptr;
    const char* output_file = nullptr;
    
    // Parse args
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-e") == 0) {
            operation = 'e';
        } else if (strcmp(argv[i], "-d") == 0) {
            operation = 'd';
        } else if (strcmp(argv[i], "-l") == 0) {
            operation = 'l';
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            mode = get_mode(argv[++i]);
            if (mode < 0) {
                fprintf(stderr, "Error: Unknown mode '%s'\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (!input_file) {
            input_file = argv[i];
        } else if (!output_file) {
            output_file = argv[i];
        }
    }
    
    if (!operation || !input_file || !output_file) {
        print_usage(argv[0]);
        return 1;
    }
    
    // Open files
    FILE* fin = fopen(input_file, "rb");
    if (!fin) {
        fprintf(stderr, "Error: Cannot open input file '%s'\n", input_file);
        return 1;
    }
    
    FILE* fout = fopen(output_file, "wb");
    if (!fout) {
        fprintf(stderr, "Error: Cannot open output file '%s'\n", output_file);
        fclose(fin);
        return 1;
    }
    
    // Create codec
    struct CODEC2* codec2 = codec2_create(mode);
    if (!codec2) {
        fprintf(stderr, "Error: Failed to create Codec2 instance\n");
        fclose(fin);
        fclose(fout);
        return 1;
    }
    
    int nsam = codec2_samples_per_frame(codec2);
    int nbytes = codec2_bytes_per_frame(codec2);
    int nbits = codec2_bits_per_frame(codec2);
    
    fprintf(stderr, "Codec2 %s bps: %d samples/frame, %d bits/frame, %d bytes/frame\n",
            mode_name(mode), nsam, nbits, nbytes);
    
    short* speech = new short[nsam];
    unsigned char* bits = new unsigned char[nbytes];
    
    size_t frames = 0;
    size_t samples_total = 0;
    
    if (operation == 'e') {
        // Encode: raw -> c2
        while (fread(speech, sizeof(short), nsam, fin) == (size_t)nsam) {
            codec2_encode(codec2, bits, speech);
            fwrite(bits, sizeof(unsigned char), nbytes, fout);
            frames++;
            samples_total += nsam;
        }
        fprintf(stderr, "Encoded %zu frames (%zu samples, %.2f sec)\n", 
                frames, samples_total, samples_total / 8000.0);
    }
    else if (operation == 'd') {
        // Decode: c2 -> raw
        while (fread(bits, sizeof(unsigned char), nbytes, fin) == (size_t)nbytes) {
            codec2_decode(codec2, speech, bits);
            fwrite(speech, sizeof(short), nsam, fout);
            frames++;
            samples_total += nsam;
        }
        fprintf(stderr, "Decoded %zu frames (%zu samples, %.2f sec)\n",
                frames, samples_total, samples_total / 8000.0);
    }
    else if (operation == 'l') {
        // Loopback: raw -> encode -> decode -> raw
        while (fread(speech, sizeof(short), nsam, fin) == (size_t)nsam) {
            codec2_encode(codec2, bits, speech);
            codec2_decode(codec2, speech, bits);
            fwrite(speech, sizeof(short), nsam, fout);
            frames++;
            samples_total += nsam;
        }
        fprintf(stderr, "Loopback %zu frames (%zu samples, %.2f sec)\n",
                frames, samples_total, samples_total / 8000.0);
    }
    
    delete[] speech;
    delete[] bits;
    codec2_destroy(codec2);
    fclose(fin);
    fclose(fout);
    
    fprintf(stderr, "Done.\n");
    return 0;
}
