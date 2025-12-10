/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		var600_bfi.c
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Definition of variables for BFI simulation.
-------------------------------------------------------------------------- DE */

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Include Files
------------------------------------------------------------------------------*/

#include "sc1200.h"

/*------------------------------------------------------------------------------
	Declaration of global variables
------------------------------------------------------------------------------*/

/* bad frame indicator for MELP600 */
int		bfi600;

/* attenuation gain when bfi600 = 1 */
Shortword	att_gain;

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif
