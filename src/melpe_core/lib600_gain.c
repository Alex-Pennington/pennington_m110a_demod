/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		lib600_gain.c
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Gain Encoding Library.
-------------------------------------------------------------------------- DE */

/*------------------------------------------------------------------------------
	Include Files
------------------------------------------------------------------------------*/

#include "main600.h"
#include "sc600.h"
#include "lib600_srt.h"
#include "lib600_gain.h"
#include "ext600_gain.h"

#include "mathhalf.h"
#include "mat_lib.h"

/*------------------------------------------------------------------------------
	Prototypes
------------------------------------------------------------------------------*/

Shortword *v_equ (Shortword vec1[], const Shortword vec2[], Shortword n);

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

/* DS --------------------------------------------------------------------------
  Name:				GAIN_gain_quantization_s
  Purpose:			this function performs gain quantization in MELP 600 mode.
					this function is called during:
						1: MELP encoding in routine analysis().
						2: MELP transcoding in routine TRSC_transcode_24to6_s().
  Arguments:		1 - (struct melp_param *) par: melp structure.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void GAIN_gain_quantization_s (struct melp_param *par, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for GAIN_gain_quantization_s () -------- */

	short		i;

	/* ---------------------------------------------------------------------- */

	for (i = 0; i < NF600; i ++)
	{
		g600_s[(2*i)]   = par[i].gain[0];
		g600_s[(2*i)+1] = par[i].gain[1];
	}

	if (qpar->icbk_gain == 0)
	{
//		GAIN_MSVQ76_s (g600_s, g600q_s, qpar);
		GAIN_D_MSVQ76_s (g600_s, qpar);
		GAIN_I_MSVQ76_s (g600q_s, qpar);
	}
	else if (qpar->icbk_gain == 1)
	{
//		GAIN_MSVQ65_s (g600_s, g600q_s, qpar);
		GAIN_D_MSVQ65_s (g600_s, qpar);
		GAIN_I_MSVQ65_s (g600q_s, qpar);
	}
	else if (qpar->icbk_gain == 2)
	{
//		GAIN_VQ9_s (g600_s, g600q_s, qpar);
		GAIN_D_VQ9_s (g600_s, qpar);
		GAIN_I_VQ9_s (g600q_s, qpar);
	}
	else
		exit(1);

	for (i = 0; i < NF600; i ++)
	{
		par[i].gain[0] = g600q_s[(2*i)];
		par[i].gain[1] = g600q_s[(2*i)+1];
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of GAIN_gain_quantization_s () ------------------------------ */
}

/* DS --------------------------------------------------------------------------
  Name:				GAIN_VQ9_s
  Purpose:			this function performs full quantization process, including
					recovery of the quantised vector.
  Arguments:		1 - (Shortword[]) vs: input vector.
					2 - (Shortword[]) vq_s: output quantised vector.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void GAIN_VQ9_s (Shortword vs[], Shortword vq_s[], quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for GAIN_VQ9_s () ---------------------- */

	short		iq, n;

	/* ---------------------------------------------------------------------- */

	/* find the M closest codebook vectors for the 1st stage... */
	GAIN_single_mbest_s (vs, g9_s, N9);

	/* last stage contribution ... */
	iq = qpar->gain_iq[0] = giq[0][0];

	/* rebuild quantised vector ... */
	for (n = 0; n < NGAIN; n ++)
		vq_s[n] = g9_s[(iq*NGAIN)+n];

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of GAIN_VQ9_s () -------------------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				GAIN_MSVQ76_s
  Purpose:			this function performs full quantization process, including
					recovery of the quantised vector.
  Arguments:		1 - (Shortword[]) vs: input vector.
					2 - (Shortword[]) vq_s: output quantised vector.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void GAIN_MSVQ76_s (Shortword vs[], Shortword vq_s[], quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for GAIN_MSVQ76_s () ------------------- */

	short		ip, iq, n;

	/* ---------------------------------------------------------------------- */

	/* find the M closest codebook vectors for the 1st stage... */
	GAIN_single_mbest_s (vs, g76st1_s, N76ST1);

	/* and for the second stage ... */
	GAIN_multi_mbest_s (vs, g76st2_s, N76ST2);

	/* last stage contribution ... */
	iq = qpar->gain_iq[1] = giq[1][0];
	ip = gip[1][0];

	/* rebuild quantised vector ... */
	for (n = 0; n < NGAIN; n ++)
		vq_s[n] = g76st2_s[(iq*NGAIN)+n];

	/* get the optimal predecessor ... */
	iq = qpar->gain_iq[0] = giq[0][ip];

	/* prepare for previous stage ... */
	ip = gip[0][ip];

	/* rebuild quantised vector ... */
	for (n = 0; n < NGAIN; n ++)
		vq_s[n] = add (vq_s[n], g76st1_s[(iq*NGAIN)+n]);

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of GAIN_MSVQ76_s () ----------------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				GAIN_MSVQ65_s
  Purpose:			this function performs full quantization process, including
					recovery of the quantised vector.
  Arguments:		1 - (Shortword[]) vs: input vector.
					2 - (Shortword[]) vq_s: output quantised vector.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void GAIN_MSVQ65_s (Shortword vs[], Shortword vq_s[], quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for GAIN_MSVQ65_s () ------------------- */

	short		ip, iq, n;

	/* ---------------------------------------------------------------------- */

	/* find the M closest codebook vectors for the 1st stage... */
	GAIN_single_mbest_s (vs, g65st1_s, N65ST1);

	/* and for the second stage ... */
	GAIN_multi_mbest_s (vs, g65st2_s, N65ST2);

	/* last stage contribution ... */
	iq = qpar->gain_iq[1] = giq[1][0];
	ip = gip[1][0];

	/* rebuild quantised vector ... */
	for (n = 0; n < NGAIN; n ++)
		vq_s[n] = g65st2_s[(iq*NGAIN)+n];

	/* get the optimal predecessor ... */
	iq = qpar->gain_iq[0] = giq[0][ip];

	/* prepare for previous stage ... */
	ip = gip[0][ip];

	/* rebuild quantised vector ... */
	for (n = 0; n < NGAIN; n ++)
		vq_s[n] = add (vq_s[n], g65st1_s[(iq*NGAIN)+n]);

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of GAIN_MSVQ65_s () ----------------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				GAIN_D_VQ9_s
  Purpose:			this function performs direct quantisation process, with
					no recovery of the quantised vector.
  Arguments:		1 - (Shortword[]) vs: input vector.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void GAIN_D_VQ9_s (Shortword vs[], quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for GAIN_D_VQ9_s () -------------------- */


	/* ---------------------------------------------------------------------- */

	/* find the M closest codebook vectors for the 1st stage... */
	GAIN_single_mbest_s (vs, g9_s, N9);

	/* last stage contribution ... */
	qpar->gain_iq[0] = giq[0][0];

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of GAIN_D_VQ9_s () ------------------------------------------ */
}

/* DS --------------------------------------------------------------------------
  Name:				GAIN_D_MSVQ76_s
  Purpose:			this function performs direct quantisation process, with
					no recovery of the quantised vector.
  Arguments:		1 - (Shortword[]) vs: input vector.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void GAIN_D_MSVQ76_s (Shortword vs[], quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for GAIN_D_MSVQ76_s () ----------------- */


	/* ---------------------------------------------------------------------- */

	/* find the M closest codebook vectors for the 1st stage... */
	GAIN_single_mbest_s (vs, g76st1_s, N76ST1);

	/* and for the second stage ... */
	GAIN_multi_mbest_s (vs, g76st2_s, N76ST2);

	/* last stage contribution ... */
	qpar->gain_iq[1] = giq[1][0];

	/* get the optimal predecessor ... */
	qpar->gain_iq[0] = giq[0][gip[1][0]];

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of GAIN_D_MSVQ76_s () --------------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				GAIN_D_MSVQ65_s
  Purpose:			this function performs direct quantisation process, with
					no recovery of the quantised vector.
  Arguments:		1 - (Shortword[]) vs: input vector.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void GAIN_D_MSVQ65_s (Shortword vs[], quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for GAIN_D_MSVQ65_s () ----------------- */


	/* ---------------------------------------------------------------------- */

	/* find the M closest codebook vectors for the 1st stage... */
	GAIN_single_mbest_s (vs, g65st1_s, N65ST1);

	/* and for the second stage ... */
	GAIN_multi_mbest_s (vs, g65st2_s, N65ST2);

	/* last stage contribution ... */
	qpar->gain_iq[1] = giq[1][0];

	/* get the optimal predecessor ... */
	qpar->gain_iq[0] = giq[0][gip[1][0]];

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of GAIN_D_MSVQ65_s () --------------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				GAIN_I_VQ9_s
  Purpose:			this function performs inverse quantisation process, i.e.
					recovery of the quantised vector from quantised indices.
  Arguments:		1 - (Shortword[]) vq_s: output quantised vector.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void GAIN_I_VQ9_s (Shortword vq_s[], quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for GAIN_I_VQ9_s () -------------------- */

	short		iq, n;

	/* ---------------------------------------------------------------------- */

	/* last stage contribution ... */
	iq = qpar->gain_iq[0];

	/* rebuild quantised vector ... */
	for (n = 0; n < NGAIN; n ++)
		vq_s[n] = g9_s[(iq*NGAIN)+n];

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of GAIN_I_VQ9_s () ------------------------------------------ */
}

/* DS --------------------------------------------------------------------------
  Name:				GAIN_I_MSVQ76_s
  Purpose:			this function performs inverse quantisation process, i.e.
					recovery of the quantised vector from quantised indices.
  Arguments:		1 - (Shortword[]) vq_s: output quantised vector.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void GAIN_I_MSVQ76_s (Shortword vq_s[], quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for GAIN_I_MSVQ76_s () ----------------- */

	short		iq, n;

	/* ---------------------------------------------------------------------- */

	/* last stage contribution ... */
	iq = qpar->gain_iq[1];

	/* rebuild quantised vector ... */
	for (n = 0; n < NGAIN; n ++)
		vq_s[n] = g76st2_s[(iq*NGAIN)+n];

	/* get the optimal predecessor ... */
	iq = qpar->gain_iq[0];

	/* rebuild quantised vector ... */
	for (n = 0; n < NGAIN; n ++)
		vq_s[n] = add (vq_s[n], g76st1_s[(iq*NGAIN)+n]);

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of GAIN_I_MSVQ76_s () --------------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				GAIN_I_MSVQ65_s
  Purpose:			this function performs inverse quantisation process, i.e.
					recovery of the quantised vector from quantised indices.
  Arguments:		1 - (Shortword[]) vq_s: output quantised vector.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void GAIN_I_MSVQ65_s (Shortword vq_s[], quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for GAIN_I_MSVQ65_s () ----------------- */

	short		iq, n;

	/* ---------------------------------------------------------------------- */

	/* last stage contribution ... */
	iq = qpar->gain_iq[1];

	/* rebuild quantised vector ... */
	for (n = 0; n < NGAIN; n ++)
		vq_s[n] = g65st2_s[(iq*NGAIN)+n];

	/* get the optimal predecessor ... */
	iq = qpar->gain_iq[0];

	/* rebuild quantised vector ... */
	for (n = 0; n < NGAIN; n ++)
		vq_s[n] = add (vq_s[n], g65st1_s[(iq*NGAIN)+n]);

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of GAIN_I_MSVQ65_s () --------------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				GAIN_single_mbest_s
  Purpose:			test function.
  Arguments:		1 - (Shortword[]) v: input vector.
					2 - (Shortword[]) g_cbk_s: codebook array.
					3 - (int) cbk_size: codebook size.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void GAIN_single_mbest_s (Shortword v[], Shortword g_cbk_s[], \
																int cbk_size)
{
/* (CD) --- Declaration of variables for GAIN_single_mbest_s () ------------- */

	short		n, m;

	/* ---------------------------------------------------------------------- */

	/* distance ... */
	for (n = 1; n <= cbk_size; n ++)
	{
		ds_gain[n] = GAIN_L2_distance_s (v, &g_cbk_s[(n-1)*NGAIN], 0, 2*NF600-1);
		i_gain[n] = n-1;
	}

	/* sorting ... */
	SRT_ssort (ds_gain, i_gain, cbk_size);

	/* save results ... */
	for (m = 1; m <= MBEST_GAIN; m ++)
	{
		for (n = 0; n < NGAIN; n ++)
			gxq_s[0][m-1][n] = g_cbk_s[(i_gain[m]*NGAIN)+n];

		giq[0][m-1] = i_gain[m];
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of GAIN_single_mbest_s () ----------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				GAIN_multi_mbest_s
  Purpose:			test function.
  Arguments:		1 - (Shortword[]) vs: input vector.
					2 - (Shortword[]) g_cbk_s: codebook array.
					3 - (int) cbk_size: codebook size.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void GAIN_multi_mbest_s (Shortword vs[], Shortword g_cbk_s[], \
																int cbk_size)
{
/* (CD) --- Declaration of variables for GAIN_multi_mbest_s () -------------- */

	short		j, k, m, n;

	/* ---------------------------------------------------------------------- */

	/* M-best ... */
	for (m = 0; m < MBEST_GAIN; m ++)
	{
		j = (m * cbk_size);

		for (k = 1; k <= cbk_size; k ++)
		{
			/* rebuild quantized vector at stage is ... */
			for (n = 0; n < NGAIN; n ++)
				gx_s[n] = add (gxq_s[0][m][n], g_cbk_s[(k-1)*NGAIN+n]);

			/* compute distorsion ... */
			dg_s[j+k] = GAIN_L2_distance_s (vs, gx_s, 0, 2*NF600-1);
		}
	}

	/* sorting ... */
	for (n = 1; n <= cbk_size; n ++)
	{
		for (m = 0; m < MBEST_GAIN; m ++)
		{
			ig_best[n+(m*cbk_size)] = n-1;
			ig_mem[n+(m*cbk_size)] = m;
		}
	}

	GAIN_sorting_s (dg_s, ig_best, ig_mem, MBEST_GAIN * cbk_size);

	/* save results ... */
	for (m = 1; m <= MBEST_GAIN; m ++)
	{
		for (n = 0; n < NGAIN; n ++)
			gxq_s[1][m-1][n] = add (gxq_s[0][ig_mem[m]][n], g_cbk_s[(ig_best[m]*NGAIN)+n]);

		giq[1][m-1] = ig_best[m];
		gip[1][m-1] = ig_mem[m];
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of GAIN_multi_mbest_s () ------------------------------------ */
}

/* DS --------------------------------------------------------------------------
  Name:				GAIN_L2_distance_s
  Purpose:			test function.
  Arguments:		1 - (Shortword[]) c: 1st vector.	
					2 - (Shortword[]) x: 2nd vector.
					3 - (int) N: vector size.
  Return:			R - (Shortword) distance.
-------------------------------------------------------------------------- DE */

static Shortword GAIN_L2_distance_s (Shortword c[], Shortword v[], \
																int nl, int nh)
{
/* (CD) --- Declaration of variables for GAIN_L2_distance_s () -------------- */

	short		n;
	Shortword	Ds;
	Shortword	ts[2*NF600];
	Longword	D;

	/* --- Initialization of bounds. ---------------------------------------- */

	v_equ (ts, v, (short)(nh-nl+1));
	v_sub (ts, c, (short)(nh-nl+1));

	D = 0;

	for (n = 0; n < (short)(nh-nl+1); n++)
		D = L_mac (D, ts[n], ts[n]);
	
	Ds = extract_h (D);

	/* ---------------------------------------------------------------------- */

	return (Ds);

/* (CD) --- End of GAIN_L2_distance_s () ------------------------------------ */
}

/*DS----------------------------------------------------------------------------
  Name:				GAIN_sorting_s
  Purpose:			test function.
  Arguments:		1 - (Shortword[]) ra: .
					2 - (int[]) rb1: .
					3 - (int[]) rb2: .
					4 - (int) n: .
  Return:			R - none.
------------------------------------------------------------------------------*/

static void GAIN_sorting_s (Shortword ra[], int rb1[], int rb2[], int n)
{
/* (CD) --- Declaration of variables for GAIN_sorting_s () ------------------ */
	
	short		i, j, l, ir;
	int		rrb1, rrb2;
	Shortword	rra;

	/* --- Output display of error message and exit ------------------------- */

	l = (n >> 1) + 1;
	ir = n;

	for (;;)
	{
		if (l > 1)
		{
			rra = ra[--l];

			rrb1 = rb1[l];
			rrb2 = rb2[l];
		}
		else
		{
			rra = ra[ir];
			rrb1 = rb1[ir];
			rrb2 = rb2[ir];

			ra[ir] = ra[1];
			rb1[ir] = rb1[1];
			rb2[ir] = rb2[1];
			
			if (--ir == 1)
			{
				ra[1] = rra;
				rb1[1] = rrb1;
				rb2[1] = rrb2;

				return;
			}
		}
		
		i = l;
		j = l << 1;
		
		while (j <= ir)
		{
			if ((j < ir) && (ra[j] < ra[j+1]))
				++ j;

			if (rra < ra[j])
			{
				ra[i] = ra[j];
				rb1[i] = rb1[j];
				rb2[i] = rb2[j];
				
				j += (i = j);
			}
			else
				j = ir + 1;
		}

		ra[i] = rra;
		rb1[i] = rrb1;
		rb2[i] = rrb2;
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of GAIN_sorting_s () ---------------------------------------- */
}

/*----------------------------------------------------------------------------*/