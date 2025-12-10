/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		lib600_voicing.h
  VERSION:			v001
  CREATION:			07 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Prototype file - Voicing Encoding Library.
-------------------------------------------------------------------------- DE */

#ifndef __LIB600_VOICING_H
#define __LIB600_VOICING_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

void VOI_voicing_quantization_s (struct melp_param *par, Shortword *viq);

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __LIB600_VOICING_H */
