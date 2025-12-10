/* ================================================================== */
/*                                                                    */
/*    Microsoft Speech coder     ANSI-C Source Code                   */
/*    1200/2400 bps MELPe speech coder                                */
/*    Fixed Point Implementation      Version 8.0                     */
/*    Copyright (C) 2000-2001, Microsoft Corp.                        */
/*    All rights reserved.                                            */
/*                                                                    */
/* ================================================================== */

/*------------------------------------------------------------------*/
/*                                                                  */
/* File:        "postfilt.c"                                        */
/*                                                                  */
/* Description:     postfilter and postprocessing in decoder        */
/*                                                                  */
/*------------------------------------------------------------------*/
#include "sc1200.h"
#include "constant.h"
#include "postfilt.h"
#include "dsp_sub.h"
#include "mat_lib.h"
#include "math_lib.h"
#include "lpc_lib.h"
#include "mathhalf.h"

#include "sc600.h"
#include "global.h"

/* ---- Postfilter ---- */
#define ALPHA           18350                                /* 0.56, Q15 */
#define BETA            24576                                /* 0.75, Q15 */
#define TILT_FACTOR     6553                                 /* 0.20, Q15 */

#define postHPFOrder    2
#define postLPFOrder    2

/* ===== Prototypes ===== */
static void plhFilter(Shortword *inBuf);
void poleZeroFilter( Shortword *inputBuffer, Shortword *outputBuffer, 
                     Shortword *fltNumerator, Shortword *fltDenominator,
                     Shortword *inputHistory, Shortword *outputHistory,
                     Shortword length, Shortword order);

/****************************************************************
**
** Function:        postfilt()
**
** Description:     postfilter including short term postfilter
**              and tilt compensation
**
** Arguments:
**
**  Shortword   speech[]    : input/output synthesized speech (Q0)
**
** Return value:    None
**
*****************************************************************/
void postfilt(Shortword syn[], Shortword prev_lsf[], Shortword cur_lsf[])
{
    Shortword   i, subframeNum;
    Shortword   refl[LPC_ORD + 1], t, mu;
    Shortword   synt[SYN_SUBFRAME];
    Shortword   alpha, beta, lpcAlpha[LPC_ORD+1], lpcBeta[LPC_ORD+1];
    Shortword   inputGain, outputGain, gain;
    Shortword   synLPC[LPC_ORD];                                   /* Q12 */
    Shortword   inplsf[LPC_ORD];
    Shortword   shift1, shift2;
    Longword    acc;
    static Shortword    mem1[LPC_ORD] = {0};
    static Shortword    mem2[LPC_ORD] = {0};
    static Shortword    memt = 0;
    static Shortword    lastGain = 16384;                       /* 1.0 Q14 */
    static const Shortword  syn_inp[SYN_SUBNUM] = {                 /* Q15 */
                4096, 12288, 20480, 28672
    };

	/* === Modifications MELP600 ============================================= */
	
	static int	flag = 0;
	static Shortword	ALPHA0, BETA0, TILT_FACTOR0;

	if (flag == 0)
	{
		flag = 1;

		if (rate == RATE600)
		{
			ALPHA0 = ALPH600_Q15;
			BETA0 = BETA600_Q15;
			TILT_FACTOR0 = MU600_Q15;
		}
		else
		{
			ALPHA0 = ALPHA;
			BETA0 = BETA;
			TILT_FACTOR0 = TILT_FACTOR;
		}
	}
	
	/* === Filter main loop ================================================== */
	
	for (subframeNum = 0; subframeNum < SYN_SUBNUM; subframeNum ++)
	{
		/* --- Compute filter coefficients ----------------------------------- */
		
		for (i = 0; i < LPC_ORD; i ++)
		{
			acc = L_mult(prev_lsf[i], sub(ONE_Q15, syn_inp[subframeNum]));
			acc = L_mac(acc, cur_lsf[i], syn_inp[subframeNum]);
			
			inplsf[i] = melp_round(acc);			/* Q15 */
		}
		
		lpc_lsp2pred (inplsf, synLPC, LPC_ORD);
		
		/* ------------------------------------------------
			lpc_pred2refl(lpc[subframeNum], refl, LPC_ORD);
			t = (float)1.0;
			for(i = 1; i <= LPC_ORD; i++)
				t *= (float)(1.0 - refl[i]*refl[i]);
			if( t > 0.3 )   mu = 0.0;
			else    mu = (float)MU;
		------------------------------------------------ */
		
		lpc_pred2refl (synLPC, refl, LPC_ORD);
		
		t = ONE_Q15;
		
		for (i = 0; i < LPC_ORD; i++)
		{
			acc = L_sub(MAX_32, L_mult(refl[i], refl[i]));
			t = mult(t, melp_round(acc));
		}
		
		if (sub(t, X03_Q15) > 0)
			mu = 0;
		else
			mu = TILT_FACTOR0;

		/* ------------------------------------------------------------
			for (i = 0; i < SYN_SUBFRAME; i++) {
				synt[i] = syn[subframeNum*SYN_SUBFRAME+i] - mu * memt;
				memt = syn[subframeNum*SYN_SUBFRAME+i];
			}
		------------------------------------------------------------ */

		for (i = 0; i < SYN_SUBFRAME; i++)
		{
			acc = L_deposit_h(syn[subframeNum*SYN_SUBFRAME+i]);
			acc = L_sub(acc, L_mult(mu, memt));
			synt[i] = melp_round(acc);
			memt = syn[subframeNum*SYN_SUBFRAME+i];
		}

		/* === Short-term postfilter ========================================= */
		
		/* ------------------------------------------------------------
			alpha = (float)ALPHA;
			beta = (float)BETA;
			lpcAlpha[0] = 1.0;
			lpcBeta[0] = 1.0;
			for(i = 0; i < LPC_ORD; i++){
				lpcAlpha[i+1] = lpc[subframeNum][i+1]*alpha;
				lpcBeta[i+1] = lpc[subframeNum][i+1]*beta;
				alpha *= (float)ALPHA;
				beta *= (float)BETA;
				}
			poleZeroFilter(synt, synt, lpcAlpha, lpcBeta,
							mem1, mem2, SYN_SUBFRAME, LPC_ORD);
		------------------------------------------------------------ */
		
		alpha = ALPHA0;				/* Q15 */
		beta = BETA0;
		
		for(i = 0; i < LPC_ORD; i++)
		{
			lpcAlpha[i] = mult(synLPC[i], alpha);
			lpcBeta[i] = mult(synLPC[i], beta);
			alpha = mult(alpha, ALPHA0);
			beta = mult(beta, BETA0);
		}

		poleZeroFilter (synt, synt, lpcAlpha, lpcBeta, mem1, mem2, \
													SYN_SUBFRAME, LPC_ORD);
		
		/* === Gain adjustment =============================================== */
		
		/* -----------------------------------------------------------
			inputGain = (float)0.0;
			outputGain = (float)0.0;
			for(i = 0; i < SYN_SUBFRAME; i++){
				inputGain += syn[subframeNum*SYN_SUBFRAME+i] * 
										syn[subframeNum*SYN_SUBFRAME+i];
				outputGain += synt[i] * synt[i];
			}
			if (outputGain < 128.0)
				gain = (float)0.0;
			else
				gain = (float)sqrt(inputGain/outputGain);
		------------------------------------------------------------ */
		
		acc = 0;
		
		for (i = 0; i < SYN_SUBFRAME; i++)
		{
			acc = L_add(acc, L_deposit_l(abs_s(syn[subframeNum*SYN_SUBFRAME+i])));
		}
		
		shift1 = norm_l(acc);
		acc = L_shl(acc, sub(shift1,1));
		inputGain = melp_round(acc);
		
		acc = 0;
		
		for(i = 0; i < SYN_SUBFRAME; i++)
		{
			acc = L_add(acc, L_deposit_l(abs_s(synt[i])));
		}
		
		if (L_sub(acc, 64L) < 0)
			gain = lastGain;
		else
		{
			shift2 = norm_l(acc);
			acc = L_shl(acc, shift2);
			outputGain = melp_round(acc);
			gain = divide_s(inputGain, outputGain);			/* Q14 */
			gain = shr(gain, sub(shift1, shift2));
			/* may overflow but it is fine */
		}
		
		/* ------------------------------------------------------------
			alpha = (float)1.0;
			beta = (float)1.0 / (float)SYN_SUBFRAME;
			
			for(i = 0; i < SYN_SUBFRAME; i++){
				syn[subframeNum*SYN_SUBFRAME+i] = 
					(float)(alpha*lastGain+(1.0-alpha)*gain)*synt[i];
				alpha -= beta;
			}
			lastGain = gain;
		------------------------------------------------------------ */
		
		alpha = ONE_Q15;
		beta = 730;				/* 1.0/SYN_SUBFRAME Q15 */
		
		for (i = 0; i < SYN_SUBFRAME; i++)
		{
			acc = L_mult(alpha, lastGain);
			acc = L_mac(acc, sub(ONE_Q15, alpha), gain);
			acc = L_mult(melp_round(acc), synt[i]);
			syn[subframeNum*SYN_SUBFRAME+i] = melp_round(L_shl(acc,1));
			alpha = sub(alpha, beta);
		}
		
		lastGain = gain;
	}
	
	plhFilter (syn);
}

/****************************************************************************
**
** Function:        plhFilter
**
** Description:     post low-pass and high-pass filter
**
** Return value:    void
**
*****************************************************************************/
static void plhFilter(Shortword *inBuf)
{
    static Shortword    postLPFInHis[postHPFOrder] = {0};
    static Shortword    postLPFOutHis_lo[postHPFOrder] = {0};
    static Shortword    postLPFOutHis_hi[postHPFOrder] = {0};
    static Shortword    postHPFInHis[postHPFOrder] = {0}; 
    static Shortword    postHPFOutHis_lo[postHPFOrder] = {0};
    static Shortword    postHPFOutHis_hi[postHPFOrder] = {0};
    static Shortword    postLPFNum[postHPFOrder+1] = {      /* butterworth 0.95 */
        /* (float)  0.894859, (float)  1.789717, (float)  0.894859 */
        8192, 16384, 8192       /* Q13 */
    };
    static Shortword    postLPFDen[postHPFOrder+1] = {
        /* (float)  1.000000, (float)  1.778632, (float)  0.800803 */
        -8192, -14571, -6560
    };
    static Shortword    postHPFNum[postHPFOrder+1] = {      /* butterworth 0.15 */
        /* (float)  0.967227, (float) -1.934455, (float)  0.967227 */
        8192, -16384, 8192
    };
    static Shortword    postHPFDen[postHPFOrder+1] = {
        /* (float)  1.000000, (float) -1.933380, (float)  0.935529 */
        -8192, 15838, -7664
    };
    static Shortword gain = 28362;

    v_scale(inBuf, gain, FRAME);

    iir_2nd_d(inBuf, postHPFDen, postHPFNum, inBuf, postHPFInHis,
              postHPFOutHis_hi, postHPFOutHis_lo, FRAME);

    iir_2nd_d(inBuf, postLPFDen, postLPFNum, inBuf, postLPFInHis,
              postLPFOutHis_hi, postLPFOutHis_lo, FRAME);

}

/****************************************************************************
**
** Function:        poleZeroFilter
**
** Description:     General filter
**
** Return value:    void
**
*****************************************************************************/
void poleZeroFilter( Shortword *inputBuffer, Shortword *outputBuffer, 
                     Shortword *fltNumerator, Shortword *fltDenominator,
                     Shortword *inputHistory, Shortword *outputHistory,
                     Shortword length, Shortword order)
{
    Shortword   i, j;
    Longword    acc;            /* DSP accumulator */

    /* -------- Filtering -------- */
    for (i = 0; i < length; i++){
        acc = L_deposit_h(inputBuffer[i]);
        acc = L_shr(acc, 3);
        for (j = 0; j < order; j++){
            acc = L_mac(acc, inputHistory[j], fltNumerator[j]);
            acc = L_msu(acc, outputHistory[j], fltDenominator[j]);
        }
        for (j = order-1; j > 0; j--) {
            inputHistory[j] = inputHistory[j-1];
            outputHistory[j] = outputHistory[j-1];
        }
        inputHistory[0] = inputBuffer[i];
        acc = L_shl(acc, 3);        /* compensate Q12 of lpc */
        outputHistory[0] = melp_round(acc);
        outputBuffer[i] = outputHistory[0];
    }
}


