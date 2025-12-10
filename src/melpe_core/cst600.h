/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		cst600.h
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Definition of constants for MELP 600 bit/sec.
-------------------------------------------------------------------------- DE */

#ifndef __CST600_H
#define __CST600_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Definition of constants
------------------------------------------------------------------------------*/

#define FL				(float)

/* MELP600 bit-rate in bit/sec */
#define RATE600				600

/* size of MELP600 super-frame */
#define NF600				4

/* number of consecutive frames in MELP600 quantization scheme */
#define	NSUBFRAME600		2

/* size of MELP600 super-frame in samples */
#define BLOCK600			(NF600 * FRAME)

#define TRACK_NUM_600		11

#define NODE600				10

/* number of bits in MELP600 bit-stream */
#define NBITS600			54

/* number of bytes used to store MELP600 bit-stream */
#define NBYTES600			7

/* number of encoding modes for MELP600 */
#define NMODE600			6

/* maximum number for stages for LSF quantization */
#define	NSTAGEMAX		4

/* number of m-best candidates for MSVQ */
#define	MBEST_LSF		8

/* MELP600 post-filter constants */
#define ALPH600_Q15			11468				/* 0.35 in Q15 */
#define BETA600_Q15			26214				/* 0.80 in Q15 */

/* MODIFICATIONS version 4,5  vers version 6 : 0.25 ---> 0.50 */
#define MU600_Q15			16384				/* 0.50 in Q15 */

/* Pitch refinement indicator */
#define REFINEMENT600		TRUE

/* CPU Measure activation flag - disable for portable builds */
#ifdef _WIN32
#define CPU_MEASURE			FALSE
#else
#define CPU_MEASURE			FALSE
#endif

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __CST600_H */
