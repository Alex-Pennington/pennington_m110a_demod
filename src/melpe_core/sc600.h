/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		sc600.h
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Include files for MELP 600 bit/sec.
-------------------------------------------------------------------------- DE */

#ifndef __SC600_H
#define __SC600_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Include files
------------------------------------------------------------------------------*/

#include "sc1200.h"

#include "main600.h"
#include "cst600.h"

typedef struct{
	Shortword	pit[NODE600];			/* pitch for each node			*/
	Shortword	weight[NODE600];		/* time domain correlation		*/
	Shortword	cost[NODE600]; 			/* cost function				*/
} pitTrackParam600;

typedef struct{
	/* encoding/decoding mode */
	Shortword	mode600;

	/* voicing index */
	Shortword	voicing_iq;

	/* class index */
	Shortword	iclass[NSUBFRAME600];

	/* lsf codebook index */
	Shortword	icbk_lsf[NSUBFRAME600];
	/* number of stages in LSF-MSVQ */
	Shortword	nstg_lsf[NSUBFRAME600];
	/* number of bits for each stage */
	Shortword	nbits_lsf[NSUBFRAME600][NSTAGEMAX];
	/* lsf quantization index */
	Shortword	lsf_iq[NSUBFRAME600][NSTAGEMAX];

	/* pitch lag index */
	Shortword	lag0_iq;
	/* pitch lag location index */
	Shortword	lag0_lq;
	/* pitch trajectory type */
	Shortword	lag0_tq;

	/* gain codebook index */
	Shortword	icbk_gain;
	/* number of stages in GAIN-MSVQ */
	Shortword	nstg_gain;
	/* number of bits for each stage */
	Shortword	nbits_gain[NSTAGEMAX];
	/* gain quantization index */
	Shortword	gain_iq[NSUBFRAME600];

} quant_param600;

#include "cst600_bfi.h"
#include "cst600_mode.h"
#include "cst600_gain.h"
#include "cst600_msvq.h"
#include "cst600_qpit.h"
#include "cst600_voicing.h"

#include "ext600_bfi.h"
#include "ext600_mode.h"
#include "ext600_gain.h"
#include "ext600_msvq.h"
#include "ext600_qpit.h"
#include "ext600_voicing.h"

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __SC600_H */
