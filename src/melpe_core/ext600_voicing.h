/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		ext600_voicing.h
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Definition of external variables for voicing encoding.
-------------------------------------------------------------------------- DE */

#ifndef __EXT600_VOICING_H
#define __EXT600_VOICING_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Declaration of external variables
------------------------------------------------------------------------------*/

/* voicing quantization index ... */
//extern Shortword		voicing_iq;

/* initial band-pass voicing pattern ... */
extern Shortword		bpvi[NF600][NUM_BANDS];

/* quantized band-pass voicing pattern ... */
extern Shortword		bpviq[NF600][NUM_BANDS];

/* weighting coefficients for voicing quantization */
extern Shortword		v_weight[];

/* voicing codebook */
extern Shortword		v_cbk[];

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __EXT600_VOICING_H */
