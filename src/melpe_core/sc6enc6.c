/* ================================================================== */
/*                                                                    */
/*    Microsoft Speech coder     ANSI-C Source Code                   */
/*    1200/2400 bps MELPe speech coder                                */
/*    Fixed Point Implementation      Version 8.0                     */
/*    Copyright (C) 2000-2001, Microsoft Corp.                        */
/*    All rights reserved.                                            */
/*                                                                    */
/* ================================================================== */

/* ================================================================== */
/*                                                                    */
/*    Thales Communications Modified Speech coder ANSI-C Source Code  */
/*    600/1200/2400 bps MELPe speech coder                            */
/*    Fixed Point Implementation      Version 8.3                     */
/*    Copyright (C) 2003-2004, Thales Communications.                 */
/*    All rights reserved.                                            */
/*                                                                    */
/* ================================================================== */

/* ========================================================================== */
/* sc6enc4.c: Enhanced Mixed Excitation LPC speech coder                      */
/* ========================================================================== */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
//#include <math.h>

#include "sc1200.h"
#include "mat_lib.h"
#include "global.h"
#include "macro.h"
#include "mathhalf.h"
#include "dsp_sub.h"
#include "melp_sub.h"
#include "constant.h"
#include "math_lib.h"
#include "transcode.h"
#include "npp.h"

/*==============================================================================
	Added included files for MELP 600 bit/sec
==============================================================================*/

#include "sc600.h"
#include "lib600_transc.h"

/* for processing time evaluation - Windows only */
#ifdef _WIN32
#include <windows.h>
#include <winbase.h>
LARGE_INTEGER	freq,start,end,duration;
#endif
#include <time.h>

/* ========================================================================== */

#define X05_Q7              64         /* 0.5 * (1 << 7) */
#define THREE_Q7            384        /* 3 * (1 << 7) */

/* === External memory ====================================================== */

Shortword   mode;
extern Shortword chwordsize;  /* Defined in global.c */

/*==============================================================================
	Added declaration for MELP 600 bit/sec
==============================================================================*/

/* 600bps globals defined in global.c */
extern struct melp_param   melp_par_600[NF600];

extern quant_param600   quant_par600;

extern Shortword hpspeech600[IN_BEG + BLOCK600];

extern char chbuf600[NBYTES600];

/* === Static definations =================================================== */

#define PROGRAM_NAME			"SC600 600 bps speech coder"
#define PROGRAM_VERSION 		"Version 8.3 - Fixed-Point"
#define PROGRAM_DATE			"January 2005"

/* === Static Variables ===================================================== */

char in_name[100], out_name[100];

/* === Local Private Prototypes ============================================= */

static void	parseCommandLine (int argc, char *argv[]);
static void	printHelpMessage (char *argv[]);

/* === ARCON - Globals for new swtches ====================================== */

int	NoNpp = FALSE;			/* Bypass for Noise Preprocessor */
int	NoPost = FALSE;			/* Bypass for post filter */
int	bit_density = 54;		/* indicates bit density */
int	quiet = 0;				/* Disable "Frame = x" printout */

// additional local prototypes for packed channel bit density

int readBits (unsigned char *chbuf, int bitBufSize, int bitNum, FILE *fp);
void writeBits (unsigned char *chbuf, int bitBufSize, int bitNum, FILE *fp);
void flushBuf (FILE *fp);
void insertBits (unsigned char *chBuf, int *bufIndex, unsigned char data, int bitNum);
void shiftBits (unsigned char *chbuf, int *bufIndex, int bitNum);

/****************************************************************************
**
** Function:        main
**
** Description:     The main function of the speech coder
**
** Arguments:
**
**  int     argc    ---- number of command line parameters
**  char    *argv[] ---- command line parameters
**
** Return value:    None
**
*****************************************************************************/
int sc6enc6 (int argc, char *argv[])
{
	/* ====================================================================== */
	
	FILE		*fp_in, *fp_out;
	Longword	length;
	Shortword	bitBufSize, bitBufSize6, bitBufSize12, bitBufSize24;
	Boolean		eof_reached = FALSE;

/*=============================================================================
	Added declaration for MELP 600 bit/sec
=============================================================================*/

	Shortword	speech_in_600[BLOCK600];
	Shortword	speech_out_600[BLOCK600];

/*===========================================================================*/

    /* === Get input parameters from command line =========================== */
	parseCommandLine(argc, argv);

    /* === Open input, output, and parameter files ========================== */
	if ((fp_in = fopen(in_name,"rb")) == NULL){
		fprintf(stderr, "  ERROR: cannot read file %s.\n", in_name);
		exit(1);
	}
	
	if ((fp_out = fopen(out_name,"wb")) == NULL){
		fprintf(stderr, "  ERROR: cannot write file %s.\n", out_name);
		exit(1);
	}

	/* === Initialize MELP analysis and synthesis =========================== */

	if (rate == RATE2400)
		frameSize = (Shortword)(FRAME);

	if (rate == RATE1200)
		frameSize = (Shortword)(BLOCK);

	if (rate == RATE600)
		frameSize = (Shortword)(BLOCK600);
	
	/* Computing bit=Num = rate * frameSize / FSAMP.                          */
	/* Note that bitNum computes the number of bytes written to the channel   */
	/* and it has to be exact. We first carry out the division and then have  */
	/* the multiplication with rounding.                                      */
	
	bitNum12 = 54;
	bitNum12 = 81;
	bitNum24 = 54;

	if (chwordsize == 8)
	{
		/* bitBufSize6  = 7; */
		bitBufSize12 = 11;
		bitBufSize24 = 7;
	}
	else if (chwordsize == 6)
	{
		/* bitBufSize6  = 9; */
		bitBufSize12 = 14;
		bitBufSize24 = 9;
	}
	else
	{
		fprintf (stderr, "Channel word size is wrong!\n");
		exit (-1);
	}

	bitBufSize6 = NBYTES600;

	if (rate == RATE2400)
	{
		frameSize = FRAME;
		bitBufSize = bitBufSize24;
	}

	if (rate == RATE1200)
	{
		frameSize = BLOCK;
		bitBufSize = bitBufSize12;
	}

	if (rate == RATE600)
	{
		frameSize = BLOCK600;
		bitBufSize = bitBufSize6;
	}

	if (mode != SYNTHESIS)
		melp_ana_init();
	
	if (mode != ANALYSIS)
		melp_syn_init();
	
	/* --- Timer Initialisation --------------------------------------------- */

	if (CPU_MEASURE == TRUE)
	{
		QueryPerformanceFrequency(&freq);
		freq.QuadPart=freq.QuadPart/1000;
		duration.QuadPart=0;
	}

	/* --- Start Duration Measure ------------------------------------------- */
	
	if (CPU_MEASURE == TRUE)
	{
		duration.QuadPart=0;
		QueryPerformanceCounter(&start);
	}

	/* === Run MELP coder on input signal =================================== */

	frame_count = 0;
	eof_reached = FALSE;
	
	/* --- Main Processing Loop --------------------------------------------- */
	while (!eof_reached)
	{
//ARCON check to supress fprintf
		if (!quiet)
			fprintf (stderr, "**************** Frame = %ld\r", frame_count);


		/* === DOWN TRANSCODING from 2400 bit/sec to 600 bit/sec ============ */

		if (mode == DOWN_TRANS)
		{
			/* --- Read at 2400 bit/sec the input channel. ------------------ */
//ARCON additional check for packed bit density
			
			if (bit_density == 56)
			{
				if (rate == RATE1200)
				{
					length = readBits (chbuf, bitBufSize24, bitNum24, fp_in);
					length += readBits (&chbuf[bitBufSize24], bitBufSize24, bitNum24, fp_in);
					length += readBits (&chbuf[bitBufSize24*2], bitBufSize24, bitNum24, fp_in);
				}

				if (rate == RATE600)
				{
					length = readBits (chbuf, bitBufSize24, bitNum24, fp_in);
					length += readBits (&chbuf[bitBufSize24], bitBufSize24, bitNum24, fp_in);
					length += readBits (&chbuf[bitBufSize24*2], bitBufSize24, bitNum24, fp_in);
					length += readBits (&chbuf[bitBufSize24*3], bitBufSize24, bitNum24, fp_in);
				}
			}
			else
			{
				if (rate == RATE1200)
				{
					if (chwordsize == 8)
					{
						length = fread (chbuf, sizeof(unsigned char), \
												(bitBufSize24 * NF), fp_in);
					}
					else
					{
						int		i, readNum;
						unsigned int	bitData;
					
						for (i = 0; i < bitBufSize24 * NF; i++)
						{
							readNum = fread (&bitData, sizeof (unsigned int),\
																	1, fp_in);	
							if (readNum != 1)
								break;
						
							chbuf[i] = (unsigned char)(bitData);
						}
					
						length = i;
					}
				}

				if (rate == RATE600)
				{
					if (chwordsize == 8)
					{
						length = fread (chbuf, sizeof(unsigned char), \
												(bitBufSize24 * NF600), fp_in);
					}
					else
					{
						int		i, readNum;
						unsigned int	bitData;
					
						for (i = 0; i < bitBufSize24 * NF600; i++)
						{
							readNum = fread (&bitData, sizeof (unsigned int),\
																	1, fp_in);	
							if (readNum != 1)
								break;
						
							chbuf[i] = (unsigned char)(bitData);
						}
					
						length = i;
					}
				}
			}
			
			if ((length < (bitBufSize24 * NF)) && (rate == RATE1200))
			{
				eof_reached = TRUE;
				break;
			}

			if ((length < (bitBufSize24 * NF600)) && (rate == RATE600))
			{
				eof_reached = TRUE;
				break;
			}

			/* --- Transcoding from MELP 2400 to MELP 1200 ------------------ */

			if (rate == RATE1200)
			{
				transcode_down ();
			}

			/* --- Transcoding from MELP 2400 to MELP 600 ------------------- */
			
			if (rate == RATE600)
			{
				TRSC_transcode_24to6_s (melp_par_600, &quant_par600, chbuf600);
			}

			/* --- Write at 1200 bit/sec to the output channel -------------- */
// ARCON An additional if for bit density
			
			if (rate == RATE1200)
			{
				if (bit_density == 56)
					writeBits (chbuf, bitBufSize12, bitNum12, fp_out);
				else
				{
					if (chwordsize == 8)
					{
						fwrite (chbuf, sizeof (unsigned char), bitBufSize12,\
																	fp_out);
					}
					else
					{
						int		i;
						unsigned int	bitData;
					
						for (i = 0; i < bitBufSize12; i++)
						{
							bitData = (unsigned int)(chbuf[i]);
							fwrite (&bitData, sizeof (unsigned int), 1, fp_out);
						}
					}
				}
			}

			/* --- Write at 600 bit/sec to the output channel --------------- */

			if (rate == RATE600)
			{				
				fwrite (chbuf600, sizeof(char), bitBufSize6, fp_out);
			}
		}

		/* === UP TRANSCODING from 600 bit/sec to 2400 bit/sec ============== */

		else if (mode == UP_TRANS)
		{
			/* --- Read at 1200 bit/sec the input channel ------------------- */
// ARCON An additional if for bit density
			
			if (rate == RATE1200)
			{
				if (bit_density == 56)
				{
					length = readBits (chbuf, bitBufSize12, bitNum12, fp_in);
				}
				else
				{
					if (chwordsize == 8)
					{
						length = fread (chbuf, sizeof (unsigned char), \
														bitBufSize12, fp_in);
					}
					else
					{
						int		i, readNum;
						unsigned int	bitData;
					
						for (i = 0; i < bitBufSize12; i++)
						{
							readNum = fread (&bitData, sizeof (unsigned int), \
																	1, fp_in);
							if (readNum != 1)
								break;
						
							chbuf[i] = (unsigned char)(bitData);
						}
					
						length = i;
					}
				}
			
				if (length < bitBufSize12)
				{
					eof_reached = TRUE;
					break;
				}
			}

			/* --- Read at 600 bit/sec the input channel -------------------- */

            if (rate == RATE600)
			{
				int		readNum;

				readNum = fread (chbuf600, sizeof(char), bitBufSize6, fp_in);

				if (readNum != bitBufSize6)
				{
					eof_reached = TRUE;
					break;
				}
			}
			
			/* --- Transcoding from MELP 1200 to MELP 2400 ------------------ */

			if (rate == RATE1200)
			{
				transcode_up ();
			
				if (bit_density == 56)
				{
					writeBits (chbuf, bitBufSize24, bitNum24, fp_out);
					writeBits (&chbuf[bitBufSize24], bitBufSize24, bitNum24, fp_out);
					writeBits (&chbuf[bitBufSize24*2], bitBufSize24, bitNum24, fp_out);
				}
				else
				{
					if (chwordsize == 8)
					{
						fwrite (chbuf, sizeof(unsigned char), \
												(bitBufSize24 * NF), fp_out);
					}
					else
					{
						int		i;
						unsigned int	bitData;
					
						for (i = 0; i < bitBufSize24 * NF; i++)
						{
							bitData = (unsigned int)(chbuf[i]);
							fwrite (&bitData, sizeof (unsigned int), 1, fp_out);
						}
					}
				}
			}

			/* --- Transcoding from MELP 600 to MELP 2400 ------------------- */
			
			if (rate == RATE600)
			{
				TRSC_transcode_6to24_s (chbuf600, melp_par_600);

				if (bit_density == 56)
				{
					writeBits (chbuf, bitBufSize24, bitNum24, fp_out);
					writeBits (&chbuf[bitBufSize24], bitBufSize24, bitNum24, fp_out);
					writeBits (&chbuf[bitBufSize24*2], bitBufSize24, bitNum24, fp_out);
					writeBits (&chbuf[bitBufSize24*3], bitBufSize24, bitNum24, fp_out);
				}
				else
				{
					if (chwordsize == 8)
					{
						fwrite (chbuf, sizeof(unsigned char), \
												(bitBufSize24 * NF600), fp_out);
					}
					else
					{
						int		i;
						unsigned int	bitData;
					
						for (i = 0; i < bitBufSize24 * NF600; i++)
						{
							bitData = (unsigned int)(chbuf[i]);
							fwrite (&bitData, sizeof (unsigned int), 1, fp_out);
						}
					}
				}
			}
		}

		/* === Performs MELP ANALYSIS at 600 bit/sec ======================== */

		else
		{
			if (mode != SYNTHESIS)
			{
				/* --- read input speech ------------------------------------ */
				
				length = readbl (speech_in_600, fp_in, frameSize);
				
				if (length < frameSize)
				{
					v_zap (&speech_in_600[length], (Shortword)(FRAME - length));
					eof_reached = TRUE;
				}

				/* --- Noise Pre-Processor ---------------------------------- */
//ARCON Bypass the Noise Preprocessor if the "-p" arg is used
				if (!NoNpp)
				{
					if (rate == RATE2400)
					{
						npp (speech_in_600, speech_in_600);
					}

					if (rate == RATE1200)
					{
						npp (speech_in_600, speech_in_600);
						npp (&(speech_in_600[FRAME]), &(speech_in_600[FRAME]));
						npp (&(speech_in_600[2*FRAME]), &(speech_in_600[2*FRAME]));
					}

					if (rate == RATE600)
					{
						npp (speech_in_600, speech_in_600);
						npp (&(speech_in_600[FRAME]), &(speech_in_600[FRAME]));
						npp (&(speech_in_600[2*FRAME]), &(speech_in_600[2*FRAME]));
						npp (&(speech_in_600[3*FRAME]), &(speech_in_600[3*FRAME]));
					}
				}
				
				/* --- Run MELP analyzer ------------------------------------ */

				analysis (speech_in_600, melp_par_600);

				/* --- Write channel output if needed ----------------------- */

				if (mode == ANALYSIS)
				{
// ARCON An additional if for bit density
					if (rate == RATE600)
					{				
						fwrite (chbuf600, sizeof(char), bitBufSize6, fp_out);
					}
					else
					{
						if (bit_density == 56)
						{
							if (rate == RATE2400)
							{
								writeBits (chbuf, bitBufSize24, bitNum24, fp_out);
							}
							else
							{
								writeBits (chbuf, bitBufSize12, bitNum12, fp_out);
							}
						}
						else
						{
							if (chwordsize == 8)
							{
								fwrite (chbuf, sizeof(unsigned char), \
															bitBufSize,	fp_out);
							}
							else
							{
								int		i;
								unsigned int	bitData;
							
								for (i = 0; i < bitBufSize; i++)
								{
									bitData = (unsigned int)(chbuf[i]);
									fwrite (&bitData, sizeof(unsigned int), 1, \
																	fp_out);
								}
							}
						}
					}
				}
			}

			/* === Performs MELP SYNTHESIS (skip first frame) at 600 bit/sec. */
			
			if (mode != ANALYSIS)
			{
				/* --- Read channel input if needed ------------------------- */
				
				if (mode == SYNTHESIS)
				{
					if (rate == RATE600)
					{
						int		readNum;

                        readNum = fread (chbuf600, sizeof(char), bitBufSize6, fp_in);

						if (readNum != bitBufSize6)
						{
							eof_reached = TRUE;
							break;
						}
					}
					else
					{
// ARCON An additional if for bit density
						if (bit_density == 56)
						{
							if (rate == RATE2400)
							{
								length = readBits (chbuf, bitBufSize24, \
															bitNum24, fp_in);
							}
							if (rate == RATE1200)
							{
								length = readBits (chbuf, bitBufSize12, \
															bitNum12, fp_in);
							}
						}
						else
						{
							if (chwordsize == 8)
							{
								length = fread (chbuf, sizeof (unsigned char),\
															bitBufSize, fp_in);
							}
							else
							{
								int		i, readNum;
								unsigned int	bitData;
							
								for (i = 0; i < bitBufSize; i++)
								{
									readNum = fread (&bitData, \
											sizeof(unsigned int), 1, fp_in);
								
									if (readNum != 1)
										break;
								
									chbuf[i] = (unsigned char)(bitData);
								}
							
								length = i;
							}
						}
					
						if (length < bitBufSize)
						{
							eof_reached = TRUE;
							break;
						}
					}
				}

//ARCON added an int parameter for post filter bypass
				synthesis (melp_par_600, speech_out_600, NoPost);
				
				writebl (speech_out_600, fp_out, frameSize);
			}
		}
		
		frame_count ++;
    }//while (!eof_reached)

	/* --- Stop Duration Measure -------------------------------------------- */

	if (CPU_MEASURE == TRUE)
	{
		QueryPerformanceCounter(&end);
		duration.QuadPart+=(end.QuadPart-start.QuadPart);
		duration.QuadPart=duration.QuadPart/freq.QuadPart;
	
		fprintf (stdout, "\n");
		fprintf (stdout,"time required for codec :%ld ms\n", duration.QuadPart);
		fprintf (stdout, "\n");
	}

	/* ---------------------------------------------------------------------- */

//ARCON when using packed bits, the buffer must be flushed

	if (bit_density == 56)
		flushBuf (fp_out);

	fclose (fp_in);
	fclose (fp_out);
	
	fprintf (stderr, "\n\n");

	return (0);
}


/****************************************************************************
**
** Function:        parseCommandLine
**
** Description:     Translate command line parameters
**
** Arguments:
**
**  int     argc    ---- number of command line parameters
**  char    *argv[] ---- command line parameters
**
** Return value:    None
**
*****************************************************************************/
static void     parseCommandLine(int argc, char *argv[])
{
	int			i;
	Boolean		error_flag = FALSE;

	/* Setting default values. */
	chwordsize = 8;

	in_name[0] = '\0';
	out_name[0] = '\0';

	for (i = 1; i < argc; i++)
	{
		if ((strncmp(argv[i],"-h",2) == 0) || (strncmp(argv[i],"-help",5) == 0))
		{
			printHelpMessage(argv);
			exit(0);
		}
		else if ((strncmp(argv[i],"-q",2)) == 0)
			quiet = 1;
		else if ((strncmp(argv[i],"-i",2)) == 0)
		{
			i++;
			if (i < argc)
				strcpy(in_name, argv[i]);
			else
				error_flag = TRUE;
		}
		else if ((strncmp(argv[i],"-o",2)) == 0)
		{
			i++;
			if (i < argc)
				strcpy(out_name, argv[i]);
			else
				error_flag = TRUE;
// ARCON Renamed to use "p" for  noise preprocessor switch
		}
		else if ((strncmp(argv[i],"-p",2)) == 0)
		{
// ARCON set to bypass Noise Preprocessor
			NoNpp = TRUE;
// ARCON a "-b arg" switch to indicate channel bit density
		}
		else if ((strncmp(argv[i],"-b",2)) == 0)
		{
			i++;
			if (i < argc)
			{
				if ((strncmp(argv[i],"06",2)) == 0)
				{ 
					bit_density = 6;
					chwordsize = 6;
				}
				else if ((strncmp(argv[i],"54",2)) == 0)
				{
					bit_density = 54;
				}
				else if ((strncmp(argv[i],"56",2)) == 0)
				{
					bit_density = 56;
				}
			}
			else
				error_flag = TRUE;
		}
		else if ((strncmp(argv[i],"-r",2)) == 0)
		{
			i++;
			if (i < argc)
			{
				if ((strncmp(argv[i],"2400",4)) == 0)
				{
					rate = RATE2400;
				}
				if ((strncmp(argv[i],"1200",4)) == 0)
				{
					rate = RATE1200;
				}
				if ((strncmp(argv[i],"600",4)) == 0)
				{
					rate = RATE600;
				}
			}
			else
				error_flag = TRUE;
		}
		else if ((strncmp(argv[i],"-m",2)) == 0)
		{
			i++;
			if (i < argc)
			{
				if ((strncmp(argv[i],"C",1)) == 0)
				{
					mode = ANA_SYN;
				}
				if ((strncmp(argv[i],"A",1)) == 0)
				{
					mode = ANALYSIS;
				}
				if ((strncmp(argv[i],"S",1)) == 0)
				{
					mode = SYNTHESIS;
				}
				if ((strncmp(argv[i],"U",1)) == 0)
				{
					mode = UP_TRANS;
				}
				if ((strncmp(argv[i],"D",1)) == 0)
				{
					mode = DOWN_TRANS;
				}
			}
			else
				error_flag = TRUE;
		}
		else
			error_flag = TRUE;
	}
	
	if ((in_name[0] == '\0') || (out_name[0] == '\0'))
		error_flag = TRUE;

	if (error_flag)
	{
		printHelpMessage(argv);
		exit(1);
	}
    if (TRUE == FALSE) {
        fprintf(stderr, "\n\n\t%s %s, %s\n\n", \
                PROGRAM_NAME, PROGRAM_VERSION, PROGRAM_DATE);
        switch (mode)
        {
        case ANA_SYN:
        case ANALYSIS:
        case SYNTHESIS:		if (rate == RATE2400)
                fprintf(stderr, " ---- 2.4kbps mode.\n");
            if (rate == RATE1200)
                fprintf(stderr, " ---- 1.2kbps mode.\n");
            if (rate == RATE600)
                fprintf(stderr, " ---- 0.6kbps mode.\n");
            break;
        }

        switch (mode)
        {
        case ANA_SYN:		fprintf(stderr, " ---- Analysis and Synthesis.\n");
            break;
        case ANALYSIS:		fprintf(stderr, " ---- Analysis only.\n");
            break;
        case SYNTHESIS:		fprintf(stderr, " ---- Synthesis only.\n");
            break;
        case UP_TRANS:		fprintf(stderr, " ---- Transcoding from 0.6kbps to 2.4kbps.\n");
            break;
        case DOWN_TRANS:	fprintf(stderr, " ---- Transcoding from 2.4kbps to 0.6kbps.\n");
            break;
        }

        //ARCON Noise preprocessor bypass and bit density notifications

        if (NoNpp)
            fprintf(stderr, " ---- Noise Preprocessor is being Bypassed.\n");

        switch (bit_density)
        {
        case 6:		fprintf(stderr, " ---- CTF compatible channel bit density: 6 bits in each word \n");
            break;
        case 54:	fprintf(stderr, " ---- Default channel bit density: 54 bits in each 56 bits\n");
            break;
        case 56:	fprintf(stderr, " ---- Packed channel bit density: 56 bits in each 56 bits\n");
            break;
        }

        fprintf(stderr, " ---- input from %s.\n", in_name);
        fprintf(stderr, " ---- output to %s.\n", out_name);

    }
}

/****************************************************************************
**
** Function:        printHelpMessage
**
** Description:     Print Command Line Usage
**
** Arguments:
**
** Return value:    None
**
*****************************************************************************/
static void     printHelpMessage(char *argv[])
{
    fprintf(stderr, "\n\n\t%s %s, %s\n\n", PROGRAM_NAME, PROGRAM_VERSION,
            PROGRAM_DATE);
    fprintf(stdout, "Usage:\n");
    fprintf(stdout, "%s [-q][-p] [-b bit density] [-r rate] [-m mode] -i infile -o outfile\n\n", argv[0]);
    fprintf(stdout, "\t\t-p --Bypass the Noise Preprocessor\n");
    fprintf(stdout, "\t\t-b --Channel Data Bit Density/int\n");
    fprintf(stdout, "\t\t\t      06 = 6 bits/word/int (CTF)\n");
    fprintf(stdout, "\t\t\t      54 = 54 of each 56 bits (default)\n");
    fprintf(stdout, "\t\t\t      56 = 56 of each 56 bits (packed) \n\n");
    fprintf(stdout, "\t\t-r --Encoding Rate/int\n");
    fprintf(stdout, "\t\t\t      2400 = melp 2400 bit/sec\n");
    fprintf(stdout, "\t\t\t      1200 = melp 1200 bit/sec\n");
    fprintf(stdout, "\t\t\t       600 = melp  600 bit/sec\n");
    fprintf(stdout, "\t\t-m --Processing Mode/int\n");
    fprintf(stdout, "\t\t\t      C = analysis + synthesis\n");
    fprintf(stdout, "\t\t\t      A = analysis\n");
    fprintf(stdout, "\t\t\t      S = synthesis\n");
    fprintf(stdout, "\t\t\t      U = transcoding up to 2400\n");
    fprintf(stdout, "\t\t\t      D = transcoding down from 2400\n");
}

// ARCON added for 56 packed density

#define BITS_PER_BYTE        8

/****************************************************************************
**
** Function:        readBits
**
** Description:     Read tightly packed bits from a file
**
** Arguments:
**
** Return value:    int ---- number of bytes written to the buffer
**
*****************************************************************************/
static unsigned char rgiInputBuf[2];
static int iInputBufIndex = 0;
int readBits(unsigned char *chbuf, int bitBufSize, int bitNum, FILE *fp)
{
    char    data;
    int     length = 0, bitIndex = 0, readNum, fEndofFile;

    fEndofFile = 0;
    while( bitIndex < bitNum ){

        if( bitNum - bitIndex >= BITS_PER_BYTE ){
            /* write one byte in the chbuf */
            if( iInputBufIndex < BITS_PER_BYTE ){
                readNum = fread(&data, sizeof(char), 1, fp);
                if( readNum > 0 )
                    insertBits(rgiInputBuf, &iInputBufIndex, data, BITS_PER_BYTE);
                else
                    fEndofFile = 1;
            }
            if( iInputBufIndex > 0 ){
                chbuf[length] = rgiInputBuf[0];
                length++;
                bitIndex += BITS_PER_BYTE;
                rgiInputBuf[0] = rgiInputBuf[1];
                iInputBufIndex -= BITS_PER_BYTE;
            }
        }else{
            /* less than bits in a byte are needed */
            if( iInputBufIndex < bitNum - bitIndex ){
                readNum = fread(&data, sizeof(char), 1, fp);
                if( readNum > 0 )
                    insertBits(rgiInputBuf, &iInputBufIndex, data, BITS_PER_BYTE);
                else
                    fEndofFile = 1;
            }
            if( iInputBufIndex > 0 ){
                chbuf[length] = (unsigned char)rgiInputBuf[0] >> (BITS_PER_BYTE -(bitNum-bitIndex));
                length++;
                shiftBits(rgiInputBuf, &iInputBufIndex, bitNum - bitIndex );
                bitIndex = bitNum;
            }
        }
        if( fEndofFile == 1 ){
            if( iInputBufIndex < 0 )    iInputBufIndex = 0;
            break;
        }
    }

    return length;

}

/****************************************************************************
**
** Function:        writeBits
**
** Description:     write tightly packed bits into a file
**
** Arguments:
**
** Return value:    None
**
*****************************************************************************/
static unsigned char rgiOutputBuf[2];
static int iOutputBufIndex = 0;
void writeBits(unsigned char *chbuf, int bitBufSize, int bitNum, FILE *fp)
{
    int     bitIndex = 0, bufIndex = 0;
    unsigned char data;

    while( bitIndex < bitNum ){

        if( iOutputBufIndex >= BITS_PER_BYTE ){
            fwrite(rgiOutputBuf, sizeof(char), 1, fp);
            rgiOutputBuf[0] = rgiOutputBuf[1];
            iOutputBufIndex -= BITS_PER_BYTE;
        }

        if( bitNum - bitIndex >= BITS_PER_BYTE ){
            /* write one byte out */
            insertBits(rgiOutputBuf, &iOutputBufIndex, chbuf[bufIndex++], BITS_PER_BYTE);
            bitIndex += BITS_PER_BYTE;
        }else{
            data = chbuf[bufIndex++] << ( BITS_PER_BYTE - (bitNum-bitIndex) );
            insertBits(rgiOutputBuf, &iOutputBufIndex, data, bitNum - bitIndex);
            bitIndex = bitNum;
        }

        if( bufIndex >= bitBufSize )    break;
    }
}

/****************************************************************************
**
** Function:        flushBuf
**
** Description:     Flush the output buffer
**
** Return value:    void
**
*****************************************************************************/
void flushBuf(FILE *fp)
{
    if( iOutputBufIndex > BITS_PER_BYTE )
        fwrite(rgiOutputBuf, sizeof(char), 2, fp);
    else if( iOutputBufIndex > 0 )
        fwrite(rgiOutputBuf, sizeof(char), 1, fp);
}

/****************************************************************************
**
** Function:        shiftBits
**
** Description:     shift the bit stream buffer
**
** Arguments:
**
** Return value:    None
**
*****************************************************************************/
void shiftBits(unsigned char *chbuf, int *bufIndex, int bitNum)
{
    unsigned short iCodeword;
  
    /* ---- The output buffer is byte aligned ---- */
    iCodeword = (unsigned short)chbuf[0];
    iCodeword = (unsigned short)(iCodeword << BITS_PER_BYTE);
    iCodeword = (unsigned short)(iCodeword + (unsigned short)chbuf[1]);
    chbuf[0] = (unsigned char)(iCodeword >> (BITS_PER_BYTE - bitNum));

    if( *bufIndex - bitNum > BITS_PER_BYTE ){
        /* need to save one more byte */
        iCodeword = iCodeword << bitNum;
        chbuf[1] = (unsigned char)iCodeword;
    }

    *bufIndex -= bitNum;
}

/****************************************************************************
**
** Function:        insertBits
**
** Description:     insert bits into bitstream buffer
**
** Arguments:
**
** Return value:    None
**
*****************************************************************************/
void insertBits(unsigned char *chBuf, int *bufIndex, unsigned char data, int bitNum)
{
    int     index, shift;
    unsigned short   iSrcCodeword, iDstCodeword;

    index = 0;      /* the index for chBuf */
    shift = *bufIndex;
    if( shift > BITS_PER_BYTE ){
        index = 1;
        shift -= BITS_PER_BYTE;
    }

    if( shift == 0 ){
        chBuf[index] = data;
    }else{
        iDstCodeword = (unsigned short)chBuf[index];
        iDstCodeword = (unsigned short)((iDstCodeword >> (BITS_PER_BYTE - shift)) 
                           << (BITS_PER_BYTE - shift));
       	iSrcCodeword = (unsigned short)data;
        iSrcCodeword = (unsigned short)(iSrcCodeword >> shift);
        iDstCodeword = (unsigned short)(iSrcCodeword | iDstCodeword);
        chBuf[index++] = (unsigned char)iDstCodeword;
        if( bitNum > (BITS_PER_BYTE - shift) ){
            iSrcCodeword = (unsigned short)data;
            iDstCodeword = (unsigned short)(iSrcCodeword << (BITS_PER_BYTE - shift));
            chBuf[index] = (unsigned char)iDstCodeword;
        }
    }

    *bufIndex += bitNum;
}

