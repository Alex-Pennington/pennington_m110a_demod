/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		lib600_wrs.h
  VERSION:			v001
  CREATION:			07 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Prototype file - Voicing Encoding Library.
-------------------------------------------------------------------------- DE */

#ifndef __LIB600_WRS_H
#define __LIB600_WRS_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

void WRS_build_stream (quant_param600 *qpar, char *stream_bit);

static void WRS_build_mode0 (quant_param600 *qpar, char *stream_char);
static void WRS_build_mode1 (quant_param600 *qpar, char *stream_char);
static void WRS_build_mode2 (quant_param600 *qpar, char *stream_char);
static void WRS_build_mode3 (quant_param600 *qpar, char *stream_char);
static void WRS_build_mode4 (quant_param600 *qpar, char *stream_char);
static void WRS_build_mode5 (quant_param600 *qpar, char *stream_char);

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __LIB600_WRS_H */
