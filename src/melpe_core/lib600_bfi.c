/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		lib_bfi.c
  VERSION:			v001
  CREATION:			07 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Bad Frame Indicator Handling Library.
-------------------------------------------------------------------------- DE */

#include "main600.h"
#include "sc600.h"
#include "lib600_bfi.h"
#include "ext600_bfi.h"

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

/* DS --------------------------------------------------------------------------
  Name:				BFI_random
  Purpose:			test function.
  Arguments:		1 - (int *) bfi: bad frame indicator.
					2 - (float) rate: simulated random frame error rate in
											percentage [0.0/1.0]
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void BFI_random (int *bfi, float rate)
{
/* (CD) --- Declaration of variables for BFI_random () ---------------------- */

	float	v;

	/* ---------------------------------------------------------------------- */

	v = (float)(rand()) / (float)(RAND_MAX);

	*bfi = (v < rate) ? 1 : 0;

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of BFI_random () -------------------------------------------- */
}

/*----------------------------------------------------------------------------*/