/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		lib600_srt.c
  VERSION:			v001
  CREATION:			21 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Sorting Library.
-------------------------------------------------------------------------- DE */

/*------------------------------------------------------------------------------
	Include files
------------------------------------------------------------------------------*/

#include "main600.h"
#include "lib600_srt.h"

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

/* DS --------------------------------------------------------------------------
  Name:				SRT_fsort
  Purpose:			this function performs array sorting.
  Arguments:		1 - (float[]) ra: input array.
					2 - (int[]) rb: ordering array.
					3 - (int) n: array size.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void SRT_fsort (float ra[], int rb[], int n)
{
/* (CD) --- Declaration of variables for SRT_fsort () ----------------------- */
	
	int		i, j, l, ir;
	float	rra;
	int		rrb;

	/* --- Output display of error message and exit ------------------------- */

	l = (n >> 1) + 1;
	ir = n;

	for (;;)
	{
		if (l > 1)
		{
			rra = ra[--l];

			rrb = rb[l];
		}
		else
		{
			rra = ra[ir];
			rrb = rb[ir];

			ra[ir] = ra[1];
			rb[ir] = rb[1];
			
			if (--ir == 1)
			{
				ra[1] = rra;
				rb[1] = rrb;

				return;
			}
		}
		
		i = l;
		j = l << 1;
		
		while (j <= ir)
		{
			if ((j < ir) && (ra[j] < ra[j+1]))
				++ j;

			if (rra < ra[j])
			{
				ra[i] = ra[j];
				rb[i] = rb[j];
				
				j += (i = j);
			}
			else
				j = ir + 1;
		}

		ra[i] = rra;
		rb[i] = rrb;
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of SRT_fsort () --------------------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				SRT_ssort
  Purpose:			this function performs array sorting.
  Arguments:		1 - (short[]) ra: input array.
					2 - (int[]) rb: ordering array.
					3 - (int) n: array size.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void SRT_ssort (short ra[], int rb[], int n)
{
/* (CD) --- Declaration of variables for SRT_ssort () ----------------------- */
	
	int		i, j, l, ir;
	short	rra;
	int		rrb;

	/* --- Output display of error message and exit ------------------------- */

	l = (n >> 1) + 1;
	ir = n;

	for (;;)
	{
		if (l > 1)
		{
			rra = ra[--l];

			rrb = rb[l];
		}
		else
		{
			rra = ra[ir];
			rrb = rb[ir];

			ra[ir] = ra[1];
			rb[ir] = rb[1];
			
			if (--ir == 1)
			{
				ra[1] = rra;
				rb[1] = rrb;

				return;
			}
		}
		
		i = l;
		j = l << 1;
		
		while (j <= ir)
		{
			if ((j < ir) && (ra[j] < ra[j+1]))
				++ j;

			if (rra < ra[j])
			{
				ra[i] = ra[j];
				rb[i] = rb[j];
				
				j += (i = j);
			}
			else
				j = ir + 1;
		}

		ra[i] = rra;
		rb[i] = rrb;
	}

	/* ---------------------------------------------------------------------- */

	return;

/* (CD) --- End of SRT_ssort () --------------------------------------------- */
}

/*----------------------------------------------------------------------------*/