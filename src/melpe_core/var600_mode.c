/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		var600_mode.c
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Definition of variables for encoding mode determination.
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
	Declaration of "static" variables
------------------------------------------------------------------------------*/

/* table look-up for encoding mode determination */
Shortword	MODE600[NMODE600][NMODE600] = {	{0,1,1,3,3,3}, {1,2,2,4,4,4}, \
											{1,2,2,4,4,4}, {3,4,4,5,5,5}, \
											{3,4,4,5,5,5}, {3,4,4,5,5,5} };

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif
