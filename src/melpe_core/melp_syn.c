/* ================================================================== */
/*                                                                    */
/*    Microsoft Speech coder     ANSI-C Source Code                   */
/*    1200/2400 bps MELPe speech coder                                */
/*    Fixed Point Implementation      Version 8.0                     */
/*    Copyright (C) 2000-2001, Microsoft Corp.                        */
/*    All rights reserved.                                            */
/*                                                                    */
/* ================================================================== */

/*

2.4 kbps MELP Proposed Federal Standard speech coder

Fixed-point C code, version 1.0

Copyright (c) 1998, Texas Instruments, Inc.

Texas Instruments has intellectual property rights on the MELP
algorithm.  The Texas Instruments contact for licensing issues for
commercial and non-government use is William Gordon, Director,
Government Contracts, Texas Instruments Incorporated, Semiconductor
Group (phone 972 480 7442).

The fixed-point version of the voice codec Mixed Excitation Linear
Prediction (MELP) is based on specifications on the C-language software
simulation contained in GSM 06.06 which is protected by copyright and
is the property of the European Telecommunications Standards Institute
(ETSI). This standard is available from the ETSI publication office
tel. +33 (0)4 92 94 42 58. ETSI has granted a license to United States
Department of Defense to use the C-language software simulation contained
in GSM 06.06 for the purposes of the development of a fixed-point
version of the voice codec Mixed Excitation Linear Prediction (MELP).
Requests for authorization to make other use of the GSM 06.06 or
otherwise distribute or modify them need to be addressed to the ETSI
Secretariat fax: +33 493 65 47 16.

*/

#include "sc1200.h"
#include "mathhalf.h"
#include "macro.h"
#include "lpc_lib.h"
#include "mat_lib.h"
#include "vq_lib.h"
#include "fs_lib.h"
#include "math_lib.h"
#include "constant.h"
#include "global.h"
#include "harm.h"
#include "fsvq_cb.h"
#include "dsp_sub.h"
#include "melp_sub.h"
#include "coeff.h"
#include "postfilt.h"

#include "msvq_cb.h"

/*=============================================================================
	Added included files for MELP 600 bit/sec
=============================================================================*/

#include "sc600.h"
#include "lib600_rds.h"

/*===========================================================================*/

#define INV_LPC_ORD         2979
                               /* ((1.0/(LPC_ORD + 1))*(1<<15) + 0.5) for Q15 */
#define X005_Q19            26214                         /* 0.05 * (1 << 19) */
#define X025_Q15            8192                          /* 0.25 * (1 << 15) */
#define X1_732_Q14          28377                        /* 1.732 * (1 << 14) */
#define X12_Q8              3072                           /* 12.0 * (1 << 8) */
#define X30_Q8              7680                           /* 30.0 * (1 << 8) */
#define TILT_ORD            1
#define SCALEOVER           10
#define INV_SCALEOVER_Q18   26214              /* ((1.0/SCALEOVER)*(1 << 18)) */
#define PDEL                SCALEOVER

#if (MIX_ORD > DISP_ORD)
#define BEGIN MIX_ORD
#else
#define BEGIN DISP_ORD
#endif

#define ORIGINAL_SYNTH_GAIN     FALSE
#if ORIGINAL_SYNTH_GAIN
#define SYN_GAIN_Q4             16000                    /* (1000.0*(1 << 4)) */
#else
#define SYN_GAIN_Q4             32000
#endif


static struct melp_param    prev_par;
static Shortword    sigsave[PITCHMAX];
static Shortword    syn_begin;
static Boolean  erase;

extern char chbuf600[NBYTES600];

extern quant_param600	quant_par600;

/* Prototype */

//static void     melp_syn(struct melp_param *par, Shortword sp_out[]);
//ARCON added an int parameter for post filter bypass
static void     melp_syn(struct melp_param *par, Shortword sp_out[], int filter_flag);

/****************************************************************************
**
** Function:        synthesis
**
** Description:     The synthesis routine for the sc1200 coder
**
** Arguments:
**
**  melp_param *par ---- output encoded melp parameters
**  Shortword sp_in[] ---- input speech data buffer
**
** Return value:    None
**
*****************************************************************************/
// void synthesis(struct melp_param *par, Shortword sp_out[])
//ARCON added an int parameter for post filter bypass
void synthesis(struct melp_param *par, Shortword sp_out[], int filter_flag)
{
    register Shortword  i;

	/* --- Copy previous period of processed speech to output array --------- */

	if (syn_begin > 0)
	{
		if (syn_begin > frameSize)
		{
			v_equ (sp_out, sigsave, frameSize);
			
			/* past end: save remainder in sigsave[0] */
			v_equ (sigsave, &sigsave[frameSize], \
										(Shortword)(syn_begin - frameSize));
		}
		else
			v_equ (sp_out, sigsave, syn_begin);
	}
	
	erase = FALSE;	/* no erasures yet */

	if (rate == RATE2400)
	{
		erase = melp_chn_read (&quant_par, par, &prev_par, chbuf);
		par->uv_flag = quant_par.uv_flag[0];
		melp_syn (par, sp_out, filter_flag);
	}

	if (rate == RATE1200)
	{
		erase = (Boolean)low_rate_chn_read (&quant_par, par, &prev_par);
		
		for (i = 0; i < NF; i++)
		{
			melp_syn (&par[i], &sp_out[i*FRAME], filter_flag);
			
			if ((syn_begin > 0) && (i < NF - 1))
				v_equ (&sp_out[(i + 1)*FRAME], sigsave, syn_begin);
		}
	}

	if (rate == RATE600)
	{
		RDS_read_stream (chbuf600, &quant_par600);

		for (i = 0; i < NF600; i ++)
		{
			melp_syn (&par[i], &sp_out[i*FRAME], filter_flag);
			
			if ((syn_begin > 0) && (i < NF600 - 1))
				v_equ (&sp_out[(i+1)*FRAME], sigsave, syn_begin);
		}
	}

	return;
}


/* Name: melp_syn.c                                                           */
/*  Description: MELP synthesis                                               */
/*    This program takes the new parameters for a speech                      */
/*    frame and synthesizes the output speech.  It keeps                      */
/*    an internal record of the previous frame parameters                     */
/*    to use for interpolation.                                               */
/*  Inputs:                                                                   */
/*    *par - MELP parameter structure                                         */
/*  Outputs:                                                                  */
/*    speech[] - output speech signal                                         */
/*  Returns: void                                                             */

// static void     melp_syn(struct melp_param *par, Shortword sp_out[])
//ARCON added an int parameter for post filter bypass
static void     melp_syn(struct melp_param *par, Shortword sp_out[], int filter_flag)
{
	register Shortword  i;
    static Boolean  firstTime = TRUE;
    static Shortword    noise_gain = MIN_NOISE_Q8;
    static Shortword    prev_lpc_gain = ONE_Q15;
    static Shortword    lpc_del[LPC_ORD];                               /* Q0 */
    static Shortword    prev_tilt;
    static Shortword    prev_pcof[MIX_ORD + 1], prev_ncof[MIX_ORD + 1];
    static Shortword    disp_del[DISP_ORD];
    static Shortword    ase_del[LPC_ORD], tilt_del[TILT_ORD];
    static Shortword    pulse_del[MIX_ORD], noise_del[MIX_ORD];
    Shortword   fs_real[PITCHMAX];
    Shortword   refl_coef[LPC_ORD];
    Shortword   sigbuf[BEGIN + PITCHMAX];
    Shortword   gaincnt, length;
    Shortword   intfact, intfact1, ifact, ifact_gain;
    Shortword   gain, pulse_gain, pitch, jitter;
    Shortword   curr_tilt, tilt_cof[TILT_ORD + 1];
    Shortword   sig_prob, syn_gain, lpc_gain;
    Shortword   lsf[LPC_ORD];
    Shortword   lpc[LPC_ORD + 1];
    Shortword   ase_num[LPC_ORD + 1], ase_den[LPC_ORD];
    Shortword   curr_pcof[MIX_ORD + 1], curr_ncof[MIX_ORD + 1];
    Shortword   pulse_cof[MIX_ORD + 1], noise_cof[MIX_ORD + 1];
    Shortword   temp1, temp2;
    Longword    L_temp1, L_temp2;
    Shortword   fc_prev, fc_curr, fc;

	/* --- Update adaptive noise level estimate based on last gain ---------- */
	if (firstTime)
	{
		noise_gain = par->gain[NUM_GAINFR-1];		/* noise_gain in Q8 */
		prev_tilt = 0;
		
		v_zap (prev_pcof, MIX_ORD + 1);
		v_zap (prev_ncof, MIX_ORD + 1);
		
		prev_ncof[MIX_ORD/2] = ONE_Q15;
		
		v_zap (disp_del, DISP_ORD);
		v_zap (ase_del, LPC_ORD);
		v_zap (tilt_del, TILT_ORD);
		v_zap (pulse_del, MIX_ORD);
		v_zap (noise_del, MIX_ORD);
		
		firstTime = FALSE;
	}
	else if (!erase)
	{
		for (i = 0; i < NUM_GAINFR; i++)
		{
			noise_est (par->gain[i], &noise_gain, UPCONST_Q19, DOWNCONST_Q17,\
													MIN_NOISE_Q8, MAX_NOISE_Q8);
			
			/* --- Adjust gain based on noise level (noise suppression) ----- */
			noise_sup (&par->gain[i], noise_gain, MAX_NS_SUP_Q8, \
													MAX_NS_ATT_Q8, NFACT_Q8);
		}
	}

	if (par->uv_flag && (rate == RATE1200))
	{
		fill (par->fs_mag, ONE_Q13, NUM_HARM);
		par->pitch = UV_PITCH_Q7;
		par->jitter = X025_Q15;
	}

	if (rate == RATE600)
	{
		fill (par->fs_mag, ONE_Q13, NUM_HARM);

		if (par->uv_flag)
		{
			par->pitch = UV_PITCH_Q7;
			par->jitter = MAX_JITTER_Q15;
		}
		else
		{
			par->jitter = 0;
		}
	}

	/* --- Un-weight Fourier magnitudes ------------------------------------- */
	
	if (!par->uv_flag && !erase)
		window_Q (par->fs_mag, w_fs_inv, par->fs_mag, NUM_HARM, 14);

	/* --- Clamp LSP bandwidths to avoid sharp LPC filters ------------------ */
	
	lpc_clamp (par->lsf, BWMIN_Q15, LPC_ORD);
	
	/* --- Calculate spectral tilt for current frame for spectral enhancement */
	
	tilt_cof[0] = ONE_Q15;
	lpc_lsp2pred (par->lsf, &(lpc[1]), LPC_ORD);

	/* --- Use LPC prediction gain for adaptive scaling -------------------- */

	/*
		lpc_gain = sqrt(lpc_pred2refl(lpc, tilt_cof, LPC_ORD));
	
		Here we only make use of tilt_cof[0] from the returned value
		instead of using the whole array tilt_cof[].
	*/
	lpc_gain = lpc_pred2refl (&(lpc[1]), refl_coef, LPC_ORD);
	lpc_gain = sqrt_fxp (lpc_gain, 15);			/* lpc_gain in Q15 */
	
	if (refl_coef[0] < 0)
		curr_tilt = shr(refl_coef[0], 1);		/* curr_tilt in Q15 */
	else
		curr_tilt = 0;

	/* --- Disable pitch interpolation for high-pitched onsets -------------- */
	
	temp1 = shr(prev_par.pitch, 1);
	temp2 = add(SIX_Q8, prev_par.gain[NUM_GAINFR - 1]);

	if ((par->pitch < temp1) && (par->gain[0] > temp2))
	{
		/* copy current pitch into previous */
		prev_par.pitch = par->pitch;
	}

	/* --- Set pulse and noise coefficients based on voicing strengths ------ */
	
	v_zap (curr_pcof, MIX_ORD + 1);					/* curr_pcof in Q14 */
	v_zap (curr_ncof, MIX_ORD + 1);					/* curr_ncof in Q14 */

	for (i = 0; i < NUM_BANDS; i++)
	{
		if (par->bpvc[i] > X05_Q14)
			v_add(curr_pcof, bp_cof[i], MIX_ORD + 1);	/* bp_cof in Q14 */
		else
			v_add(curr_ncof, bp_cof[i], MIX_ORD + 1);
	}
	
	/* --- Process each pitch period ---------------------------------------- */
	
	while (syn_begin < FRAME)
	{
		/* --- interpolate previous and current parameters ------------------ */
		
		ifact = divide_s(syn_begin, FRAME);				/* ifact in Q15 */
		
		if (syn_begin >= GAINFR)
		{
			gaincnt = 2;
			temp1 = sub(syn_begin, GAINFR);
			ifact_gain = divide_s(temp1, GAINFR);
		}
		else
		{
			gaincnt = 1;
			ifact_gain = divide_s(syn_begin, GAINFR);
		}

		/* interpolate gain.  It is assumed that par->gain[] are obtained     */
        /* from gain_vq_cb[] in "qnt12_cb.c", and gain_vq_cb[] lies between   */
        /* 2564 and 18965 (Q8).  Therefore, the interpolated "gain" is also   */
        /* assumed to be between these two values.                            */
		
		if (gaincnt > 1)
		{
			/*
				gain = ifact_gain * par->gain[gaincnt - 1] + 
						(1.0 - ifact_gain) * par->gain[gaincnt - 2];
			*/
			L_temp1 = L_mult(par->gain[gaincnt - 1], ifact_gain);
			temp1 = sub(ONE_Q15, ifact_gain);
			L_temp2 = L_mult(par->gain[gaincnt - 2], temp1);
			gain = extract_h(L_add(L_temp1, L_temp2));		/* gain in Q8 */
		}
		else
		{
			/*
				gain = ifact_gain * par->gain[gaincnt - 1] + 
						(1.0 - ifact_gain) * prev_par.gain[NUM_GAINFR - 1];
			*/
			L_temp1 = L_mult(par->gain[gaincnt - 1], ifact_gain);
			temp1 = sub(ONE_Q15, ifact_gain);
			L_temp2 = L_mult(prev_par.gain[NUM_GAINFR - 1], temp1);
			gain = extract_h(L_add(L_temp1, L_temp2));		/* gain in Q8 */
		}

		/* --- Set overall interpolation path based on gain change ---------- */
		
		temp1 = sub(par->gain[NUM_GAINFR - 1], prev_par.gain[NUM_GAINFR - 1]);
		
		if (abs_s(temp1) > SIX_Q8)
		{
			/* Power surge: use gain adjusted interpolation */
			/* intfact = (gain - prev_par.gain[NUM_GAINFR - 1])/temp; */

            temp2 = sub(gain, prev_par.gain[NUM_GAINFR - 1]);
			
			if (((temp2 > 0) && (temp1 < 0)) || ((temp2 < 0) && (temp1 > 0)))
				intfact = 0;
			else
			{
				temp1 = abs_s(temp1);
				temp2 = abs_s(temp2);
				
				if (temp2 >= temp1)
					intfact = ONE_Q15;
				else
					intfact = divide_s(temp2, temp1);	/* intfact in Q15 */
			}
		}
		else							/* Otherwise, linear interpolation */
			intfact = ifact;

		/* --- interpolate LSF's and convert to LPC filter ------------------ */
		
		interp_array (prev_par.lsf, par->lsf, lsf, intfact, LPC_ORD);

		lpc_lsp2pred (lsf, &(lpc[1]), LPC_ORD);

		/* Check signal probability for adaptive spectral enhancement filter */
		
		temp1 = add(noise_gain, X12_Q8);
        temp2 = add(noise_gain, X30_Q8);
		
		/* sig_prob in Q15 */
		sig_prob = lin_int_bnd (gain, temp1, temp2, 0, ONE_Q15);

		/* --- Calculate adaptive spectral enhancement filter coefficients -- */
		
		ase_num[0] = ONE_Q12;				/* ase_num and ase_den in Q12 */
		
		temp1 = mult(sig_prob, ASE_NUM_BW_Q15);
		lpc_bw_expand (&(lpc[1]), &(ase_num[1]), temp1, LPC_ORD);
		
		temp1 = mult(sig_prob, ASE_DEN_BW_Q15);
		lpc_bw_expand (&(lpc[1]), ase_den, temp1, LPC_ORD);

		/*
			tilt_cof[1] = sig_prob * (intfact * curr_tilt + 
											(1.0 - intfact) * prev_tilt);
		*/
		temp1 = mult(curr_tilt, intfact);
		intfact1 = sub(ONE_Q15, intfact);
		temp2 = mult(prev_tilt, intfact1);
		temp1 = add(temp1, temp2);
		tilt_cof[1] = mult(sig_prob, temp1);			/* tilt_cof in Q15 */

		/* --- interpolate pitch and pulse gain ----------------------------- */
		/*
			syn_gain = SYN_GAIN * (intfact * lpc_gain +
										(1.0 - intfact) * prev_lpc_gain);
		*/
		temp1 = mult(lpc_gain, intfact);				/* lpc_gain in Q15 */
		temp2 = mult(prev_lpc_gain, intfact1);
		temp1 = add(temp1, temp2);						/* temp1 in Q15 */
		syn_gain = mult(SYN_GAIN_Q4, temp1);			/* syn_gain in Q4 */

		/*
			pitch = intfact * par->pitch + (1.0 - intfact) * prev_par.pitch;
		*/
		temp1 = mult(par->pitch, intfact);
		temp2 = mult(prev_par.pitch, intfact1);
		pitch = add(temp1, temp2);						/* pitch in Q7 */

		/*
			pulse_gain = syn_gain * sqrt(pitch);
		*/
		temp1 = sqrt_fxp(pitch, 7);
		L_temp1 = L_mult(syn_gain, temp1);
		L_temp1 = L_shl(L_temp1, 4);
		pulse_gain = extract_h(L_temp1);				/* pulse_gain in Q0 */

		/* --- interpolate pulse and noise coefficients --------------------- */
		
		temp1 = sqrt_fxp (ifact, 15);
		
		interp_array (prev_pcof, curr_pcof, pulse_cof, temp1, MIX_ORD + 1);
		interp_array (prev_ncof, curr_ncof, noise_cof, temp1, MIX_ORD + 1);

		set_fc (prev_par.bpvc, &fc_prev);
		set_fc (par->bpvc, &fc_curr);
		
		temp2 = sub(ONE_Q15, temp1);
		temp1 = mult(temp1, fc_curr);				/* temp1 is now Q3 */
		temp2 = mult(temp2, fc_prev);				/* Q3 */
        fc = add(temp1, temp2);						/* Q3 */

		/* --- interpolate jitter ------------------------------------------- */
		/*
			jitter = ifact * par->jitter + (1.0 - ifact) * prev_par.jitter;
		*/
		temp1 = mult(par->jitter, ifact);
		temp2 = sub(ONE_Q15, ifact);				/* temp2 is Q15 */
		temp2 = mult(prev_par.jitter, temp2);
		jitter = add(temp1, temp2);					/* jitter is Q15 */

		/* --- scale gain by 0.05 but keep gain in log. --------------------- */
		/*
			gain = pow(10.0, 0.05 * gain);
		*/
		gain = mult(X005_Q19, gain);				/* gain in Q12 */

		/* --- Set period length based on pitch and jitter ------------------ */
		rand_num (&temp1, ONE_Q15, 1);
		
		/*
			length = pitch * (1.0 - jitter * temp) + 0.5;
		*/
		temp1 = mult(jitter, temp1);
		temp1 = shr(temp1, 1);						/* temp1 in Q14 */
		temp1 = sub(ONE_Q14, temp1);
		temp1 = mult(pitch, temp1);					/* length in Q6 */
		length = shift_r(temp1, -6);		/* length in Q0 with rounding */
		
		if (length < PITCHMIN)
			length = PITCHMIN;
		
		if (length > PITCHMAX)
			length = PITCHMAX;

		fill (fs_real, ONE_Q13, length);
		
		fs_real[0] = 0;
		
		interp_array (prev_par.fs_mag, par->fs_mag, &fs_real[1], intfact, \
																	NUM_HARM);
		harm_syn_pitch (fs_real, &sigbuf[BEGIN], fc, length);
		
		v_scale (&sigbuf[BEGIN], pulse_gain, length);	/* sigbuf[] is Q0 */

		/* --- Adaptive spectral enhancement -------------------------------- */
		
		v_equ (&sigbuf[BEGIN - LPC_ORD], ase_del, LPC_ORD);
		
		lpc_synthesis (&sigbuf[BEGIN], &sigbuf[BEGIN], ase_den, LPC_ORD, length);
		
		v_equ (ase_del, &sigbuf[BEGIN + length - LPC_ORD], LPC_ORD);

		zerflt (&sigbuf[BEGIN], ase_num, &sigbuf[BEGIN], LPC_ORD, length);
		
		v_equ (&sigbuf[BEGIN - TILT_ORD], tilt_del, TILT_ORD);
		
		v_equ (tilt_del, &sigbuf[length + BEGIN - TILT_ORD], TILT_ORD);
		
		zerflt_Q (&sigbuf[BEGIN], tilt_cof, &sigbuf[BEGIN], TILT_ORD, length, 15);

		/* --- Possible Signal overflow at this point! ---------------------- */
		
		/* --- Perform LPC synthesis filtering ------------------------------ */
		
		v_equ (&sigbuf[BEGIN - LPC_ORD], lpc_del, LPC_ORD);
		
		lpc_synthesis(&sigbuf[BEGIN], &sigbuf[BEGIN], &(lpc[1]), LPC_ORD, length);
		
		v_equ (lpc_del, &sigbuf[length + BEGIN - LPC_ORD], LPC_ORD);

		/* --- Adjust scaling of synthetic speech, sigbuf in Q0 ------------- */
		
		scale_adj (&sigbuf[BEGIN], gain, length, SCALEOVER, INV_SCALEOVER_Q18);

		/* --- Implement pulse dispersion filter on output speech ----------- */
		
		v_equ (&sigbuf[BEGIN - DISP_ORD], disp_del, DISP_ORD);
		v_equ (disp_del, &sigbuf[length + BEGIN - DISP_ORD], DISP_ORD);
		
		zerflt_Q (&sigbuf[BEGIN], disp_cof, &sigbuf[BEGIN], DISP_ORD, length, 15);

		/* --- Copy processed speech to output array (not past frame end) --- */
		
		if (add(syn_begin, length) >= FRAME)
		{
			v_equ (&sp_out[syn_begin], &sigbuf[BEGIN], \
											(Shortword)(FRAME - syn_begin));

			if (!filter_flag)
				postfilt (sp_out, prev_par.lsf, par->lsf);

			/* --- past end: save remainder in sigsave[0] ------------------- */
			
			v_equ (sigsave, &sigbuf[BEGIN + FRAME - syn_begin], \
								(Shortword)(length - (FRAME - syn_begin)));
		}
		else
			v_equ (&sp_out[syn_begin], &sigbuf[BEGIN], length);

		/* --- Update syn_begin for next period ----------------------------- */

		syn_begin = add(syn_begin, length);
	}
	
	/* --- Save previous pulse and noise filters for next frame ------------- */
	
	v_equ (prev_pcof, curr_pcof, MIX_ORD + 1);
	v_equ (prev_ncof, curr_ncof, MIX_ORD + 1);

	/* --- Copy current parameters to previous parameters for next time ----- */
	
	prev_par = *par;
	prev_tilt = curr_tilt;
	prev_lpc_gain = lpc_gain;

	/* --- Update syn_begin for next frame ---------------------------------- */
	
	syn_begin = sub(syn_begin, FRAME);
}

/* =========================================================== */
/* melp_syn_init() performs initialization for melp synthesis. */
/* =========================================================== */

void melp_syn_init()
{
    register Shortword  i;
    Shortword   temp;


    v_zap(prev_par.gain, NUM_GAINFR);
    prev_par.pitch = UV_PITCH_Q7;
    temp = 0;
    for (i = 0; i < LPC_ORD; i++){
        temp = add(temp, INV_LPC_ORD);
        prev_par.lsf[i] = temp;
    }
    prev_par.jitter = 0;
    v_zap(&prev_par.bpvc[0], NUM_BANDS);
    syn_begin = 0;
    v_zap(sigsave, PITCHMAX);

    fill(prev_par.fs_mag, ONE_Q13, NUM_HARM);

    /* Initialize fixed MSE weighting and inverse of weighting */

    if (!w_fs_init){
        vq_fsw(w_fs, NUM_HARM, X60_Q9);
        for (i = 0; i < NUM_HARM; i++)
            w_fs_inv[i] = divide_s(ONE_Q13, w_fs[i]);      /* w_fs_inv in Q14 */
        w_fs_init = TRUE;
    }
}


