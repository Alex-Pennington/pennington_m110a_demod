/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILENAME:			lib600_qpit.h
  VERSION:			v001
  CREATION:			September, 10th, 2003.
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Prototype file - Pitch Encoding Library.
-------------------------------------------------------------------------- DE */

#ifndef __LIB600_QPIT_H
#define __LIB600_QPIT_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

static void QPIT_encoding_init (void);
static void QPIT_decoding_init (void);

void QPIT_encoding_s (struct melp_param *par, quant_param600 *qpar);
void QPIT_decoding_s (struct melp_param *par, quant_param600 *qpar);

static void QPIT_encoding_mode0_s (struct melp_param *par, quant_param600 *qpar);
static void QPIT_encoding_mode1_s (struct melp_param *par, quant_param600 *qpar);
static void QPIT_encoding_mode2_s (struct melp_param *par, quant_param600 *qpar);

static void QPIT_decoding_mode0_s (struct melp_param *par, quant_param600 *qpar);
static void QPIT_decoding_mode1_s (struct melp_param *par, quant_param600 *qpar);
static void QPIT_decoding_mode2_s (struct melp_param *par, quant_param600 *qpar);

static void QPIT_lag_quantization_s (Shortword lag, Shortword cbk_lag[], \
													int Nval, Shortword *indice);

static void QPIT_f0_quantization_s (Shortword f0, Shortword cbk_f0[], \
													int Nval, Shortword *indice);

static void QPIT_direct_path_optimization_s (Shortword f0_traj_s[], \
					Shortword f0_grid_s[], int Ngrid, Shortword *error_min, \
													int *iq, int *lq, int *tq);

static void QPIT_first_type_optimization_s (Shortword f0_traj_s[], \
					Shortword f0_grid_s[], int Ngrid, Shortword *error_min, \
													int *iq, int *lq, int *tq);

static void QPIT_second_type_optimization_s (Shortword f0_traj_s[], \
					Shortword f0_grid_s[], int Ngrid, Shortword *error_min, \
													int *iq, int *lq, int *tq);

static void QPIT_constant_path_optimization_s (Shortword f0_traj_s[], \
					Shortword f0_grid_s[], int Ngrid, Shortword *error_min, \
													int *iq, int *lq, int *tq);

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __LIB600_QPIT_H */
