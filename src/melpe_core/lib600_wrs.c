/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		lib600_wrs.c
  VERSION:			v001
  CREATION:			07 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Bit Stream Handling Library.
-------------------------------------------------------------------------- DE */

/*------------------------------------------------------------------------------
	Include Files
------------------------------------------------------------------------------*/

#include "main600.h"
#include "sc600.h"
#include "lib600_str.h"
#include "lib600_wrs.h"

/*------------------------------------------------------------------------------
	Declaration of external variables
------------------------------------------------------------------------------*/

extern int bit_order600[NMODE600][NBITS600];

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

/* DS --------------------------------------------------------------------------
  Name:				WRS_build_stream
  Purpose:			this function writes encoded stream into file.
  Arguments:		1 - (quant_param600*) qpar: quantization structure.
					2 - (char*) stream_bit: output bit stream.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void WRS_build_stream (quant_param600 *qpar, char *stream_bit)
{
/* (CD) --- Declaration of variables for WRS_build_stream () ---------------- */

	short	n, k;
	char	stream_char[NBITS600];
	char	stream_char_reord[NBITS600];

	/* ---------------------------------------------------------------------- */

	/* generates bit stream according to classification mode ... */
	switch (qpar->mode600)
	{
		case 0:		/* 4 unvoiced frames ... */
					WRS_build_mode0 (qpar, stream_char);
					break;
		case 1:		/* 2 unvoiced frames / 2 mixed frames ... */
					WRS_build_mode1 (qpar, stream_char);
					break;
		case 2:		/* mixed frames ... */
					WRS_build_mode2 (qpar, stream_char);
					break;
		case 3:		/* 2 voiced frames / 2 unvoiced frames ... */
					WRS_build_mode3 (qpar, stream_char);
					break;
		case 4:		/* 2 voiced frames / 2 mixed frames ... */
					WRS_build_mode4 (qpar, stream_char);
					break;
		case 5:		/* 4 voiced frames ... */
					WRS_build_mode5 (qpar, stream_char);
					break;
		default:	fprintf (stderr,"\nWrong encoding mode!\n");
					exit(1);
	}

	/* set BFI = 0 ... */
	bfi600 = 0x00;

	/* stream reordering ... */
	for (n = 0; n < NBITS600; n ++)
	{
		k = bit_order600[qpar->mode600][n];
		stream_char_reord[n] = stream_char[k];
	}

	/* MELP600 bit stream compression: 54 bits encoded with 7 bytes ... */
	STR_bitstream_compression (stream_char_reord, stream_bit, NBITS600);

	/* ---------------------------------------------------------------------- */

	return;

	/* (CD) --- End of WRS_build_stream () ---------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				WRS_build_mode0
  Purpose:			this function generates encoded bit stream in mode 0,
					using 1 char for 1 bit encoding.
  Arguments:		1 - (char *) stream_char: bit stream.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void WRS_build_mode0 (quant_param600 *qpar, char *stream_char)
{
/* (CD) --- Declaration of variables for WRS_build_mode0 () ----------------- */

	short	i, k, nb;
	short	icl, icb;
	char	*ptr;
	short	v;

	/* ---------------------------------------------------------------------- */

	/* initialization ... */
	ptr = stream_char;

	/* save voicing information ... */
	STR_put_val (qpar->voicing_iq, NBIT_VOICING, ptr);
	ptr += NBIT_VOICING;

	/* save lsf parameters of each sub-frame ... */
	for (k = 0; k < NSUBFRAME600; k ++)
	{
		icl = qpar->iclass[k];
		icb = qpar->icbk_lsf[k];

		nb = qpar->nbits_lsf[k][qpar->nstg_lsf[k]-1];
		v = qpar->lsf_iq[k][qpar->nstg_lsf[k]-1];

		STR_put_val (v, nb, ptr);
		ptr += nb;

		for (i = qpar->nstg_lsf[k]-2; i >= 0; i --)
		{
			nb = qpar->nbits_lsf[k][i];
			v = qpar->lsf_iq[k][i];

			STR_put_val (v, nb, ptr);
			ptr += nb;
		}
	}

	/* save gain information ... */
	icb = qpar->icbk_gain;

	nb = qpar->nbits_gain[qpar->nstg_gain-1];
	v = qpar->gain_iq[qpar->nstg_gain-1];

	STR_put_val (v, nb, ptr);
	ptr += nb;

	for (i = qpar->nstg_gain-2; i >= 0; i --)
	{
		nb = qpar->nbits_gain[i];
		v = qpar->gain_iq[i];

		STR_put_val (v, nb, ptr);
		ptr += nb;
	}

	/* ---------------------------------------------------------------------- */

	return;

	/* (CD) --- End of WRS_build_mode0 () ----------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				WRS_build_mode1
  Purpose:			this function writes encoded stream in mode 1,
					using 1 char for 1 bit encoding.
  Arguments:		1 - (char *) stream_char: bit stream.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void WRS_build_mode1 (quant_param600 *qpar, char *stream_char)
{
/* (CD) --- Declaration of variables for WRS_build_mode1 () ----------------- */

	short	i, k, nb;
	short	icl, icb;
	char	*ptr;
	short	v;

	/* ---------------------------------------------------------------------- */

	/* initialization ... */
	ptr = stream_char;

	/* save voicing information ... */
	STR_put_val (qpar->voicing_iq, NBIT_VOICING, ptr);
	ptr += NBIT_VOICING;

	/* save pitch information ... */
	STR_put_val (qpar->lag0_iq, NBIT_PITCH1, ptr);
	ptr += NBIT_PITCH1;

	/* save lsf parameters of each sub-frame ... */
	for (k = 0; k < NSUBFRAME600; k ++)
	{
		icl = qpar->iclass[k];
		icb = qpar->icbk_lsf[k];

		nb = qpar->nbits_lsf[k][qpar->nstg_lsf[k]-1];
		v = qpar->lsf_iq[k][qpar->nstg_lsf[k]-1];

		STR_put_val (v, nb, ptr);
		ptr += nb;

		for (i = qpar->nstg_lsf[k]-2; i >= 0; i --)
		{
			nb = qpar->nbits_lsf[k][i];
			v = qpar->lsf_iq[k][i];

			STR_put_val (v, nb, ptr);
			ptr += nb;
		}
	}

	/* save gain information ... */
	icb = qpar->icbk_gain;

	nb = qpar->nbits_gain[qpar->nstg_gain-1];
	v = qpar->gain_iq[qpar->nstg_gain-1];

	STR_put_val (v, nb, ptr);
	ptr += nb;

	for (i = qpar->nstg_gain-2; i >= 0; i --)
	{
		nb = qpar->nbits_gain[i];
		v = qpar->gain_iq[i];

		STR_put_val (v, nb, ptr);
		ptr += nb;
	}

	/* ---------------------------------------------------------------------- */

	return;

	/* (CD) --- End of WRS_build_mode1 () ----------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				WRS_build_mode2
  Purpose:			this function writes encoded stream in mode 2,
					using 1 char for 1 bit encoding.
  Arguments:		1 - (char *) stream_char: bit stream.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void WRS_build_mode2 (quant_param600 *qpar, char *stream_char)
{
/* (CD) --- Declaration of variables for WRS_build_mode2 () ----------------- */

	short	i, k, nb;
	short	icl, icb;
	char	*ptr;
	short	v;

	/* ---------------------------------------------------------------------- */

	/* initialization ... */
	ptr = stream_char;

	/* save voicing information ... */
	STR_put_val (qpar->voicing_iq, NBIT_VOICING, ptr);
	ptr += NBIT_VOICING;

	/* save pitch information ... */
	STR_put_val (qpar->lag0_iq, NBIT_PITCH2, ptr);
	ptr += NBIT_PITCH2;
	STR_put_val (qpar->lag0_lq, 2, ptr);
	ptr += 2;
	STR_put_val (qpar->lag0_tq, 1, ptr);
	ptr += 1;

	/* save lsf parameters of each sub-frame ... */
	for (k = 0; k < NSUBFRAME600; k ++)
	{
		icl = qpar->iclass[k];
		icb = qpar->icbk_lsf[k];

		nb = qpar->nbits_lsf[k][qpar->nstg_lsf[k]-1];
		v = qpar->lsf_iq[k][qpar->nstg_lsf[k]-1];

		STR_put_val (v, nb, ptr);
		ptr += nb;

		for (i = qpar->nstg_lsf[k]-2; i >= 0; i --)
		{
			nb = qpar->nbits_lsf[k][i];
			v = qpar->lsf_iq[k][i];

			STR_put_val (v, nb, ptr);
			ptr += nb;
		}
	}

	/* save gain information ... */
	icb = qpar->icbk_gain;

	nb = qpar->nbits_gain[qpar->nstg_gain-1];
	v = qpar->gain_iq[qpar->nstg_gain-1];

	STR_put_val (v, nb, ptr);
	ptr += nb;

	for (i = qpar->nstg_gain-2; i >= 0; i --)
	{
		nb = qpar->nbits_gain[i];
		v = qpar->gain_iq[i];

		STR_put_val (v, nb, ptr);
		ptr += nb;
	}

	/* ---------------------------------------------------------------------- */

	return;

	/* (CD) --- End of WRS_build_mode2 () ----------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				WRS_build_mode3
  Purpose:			this function writes encoded stream in mode 3,
					using 1 char for 1 bit encoding.
  Arguments:		1 - (char *) stream_char: bit stream.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void WRS_build_mode3 (quant_param600 *qpar, char *stream_char)
{
/* (CD) --- Declaration of variables for WRS_build_mode3 () ----------------- */

	short	i, k, nb;
	short	icl, icb;
	char	*ptr;
	short	v;

	/* ---------------------------------------------------------------------- */

	/* initialization ... */
	ptr = stream_char;

	/* save voicing information ... */
	STR_put_val (qpar->voicing_iq, NBIT_VOICING, ptr);
	ptr += NBIT_VOICING;

	/* save pitch information ... */
	STR_put_val (qpar->lag0_iq, NBIT_PITCH2, ptr);
	ptr += NBIT_PITCH2;
	STR_put_val (qpar->lag0_lq, 2, ptr);
	ptr += 2;
	STR_put_val (qpar->lag0_tq, 1, ptr);
	ptr += 1;

	/* save lsf parameters of each sub-frame ... */
	for (k = 0; k < NSUBFRAME600; k ++)
	{
		icl = qpar->iclass[k];
		icb = qpar->icbk_lsf[k];

		nb = qpar->nbits_lsf[k][qpar->nstg_lsf[k]-1];
		v = qpar->lsf_iq[k][qpar->nstg_lsf[k]-1];

		STR_put_val (v, nb, ptr);
		ptr += nb;

		for (i = qpar->nstg_lsf[k]-2; i >= 0; i --)
		{
			nb = qpar->nbits_lsf[k][i];
			v = qpar->lsf_iq[k][i];

			STR_put_val (v, nb, ptr);
			ptr += nb;
		}
	}

	/* save gain information ... */
	icb = qpar->icbk_gain;

	nb = qpar->nbits_gain[qpar->nstg_gain-1];
	v = qpar->gain_iq[qpar->nstg_gain-1];

	STR_put_val (v, nb, ptr);
	ptr += nb;

	for (i = qpar->nstg_gain-2; i >= 0; i --)
	{
		nb = qpar->nbits_gain[i];
		v = qpar->gain_iq[i];

		STR_put_val (v, nb, ptr);
		ptr += nb;
	}

	/* ---------------------------------------------------------------------- */

	return;

	/* (CD) --- End of WRS_build_mode3 () ----------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				WRS_build_mode4
  Purpose:			this function writes encoded stream in mode 4,
					using 1 char for 1 bit encoding.
  Arguments:		1 - (char *) stream_char: bit stream.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void WRS_build_mode4 (quant_param600 *qpar, char *stream_char)
{
/* (CD) --- Declaration of variables for WRS_build_mode4 () ----------------- */

	short	i, k, nb;
	short	icl, icb;
	char	*ptr;
	short	v;

	/* ---------------------------------------------------------------------- */

	/* initialization ... */
	ptr = stream_char;

	/* save voicing information ... */
	STR_put_val (qpar->voicing_iq, NBIT_VOICING, ptr);
	ptr += NBIT_VOICING;

	/* save pitch information ... */
	STR_put_val (qpar->lag0_iq, NBIT_PITCH2, ptr);
	ptr += NBIT_PITCH2;
	STR_put_val (qpar->lag0_lq, 2, ptr);
	ptr += 2;
	STR_put_val (qpar->lag0_tq, 1, ptr);
	ptr += 1;

	/* save lsf parameters of each sub-frame ... */
	for (k = 0; k < NSUBFRAME600; k ++)
	{
		icl = qpar->iclass[k];
		icb = qpar->icbk_lsf[k];

		nb = qpar->nbits_lsf[k][qpar->nstg_lsf[k]-1];
		v = qpar->lsf_iq[k][qpar->nstg_lsf[k]-1];

		STR_put_val (v, nb, ptr);
		ptr += nb;

		for (i = qpar->nstg_lsf[k]-2; i >= 0; i --)
		{
			nb = qpar->nbits_lsf[k][i];
			v = qpar->lsf_iq[k][i];

			STR_put_val (v, nb, ptr);
			ptr += nb;
		}
	}

	/* save gain information ... */
	icb = qpar->icbk_gain;

	nb = qpar->nbits_gain[qpar->nstg_gain-1];
	v = qpar->gain_iq[qpar->nstg_gain-1];

	STR_put_val (v, nb, ptr);
	ptr += nb;

	for (i = qpar->nstg_gain-2; i >= 0; i --)
	{
		nb = qpar->nbits_gain[i];
		v = qpar->gain_iq[i];

		STR_put_val (v, nb, ptr);
		ptr += nb;
	}

	/* ---------------------------------------------------------------------- */

	return;

	/* (CD) --- End of WRS_build_mode4 () ----------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				WRS_build_mode5
  Purpose:			this function writes encoded stream in mode 5,
					using 1 char for 1 bit encoding.
  Arguments:		1 - (char *) stream_char: bit stream.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

static void WRS_build_mode5 (quant_param600 *qpar, char *stream_char)
{
/* (CD) --- Declaration of variables for WRS_build_mode5 () ----------------- */

	short	i, k, nb;
	short	icl, icb;
	char	*ptr;
	short	v;

	/* ---------------------------------------------------------------------- */

	/* initialization ... */
	ptr = stream_char;

	/* save voicing information ... */
	STR_put_val (qpar->voicing_iq, NBIT_VOICING, ptr);
	ptr += NBIT_VOICING;

	/* save pitch information ... */
	STR_put_val (qpar->lag0_iq, NBIT_PITCH2, ptr);
	ptr += NBIT_PITCH2;
	STR_put_val (qpar->lag0_lq, 2, ptr);
	ptr += 2;
	STR_put_val (qpar->lag0_tq, 1, ptr);
	ptr += 1;

	/* save lsf parameters of each sub-frame ... */
	for (k = 0; k < NSUBFRAME600; k ++)
	{
		icl = qpar->iclass[k];
		icb = qpar->icbk_lsf[k];

		nb = qpar->nbits_lsf[k][qpar->nstg_lsf[k]-1];
		v = qpar->lsf_iq[k][qpar->nstg_lsf[k]-1];

		STR_put_val (v, nb, ptr);
		ptr += nb;

		for (i = qpar->nstg_lsf[k]-2; i >= 0; i --)
		{
			nb = qpar->nbits_lsf[k][i];
			v = qpar->lsf_iq[k][i];

			STR_put_val (v, nb, ptr);
			ptr += nb;
		}
	}

	/* save gain information ... */
	{
		icb = qpar->icbk_gain;

		nb = qpar->nbits_gain[qpar->nstg_gain-1];
		v = qpar->gain_iq[qpar->nstg_gain-1];

		STR_put_val (v, nb, ptr);
		ptr += nb;

		for (i = qpar->nstg_gain-2; i >= 0; i --)
		{
			nb = qpar->nbits_gain[i];
			v = qpar->gain_iq[i];

			STR_put_val (v, nb, ptr);
			ptr += nb;
		}
	}

	/* ---------------------------------------------------------------------- */

	return;

	/* (CD) --- End of WRS_build_mode5 () ----------------------------------- */
}

/*----------------------------------------------------------------------------*/