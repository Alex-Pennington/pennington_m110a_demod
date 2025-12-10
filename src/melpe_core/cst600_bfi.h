/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		cst600_bfi.h
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Definition of constants for BFI simulation.
-------------------------------------------------------------------------- DE */

#ifndef __CST600_BFI_H
#define __CST600_BFI_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Definition of constants
------------------------------------------------------------------------------*/

/* simulated frame erasure indicator */
#define	ERASURE_FLAG	FALSE

/* simulated frame erasure rate in percentage [0.0 = 0% / 1.0 = 100%] */
#define	ERASURE_RATE	(float)(0.02)

/* attenuation gain for successive frame erasure */
#define	ATT_GAIN		31129

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __CST600_BFI_H */
