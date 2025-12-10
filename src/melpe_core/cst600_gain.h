/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		cst600_gain.h
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Definition of constants for gain encoding.
-------------------------------------------------------------------------- DE */

#ifndef __CST600_GAIN_H
#define __CST600_GAIN_H

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

/* number of gains per super-frame */
#define	NGAIN		NUM_GAINFR * NF600

/* number of m-best candidates for MSVQ */
#define	MBEST_GAIN	8

/* size of 1st codebook, 1st stage */
#define	N76ST1		128
/* corresponding allocated bit rate */
#define	NBIT76ST1	7

/* size of 1st codebook, 2nd stage */
#define	N76ST2		64
/* corresponding allocated bit rate */
#define	NBIT76ST2	6

/* size of 2nd codebook, 1st stage */
#define	N65ST1		64
/* corresponding allocated bit rate */
#define	NBIT65ST1	6

/* size of 2nd codebook, 2nd stage */
#define	N65ST2		32
/* corresponding allocated bit rate */
#define	NBIT65ST2	5

/* size of 3rd codebook */
#define	N9			512
/* size of 3rd codebook */
#define NBIT9		9

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __CST600_GAIN_H */
