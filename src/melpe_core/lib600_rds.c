/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		lib600_rds.c
  VERSION:			v001
  CREATION:			07 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Read Bit Stream Library.
-------------------------------------------------------------------------- DE */

/*------------------------------------------------------------------------------
	Include Files
------------------------------------------------------------------------------*/

#include "main600.h"
#include "sc1200.h"

#include "sc600.h"

#include "lib600_str.h"
#include "lib600_bfi.h"
#include "lib600_mode.h"
#include "lib600_gain.h"
#include "lib600_qpit.h"
#include "lib600_msvq.h"
#include "lib600_rds.h"

#include "mathhalf.h"

/*------------------------------------------------------------------------------
	Declaration of global variables
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
	Declaration of external variables
------------------------------------------------------------------------------*/

extern struct melp_param   melp_par_600[NF600];

extern int bit_order600[NMODE600][NBITS600];

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

/* DS --------------------------------------------------------------------------
  Name:				RDS_read_stream
  Purpose:			this function performs encoded stream interpretation.
					this function is called during main processing loop for
					MELP decoding and transcoding.
  Arguments:		1 - (char*) stream_bit: input bit stream.
					2 - (quant_param600 *) qpar: quantization structure.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void RDS_read_stream (char *stream_bit, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for RDS_read_stream () ----------------- */

	short	i, k, n, p;
	char	stream_char[NBITS600];
	char	stream_char_reord[NBITS600];
	static char stream_char_memory[NBITS600];
	static int	flag = 0;

	/* ---------------------------------------------------------------------- */

	/* bit stream decompression ... */
	STR_bitstream_decompression (stream_bit, stream_char_reord, NBITS600);

	if (flag == 0)
	{
		flag = 1;
		for (i = 0; i < NBITS600; i ++)
			stream_char_memory[i] = stream_char_reord[i];
	}

	/* restore BFI ... */
	bfi600 = 0;

	/* BFI processing ... */
	/* if frame is ok, save new bit stream ... */
	if (bfi600 == 0)
	{
		for (i = 0; i < NBITS600; i ++)
			stream_char_memory[i] = stream_char_reord[i];
	}

	/* if BFI active, restore previous bit stream ... */
	if (bfi600 == 1)
	{
		for (i = 0; i < NBITS600; i ++)
			stream_char_reord[i] = stream_char_memory[i];
	}

	/* restore voicing ... */
	qpar->voicing_iq = (int)(STR_get_val (NBIT_VOICING, stream_char_reord));

	k = qpar->voicing_iq * NF600 * NUM_BANDS;

	for (i = 0; i < NF600; i ++)
	{
		n = k + (i * NUM_BANDS);

		for (p = 0; p < NUM_BANDS; p ++)
		{
			if (v_cbk[n+p] == 1)
				melp_par_600[i].bpvc[p] = 16384;
			else
				melp_par_600[i].bpvc[p] = 0;
		}

		if (melp_par_600[i].bpvc[0] > BPTHRESH_Q14)
			melp_par_600[i].uv_flag = FALSE;
		else
			melp_par_600[i].uv_flag = TRUE;
	}

	/* decoding quantization mode .... */
	MODE_decoding_mode (qpar);

	/* reordering ... */
	for (k = 0; k < NBITS600; k ++)
	{
		n = bit_order600[qpar->mode600][k];
		stream_char[n] = stream_char_reord[k];
	}

	/* get parameters according to mode ... */
	switch (qpar->mode600)
	{
		case 0:		RDS_read_mode0 (stream_char, qpar);
					break;
		case 1:		RDS_read_mode1 (stream_char, qpar);
					break;
		case 2:		RDS_read_mode2 (stream_char, qpar);
					break;
		case 3:		RDS_read_mode3 (stream_char, qpar);
					break;
		case 4:		RDS_read_mode4 (stream_char, qpar);
					break;
		case 5:		RDS_read_mode5 (stream_char, qpar);
					break;
		default:	fprintf (stderr, "\nWrong decoding mode!\n");
					exit(1);
	}

	/* restore MELP600 parameters ... */
	RDS_restore_parameters (qpar);

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of RDS_read_stream () --------------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				RDS_restore_parameters
  Purpose:			this function performs melp parameters restitution.
  Arguments:		1 - none.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void RDS_restore_parameters (quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for RDS_restore_parameters () ---------- */

	short		i, p;

	/* ---------------------------------------------------------------------- */

	/* restore LSF parameters of the first two frames ... */
	MSVQ_Iquantization_s (lsf600q_s, 0, qpar);

	for (i = 0; i < 2; i ++)
	{
		for (p = 0; p < LPC_ORD; p ++)
			melp_par_600[i].lsf[p] = lsf600q_s[(i*LPC_ORD)+p];
	}

	/* restore LSF parameters of the last two frames ... */
	MSVQ_Iquantization_s (lsf600q_s, 1, qpar);

	for (i = 0; i < 2; i ++)
	{
		for (p = 0; p < LPC_ORD; p ++)
			melp_par_600[i+2].lsf[p] = lsf600q_s[(i*LPC_ORD)+p];
	}

	/* repeat frame process if BFI == 1 ... */
	if (bfi600 == 1)
	{
		for (i = 0; i < NF600-1; i ++)
		{
			for (p = 0; p < LPC_ORD; p ++)
				melp_par_600[i].lsf[p] = melp_par_600[NF600-1].lsf[p];
		}
	}

	/* restore gain parameters ... */
	if (qpar->icbk_gain == 0)
	{
		GAIN_I_MSVQ76_s (g600q_s, qpar);
	}
	else if (qpar->icbk_gain == 1)
	{
		GAIN_I_MSVQ65_s (g600q_s, qpar);
	}
	else if (qpar->icbk_gain == 2)
	{
		GAIN_I_VQ9_s (g600q_s, qpar);
	}
	else
	{	
		fprintf (stderr, "\nWrong gain codebook index !\n");
		exit(1);
	}

	for (i = 0; i < NF600; i ++)
	{
		melp_par_600[i].gain[0] = g600q_s[(2*i)];
		melp_par_600[i].gain[1] = g600q_s[(2*i)+1];
	}

	/* repeat frame process if BFI == 1 ... */
	if (bfi600 == 1)
	{
		static int	start = 1;
		Shortword	gain;

		gain = melp_par_600[NF600-1].gain[1];

		if (start == 1)
		{
			start = 0;
			gain = 0;
		}

		for (i = 0; i < NF600; i ++)
		{
			melp_par_600[i].gain[0] = mult (att_gain, gain);
			att_gain = mult (ATT_GAIN, att_gain);

			melp_par_600[i].gain[1] = mult (att_gain, gain);
			att_gain = mult (ATT_GAIN, att_gain);
		}
	}
	else
	{
		att_gain = ATT_GAIN;
	}

	/* restore pitch trajectory ... */
	if (bfi600 == 0)
	{
		QPIT_decoding_s (melp_par_600, qpar);
	}

	/* repeat frame process if BFI == 1 ... */
	if (bfi600 == 1)
	{
		for (i = 0; i < NF600; i ++)
			melp_par_600[i].pitch = melp_par_600[NF600-1].pitch;
	}

	/* Voicing pattern repeat frame process if BFI == 1 ... */
	if (bfi600 == 1)
	{
		for (i = 0; i < NF600-1; i ++)
		{
			for (p = 0; p < NUM_BANDS; p ++)
				melp_par_600[i].bpvc[p] = melp_par_600[NF600-1].bpvc[p];
		}
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of RDS_restore_parameters () -------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				RDS_read_mode0
  Purpose:			this function performs melp parameters interpretation in
					codec mode 0.
  Arguments:		1 - (char*) stream_char: input bit stream.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void RDS_read_mode0 (char *stream_char, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for RDS_read_mode0 () ------------------ */

	short	i, k;
	short	icl, icb;
	short	v;
	char	*ptr;

	/* ---------------------------------------------------------------------- */

	/* initialization ... */
	ptr = stream_char + NBIT_VOICING;

	/* get lsf parameters ... */
	for (k = 0; k < NSUBFRAME600; k ++)
	{
		icl = qpar->iclass[k];
		icb = qpar->icbk_lsf[k];

		for (i = qpar->nstg_lsf[k]-1; i >= 0; i --)
		{
			v = STR_get_val (qpar->nbits_lsf[k][i], ptr);
			ptr += qpar->nbits_lsf[k][i];

			qpar->lsf_iq[k][i] = (int)(v);
		}
	}

	/* get gain parameter ... */
	for (i = qpar->nstg_gain-1; i >= 0; i --)
	{
		v = STR_get_val (qpar->nbits_gain[i], ptr);
		ptr += qpar->nbits_gain[i];

		qpar->gain_iq[i] = (int)(v);
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of RDS_read_mode0 () ---------------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				RDS_read_mode1
  Purpose:			this function performs melp parameters interpretation in
					codec mode 1.
  Arguments:		1 - (char*) stream_char: input bit stream.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void RDS_read_mode1 (char *stream_char, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for RDS_read_mode1 () ------------------ */

	short	i, k;
	short	icl, icb;
	short	v;
	char	*ptr;

	/* ---------------------------------------------------------------------- */

	/* initialization ... */
	ptr = stream_char + NBIT_VOICING;

	/* get pitch parameter ... */
	qpar->lag0_iq = (int)(STR_get_val (NBIT_PITCH1, ptr));
	ptr += NBIT_PITCH1;

	/* get lsf parameters ... */
	for (k = 0; k < NSUBFRAME600; k ++)
	{
		icl = qpar->iclass[k];
		icb = qpar->icbk_lsf[k];

		for (i = qpar->nstg_lsf[k]-1; i >= 0; i --)
		{
			v = STR_get_val (qpar->nbits_lsf[k][i], ptr);
			ptr += qpar->nbits_lsf[k][i];

			qpar->lsf_iq[k][i] = (int)(v);
		}
	}

	/* get gain parameter ... */
	for (i = qpar->nstg_gain-1; i >= 0; i --)
	{
		v = STR_get_val (qpar->nbits_gain[i], ptr);
		ptr += qpar->nbits_gain[i];

		qpar->gain_iq[i] = (int)(v);
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of RDS_read_mode1 () ---------------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				RDS_read_mode2
  Purpose:			this function performs melp parameters interpretation in
					codec mode 2.
  Arguments:		1 - (char*) stream_char: input bit stream.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void RDS_read_mode2 (char *stream_char, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for RDS_read_mode2 () ------------------ */

	short	i, k;
	short	icl, icb;
	short	v;
	char	*ptr;

	/* ---------------------------------------------------------------------- */

	/* initialization ... */
	ptr = stream_char + NBIT_VOICING;

	/* get pitch parameter ... */
	qpar->lag0_iq = (int)(STR_get_val (NBIT_PITCH2, ptr));
	ptr += NBIT_PITCH2;

	qpar->lag0_lq = (int)(STR_get_val (2, ptr));
	ptr += 2;

	qpar->lag0_tq = (int)(STR_get_val (1, ptr));
	ptr += 1;

	/* get lsf parameters ... */
	for (k = 0; k < NSUBFRAME600; k ++)
	{
		icl = qpar->iclass[k];
		icb = qpar->icbk_lsf[k];

		for (i = qpar->nstg_lsf[k]-1; i >= 0; i --)
		{
			v = STR_get_val (qpar->nbits_lsf[k][i], ptr);
			ptr += qpar->nbits_lsf[k][i];

			qpar->lsf_iq[k][i] = (int)(v);
		}
	}

	/* get gain parameter ... */
	for (i = qpar->nstg_gain-1; i >= 0; i --)
	{
		v = STR_get_val (qpar->nbits_gain[i], ptr);
		ptr += qpar->nbits_gain[i];

		qpar->gain_iq[i] = (int)(v);
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of RDS_read_mode2 () ---------------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				RDS_read_mode3
  Purpose:			this function performs melp parameters interpretation in
					codec mode 3.
  Arguments:		1 - (char*) stream_char: input bit stream.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void RDS_read_mode3 (char *stream_char, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for RDS_read_mode3 () ------------------ */
	
	short	i, k;
	short	icl, icb;
	short	v;
	char	*ptr;

	/* ---------------------------------------------------------------------- */

	/* initialization ... */
	ptr = stream_char + NBIT_VOICING;

	/* get pitch parameter ... */
	qpar->lag0_iq = (int)(STR_get_val (NBIT_PITCH2, ptr));
	ptr += NBIT_PITCH2;

	qpar->lag0_lq = (int)(STR_get_val (2, ptr));
	ptr += 2;

	qpar->lag0_tq = (int)(STR_get_val (1, ptr));
	ptr += 1;

	/* get lsf parameters ... */
	for (k = 0; k < NSUBFRAME600; k ++)
	{
		icl = qpar->iclass[k];
		icb = qpar->icbk_lsf[k];

		for (i = qpar->nstg_lsf[k]-1; i >= 0; i --)
		{
			v = STR_get_val (qpar->nbits_lsf[k][i], ptr);
			ptr += qpar->nbits_lsf[k][i];

			qpar->lsf_iq[k][i] = (int)(v);
		}
	}

	/* get gain parameter ... */
	for (i = qpar->nstg_gain-1; i >= 0; i --)
	{
		v = STR_get_val (qpar->nbits_gain[i], ptr);
		ptr += qpar->nbits_gain[i];

		qpar->gain_iq[i] = (int)(v);
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of RDS_read_mode3 () ---------------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				RDS_read_mode4
  Purpose:			this function performs melp parameters interpretation in
					codec mode 4.
  Arguments:		1 - (char*) stream_char: input bit stream.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void RDS_read_mode4 (char *stream_char, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for RDS_read_mode4 () ------------------ */

	short	i, k;
	short	icl, icb;
	short	v;
	char	*ptr;

	/* ---------------------------------------------------------------------- */

	/* initialization ... */
	ptr = stream_char + NBIT_VOICING;

	/* get pitch parameter ... */

	qpar->lag0_iq = (int)(STR_get_val (NBIT_PITCH2, ptr));
	ptr += NBIT_PITCH2;

	qpar->lag0_lq = (int)(STR_get_val (2, ptr));
	ptr += 2;

	qpar->lag0_tq = (int)(STR_get_val (1, ptr));
	ptr += 1;

	/* get lsf parameters ... */
	for (k = 0; k < NSUBFRAME600; k ++)
	{
		icl = qpar->iclass[k];
		icb = qpar->icbk_lsf[k];

		for (i = qpar->nstg_lsf[k]-1; i >= 0; i --)
		{
			v = STR_get_val (qpar->nbits_lsf[k][i], ptr);
			ptr += qpar->nbits_lsf[k][i];

			qpar->lsf_iq[k][i] = (int)(v);
		}
	}

	/* get gain parameter ... */
	for (i = qpar->nstg_gain-1; i >= 0; i --)
	{
		v = STR_get_val (qpar->nbits_gain[i], ptr);
		ptr += qpar->nbits_gain[i];

		qpar->gain_iq[i] = (int)(v);
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of RDS_read_mode4 () ---------------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				RDS_read_mode5
  Purpose:			this function performs melp parameters interpretation in
					codec mode 5.
  Arguments:		1 - (char*) stream_char: input bit stream.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void RDS_read_mode5 (char *stream_char, quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for RDS_read_mode5 () ------------------ */

	short	i, k;
	short	icl, icb;
	short	v;
	char	*ptr;

	/* ---------------------------------------------------------------------- */

	/* initialization ... */
	ptr = stream_char + NBIT_VOICING;

	/* get pitch parameter ... */
	qpar->lag0_iq = (int)(STR_get_val (NBIT_PITCH2, ptr));
	ptr += NBIT_PITCH2;

	qpar->lag0_lq = (int)(STR_get_val (2, ptr));
	ptr += 2;

	qpar->lag0_tq = (int)(STR_get_val (1, ptr));
	ptr += 1;

	/* get lsf parameters ... */
	for (k = 0; k < NSUBFRAME600; k ++)
	{
		icl = qpar->iclass[k];
		icb = qpar->icbk_lsf[k];

		for (i = qpar->nstg_lsf[k]-1; i >= 0; i --)
		{
			v = STR_get_val (qpar->nbits_lsf[k][i], ptr);
			ptr += qpar->nbits_lsf[k][i];

			qpar->lsf_iq[k][i] = (int)(v);
		}
	}

	/* get gain parameter ... */
	for (i = qpar->nstg_gain-1; i >= 0; i --)
	{
		v = STR_get_val (qpar->nbits_gain[i], ptr);
		ptr += qpar->nbits_gain[i];

		qpar->gain_iq[i] = (int)(v);
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of RDS_read_mode5 () ---------------------------------------- */
}

/*----------------------------------------------------------------------------*/