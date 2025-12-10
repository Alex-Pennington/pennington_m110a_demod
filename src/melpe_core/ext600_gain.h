/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		ext600_gain.h
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Definition of external variables for gain encoding.
-------------------------------------------------------------------------- DE */

#ifndef __EXT600_GAIN_H
#define __EXT600_GAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Declaration of external variables
------------------------------------------------------------------------------*/

/* variables for multi-stage quantization ... */
extern Shortword	gxq_s[2][MBEST_GAIN][NSUBFRAME600*NF600];

extern int			giq[2][MBEST_GAIN*N76ST1];
extern int			gip[2][MBEST_GAIN];

extern Shortword	ds_gain[N9+1];

extern int			i_gain[N9+1];

extern Shortword	gx_s[2*NF600];

extern Shortword	dg_s[(MBEST_GAIN*N9)+1];

extern int			ig_best[(MBEST_GAIN*N9)+1];
extern int			ig_mem[(MBEST_GAIN*N9)+1];

/* input concatenated gain vector ... */
extern Shortword	g600_s[2*NF600];

/* quantized concatenated gain vector ... */
extern Shortword	g600q_s[2*NF600];

/* pointer to selected gain codebooks ... */
extern Shortword	*cbk_gain_s[2];

/* 1st gain codebook, 1st stage, MSVQ(7,6) */
extern Shortword	g76st1_s[];
/* 1st gain codebook, 2nd stage, MSVQ(7,6) */
extern Shortword	g76st2_s[];

/* 2nd gain codebook, 1st stage, MSVQ(6,5) */
extern Shortword	g65st1_s[];
/* 2nd gain codebook, 2nd stage, MSVQ(6,5) */
extern Shortword	g65st2_s[];
/* 3rd gain codebook, VQ(9) */
extern Shortword	g9_s[];

/* table look-up for gain codebook selection */
extern Shortword	ICBKGAIN[NMODE600];

/* table look-up for number of quantization stages */
extern Shortword	NSTGGAIN[NMODE600];

/* table look-up for bit allocation */
extern Shortword	NBITS1GAIN[NMODE600];
extern Shortword	NBITS2GAIN[NMODE600];

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __EXT600_GAIN_H */
