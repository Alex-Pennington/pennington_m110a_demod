/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		lib600_mode.h
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Prototype file - Encoding Mode Determination Library.
-------------------------------------------------------------------------- DE */

#ifndef __LIB600_MODE_H
#define __LIB600_MODE_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

void MODE_encoding_mode (quant_param600 *qpar);
void MODE_decoding_mode (quant_param600 *qpar);

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __LIB600_MODE_H */
