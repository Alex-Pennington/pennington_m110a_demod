/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		var600_voicing.c
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Definition of variables for voicing encoding.
-------------------------------------------------------------------------- DE */

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Include Files
------------------------------------------------------------------------------*/

#include "sc1200.h"
#include "cst600.h"

/*------------------------------------------------------------------------------
	Declaration of global variables
------------------------------------------------------------------------------*/

/* voicing quantization index ... */
//Shortword	voicing_iq;

/* initial band-pass voicing pattern ... */
Shortword	bpvi[NF600][NUM_BANDS];

/* quantized band-pass voicing pattern ... */
Shortword	bpviq[NF600][NUM_BANDS];

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif
