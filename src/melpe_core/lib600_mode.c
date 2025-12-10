/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		lib600_mode.c
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Encoding Mode Determination Library.
-------------------------------------------------------------------------- DE */

/*------------------------------------------------------------------------------
	Include Files
------------------------------------------------------------------------------*/

#include "main600.h"
#include "sc600.h"
#include "lib600_mode.h"
#include "ext600_mode.h"

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

/* DS --------------------------------------------------------------------------
  Name:				MODE_encoding_mode
  Purpose:			this function performs encoding mode determination.
					this function is called during:
						1: MELP encoding in routine analysis().
						2: MELP transcoding in routine TRSC_transcode_24to6_s().
  Arguments:		1 - (void): none.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void MODE_encoding_mode (quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for MODE_encoding_mode () -------------- */


	/* ---------------------------------------------------------------------- */

	/* classification of the first two frames ... */
	if (bpviq[0][0] == 0)
	{
		if (bpviq[1][0] == 0)
			qpar->iclass[0] = 0;				/* class 0: UU */
		else
			qpar->iclass[0] = 1;				/* class 1: UV */
	}
	else
	{
		if (bpviq[1][0] == 0)
			qpar->iclass[0] = 2;				/* class 2: VU */
		else
			qpar->iclass[0] = 3;				/* class 3,4,5: VV */
	}
	
	/* if fully voiced ... */
	if (qpar->iclass[0] == 3)
	{
		if ((bpviq[0][1] == 0) || (bpviq[1][1] == 0))
			qpar->iclass[0] = 3;		/* class 3: "low" voicing level */
		else
		{
			if ((bpviq[0][3] == 0) || (bpviq[1][3] == 0))
				qpar->iclass[0] = 4;	/* class 4: "medium" voicing level */
			else
				qpar->iclass[0] = 5;	/* class 5: "high" voicing level */
		}
	}

	/* classification of the last two frames ... */
	if (bpviq[2][0] == 0)
	{
		if (bpviq[3][0] == 0)
			qpar->iclass[1] = 0;				/* class 0: UU */
		else
			qpar->iclass[1] = 1;				/* class 1:	UV */
	}
	else
	{
		if (bpviq[3][0] == 0)
			qpar->iclass[1] = 2;				/* class 2: VU */
		else
			qpar->iclass[1] = 3;				/* class 3,4,5: VV */
	}

	/* if fully voiced ... */
	if (qpar->iclass[1] == 3)
	{
		if ((bpviq[2][1] == 0) || (bpviq[3][1] == 0))
			qpar->iclass[1] = 3;		/* class 3: "low" voicing level */
		else
		{
			if ((bpviq[2][3] == 0) || (bpviq[3][3] == 0))
				qpar->iclass[1] = 4;	/* class 4: "medium" voicing level */
			else
				qpar->iclass[1] = 5;	/* class 5: "high" voicing level */
		}
	}

	/* determination of the encoding mode ... */
	qpar->mode600 = MODE600[qpar->iclass[0]][qpar->iclass[1]];

	/* lsf codebook selection ... */
	qpar->icbk_lsf[0] = ICBK1LSF[qpar->iclass[0]][qpar->iclass[1]];
	qpar->icbk_lsf[1] = ICBK2LSF[qpar->iclass[0]][qpar->iclass[1]];

	/* gain codebook selection ... */
	qpar->icbk_gain = ICBKGAIN[qpar->mode600];
	qpar->nstg_gain = NSTGGAIN[qpar->mode600];

	qpar->nbits_gain[0] = NBITS1GAIN[qpar->mode600];
	qpar->nbits_gain[1] = NBITS2GAIN[qpar->mode600];

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of MODE_encoding_mode () ------------------------------------ */
}

/* DS --------------------------------------------------------------------------
  Name:				MODE_decoding_mode
  Purpose:			test function.
  Arguments:		1 - (void): none.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void MODE_decoding_mode (quant_param600 *qpar)
{
/* (CD) --- Declaration of variables for MODE_decoding_mode () -------------- */

	unsigned short		n, k, p;

	/* ---------------------------------------------------------------------- */

	/* restore voicing pattern ... */

	p = qpar->voicing_iq * NUM_BANDS * NF600;

	for (n = 0; n < NF600; n ++)
	{
		for (k = 0; k < NUM_BANDS; k ++)
		{
			bpviq[n][k] = v_cbk[p++];
		}
	}

	/* classification of the first two frames ... */
	if (bpviq[0][0] == 0)
	{
		if (bpviq[1][0] == 0)
			qpar->iclass[0] = 0;				/* class 0: UU */
		else
			qpar->iclass[0] = 1;				/* class 1: UV */
	}
	else
	{
		if (bpviq[1][0] == 0)
			qpar->iclass[0] = 2;				/* class 2: VU */
		else
			qpar->iclass[0] = 3;				/* class 3,4,5: VV */
	}
	
	/* if fully voiced ... */
	if (qpar->iclass[0] == 3)
	{
		if ((bpviq[0][1] == 0) || (bpviq[1][1] == 0))
			qpar->iclass[0] = 3;		/* class 3: "low" voicing level */
		else
		{
			if ((bpviq[0][3] == 0) || (bpviq[1][3] == 0))
				qpar->iclass[0] = 4;	/* class 4: "medium" voicing level */
			else
				qpar->iclass[0] = 5;	/* class 5: "high" voicing level */
		}
	}

	/* classification of the last two frames ... */
	if (bpviq[2][0] == 0)
	{
		if (bpviq[3][0] == 0)
			qpar->iclass[1] = 0;				/* class 0: UU */
		else
			qpar->iclass[1] = 1;				/* class 1:	UV */
	}
	else
	{
		if (bpviq[3][0] == 0)
			qpar->iclass[1] = 2;				/* class 2: VU */
		else
			qpar->iclass[1] = 3;				/* class 3,4,5: VV */
	}

	/* if fully voiced ... */
	if (qpar->iclass[1] == 3)
	{
		if ((bpviq[2][1] == 0) || (bpviq[3][1] == 0))
			qpar->iclass[1] = 3;		/* class 3: "low" voicing level */
		else
		{
			if ((bpviq[2][3] == 0) || (bpviq[3][3] == 0))
				qpar->iclass[1] = 4;	/* class 4: "medium" voicing level */
			else
				qpar->iclass[1] = 5;	/* class 5: "high" voicing level */
		}
	}

	/* determination of the encoding mode ... */
	qpar->mode600 = MODE600[qpar->iclass[0]][qpar->iclass[1]];

	/* lsf codebook selection ... */
	qpar->icbk_lsf[0] = ICBK1LSF[qpar->iclass[0]][qpar->iclass[1]];
	qpar->icbk_lsf[1] = ICBK2LSF[qpar->iclass[0]][qpar->iclass[1]];

	/* gain codebook selection ... */
	qpar->icbk_gain = ICBKGAIN[qpar->mode600];
	qpar->nstg_gain = NSTGGAIN[qpar->mode600];

	qpar->nbits_gain[0] = NBITS1GAIN[qpar->mode600];
	qpar->nbits_gain[1] = NBITS2GAIN[qpar->mode600];

	/* set gain codebook indicator ... */
	if ((qpar->mode600 == 0) || (qpar->mode600 == 1))
	{
		cbk_gain_s[0] = g76st1_s;
		cbk_gain_s[1] = g76st2_s;
	}
	else if ((qpar->mode600 == 2) || (qpar->mode600 == 3) || (qpar->mode600 == 4))
	{
		cbk_gain_s[0] = g65st1_s;
		cbk_gain_s[1] = g65st2_s;
	}
	else if (qpar->mode600 == 5)
	{
		cbk_gain_s[0] = g9_s;
	}
	else
	{
		fprintf (stderr, "\nWrong Mode Determination !\n");
		exit(1);
	}

	/* number of stages for lsf quantization ... */
	if ((qpar->iclass[0] == 0) && (qpar->icbk_lsf[0] == 0))
		qpar->nstg_lsf[0] = 4;
	else
		qpar->nstg_lsf[0] = 3;

	if ((qpar->iclass[1] == 0) && (qpar->icbk_lsf[1] == 0))
		qpar->nstg_lsf[1] = 4;
	else
		qpar->nstg_lsf[1] = 3;

	/* determination of the number of bits */
	if (qpar->iclass[0] == 0)
	{
		qpar->nbits_lsf[0][0] = NBITST1;
		qpar->nbits_lsf[0][1] = NBITST2;
		qpar->nbits_lsf[0][2] = NBITST3;
		qpar->nbits_lsf[0][3] = NBITST4;
	}
	else
	{
		if (qpar->icbk_lsf[0] == 0)
		{
			qpar->nbits_lsf[0][0] = NBITaST1;
			qpar->nbits_lsf[0][1] = NBITaST2;
			qpar->nbits_lsf[0][2] = NBITaST3;
		}
		else
		{
			qpar->nbits_lsf[0][0] = NBITbST1;
			qpar->nbits_lsf[0][1] = NBITbST2;
			qpar->nbits_lsf[0][2] = NBITbST3;
		}
	}

	if (qpar->iclass[1] == 0)
	{
		qpar->nbits_lsf[1][0] = NBITST1;
		qpar->nbits_lsf[1][1] = NBITST2;
		qpar->nbits_lsf[1][2] = NBITST3;
		qpar->nbits_lsf[1][3] = NBITST4;
	}
	else
	{
		if (qpar->icbk_lsf[1] == 0)
		{
			qpar->nbits_lsf[1][0] = NBITaST1;
			qpar->nbits_lsf[1][1] = NBITaST2;
			qpar->nbits_lsf[1][2] = NBITaST3;
		}
		else
		{
			qpar->nbits_lsf[1][0] = NBITbST1;
			qpar->nbits_lsf[1][1] = NBITbST2;
			qpar->nbits_lsf[1][2] = NBITbST3;
		}
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of MODE_decoding_mode () ------------------------------------ */
}

/*----------------------------------------------------------------------------*/