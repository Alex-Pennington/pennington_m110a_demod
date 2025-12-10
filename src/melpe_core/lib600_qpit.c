/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILENAME:			lib600_qpit.c
  VERSION:			v001
  CREATION:			September, 10th, 2003.
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Pitch Encoding Library.
-------------------------------------------------------------------------- DE */

/*------------------------------------------------------------------------------
	Include Files
------------------------------------------------------------------------------*/

#include "main600.h"
#include "sc600.h"
#include "math_lib.h"
#include "mathhalf.h"
#include "dsp_sub.h"
#include "global.h"
#include "lib600_qpit.h"
#include "ext600_qpit.h"

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

/* DS --------------------------------------------------------------------------
  Name:				QPIT_encoding_init
  Purpose:			this function performs pitch encoding initialization.
  Arguments:		1 - none.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void QPIT_encoding_init (void)
{
/* (CD) --- Declaration of variables for QPIT_encoding_init () -------------- */

	/* ---------------------------------------------------------------------- */

	lag0q_mem_s = lag1_s[NPITCH_VAL1/2];
	f0q_mem_s = f01_s[NPITCH_VAL1/2];

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of QPIT_encoding_init () ------------------------------------ */
}

/* DS --------------------------------------------------------------------------
  Name:				QPIT_decoding_init
  Purpose:			this function performs pitch decoding initialization.
  Arguments:		1 - none.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void QPIT_decoding_init (void)
{
/* (CD) --- Declaration of variables for QPIT_decoding_init () -------------- */

	/* ---------------------------------------------------------------------- */

	lag0q_dec_mem_s = lag1_s[NPITCH_VAL1/2];
	f0q_dec_mem_s = f01_s[NPITCH_VAL1/2];

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of QPIT_decoding_init () ------------------------------------ */
}

/* DS --------------------------------------------------------------------------
  Name:				QPIT_encoding_s
  Purpose:			this function performs pitch encoding.
					this function is called during:
						1: MELP encoding in routine analysis().
						2: MELP transcoding in routine TRSC_transcode_24to6_s().
  Arguments:		1 - (struct melp_param*) par: melp structure.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void QPIT_encoding_s (struct melp_param *par, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for QPIT_encoding_s () ----------------- */

	static int	start = 1;

	/* ---------------------------------------------------------------------- */

	if (start == 1)
	{
		start = 0;
		QPIT_encoding_init();
	}

	switch (qpar->mode600)
	{
		case 0:		QPIT_encoding_mode0_s (par, qpar);
					break;
		case 1:		QPIT_encoding_mode1_s (par, qpar);
					break;
		default:	QPIT_encoding_mode2_s (par, qpar);
					break;
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of QPIT_encoding_s () --------------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				QPIT_decoding_s
  Purpose:			this function performs pitch decoding.
					this function is called during:
						1: MELP encoding in routine analysis().
						2: MELP transcoding in routine TRSC_transcode_24to6_s().
  Arguments:		1 - (struct melp_param*) par: melp structure.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void QPIT_decoding_s (struct melp_param *par, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for QPIT_decoding_s () ----------------- */

	static int	start = 1;

	/* ---------------------------------------------------------------------- */

	if (start == 1)
	{
		start = 0;
		QPIT_decoding_init();
	}
			
	switch (qpar->mode600)
	{
		case 0:		QPIT_decoding_mode0_s (par, qpar);
					break;
		case 1:		QPIT_decoding_mode1_s (par, qpar);
					break;
		default:	QPIT_decoding_mode2_s (par, qpar);
					break;
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of QPIT_decoding_s () --------------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				QPIT_encoding_mode0_s
  Purpose:			this function performs pitch encoding in mode 0.
  Arguments:		1 - (struct melp_param*) par: melp structure.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void QPIT_encoding_mode0_s (struct melp_param *par, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for QPIT_encoding_mode0_s () ----------- */

	short		n;

	/* ---------------------------------------------------------------------- */

	for (n = 0; n < NF600; n ++)
	{
		f0q_s[n+1] = f0q_mem_s;
		lag0q_s[n+1] = lag0q_mem_s;
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of QPIT_encoding_mode0_s () --------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				QPIT_encoding_mode1_s
  Purpose:			this function performs melp encoding in mode 1.
  Arguments:		1 - (struct melp_param*) par: melp structure.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void QPIT_encoding_mode1_s (struct melp_param *par, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for QPIT_encoding_mode1_s () ----------- */

	short		n;

	/* ---------------------------------------------------------------------- */

	for (n = 0; n < NF600; n ++)
	{
		if (bpviq[n][0] == 1)
		{
			QPIT_lag_quantization_s (par[n].pitch, lag1_s, NPITCH_VAL1, \
														&qpar->lag0_iq);

			lag0q_mem_s = lag1_s[qpar->lag0_iq];
			f0q_mem_s = f01_s[qpar->lag0_iq];
		}
	}

	for (n = 0; n < NF600; n ++)
	{
		lag0q_s[n+1] = lag0q_mem_s;
		f0q_s[n+1] = f0q_mem_s;
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of QPIT_encoding_mode1_s () --------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				QPIT_encoding_mode2_s
  Purpose:			this function performs melp encoding in mode 2 and above.
  Arguments:		1 - (struct melp_param*) par: melp structure.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void QPIT_encoding_mode2_s (struct melp_param *par, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for QPIT_encoding_mode2_s () ----------- */

	short		i, n;
	short		i0_min, i0_max, Ngrid;
	short		i0_grid[NPITCH_VAL2];
	int			flag;
	int			iq[NCAND], lq[NCAND], tq[NCAND];
	Shortword	indice[NF600+1];
	Shortword	eq_min, eq[NCAND];
	Shortword	f0t_min, f0t_max;
	Shortword	f0_traj_s[NF600+1], f0_grid_s[NPITCH_VAL2];

	/* ---------------------------------------------------------------------- */

	f0_traj_s[0] = f0q_mem_s;

	for (n = 1; n <= NF600; n ++)
	{
		QPIT_lag_quantization_s (par[n-1].pitch, lag2_s, NPITCH_VAL2, &indice[n]);
		f0_traj_s[n] = f02_s[indice[n]];
	}

	/* find extrema ... */
	f0t_max = 0;
	f0t_min = (Shortword)(32767);

	for (n = 1; n <= NF600; n ++)
	{
		if (f0t_max < f0_traj_s[n])
		{
			f0t_max = f0_traj_s[n];
			i0_max = indice[n];
		}

		if ((f0t_min > f0_traj_s[n]) && (f0_traj_s[n] > 0))
		{
			f0t_min = f0_traj_s[n];
			i0_min = indice[n];
		}
	}

	if (i0_max > 0)
		i0_max --;

	if (i0_min < NPITCH_VAL2-1)
		i0_min ++;

	/* build f0 encoding grid ... */
	Ngrid = i0_min - i0_max + 1;

	for (i = i0_max, n = 0; i <= i0_min; i ++, n ++)
	{
		i0_grid[n] = i;
		f0_grid_s[n] = f02_s[i];
	}

	/* minimise direct path hypothesis ... */
	QPIT_direct_path_optimization_s (f0_traj_s, f0_grid_s, Ngrid, \
												&eq[0], &iq[0], &lq[0], &tq[0]);
	/* minimise first type hypothesis ... */
	QPIT_first_type_optimization_s (f0_traj_s, f0_grid_s, Ngrid, \
												&eq[1], &iq[1], &lq[1], &tq[1]);
	/* minimise second type hypothesis ... */
	QPIT_second_type_optimization_s (f0_traj_s, f0_grid_s, Ngrid, \
												&eq[2], &iq[2], &lq[2], &tq[2]);
	/* minimise constant path hypothesis ... */
	QPIT_constant_path_optimization_s (f0_traj_s, f0_grid_s, Ngrid, \
												&eq[3], &iq[3], &lq[3], &tq[3]);
	/* global minimization ... */
	eq_min = (Shortword)(32767);
	flag = 0;

	for (n = 0; n < NCAND; n ++)
	{
		if (eq[n] < eq_min)
		{
			flag = 1;
			eq_min = eq[n];
			qpar->lag0_iq = i0_grid[iq[n]];
			qpar->lag0_lq = lq[n];
			qpar->lag0_tq = tq[n];
		}
	}

	/* trajectory is too erratic ... */
	if (flag == 0)
	{
		/* set direct path to last non zero pitch value */
		n = NF600;
		while ((f0_traj_s[n] == 0) && (n > 1))
		{
			n--;
		}

		qpar->lag0_iq = indice[n];
		qpar->lag0_tq = 0;
		qpar->lag0_lq = NF600-1;
	}

	/* save last quantized f0 value ... */
	f0q_mem_s = f02_s[qpar->lag0_iq];
	lag0q_mem_s = lag2_s[qpar->lag0_iq];

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of QPIT_encoding_mode2_s () --------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				QPIT_decoding_mode0_s
  Purpose:			this function performs pitch decoding in mode 0.
  Arguments:		1 - (struct melp_param*) par: melp structure.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void QPIT_decoding_mode0_s (struct melp_param *par, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for QPIT_decoding_mode0_s () ----------- */

	short		n;

	/* ---------------------------------------------------------------------- */

	for (n = 0; n < NF600; n ++)
	{
		f0q_s[n+1] = f0q_dec_mem_s;
		lag0q_s[n+1] = lag0q_dec_mem_s;

		par[n].pitch = lag0q_dec_mem_s;
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of QPIT_decoding_mode0_s () --------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				QPIT_decoding_mode1_s
  Purpose:			this function performs pitch decoding in mode 1.
  Arguments:		1 - (struct melp_param*) par: melp structure.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void QPIT_decoding_mode1_s (struct melp_param *par, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for QPIT_decoding_mode1_s () ----------- */

	short		n;

	/* ---------------------------------------------------------------------- */

	lag0q_dec_mem_s = lag1_s[qpar->lag0_iq];
	f0q_dec_mem_s = f01_s[qpar->lag0_iq];

	for (n = 0; n < NF600; n ++)
	{
		lag0q_s[n+1] = lag0q_dec_mem_s;
		f0q_s[n+1] = f0q_dec_mem_s;

		par[n].pitch = lag0q_dec_mem_s;
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of QPIT_decoding_mode1_s () --------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				QPIT_decoding_mode2_s
  Purpose:			this function performs pitch decoding in mode 2 and above.
  Arguments:		1 - (struct melp_param*) par: melp structure.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void QPIT_decoding_mode2_s (struct melp_param *par, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for QPIT_decoding_mode2_s () ----------- */

	short		n;
	Shortword	indice, ds, tmp1_s, tmp2_s, tmp3_s, f0, lag0;
	Shortword	f0_traj_s[NF600];

	/* ---------------------------------------------------------------------- */

	f0 = f02_s[qpar->lag0_iq];
	lag0 = lag2_s[qpar->lag0_iq];

	if (qpar->lag0_tq == 0)
	{
		if (qpar->lag0_lq == NF600-1)
		{
			/* direct path ... */
			f0_traj_s[3] = f0;

			tmp1_s = shr(f0q_dec_mem_s,1);
			tmp3_s = shr(f0_traj_s[3],1);

			f0_traj_s[1] = add (tmp1_s, tmp3_s);

			tmp2_s = shr(f0_traj_s[1],1);

			f0_traj_s[0] = add (tmp1_s, tmp2_s);
			f0_traj_s[2] = add (tmp2_s, tmp3_s);

			for (n = 0; n < NF600; n ++)
			{
				QPIT_f0_quantization_s (f0_traj_s[n], f02_s, NPITCH_VAL2, &indice);
				par[n].pitch = lag2_s[indice];
			}

			f0q_dec_mem_s = f02_s[indice];
			lag0q_dec_mem_s = lag2_s[indice];
		}
		else
		{
			/* build linear f0 trajectory up to location n ... */
			if (qpar->lag0_lq == 0)
				f0_traj_s[0] = f0;
			else if (qpar->lag0_lq == 1)
			{
				tmp1_s = shr (f0q_dec_mem_s,1);
				tmp2_s = shr (f0,1);

				f0_traj_s[0] = add (tmp1_s, tmp2_s);
				f0_traj_s[1] = f0;
			}
			else if (qpar->lag0_lq == 2)
			{
				ds = sub (f0,f0q_dec_mem_s);
				ds = mult (ds, F0_STEP3);

				f0_traj_s[0] = add (f0q_dec_mem_s, ds);
				f0_traj_s[1] = add (f0_traj_s[0], ds);
				f0_traj_s[2] = f0;
			}

			for (n = qpar->lag0_lq+1; n < NF600; n ++)
				f0_traj_s[n] = f0;

			for (n = 0; n < NF600; n ++)
			{
				QPIT_f0_quantization_s (f0_traj_s[n], f02_s, NPITCH_VAL2, &indice);
				par[n].pitch = lag2_s[indice];
			}

			f0q_dec_mem_s = f02_s[indice];
			lag0q_dec_mem_s = lag2_s[indice];
		}
	}
	else /* lag0_tq == 1 */
	{
		if (qpar->lag0_lq == NF600-1)
		{
			/* constant path ... */
			QPIT_f0_quantization_s (f0, f02_s, NPITCH_VAL2, &indice);

			for (n = 0; n < NF600; n ++)
				par[n].pitch = lag2_s[indice];

			f0q_dec_mem_s = f02_s[indice];
			lag0q_dec_mem_s = lag2_s[indice];
		}
		else
		{
			/* keep constant trajectory up to location n ... */
			for (n = 0; n <= qpar->lag0_lq; n ++)
				f0_traj_s[n] = f0q_dec_mem_s;

			/* build linear f0 trajectory up to the end ... */
			if (qpar->lag0_lq == 2)
				f0_traj_s[3] = f0;
			else if (qpar->lag0_lq == 1)
			{
				tmp1_s = shr (f0q_dec_mem_s,1);
				tmp2_s = shr (f0,1);
				f0_traj_s[2] = add (tmp1_s,tmp2_s);
				f0_traj_s[3] = f0;
			}
			else if (qpar->lag0_lq == 0)
			{
				ds = sub (f0, f0q_dec_mem_s);
				ds = mult (ds, F0_STEP3);
				f0_traj_s[1] = add (f0q_dec_mem_s,ds);
				f0_traj_s[2] = add (f0_traj_s[1],ds);
				f0_traj_s[3] = f0;
			}

			for (n = 0; n < NF600; n ++)
			{
				QPIT_f0_quantization_s (f0_traj_s[n], f02_s, NPITCH_VAL2, &indice);
				par[n].pitch = lag2_s[indice];
			}

			f0q_dec_mem_s = f02_s[indice];
			lag0q_dec_mem_s = lag2_s[indice];
		}
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of QPIT_decoding_mode2_s () --------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				QPIT_lag_quantization_s
  Purpose:			this function performs pitch lag quantization.
  Arguments:		1 - (Shortword) lag: input pitch lag.
					2 - (Shortword[]) cbk_lag: pitch lag codebook.
					3 - (int) Nval: codebook size.
					4 - (Shortword*) indice: output quantization index.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void QPIT_lag_quantization_s (Shortword lag, Shortword cbk_lag[], \
												int Nval, Shortword *indice)
{
/* (CD) --- Declaration of variables for QPIT_lag_quantization_s () --------- */

	short		n;
	Shortword	d, error, error_min;

	/* ---------------------------------------------------------------------- */

	error_min = (Shortword)(32767);
	*indice = 0;

	for (n = 0; n < Nval; n ++)
	{
		d = sub (lag, cbk_lag[n]);
		error = abs_s (d);

		if (error < error_min)
		{
			error_min = error;
			*indice = n;
		}
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of QPIT_lag_quantization_s () ------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				QPIT_f0_quantization_s
  Purpose:			this function performs f0 quantization.
  Arguments:		1 - (float) f0: input f0.
					2 - (float[]) cbk_f0: f0 codebook.
					3 - (int) Nval: codebook size.
					4 - (Shortword*) indice: output quantization index.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void QPIT_f0_quantization_s (Shortword f0, Shortword cbk_f0[], int Nval, \
															Shortword *indice)
{
/* (CD) --- Declaration of variables for QPIT_f0_quantization_s () ---------- */

	short		n;
	Shortword	d, error, error_min;

	/* ---------------------------------------------------------------------- */

	error_min = (Shortword)(32767);
	*indice = 0;

	for (n = 0; n < Nval; n ++)
	{
		d = sub (f0, cbk_f0[n]);
		error = abs_s (d);
		if (error < error_min)
		{
			error_min = error;
			*indice = n;
		}
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of QPIT_f0_quantization_s () -------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				QPIT_direct_path_optimization_s
  Purpose:			this function performs direct path modelling.
  Arguments:		1 - (Shortword[]) f0_traj_s: input f0 trajectory.
					2 - (Shortword[]) f0_grid_s: input f0 optimisation grid.
					3 - (int) Ngrid: grid size.
					4 - (Shortword*) error_min: output modelling error.
					5 - (int*) iq: output quantization index.
					6 - (int*) lq: output quantized value location.
					7 - (int*) tq: output trajectory type.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void QPIT_direct_path_optimization_s (Shortword f0_traj_s[], \
					Shortword f0_grid_s[], int Ngrid, Shortword *error_min, \
													int *iq, int *lq, int *tq)
{
/* (CD) --- Declaration of variables for QPIT_direct_path_optimization_s () -- */

	short		i, n;
	Shortword	tmp1_s, tmp2_s, tmp3_s, ds, error;
	Shortword	f0[NF600+1];
	Longword	tmp_l;

	/* ---------------------------------------------------------------------- */

	/* initialization ... */
	*error_min = (Shortword)(32767);
	*iq = 0;

	/* for each f0 candidate in the grid ... */
	for (i = 0; i < Ngrid; i ++)
	{
		/* build linear f0 trajectory ... */
		f0[0] = f0_traj_s[0];
		f0[4] = f0_grid_s[i];

		tmp1_s = shr(f0[0],1);
		tmp3_s = shr(f0[4],1);

		f0[2] = add (tmp1_s, tmp3_s);

		tmp2_s = shr(f0[2],1);

		f0[1] = add (tmp1_s, tmp2_s);
		f0[3] = add (tmp2_s, tmp3_s);

		/* estimate trajectory error ... */
		tmp_l = 0;

		for (n = 1; n <= NF600; n ++)
		{
			if (f0_traj_s[n] > 0)
			{
				ds = sub (f0_traj_s[n], f0[n]);
				ds = abs_s (ds);
				ds = shl (ds, D_SHIFT);
				tmp_l = L_mac (tmp_l, ds, ds);
			}
		}

		error = extract_h (tmp_l);

		/* save minimised error ... */
		if (error < *error_min)
		{
			*error_min = error;
			*iq = i;
		}
	}

	*lq = NF600-1;
	*tq = 0;

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of QPIT_direct_path_optimization_s () ----------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				QPIT_first_type_optimization_s
  Purpose:			this function performs first type trajectory modelling.
  Arguments:		1 - (Shortword[]) f0_traj_s: input f0 trajectory.
					2 - (Shortword[]) f0_grid_s: input f0 optimisation grid.
					3 - (int) Ngrid: f0 grid size.
					4 - (Shortword*) error_min: output modelling error.
					5 - (int*) iq: output quantization index.
					6 - (int*) lq: output quantized value location.
					7 - (int*) tq: output trajectory type.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void QPIT_first_type_optimization_s (Shortword f0_traj_s[], \
					Shortword f0_grid_s[], int Ngrid, Shortword *error_min, \
													int *iq, int *lq, int *tq)
{
/* (CD) --- Declaration of variables for QPIT_first_type_optimization_s () -- */

	short		i, k, n;
	Shortword	tmp1_s, tmp2_s, ds, error;
	Shortword	f0[NF600+1];
	Longword	tmp_l;

	/* ---------------------------------------------------------------------- */

	/* initialization ... */
	*error_min = (Shortword)(32767);
	*iq = 0;
	*lq = 0;

	/* for each f0 candidate in the grid ... */
	for (i = 0; i < Ngrid; i ++)
	{
		/* for each possible location ... */
		for (n = 1; n < NF600; n ++)
		{
			/* build linear f0 trajectory up to location n ... */
			if (n == 1)
				f0[1] = f0_grid_s[i];
			else if (n == 2)
			{
				tmp1_s = shr (f0_traj_s[0],1);
				tmp2_s = shr (f0_grid_s[i],1);

				f0[1] = add (tmp1_s, tmp2_s);
				f0[2] = f0_grid_s[i];
			}
			else if (n == 3)
			{
				ds = sub (f0_grid_s[i],f0_traj_s[0]);
				ds = mult (ds, F0_STEP3);

				f0[1] = add (f0_traj_s[0], ds);
				f0[2] = add (f0[1], ds);
				f0[3] = f0_grid_s[i];
			}

			for (k = n+1; k <= NF600; k ++)
				f0[k] = f0_grid_s[i];

			/* estimate trajectory error ... */
			tmp_l = 0;

			for (k = 1; k <= NF600; k ++)
			{
				if (f0_traj_s[k] > 0)
				{
					ds = sub (f0_traj_s[k], f0[k]);
					ds = abs_s (ds);
					ds = shl (ds, D_SHIFT);
					tmp_l = L_mac (tmp_l, ds, ds);
				}
			}

			error = extract_h (tmp_l);

			/* save minimised error ... */
			if (error < *error_min)
			{
				*error_min = error;
				*iq = i;
				*lq = n-1;
			}
		}
	}

	*tq = 0;

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of QPIT_first_type_optimization_s () ------------------------ */
}

/* DS --------------------------------------------------------------------------
  Name:				QPIT_second_type_optimization_s
  Purpose:			this function performs second type trajectory modelling.
  Arguments:		1 - (Shortword[]) f0_traj_s: input f0 trajectory.
					2 - (Shortword[]) f0_grid_s: input f0 grid.
					3 - (int) Ngrid: f0 grid size.
					4 - (Shortword*) error_min: output modelling error.
					5 - (int*) iq: output quantization index.
					6 - (int*) lq: output quantized value location.
					7 - (int*) tq: output trajectory type.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void QPIT_second_type_optimization_s (Shortword f0_traj_s[], \
						Shortword f0_grid_s[], int Ngrid, Shortword *error_min,\
													int *iq, int *lq, int *tq)
{
/* (CD) --- Declaration of variables for QPIT_second_type_optimization_s () - */

	short		i, k, n;
	Shortword	tmp1_s, tmp2_s, ds, error;
	Shortword	f0[NF600+1];
	Longword	tmp_l;

	/* ---------------------------------------------------------------------- */

	/* initialization ... */
	*error_min = (Shortword)(32767);
	*iq = 0;
	*lq = 0;

	/* for each f0 candidate in the grid ... */
	for (i = 0; i < Ngrid; i ++)
	{
		/* for each possible location ... */
		for (n = 1; n < NF600; n ++)
		{
			/* keep constant trajectory up to location n ... */
			for (k = 0; k <= n; k ++)
				f0[k] = f0_traj_s[0];

			/* build linear f0 trajectory up to the end ... */
			if (n == 3)
				f0[4] = f0_grid_s[i];
			else if (n == 2)
			{
				tmp1_s = shr (f0_traj_s[0],1);
				tmp2_s = shr (f0_grid_s[i],1);
				f0[3] = add (tmp1_s,tmp2_s);
				f0[4] = f0_grid_s[i];
			}
			else if (n == 1)
			{
				ds = sub (f0_grid_s[i], f0_traj_s[0]);
				ds = mult (ds, F0_STEP3);
				f0[2] = add (f0_traj_s[0],ds);
				f0[3] = add (f0[2],ds);
				f0[4] = f0_grid_s[i];
			}

			/* estimate trajectory error ... */
			tmp_l = 0;

			for (k = 1; k <= NF600; k ++)
			{
				if (f0_traj_s[k] > 0)
				{
					ds = sub (f0_traj_s[k], f0[k]);
					ds = abs_s (ds);
					ds = shl (ds, D_SHIFT);
					tmp_l = L_mac (tmp_l, ds, ds);
				}
			}

			error = extract_h (tmp_l);

			/* save minimised error ... */
			if (error < *error_min)
			{
				*error_min = error;
				*iq = i;
				*lq = n-1;
			}
		}
	}

	*tq = 1;

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of QPIT_second_type_optimization_s () ----------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				QPIT_constant_path_optimization_s
  Purpose:			this function performs constant trajectory modelling.
  Arguments:		1 - (Shortword[]) f0_traj_s: input f0 trajectory.
					2 - (Shortword[]) f0_grid_s: input f0 grid.
					3 - (int) Ngrid: f0 grid size.
					4 - (Shortword*) error_min: output modelling error.
					5 - (int*) iq: output quantization index.
					6 - (int*) lq: output quantized value location.
					7 - (int*) tq: output trajectory type.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void QPIT_constant_path_optimization_s (Shortword f0_traj_s[], \
					Shortword f0_grid_s[], int Ngrid, Shortword *error_min, \
													int *iq, int *lq, int *tq)
{
/* (CD) --- Declaration of variables for QPIT_constant_path_optimization_s () */

	short		i, n;
	Shortword	ds, error;
	Longword	tmp_l;

	/* ---------------------------------------------------------------------- */

	/* initialization ... */
	*error_min = (Shortword)(32767);
	*iq = 0;

	/* for each f0 candidate in the grid ... */
	for (i = 0; i < Ngrid; i ++)
	{
		/* estimate trajectory error ... */
		tmp_l = 0;

		for (n = 1; n <= NF600; n ++)
		{
			if (f0_traj_s[n] > 0)
			{
				ds = sub (f0_traj_s[n], f0_grid_s[i]);
				ds = abs_s (ds);
				ds = shl (ds, D_SHIFT);
				tmp_l = L_mac (tmp_l, ds, ds);
			}
		}

		error = extract_h (tmp_l);

		/* save minimised error ... */
		if (error < *error_min)
		{
			*error_min = error;
			*iq = i;
		}
	}

	*lq = NF600-1;
	*tq = 1;

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of QPIT_constant_path_optimization_s () --------------------- */
}

/*----------------------------------------------------------------------------*/