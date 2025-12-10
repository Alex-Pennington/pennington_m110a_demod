/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		ext600_mode.h
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Definition of external variables for encoding mode
					determination.
-------------------------------------------------------------------------- DE */

#ifndef __EXT600_MODE_H
#define __EXT600_MODE_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Declaration of external variables
------------------------------------------------------------------------------*/

/* table look-up for encoding mode determination */
extern Shortword	MODE600[NMODE600][NMODE600];

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __EXT600_MODE_H */
