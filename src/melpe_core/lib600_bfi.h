/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		lib600_bfi.h
  VERSION:			v001
  CREATION:			07 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Bad Frame Indicator Handling Library.
-------------------------------------------------------------------------- DE */

#ifndef __LIB600_BFI_H
#define __LIB600_BFI_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

void BFI_random (int *bfi, float rate);

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __LIB600_BFI_H */
