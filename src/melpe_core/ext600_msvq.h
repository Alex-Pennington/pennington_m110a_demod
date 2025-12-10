/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		ext600_msvq.h
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Definition of external variables for LSF encoding.
-------------------------------------------------------------------------- DE */

#ifndef __EXT600_MSVQ_H
#define __EXT600_MSVQ_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Declaration of external variables
------------------------------------------------------------------------------*/

/* lsf codebook size for each stage ... */
extern Shortword		size_st[NSTAGEMAX];

/* pointers to selected LSF codebooks ... */
extern Shortword		*cbk_mst1_s;
extern Shortword		*cbk_st_s[NSTAGEMAX];

/* variables for multi-stage quantization ... */
extern Shortword		xq_s[NSTAGEMAX][MBEST_LSF][NLSF600];

extern int				iq[NSUBFRAME600][NSTAGEMAX][MBEST_LSF];
extern int				ip[NSUBFRAME600][NSTAGEMAX][MBEST_LSF];

/* lsf weighting coefficients ... */
extern Shortword		w_lsf_s[NLSF600];

/* concatenated LSF coefficients ... */
extern Shortword		lsf600_s[NLSF600];
extern Shortword		lsf600q_s[NLSF600];

/* Class 1, lsf codebook, MSVQ(6,4,4,4) */
extern Shortword		m1st1_s[];
extern Shortword		c1st1_s[], c1st2_s[], c1st3_s[], c1st4_s[];

/* Class 2, 1st lsf codebook, MSVQ(7,5,4) */
extern Shortword		m2ast1_s[];
extern Shortword		c2ast1_s[], c2ast2_s[], c2ast3_s[];

/* Class 2, 2nd lsf codebook, MSVQ(6,5,4) */
extern Shortword		m2bst1_s[];
extern Shortword		c2bst1_s[], c2bst2_s[], c2bst3_s[];

/* Class 3, 1st lsf codebook, MSVQ(7,5,4) */
extern Shortword		m3ast1_s[];
extern Shortword		c3ast1_s[], c3ast2_s[], c3ast3_s[];

/* Class 3, 2nd lsf codebook, MSVQ(6,5,4) */
extern Shortword		m3bst1_s[];
extern Shortword		c3bst1_s[], c3bst2_s[], c3bst3_s[];

/* Class 4, 1st lsf codebook, MSVQ(7,5,4) */
extern Shortword		m41ast1_s[], m42ast1_s[], m43ast1_s[];

extern Shortword		c41ast1_s[], c41ast2_s[], c41ast3_s[];
extern Shortword		c42ast1_s[], c42ast2_s[], c42ast3_s[];
extern Shortword		c43ast1_s[], c43ast2_s[], c43ast3_s[];

/* Class 4, 2nd lsf codebook, MSVQ(6,5,4) */
extern Shortword		m41bst1_s[], m42bst1_s[], m43bst1_s[];

extern Shortword		c41bst1_s[], c41bst2_s[], c41bst3_s[];
extern Shortword		c42bst1_s[], c42bst2_s[], c42bst3_s[];
extern Shortword		c43bst1_s[], c43bst2_s[], c43bst3_s[];

/* table look-up for lsf codebook selection */
extern Shortword		ICBK1LSF[NMODE600][NMODE600];

/* table look-up for lsf codebook selection */
extern Shortword		ICBK2LSF[NMODE600][NMODE600];

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __EXT600_MSVQ_H */
