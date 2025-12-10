/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		cst600_qpit.h
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Definition of constants for pitch encoding.
-------------------------------------------------------------------------- DE */

#ifndef __CST600_QPIT_H
#define __CST600_QPIT_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Definition of constants
------------------------------------------------------------------------------*/

/* maximum f0 value in Hertz */
#define	F0_MAX			(float)(400)

/* minimum f0 value in Hertz */
#define	F0_MIN			(float)(50)

/* minimum pitch lag value in samples */
#define	L0_MIN			FSAMP/F0_MAX

/* maximum pitch lag value in samples */
#define	L0_MAX			FSAMP/F0_MIN

static float farg;
#define FLOG(a) (farg=(a),(farg <= 1.0f) ? 0.0f : (float)log(farg))

/* maximum pitch lag value in log-domain */
#define LL0_MAX			FLOG (L0_MAX)

/* minimum pitch lag value in log-domain */
#define LL0_MIN			FLOG (L0_MIN)

/* number of bits for pitch lag quantization in encoding mode 1 */
#define	NBIT_PITCH1		6
#define	NPITCH_VAL1		64

/* number of bits for pitch lag quantization in encoding modes 2,3,4,5 */
#define	NBIT_PITCH2		5
#define	NPITCH_VAL2		32

/* constants used for pitch trajectory modelling */
#define	D_SHIFT			2
#define	F0_STEP3		10922

#define NCAND			4

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __CST600_QPIT_H */
