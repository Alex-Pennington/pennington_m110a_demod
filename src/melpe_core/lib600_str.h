/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILENAME:			lib600_str.h - prototype file.
  VERSION:			version 1
  CREATION:			September 12th, 2002.
  AUTHOR:			F.Capman
  LANGUAGE:			C
  PURPOSE:			Codec Bit Stream Handling library.
  PURPOSE:			Prototype file - Bit-Stream handling Library.
-------------------------------------------------------------------------- DE */

#ifndef __LIB600_STR_H
#define __LIB600_STR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stream600.h"

/*------------------------------------------------------------------------------
	Prototypes of public functions
------------------------------------------------------------------------------*/

void STR_put_val (short x, int nbits, char bitstream[]);

short STR_get_val (int nbits, char bitstream[]);

void STR_bitstream_decompression (char stream_in[], char stream_out[], int Nbits);

void STR_bitstream_compression (char stream_in[], char stream_out[], int Nbits);

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __LIB600_STR_H */
