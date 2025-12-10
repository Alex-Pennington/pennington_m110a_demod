/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		var600_msvq.c
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Definition of variables for LSF encoding.
-------------------------------------------------------------------------- DE */

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Include Files
------------------------------------------------------------------------------*/

#include "sc1200.h"
#include "cst600.h"
#include "cst600_msvq.h"

/*------------------------------------------------------------------------------
	Declaration of global variables
------------------------------------------------------------------------------*/

/* lsf codebook size for each stage ... */
Shortword size_st[NSTAGEMAX];

/* pointers to selected LSF codebooks ... */
Shortword *cbk_mst1_s;
Shortword *cbk_st_s[NSTAGEMAX];

/* variables for multi-stage quantization ... */
Shortword xq_s[NSTAGEMAX][MBEST_LSF][NLSF600];

int iq[NSUBFRAME600][NSTAGEMAX][MBEST_LSF];
int ip[NSUBFRAME600][NSTAGEMAX][MBEST_LSF];

/* lsf weighting coefficients ... */
Shortword w_lsf_s[NLSF600];

/* concatenated LSF coefficients ... */
Shortword lsf600_s[NLSF600];
Shortword lsf600q_s[NLSF600];

/*------------------------------------------------------------------------------
	Declaration of "static" variables
------------------------------------------------------------------------------*/

/* table look-up for lsf codebook selection ... */
Shortword ICBK1LSF[NMODE600][NMODE600] = {{0,1,1,1,1,1}, {0,1,1,1,1,1}, \
											{0,1,1,1,1,1}, {0,1,1,0,0,0}, \
											{0,1,1,0,0,0}, {0,1,1,0,0,0} };

/* table look-up for lsf codebook selection ... */
Shortword ICBK2LSF[NMODE600][NMODE600] = {{0,0,0,0,0,0}, {1,1,1,1,1,1}, \
											{1,1,1,1,1,1}, {1,1,1,0,0,0}, \
											{1,1,1,0,0,0}, {1,1,1,0,0,0} };

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif
