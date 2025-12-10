/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		var600_qpit.c
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Definition of variables for pitch encoding.
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

/* quantized pitch lag */
Shortword	lag0q_s[NF600+1];

/* quantized f0 */
Shortword	f0q_s[NF600+1];

/*------------------------------------------------------------------------------
	Declaration of "static" variables
------------------------------------------------------------------------------*/

/* previous pitch lag values */
Shortword	lag0q_mem_s;
Shortword	lag0q_dec_mem_s;

/* previous f0 values */
Shortword	f0q_mem_s;
Shortword	f0q_dec_mem_s;

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif
