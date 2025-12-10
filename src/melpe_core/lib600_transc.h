/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILENAME:			lib600_transc.h
  VERSION:			v001
  CREATION:			September, 10th, 2003.
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Prototype file - Transcoding Library.
-------------------------------------------------------------------------- DE */

#ifndef __LIB600_TRANSC_H
#define __LIB600_TRANSC_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

void TRSC_transcode_24to6_s (struct melp_param *par, quant_param600 *qpar, \
															char *stream_bit);

void TRSC_transcode_6to24_s (char *stream_bit, struct melp_param *par);

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __LIB600_TRANSC_H */
