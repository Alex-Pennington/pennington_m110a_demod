/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILENAME:			lib600_transc.c
  VERSION:			v001
  CREATION:			September, 10th, 2003.
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Trancoding Library.
-------------------------------------------------------------------------- DE */

/*------------------------------------------------------------------------------
	Include Files
------------------------------------------------------------------------------*/

#include "main600.h"
#include "sc600.h"
#include "lib600_voicing.h"
#include "lib600_mode.h"
#include "lib600_qpit.h"
#include "lib600_gain.h"
#include "lib600_msvq.h"
#include "lib600_rds.h"
#include "lib600_wrs.h"
#include "lib600_transc.h"

#include "global.h"
#include "constant.h"
#include "vq_lib.h"
#include "fsvq_cb.h"
#include "msvq_cb.h"
#include "lpc_lib.h"
#include "math_lib.h"
#include "mathhalf.h"
#include "dsp_sub.h"
#include "melp_sub.h"

/*------------------------------------------------------------------------------
	Constants
------------------------------------------------------------------------------*/

#define BUFSIZE24	7
#define X025_Q15	8192

/*------------------------------------------------------------------------------
	Declaration of global variables
------------------------------------------------------------------------------*/

static struct melp_param	prev_par;

extern quant_param600	quant_par600;

/*------------------------------------------------------------------------------
	Prototypes
------------------------------------------------------------------------------*/

void sc_ana600 (struct melp_param *par);

Shortword *v_equ (Shortword vec1[], const Shortword vec2[], Shortword n);

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

/* DS --------------------------------------------------------------------------
  Name:				TRSC_transcode_24to6_s
  Purpose:			this function performs transcoding from MELP2400 to MELP600.
					this function is called during main processing loop.
  Arguments:		1 - (struct melp_param *) par: melp structure.
					2 - (char *) stream_bit: output bit stream.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void TRSC_transcode_24to6_s (struct melp_param *par, quant_param600 *qpar, \
															char *stream_bit)
{
/* (CD) --- Declaration of variables for TRSC_transcode_24to6_s () ---------- */

	short		n, k;
	Shortword	lpc[LPC_ORD+1];

	/* === Read and decode channel input buffer ============================= */
	melp_chn_read (&quant_par, &par[0], &prev_par, &chbuf[0*BUFSIZE24]);
	melp_chn_read (&quant_par, &par[1], &melp_par[0], &chbuf[1*BUFSIZE24]);
	melp_chn_read (&quant_par, &par[2], &melp_par[1], &chbuf[2*BUFSIZE24]);
	melp_chn_read (&quant_par, &par[3], &melp_par[2], &chbuf[3*BUFSIZE24]);
	
	prev_par = par[3];

	/* === New routine to refine the parameters for block =================== */
	if (REFINEMENT600 == TRUE)
		sc_ana600 (par);

	/* === MELP 600 Quantization ============================================ */

	/* --- Quantization of Band-Pass Voicing -------------------------------- */

	VOI_voicing_quantization_s (par, &qpar->voicing_iq);

	/* --- Encoding mode determination -------------------------------------- */

	MODE_encoding_mode (qpar);

	/* --- Pitch encoding --------------------------------------------------- */

	QPIT_encoding_s (par, qpar);

	/* --- Gain quantization ------------------------------------------------ */

	GAIN_gain_quantization_s (par, qpar);

	/* --- quantization of LSF coefficients for the first two frames -------- */

	/* set adaptive weights ... */
	for (n = 0; n < 2; n ++)
	{
        lpc[0] = ONE_Q12;
		
		lpc_lsp2pred (par[n].lsf, &(lpc[1]), LPC_ORD);

		vq_lspw (&w_lsf_s[n*LPC_ORD], par[n].lsf, &(lpc[1]), LPC_ORD);
		
		MSVQ_check_weights (&w_lsf_s[n*LPC_ORD], LPC_ORD);
	}

	/* concatenation of lsf vectors ... */
	for (n = 0; n < 2; n ++)
	{
		for (k = 0; k < LPC_ORD; k ++)
			lsf600_s[(n*LPC_ORD)+k] = par[n].lsf[k];
	}

	/* multi-stage vector quantization ... */
	MSVQ_Dquantization_s (lsf600_s, 0, qpar);

	/* --- quantization of LSF coefficients for the last two frames --------- */

	/* set adaptive weights ... */
	for (n = 0; n < 2; n ++)
	{
        lpc[0] = ONE_Q12;
		
		lpc_lsp2pred (par[n+2].lsf, &(lpc[1]), LPC_ORD);

		vq_lspw (&w_lsf_s[n*LPC_ORD], par[n+2].lsf, &(lpc[1]), LPC_ORD);

		MSVQ_check_weights (&w_lsf_s[n*LPC_ORD], LPC_ORD);
	}

	/* concatenation of lsf vectors ... */
	for (n = 0; n < 2; n ++)
	{
		for (k = 0; k < LPC_ORD; k ++)
			lsf600_s[(n*LPC_ORD)+k] = par[n+2].lsf[k];
	}

	/* multi-stage vector quantization ... */
	MSVQ_Dquantization_s (lsf600_s, 1, qpar);

	/* build MELP600 bit stream ... */
	WRS_build_stream (&quant_par600, stream_bit);

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of TRSC_transcode_24to6_s () -------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				TRSC_transcode_6to24_s
  Purpose:			this function performs transcoding from MELP600 to MELP2400.
					this function is called during main processing loop.
  Arguments:		1 - (char *) stream_bit: input bit stream.
  					2 - (struct melp_param *) par: melp structure.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void TRSC_transcode_6to24_s (char *stream_bit, struct melp_param *par)
{
/* (CD) --- Declaration of variables for TRSC_transcode_6to24_s () ---------- */

    short  n;
    Shortword   lpc[LPC_ORD + 1], weights[LPC_ORD];

	/* ---------------------------------------------------------------------- */

	/* read MELP600 bit stream ... */
	RDS_read_stream (stream_bit, &quant_par600);

	/* === MELP 2400 quantization =========================================== */

	for (n = 0; n < NF600; n++)
	{
		/* --- LSF to prediction coefficients. ------------------------------ */
        lpc[0] = ONE_Q12;
		
		/* --- Quantize LSF's with MSVQ ------------------------------------- */	
		lpc_lsp2pred (par[n].lsf, &(lpc[1]), LPC_ORD);

		vq_lspw (weights, par[n].lsf, &(lpc[1]), LPC_ORD);

		vq_ms4 (msvq_cb, par[n].lsf, msvq_cb_mean, msvq_levels, MSVQ_M, 4,\
				LPC_ORD, weights, par[n].lsf, quant_par.msvq_index, MSVQ_MAXCNT);

		/* --- Force minimum LSF bandwidth (separation) --------------------- */
        lpc_clamp (par[n].lsf, BWMIN_Q15, LPC_ORD);

		/* --- Quantize bandpass voicing ------------------------------------ */
		par[n].uv_flag = q_bpvc (par[n].bpvc, &(quant_par.bpvc_index[0]), NUM_BANDS);

		/* --- Restore jitter parameter ------------------------------------- */
		if (par[n].uv_flag)
		{
			par[n].pitch = UV_PITCH_Q7;
			par[n].jitter = MAX_JITTER_Q15;
			quant_par.jit_index[0] = 1;
		}
		else
		{
			par[n].jitter = 0;
			quant_par.jit_index[0] = 0;
		}

		/* --- Quantize logarithmic pitch period ---------------------------- */
		par[n].pitch = log10_fxp (par[n].pitch, 7);
        quant_u (&par[n].pitch, &(quant_par.pitch_index), PIT_QLO_Q12, \
								PIT_QUP_Q12, PIT_QLEV_M1, PIT_QLEV_M1_Q8, 1, 7);
		par[n].pitch = pow10_fxp (par[n].pitch, 7);

		/* --- Quantize gain terms with uniform log quantizer --------------- */
		q_gain (par[n].gain, quant_par.gain_index, GN_QLO_Q8, GN_QUP_Q8, \
											GN_QLEV_M1, GN_QLEV_M1_Q10, 0, 5);

		/* --- Quantize Fourier coefficients -------------------------------- */
		fill (par[n].fs_mag, ONE_Q13, NUM_HARM);

		window_Q (par[n].fs_mag, w_fs, par[n].fs_mag, NUM_HARM, 14);

		vq_enc (fsvq_cb, par[n].fs_mag, FS_LEVELS, NUM_HARM, par[n].fs_mag, \
														&(quant_par.fsvq_index));
		
		quant_par.uv_flag[0] = par[n].uv_flag;

		/* --- Write channel bitstream -------------------------------------- */
		melp_chn_write (&quant_par, &chbuf[n*BUFSIZE24]);
    }

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of TRSC_transcode_6to24_s () -------------------------------- */
}

/*----------------------------------------------------------------------------*/