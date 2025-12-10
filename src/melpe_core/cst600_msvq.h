/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		cst600_msvq.h
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Definition of constants for LSF encoding.
-------------------------------------------------------------------------- DE */

#ifndef __CST600_MSVQ_H
#define __CST600_MSVQ_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Include Files
------------------------------------------------------------------------------*/

#include "sc1200.h"
#include "cst600.h"

/*------------------------------------------------------------------------------
	Definition of constants
------------------------------------------------------------------------------*/

/* number of lsf coefficients per subframe */
#define	NLSF600			NSUBFRAME600 * LPC_ORD

/* exponent value for distance weighting */
#define	LSF_EXP			0.3

/* size of 1st-type codebook, 1st stage */
#define	NST1			64
/* corresponding allocated bit rate */
#define	NBITST1			6
/* size of 1st-type codebook, 2nd stage */
#define	NST2			16
/* corresponding allocated bit rate */
#define	NBITST2			4
/* size of 1st-type codebook, 3rd stage */
#define	NST3			16
/* corresponding allocated bit rate */
#define	NBITST3			4
/* size of 1st-type codebook, 4th stage */
#define	NST4			16
/* corresponding allocated bit rate */
#define	NBITST4			4

/* size of 2nd-type codebook, 1st stage */
#define	NaST1			128
/* corresponding allocated bit rate */
#define	NBITaST1		7
/* size of 2nd-type codebook, 2nd stage */
#define	NaST2			32
/* corresponding allocated bit rate */
#define	NBITaST2		5
/* size of 2nd-type codebook, 3rd stage */
#define	NaST3			16
/* corresponding allocated bit rate */
#define	NBITaST3		4

/* size of 3rd-type codebook, 1st stage */
#define	NbST1			64
/* corresponding allocated bit rate */
#define	NBITbST1		6
/* size of 3rd-type codebook, 2nd stage */
#define	NbST2			32
/* corresponding allocated bit rate */
#define	NBITbST2		5
/* size of 3rd-type codebook, 3rd stage */
#define	NbST3			16
/* corresponding allocated bit rate */
#define	NBITbST3		4

/* left-normalisation of codebook lsf coefficients for 1st stage */
#define	L_SHIFT_ST1		2
/* left-normalisation of codebook lsf coefficients for other stages */
#define	L_SHIFT_STN		4

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __CST600_MSVQ_H */
