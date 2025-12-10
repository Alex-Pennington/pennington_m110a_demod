/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		ext600_bfi.h
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Definition of external variables for BFI simulation.
-------------------------------------------------------------------------- DE */

#ifndef __EXT600_BFI_H
#define __EXT600_BFI_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Declaration of external variables
------------------------------------------------------------------------------*/

/* bad frame indicator for MELP600 */
extern int		bfi600;

/* attenuation gain when bfi600 = 1 */
extern Shortword	att_gain;

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __EXT600_BFI_H */
