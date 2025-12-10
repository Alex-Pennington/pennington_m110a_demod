/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		lib600_voicing.c
  VERSION:			v001
  CREATION:			07 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Voicing Encoding Library.
-------------------------------------------------------------------------- DE */

/*------------------------------------------------------------------------------
	Include Files
------------------------------------------------------------------------------*/

#include "main600.h"
#include "sc600.h"
#include "mathhalf.h"
#include "lib600_voicing.h"
#include "ext600_voicing.h"

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

/* DS --------------------------------------------------------------------------
  Name:				VOI_voicing_quantization_s
  Purpose:			this function performs voicing quantization in MELP 600 mode.
					this function is called during:
						1: MELP encoding in routine analysis().
						2: MELP transcoding in routine TRSC_transcode_24to6_s().
  Arguments:		1 - (struct melp_param *) par: melp structure.
					2 - (Shortword*) viq: voicing codebook indice.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void VOI_voicing_quantization_s (struct melp_param *par, Shortword *viq)
{
/* (CD) --- Declaration of variables for VOI_voicing_quantization_s () ------- */

	short		iv, j, k, n, p;
	Shortword	D, D_min;
	Shortword	d1, d2, d3, d4;
	Shortword	x1[5] = {0,0,0,0,0};
	Shortword	x2[5] = {1,0,0,0,0};
	Shortword	x3[5] = {1,1,1,0,0};
	Shortword	x4[5] = {1,1,1,1,1};

	/* ---------------------------------------------------------------------- */

	for (n = 0; n < NF600; n ++)
	{
		if (par[n].bpvc[0] <= BPTHRESH_Q14)
		{
			for (k = 0; k < NUM_BANDS; k ++)
				bpvi[n][k] = 0;
		}
		else
		{
			bpvi[n][0] = 1;
				
			for (k = 1; k < NUM_BANDS; k ++)
				bpvi[n][k] = (par[n].bpvc[k] > BPTHRESH_Q14) ? 1 : 0;

			if ((bpvi[n][1] == 0) && (bpvi[n][2] == 0) && (bpvi[n][3] == 0))
				bpvi[n][4] = 0;
		}
	}

	/* ---------------------------------------------------------------------- */

	/* apply constraint ... */
	for (n = 0; n < NF600; n ++)
	{
		d1 = 0;
		d2 = 0;
		d3 = 0;
		d4 = 0;

		for (p = 0; p < NUM_BANDS; p ++)
		{
			d1 = add (d1, abs_s (sub (bpvi[n][p], x1[p])));
			d2 = add (d2, abs_s (sub (bpvi[n][p], x2[p])));
			d3 = add (d3, abs_s (sub (bpvi[n][p], x3[p])));
			d4 = add (d4, abs_s (sub (bpvi[n][p], x4[p])));
		}

		if ((d4 <= d3) && (d4 <= d2) && (d4 <= d1))
		{
			for (k = 0; k < NUM_BANDS; k ++)
				bpvi[n][k] = x4[k];
		}
		else if ((d3 <= d2) && (d3 <= d1))
		{
			for (k = 0; k < NUM_BANDS; k ++)
				bpvi[n][k] = x3[k];
		}
		else if (d2 <= d1)
		{
			for (k = 0; k < NUM_BANDS; k ++)
				bpvi[n][k] = x2[k];
		}
		else
		{
			for (k = 0; k < NUM_BANDS; k ++)
				bpvi[n][k] = x1[k];
		}
	}

	/* ---------------------------------------------------------------------- */

	D_min = (Shortword)(32767);
	*viq = 0;

	for (iv = 0; iv < VOICING_CBK_SIZE; iv ++)
	{
		k = iv * NF600 * NUM_BANDS;

		D = 0;

		for (n = 0; n < NF600; n ++)
		{
			j = k + (n * NUM_BANDS);

			for (p = 0; p < NUM_BANDS; p ++)
			{
				if (bpvi[n][p] != v_cbk[j+p])
					D = add (D, v_weight[p]);
			}
		}

		if (D < D_min)
		{
			D_min = D;
			*viq = iv;

			for (n = 0; n < NF600; n ++)
			{
				j = k + (n * NUM_BANDS);

				for (p = 0; p < NUM_BANDS; p ++)
					bpviq[n][p] = v_cbk[j+p];
			}
		}
	}

	/* ---------------------------------------------------------------------- */

	for (n = 0; n < NF600; n ++)
	{
		for (k = 0; k < NUM_BANDS; k ++)
		{
			if (bpviq[n][k] == 1)
				par[n].bpvc[k] = 16384;
			else
				par[n].bpvc[k] = 0;
		}

		if (par[n].bpvc[0] > BPTHRESH_Q14)
			par[n].uv_flag = FALSE;
		else
			par[n].uv_flag = TRUE;
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of VOI_voicing_quantization_s () ---------------------------- */
}

/*----------------------------------------------------------------------------*/