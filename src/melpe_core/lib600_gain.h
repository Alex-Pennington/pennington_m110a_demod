/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		lib600_gain.h
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Prototype file - Gain Encoding Library.
-------------------------------------------------------------------------- DE */

#ifndef __LIB600_GAIN_H
#define __LIB600_GAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

void GAIN_gain_quantization_s (struct melp_param *par, quant_param600 *qpar);

static void GAIN_VQ9_s (Shortword vs[], Shortword vq_s[], quant_param600 *qpar);
static void GAIN_MSVQ76_s (Shortword vs[], Shortword vq_s[], quant_param600 *qpar);
static void GAIN_MSVQ65_s (Shortword vs[], Shortword vq_s[], quant_param600 *qpar);

static void GAIN_D_VQ9_s (Shortword vs[], quant_param600 *qpar);
static void GAIN_D_MSVQ76_s (Shortword vs[], quant_param600 *qpar);
static void GAIN_D_MSVQ65_s (Shortword vs[], quant_param600 *qpar);

void GAIN_I_VQ9_s (Shortword vq_s[], quant_param600 *qpar);
void GAIN_I_MSVQ76_s (Shortword vq_s[], quant_param600 *qpar);
void GAIN_I_MSVQ65_s (Shortword vq_s[], quant_param600 *qpar);

static void GAIN_single_mbest_s (Shortword v[], Shortword g_cbk_s[], \
																int cbk_size);

static void GAIN_multi_mbest_s (Shortword vs[], Shortword g_cbk_s[], \
																int cbk_size);

static Shortword GAIN_L2_distance_s (Shortword c[], Shortword v[], \
																int nl, int nh);

static void GAIN_sorting_s (Shortword ra[], int rb1[], int rb2[], int n);

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __LIB600_GAIN_H */
