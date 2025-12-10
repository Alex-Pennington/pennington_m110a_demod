/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		var600_gain.c
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Definition of variables for gain encoding.
-------------------------------------------------------------------------- DE */

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Include Files
------------------------------------------------------------------------------*/

#include "sc1200.h"
#include "cst600.h"
#include "cst600_gain.h"

/*------------------------------------------------------------------------------
	Declaration of global variables
------------------------------------------------------------------------------*/

/* variables for multi-stage quantization ... */
Shortword gxq_s[2][MBEST_GAIN][NSUBFRAME600*NF600];

int giq[2][MBEST_GAIN*N76ST1];
int gip[2][MBEST_GAIN];

Shortword ds_gain[N9+1];

int i_gain[N9+1];

Shortword gx_s[2*NF600];

Shortword dg_s[(MBEST_GAIN*N9)+1];

int ig_best[(MBEST_GAIN*N9)+1];
int ig_mem[(MBEST_GAIN*N9)+1];

/* input concatenated gain vector ... */
Shortword g600_s[2*NF600];

/* quantized concatenated gain vector ... */
Shortword g600q_s[2*NF600];

/* pointer to selected gain codebooks ... */
Shortword *cbk_gain_s[2];

/*------------------------------------------------------------------------------
	Definition of "static" variables
------------------------------------------------------------------------------*/

/* table look-up for gain codebook selection */
Shortword	ICBKGAIN[NMODE600] = {0,0,1,1,1,2};

/* table look-up for number of quantization stages */
Shortword	NSTGGAIN[NMODE600] = {2,2,2,2,2,1};

/* table look-up for bit allocation */
Shortword	NBITS1GAIN[NMODE600] = {7,7,6,6,6,9};
Shortword	NBITS2GAIN[NMODE600] = {6,6,5,5,5,0};

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif
