/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		ext600_qpit.h
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Definition of external variables for pitch encoding.
-------------------------------------------------------------------------- DE */

#ifndef __EXT600_QPIT_H
#define __EXT600_QPIT_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Declaration of external variables
------------------------------------------------------------------------------*/

/* quantization tables ... */
extern Shortword		lag1_s[NPITCH_VAL1];
extern Shortword		lag2_s[NPITCH_VAL2];

extern Shortword		f01_s[NPITCH_VAL1];
extern Shortword		f02_s[NPITCH_VAL2];

/* quantized pitch lag */
extern Shortword		lag0q_s[NF600+1];

/* quantized f0 */
extern Shortword		f0q_s[NF600+1];

/* previous pitch lag values */
extern Shortword		lag0q_mem_s;
extern Shortword		lag0q_dec_mem_s;

/* previous f0 values */
extern Shortword		f0q_mem_s;
extern Shortword		f0q_dec_mem_s;

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __EXT600_QPIT_H */
