/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		lib600_srt.h
  VERSION:			v001
  CREATION:			21 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Prototype file, Sorting Library.
-------------------------------------------------------------------------- DE */

#ifndef __LIB600_SRT_H
#define __LIB600_SRT_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

void SRT_fsort (float ra[], int rb[], int n);

void SRT_ssort (short ra[], int rb[], int n);

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __LIB600_SRT_H */
