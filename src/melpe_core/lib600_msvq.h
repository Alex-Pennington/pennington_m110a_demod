/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		lib600_msvq.h
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Prototype file - LSF MSVQ Encoding Library.
-------------------------------------------------------------------------- DE */

#ifndef __LIB600_MSVQ_H
#define __LIB600_MSVQ_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

void MSVQ_lsf_quantization_s (struct melp_param *par, quant_param600 *qpar);

void MSVQ_Dquantization_s (Shortword v[], int isubframe, quant_param600 *qpar);

void MSVQ_Iquantization_s (Shortword vq[], int isubframe, quant_param600 *qpar);

static void MSVQ_single_mbest_s (Shortword v[], int is, int isubframe);

static void MSVQ_multi_mbest_s (Shortword v[], int is, int isubframe);

static Shortword MSVQ_WL2_distance_s (Shortword c[], Shortword v[], Shortword w[], \
																int nl, int nh);

static void MSVQ_sorting_s (Shortword ra[], int rb1[], int rb2[], int n);

static void MSVQ_set_pointers (int isubframe, quant_param600 *qpar);

void MSVQ_check_weights (Shortword w[], int N);

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __LIB600_MSVQ_H */
