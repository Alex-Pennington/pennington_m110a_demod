/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		cst600_voicing.h
  VERSION:			v001
  CREATION:			24 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Definition of constants for voicing encoding.
-------------------------------------------------------------------------- DE */

#ifndef __CST600_VOICING_H
#define __CST600_VOICING_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Definition of constants
------------------------------------------------------------------------------*/

/* number of bits for voicing pattern encoding in MELP600 */
#define	NBIT_VOICING		5

/* corresponding voicing codebook size */
#define	VOICING_CBK_SIZE	32

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __CST600_VOICING_H */
