/* ================================================================== */
/*                                                                    */
/*    STANAG 4591 MELPe Speech Coder                                  */
/*    600/1200/2400 bps rates                                         */
/*    Standalone Command-Line Interface                               */
/*                                                                    */
/* ================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* MELPe encoder/decoder entry point */
extern int sc6enc6(int argc, char *argv[]);

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "\nSTANAG 4591 MELPe Speech Coder\n");
        fprintf(stderr, "Supports 600/1200/2400 bps rates\n\n");
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  %s [-q][-p] [-b bit_density] [-r rate] [-m mode] -i infile -o outfile\n\n", argv[0]);
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  -q           Quiet mode (suppress frame counter)\n");
        fprintf(stderr, "  -p           Bypass Noise Preprocessor\n");
        fprintf(stderr, "  -b density   Channel bit density:\n");
        fprintf(stderr, "                 6  = 6 bits/word (CTF compatible)\n");
        fprintf(stderr, "                 54 = 54 of 56 bits (default)\n");
        fprintf(stderr, "                 56 = 56 of 56 bits (packed)\n");
        fprintf(stderr, "  -r rate      Encoding rate:\n");
        fprintf(stderr, "                 2400 = MELPe 2400 bps\n");
        fprintf(stderr, "                 1200 = MELPe 1200 bps\n");
        fprintf(stderr, "                 600  = MELPe 600 bps\n");
        fprintf(stderr, "  -m mode      Processing mode:\n");
        fprintf(stderr, "                 C = Analysis + Synthesis (encode/decode)\n");
        fprintf(stderr, "                 A = Analysis only (encode)\n");
        fprintf(stderr, "                 S = Synthesis only (decode)\n");
        fprintf(stderr, "                 U = Transcode up (600->2400 or 1200->2400)\n");
        fprintf(stderr, "                 D = Transcode down (2400->600 or 2400->1200)\n");
        fprintf(stderr, "  -i infile    Input file (raw 16-bit PCM or bitstream)\n");
        fprintf(stderr, "  -o outfile   Output file (bitstream or raw 16-bit PCM)\n\n");
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  Encode at 2400 bps:  %s -r 2400 -m A -i speech.raw -o speech.mel\n", argv[0]);
        fprintf(stderr, "  Decode 2400 bps:     %s -r 2400 -m S -i speech.mel -o speech.raw\n", argv[0]);
        fprintf(stderr, "  Encode at 600 bps:   %s -r 600 -m A -i speech.raw -o speech.mel\n", argv[0]);
        fprintf(stderr, "  Full codec test:     %s -r 2400 -m C -i speech.raw -o output.raw\n", argv[0]);
        return 1;
    }

    return sc6enc6(argc, argv);
}
