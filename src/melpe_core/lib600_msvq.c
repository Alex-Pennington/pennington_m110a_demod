/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		lib600_msvq.c
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			LSF MSVQ Encoding Library
-------------------------------------------------------------------------- DE */

/*------------------------------------------------------------------------------
	Include Files
------------------------------------------------------------------------------*/

#include "main600.h"
#include "sc600.h"
#include "lib600_srt.h"
#include "lib600_msvq.h"
#include "ext600_msvq.h"

#include "mathhalf.h"
#include "mat_lib.h"
#include "vq_lib.h"

/*------------------------------------------------------------------------------
	Constants
------------------------------------------------------------------------------*/

#define MAXWT       4096                    /* w[i] < 2.0 to avoid saturation */
#define MAXWT2      (MAXWT*2)
#define MAXWT4      (MAXWT*4)

/*------------------------------------------------------------------------------
	Declaration of external variables
------------------------------------------------------------------------------*/

extern Shortword mem_lpc[NF600][LPC_ORD];

/*------------------------------------------------------------------------------
	Prototypes
------------------------------------------------------------------------------*/

Shortword *v_equ (Shortword vec1[], const Shortword vec2[], Shortword n);

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

/* DS --------------------------------------------------------------------------
  Name:				MSVQ_lsf_quantization_s
  Purpose:			this function performs lsf quantization in MELP 600 mode.
					this function is called during:
						1: MELP encoding in routine analysis().
  Arguments:		1 - (struct melp_param *) par: melp structure.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void MSVQ_lsf_quantization_s (struct melp_param *par, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for MSVQ_lsf_quantization_s () --------- */

	short		i, k;
	Shortword	lpc[LPC_ORD+1];

	/* --- quantization of LSF coefficients for the first two frames -------- */

	/* set adaptive weights ... */
	for (i = 0; i < 2; i ++)
	{
		v_equ (&(lpc[1]), mem_lpc[i], LPC_ORD);

		vq_lspw (&w_lsf_s[i*LPC_ORD], par[i].lsf, &(lpc[1]), LPC_ORD);
		
		MSVQ_check_weights (&w_lsf_s[i*LPC_ORD], LPC_ORD);
	}

	/* concatenation of lsf vectors ... */
	for (i = 0; i < 2; i ++)
	{
		for (k = 0; k < LPC_ORD; k ++)
			lsf600_s[(i*LPC_ORD)+k] = par[i].lsf[k];
	}

	/* mutli-stage vector quantization ... */
	MSVQ_Dquantization_s (lsf600_s, 0, qpar);
	
	/* --- quantization of LSF coefficients for the last two frames --------- */

	/* set adaptive weights ... */
	for (i = 0; i < 2; i ++)
	{
		v_equ (&(lpc[1]), mem_lpc[i+2], LPC_ORD);

		vq_lspw (&w_lsf_s[i*LPC_ORD], par[i+2].lsf, &(lpc[1]), LPC_ORD);

		MSVQ_check_weights (&w_lsf_s[i*LPC_ORD], LPC_ORD);
	}

	/* concatenation of lsf vectors ... */
	for (i = 0; i < 2; i ++)
	{
		for (k = 0; k < LPC_ORD; k ++)
			lsf600_s[(i*LPC_ORD)+k] = par[i+2].lsf[k];
	}

	/* mutli-stage vector quantization ... */
	MSVQ_Dquantization_s (lsf600_s, 1, qpar);

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of MSVQ_lsf_quantization_s () ------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				MSVQ_Dquantization_s
  Purpose:			this function performs direct quantisation process, with
					no recovery of the quantised vector.
					this function is called during:
						1: MELP analysis in routine MSVQ_lsf_quantization_s().
						2: MELP transcoding in routine TRSC_transcode_24to6_s().
  Arguments:		1 - (Shortword[]) v: input vector.
					2 - (int) isubframe: subframe indice.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void MSVQ_Dquantization_s (Shortword v[], int isubframe, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for MSVQ_Dquantization_s () ------------ */

	short		is, ip0, NS;

	/* ---------------------------------------------------------------------- */

	/* initialization ... */
	MSVQ_set_pointers (isubframe, qpar);

	/* find the M closest codebook vectors for the 1st stage ... */
	MSVQ_single_mbest_s (v, 0, isubframe);

	/* and for each following stage ... */
	NS = qpar->nstg_lsf[isubframe];

	for (is = 1; is < NS; is ++)
		MSVQ_multi_mbest_s (v, is, isubframe);

	/* last stage contribution ... */
	qpar->lsf_iq[isubframe][NS-1] = iq[isubframe][NS-1][0];
	ip0 = ip[isubframe][NS-1][0];

	for (is = NS-2; is >= 0; is --)
	{
		/* get the optimal predecessor ... */
		qpar->lsf_iq[isubframe][is] = iq[isubframe][is][ip0];
		
		/* prepare for previous stage ... */
		ip0 = ip[isubframe][is][ip0];
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of MSVQ_Dquantization_s () ---------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				MSVQ_Iquantization_s
  Purpose:			this function performs inverse quantisation process, i.e.
					recovery of the quantised vector from quantised indices.
					this function is called during:
						1: MELP encoding in routine MSVQ_lsf_quantization_s().
						2: MELP decoding in routine RDS_restore_parameters().
  Arguments:		1 - (Shortword[]) vq: output quantised vector.
					2 - (int) isubframe: subframe indice.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void MSVQ_Iquantization_s (Shortword vq[], int isubframe, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for MSVQ_Iquantization_s () ------------ */

	short		is, iq0, n, NS;
	Shortword	tmp_s;
	Longword	l_tmp;
	Longword	vl[NLSF600];

	/* ---------------------------------------------------------------------- */

	/* initialization ... */
	MSVQ_set_pointers (isubframe, qpar);

	/* last stage contribution ... */
	NS = qpar->nstg_lsf[isubframe];

	/* last stage contribution ... */
	iq0 = qpar->lsf_iq[isubframe][NS-1];

	/* rebuild quantised vector ... */
	for (n = 0; n < NLSF600; n ++)
		vl[n] = L_deposit_l (cbk_st_s[NS-1][(iq0*NLSF600)+n]);

	for (is = NS-2; is >= 0; is --)
	{
		/* get the optimal predecessor ... */
		iq0 = qpar->lsf_iq[isubframe][is];
		
		/* rebuild quantised vector ... */
		for (n = 0; n < NLSF600; n ++)
		{
			l_tmp = L_deposit_l (cbk_st_s[is][(iq0*NLSF600)+n]);

			if (is == 0)
				l_tmp = L_shl (l_tmp, L_SHIFT_ST1);

			vl[n] = L_add (vl[n], l_tmp);
		}
	}

	for (n = 0; n < NLSF600; n ++)
	{
		l_tmp = L_shr (vl[n], L_SHIFT_STN);
		tmp_s = extract_l (l_tmp);
		vq[n] = add (tmp_s, cbk_mst1_s[n]);
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of MSVQ_Iquantization_s () ---------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				MSVQ_single_mbest_s
  Purpose:			test function.
  Arguments:		1 - (Shortword[]) v: input vector.
					2 - (int) is: stage index.
					3 - (int) isubframe: subframe indice.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void MSVQ_single_mbest_s (Shortword v[], int is, int isubframe)
{
/* (CD) --- Declaration of variables for MSVQ_single_mbest_s () ------------- */

	short		k, n, m;
	int			*indice;
	Shortword	tmp_s;
	Shortword	*ds;
	Shortword	vs[NLSF600];

	/* ---------------------------------------------------------------------- */

	/* memory allocation ... */
	ds = (short*)calloc ((unsigned)(size_st[0]+1), sizeof (short));
	if (!ds)
	{
		fprintf (stderr, "allocation failure in MSVQ_single_mbest_s !\n");
		exit(1);
	}

	indice = (int*)calloc ((unsigned)(size_st[0]+1), sizeof (int));
	if (!indice)
	{
		fprintf (stderr, "allocation failure in MSVQ_single_mbest_s !\n");
		exit(1);
	}

	/* remove codebook mean and scaling ... */
	for (k = 0; k < NLSF600; k ++)
	{
		tmp_s = sub (v[k], cbk_mst1_s[k]);
		vs[k] = shl (tmp_s, L_SHIFT_ST1);
	}

	/* distance ... */
	for (n = 1; n <= size_st[0]; n ++)
	{
		k = (n-1) * NLSF600;

		ds[n] = MSVQ_WL2_distance_s (vs, cbk_st_s[0]+k, w_lsf_s, 0, NLSF600-1);
		indice[n] = n-1;
	}

	/* sorting ... */
	SRT_ssort (ds, indice, size_st[0]);

	/* save results ... */
	for (m = 1; m <= MBEST_LSF; m ++)
	{
		k = indice[m] * NLSF600;

		for (n = 0; n < NLSF600; n ++)
			xq_s[is][m-1][n] = cbk_st_s[0][k+n];

		iq[isubframe][is][m-1] = indice[m];
	}

	/* memory deallocation ... */	
	free ((char*)(ds));
	free ((char*)(indice));

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of MSVQ_single_mbest_s () ----------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				MSVQ_multi_mbest_s
  Purpose:			test function.
  Arguments:		1 - (Shortword[]) v: input vector.
					2 - (int) is: stage index.
					3 - (int) isubframes: subframe indice.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void MSVQ_multi_mbest_s (Shortword v[], int is, int isubframe)
{
/* (CD) --- Declaration of variables for MSVQ_multi_mbest_s () -------------- */

	short		j, k, m, n;
	int			*i_best, *i_mem;
	Shortword	tmp_s;
	Shortword	xq0_s[NLSF600];
	Shortword	vs[NLSF600];
	Shortword	*ds;
	Longword	l_tmp1, l_tmp2;

	/* ---------------------------------------------------------------------- */

	/* memory allocation ... */
	ds = (short*)calloc ((unsigned)((MBEST_LSF*size_st[is])+1), sizeof (short));
	if (!ds)
	{
		fprintf (stderr, "allocation failure in MSVQ_multi_mbest_s !\n");
		exit(1);
	}

	i_best = (int*)calloc ((unsigned)((MBEST_LSF*size_st[is])+1), sizeof (int));
	if (!i_best)
	{
		fprintf (stderr, "allocation failure in MSVQ_multi_mbest_s !\n");
		exit(1);
	}

	i_mem = (int*)calloc ((unsigned)((MBEST_LSF*size_st[is])+1), sizeof (int));
	if (!i_mem)
	{
		fprintf (stderr, "allocation failure in MSVQ_multi_mbest_s !\n");
		exit(1);
	}

	/* remove codebook mean and scaling ... */
	for (k = 0; k < NLSF600; k ++)
	{
		tmp_s = sub (v[k], cbk_mst1_s[k]);
		vs[k] = shl (tmp_s, L_SHIFT_ST1);
	}

	/* M-best ... */
	for (m = 0; m < MBEST_LSF; m ++)
	{
		j = (m * size_st[is]);

		for (k = 1; k <= size_st[is]; k ++)
		{
			/* rebuild quantized vector at stage is ... */
			for (n = 0; n < NLSF600; n ++)
			{
				l_tmp1 = L_deposit_l (xq_s[is-1][m][n]);
				l_tmp2 = L_shl (l_tmp1, L_SHIFT_ST1);

				l_tmp1 = L_deposit_l (cbk_st_s[is][((k-1)*NLSF600)+n]);
				l_tmp2 = L_add (l_tmp1, l_tmp2);

				l_tmp1 = L_shr (l_tmp2, L_SHIFT_ST1);
				xq0_s[n] = extract_l (l_tmp1);
			}

			/* compute distorsion ... */
			ds[j+k] = MSVQ_WL2_distance_s (vs, xq0_s, w_lsf_s, 0, NLSF600-1);
		}
	}

	/* sorting ... */
	for (n = 1; n <= size_st[is]; n ++)
	{
		for (m = 0; m < MBEST_LSF; m ++)
		{
			i_best[n+(m*size_st[is])] = n-1;
			i_mem[n+(m*size_st[is])] = m;
		}
	}

	MSVQ_sorting_s (ds, i_best, i_mem, MBEST_LSF * size_st[is]);

	/* save results ... */
	for (m = 1; m <= MBEST_LSF; m ++)
	{
		for (n = 0; n < NLSF600; n ++)
		{
			l_tmp1 = L_deposit_l (xq_s[is-1][i_mem[m]][n]);
			l_tmp2 = L_shl (l_tmp1, L_SHIFT_ST1);

			l_tmp1 = L_deposit_l (cbk_st_s[is][(i_best[m]*NLSF600)+n]);
			l_tmp2 = L_add (l_tmp1, l_tmp2);

			l_tmp1 = L_shr (l_tmp2, L_SHIFT_ST1);
			xq_s[is][m-1][n] = extract_l (l_tmp1);
		}

		iq[isubframe][is][m-1] = i_best[m];
		ip[isubframe][is][m-1] = i_mem[m];
	}

	/* memory deallocation ... */
	free ((char*)(ds));
	free ((char*)(i_best));
	free ((char*)(i_mem));

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of MSVQ_multi_mbest_s () ------------------------------------ */
}

/* DS --------------------------------------------------------------------------
  Name:				MSVQ_WL2_distance_s
  Purpose:			this function computes weighted L2 distance.
  Arguments:		1 - (Shortword[]) c: 1st vector.	
					2 - (Shortword[]) x: 2nd vector.
					3 - (Shortword[]) w: weighting coefficients.
					4 - (int) N: vector size.
  Return:			R - (Shortword) distance.
-------------------------------------------------------------------------- DE */

static Shortword MSVQ_WL2_distance_s (Shortword c[], Shortword v[], \
												Shortword w[], int nl, int nh)
{
/* (CD) --- Declaration of variables for MSVQ_WL2_distance_s () ------------- */

	short		n;
	Shortword	ds, Ds;
	Shortword	ts[NLSF600];
	Longword	D;

	/* --- Initialization of bounds. ---------------------------------------- */

	v_equ (ts, v, (short)(nh-nl+1));
	v_sub (ts, c, (short)(nh-nl+1));

	for (n = 0; n < (short)(nh-nl+1); n++)
		ts[n] = shl (ts[n], 2);

	D = 0;

	for (n = 0; n < (short)(nh-nl+1); n++)
	{
		ds = mult (ts[n], w[n]);
		D = L_mac (D, ds, ts[n]);
	}
	
	Ds = extract_h (D);

	/* ---------------------------------------------------------------------- */

	return (Ds);

/* (CD) --- End of MSVQ_WL2_distance_s () ----------------------------------- */
}

/*DS----------------------------------------------------------------------------
  Name:				MSVQ_sorting_s
  Purpose:			this function performs array sorting.
  Arguments:		1 - (Shortword[]) ra: input/output array.
					2 - (int[]) rb1: input/output array.
					3 - (int[]) rb2: input/output array.
					4 - (int) n: array size.
  Return:			R - none.
------------------------------------------------------------------------------*/

static void MSVQ_sorting_s (Shortword ra[], int rb1[], int rb2[], int n)
{
/* (CD) --- Declaration of variables for MSVQ_sorting_s () ------------------ */
	
	short		i, j, l, ir;
	int			rrb1, rrb2;
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

/* (CD) --- End of MSVQ_sorting_s () ---------------------------------------- */
}

/*DS----------------------------------------------------------------------------
  Name:				MSVQ_set_pointers
  Purpose:			this function performs msvq codebook initialisation.
  Arguments:		1 - (int) iclass: class index.
					2 - (int) icbk: codebook index.
					3 - (int) isubframe: sub-frame index.
  Return:			R - none.
------------------------------------------------------------------------------*/

static void MSVQ_set_pointers (int isubframe, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for MSVQ_set_pointers () --------------- */

	short	icl, icb;

	/* ---------------------------------------------------------------------- */

	/* initialization */
	icl = qpar->iclass[isubframe];
	icb = qpar->icbk_lsf[isubframe];

	/* number of stages for lsf quantization ... */
	if ((icl == 0) && (icb == 0))
		qpar->nstg_lsf[isubframe] = 4;
	else
		qpar->nstg_lsf[isubframe] = 3;

	/* codebook size ... */
	if ((icl == 0) && (icb == 0))
	{
		size_st[0] = NST1;
		size_st[1] = NST2;
		size_st[2] = NST3;
		size_st[3] = NST4;

		qpar->nbits_lsf[isubframe][0] = NBITST1;
		qpar->nbits_lsf[isubframe][1] = NBITST2;
		qpar->nbits_lsf[isubframe][2] = NBITST3;
		qpar->nbits_lsf[isubframe][3] = NBITST4;
	}
	else if ((icl == 0) && (icb == 1))
	{
		size_st[0] = NST1;
		size_st[1] = NST2;
		size_st[2] = NST3;

		qpar->nbits_lsf[isubframe][0] = NBITST1;
		qpar->nbits_lsf[isubframe][1] = NBITST2;
		qpar->nbits_lsf[isubframe][2] = NBITST3;
	}
	else if (icb == 0)
	{
		size_st[0] = NaST1;
		size_st[1] = NaST2;
		size_st[2] = NaST3;

		qpar->nbits_lsf[isubframe][0] = NBITaST1;
		qpar->nbits_lsf[isubframe][1] = NBITaST2;
		qpar->nbits_lsf[isubframe][2] = NBITaST3;
	}
	else
	{
		size_st[0] = NbST1;
		size_st[1] = NbST2;
		size_st[2] = NbST3;

		qpar->nbits_lsf[isubframe][0] = NBITbST1;
		qpar->nbits_lsf[isubframe][1] = NBITbST2;
		qpar->nbits_lsf[isubframe][2] = NBITbST3;
	}

	/* set lsf codebook pointers ... */
	if ((icl == 0) && (icb == 0)) {
		cbk_mst1_s = m1st1_s;

		cbk_st_s[0] = c1st1_s;
		cbk_st_s[1] = c1st2_s;
		cbk_st_s[2] = c1st3_s;
		cbk_st_s[3] = c1st4_s;
	}
	else if ((icl == 0) && (icb == 1)) {
		cbk_mst1_s = m1st1_s;

		cbk_st_s[0] = c1st1_s;
		cbk_st_s[1] = c1st2_s;
		cbk_st_s[2] = c1st3_s;
	}
	else if ((icl == 1) && (icb == 0)) {
		cbk_mst1_s = m2ast1_s;

		cbk_st_s[0] = c2ast1_s;
		cbk_st_s[1] = c2ast2_s;
		cbk_st_s[2] = c2ast3_s;
	}
	else if ((icl == 1) && (icb == 1)) {
		cbk_mst1_s = m2bst1_s;

		cbk_st_s[0] = c2bst1_s;
		cbk_st_s[1] = c2bst2_s;
		cbk_st_s[2] = c2bst3_s;
	}
	else if ((icl == 2) && (icb == 0)) {
		cbk_mst1_s = m3ast1_s;

		cbk_st_s[0] = c3ast1_s;
		cbk_st_s[1] = c3ast2_s;
		cbk_st_s[2] = c3ast3_s;
	}
	else if ((icl == 2) && (icb == 1)) {
		cbk_mst1_s = m3bst1_s;

		cbk_st_s[0] = c3bst1_s;
		cbk_st_s[1] = c3bst2_s;
		cbk_st_s[2] = c3bst3_s;
	}
	else if ((icl == 3) && (icb == 0)) {
		cbk_mst1_s = m41ast1_s;

		cbk_st_s[0] = c41ast1_s;
		cbk_st_s[1] = c41ast2_s;
		cbk_st_s[2] = c41ast3_s;
	}
	else if ((icl == 3) && (icb == 1)) {
		cbk_mst1_s = m41bst1_s;

		cbk_st_s[0] = c41bst1_s;
		cbk_st_s[1] = c41bst2_s;
		cbk_st_s[2] = c41bst3_s;
	}
	else if ((icl == 4) && (icb == 0)) {
		cbk_mst1_s = m42ast1_s;

		cbk_st_s[0] = c42ast1_s;
		cbk_st_s[1] = c42ast2_s;
		cbk_st_s[2] = c42ast3_s;
	}
	else if ((icl == 4) && (icb == 1)) {
		cbk_mst1_s = m42bst1_s;

		cbk_st_s[0] = c42bst1_s;
		cbk_st_s[1] = c42bst2_s;
		cbk_st_s[2] = c42bst3_s;
	}
	else if ((icl == 5) && (icb == 0)) {
		cbk_mst1_s = m43ast1_s;

		cbk_st_s[0] = c43ast1_s;
		cbk_st_s[1] = c43ast2_s;
		cbk_st_s[2] = c43ast3_s;
	}
	else if ((icl == 5) && (icb == 1)) {
		cbk_mst1_s = m43bst1_s;

		cbk_st_s[0] = c43bst1_s;
		cbk_st_s[1] = c43bst2_s;
		cbk_st_s[2] = c43bst3_s;
	}
	else
	{
		fprintf (stderr, "\nWrong iclass and icbk parameters!\n");
		exit(1);
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of MSVQ_set_pointers () ------------------------------------- */
}

/*DS----------------------------------------------------------------------------
  Name:				MSVQ_check_weights
  Purpose:			test function.
					this function is called during:
						1: MELP encoding in routine MSVQ_lsf_quantization_s().
						2: MELP transcoding in routine TRSC_transcode_24to6_s().
  Arguments:		1 - (Shortword[]) w: weighting coefficients.
					2 - (int) N: number of weighting coefficients.
  Return:			R - none.
------------------------------------------------------------------------------*/

void MSVQ_check_weights (Shortword w[], int N)
{
/* (CD) --- Declaration of variables for MSVQ_check_weights () -------------- */

	short		i;
	Shortword	shift;

	/* ---------------------------------------------------------------------- */
    /* make sure weights don't get too big */
	
	shift = 0;
	
	for (i = 0; i < N; i ++)
	{
		if (w[i] > MAXWT4)
		{
			shift = 3;
			break;
		}
		else if (w[i] > MAXWT2)
		{
			shift = 2;
		}
		else if (w[i] > MAXWT)
		{
			if (shift == 0)
				shift = 1;
		}
	}
	
	for (i = 0; i < N; i++)
		w[i] = shr(w[i], shift);

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of MSVQ_check_weights () ------------------------------------ */
}

/*----------------------------------------------------------------------------*/