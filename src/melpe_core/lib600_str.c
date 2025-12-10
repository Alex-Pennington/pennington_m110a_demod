/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILENAME:			lib600_str.c
  VERSION:			version 1
  CREATION:			September 12th, 2001.
  AUTHOR:			F.Capman
  LANGUAGE:			C
  PURPOSE:			Bit Stream Handling library.
-------------------------------------------------------------------------- DE */

/*------------------------------------------------------------------------------
	Include Files
------------------------------------------------------------------------------*/

#include <math.h>
#include "main600.h"
#include "sc600.h"
#include "stream600.h"
#include "lib600_str.h"

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

/* DS --------------------------------------------------------------------------
  Name:				STR_put_val
  Purpose:			This function writes input value into bitstream.
  Arguments:		1 - (short) x: value to be stored in the bitstream.
					2 - (int) nbits: number of bits.
					3 - (char[]) bitstream: output bitstream.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void STR_put_val (short x, int nbits, char bitstream[])
{
/* (CD) --- Declaration of variables for STR_put_val () --------------------- */

	short		n;
	short	t;

	/* ---------------------------------------------------------------------- */

	t = x;

	for (n = 0; n < nbits; n ++)
	{
		if (t & 0x0001)
			bitstream[nbits-1-n] = 0x0001;
		else
			bitstream[nbits-1-n] = 0x0000;
		
		t >>= 1;
	}

	/* ---------------------------------------------------------------------- */

    return ;

/* (CD) End of STR_put_val () ----------------------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				STR_get_val
  Purpose:			This function reads value from bitstream.
  Arguments:		1 - (int) nbits: number of bits to be extracted.
					2 - (char[]) bitstream: input bitstream.
  Return:			R - (short) x: output value.
-------------------------------------------------------------------------- DE */

short STR_get_val (int nbits, char bitstream[])
{
/* (CD) --- Declaration of variables for STR_get_val () --------------------- */

	short		n;
	short	x;

	/* ---------------------------------------------------------------------- */

	x = 0;
	
	for (n = 0; n < nbits; n ++)
	{
		x <<= 1;
		
		if (bitstream[n] & 0x0001)
			x += 1;
	}

	/* ---------------------------------------------------------------------- */

    return (x);

/* (CD) End of STR_get_val () ----------------------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				STR_bitstream_decompression
  Purpose:			This function performs stream conversion from bit-format to
					char format.
  Arguments:		1 - (char[]) stream_in: input bit stream.
					2 - (char[]) stream_out: output bit stream.
					3 - (int) Nbits: number of bits.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void STR_bitstream_decompression (char stream_in[], char stream_out[], int Nbits)
{
/* (CD) --- Declaration of variables for STR_bitstream_decompression () ----- */

	short		k, n, nb, Nbytes;
	char	byte;

	/* ---------------------------------------------------------------------- */

	Nbytes = (short)ceil((double)(Nbits) / (double)(NBITS_CHAR));

	nb = 0;
	for (n = 0; n < Nbytes; n ++)
	{
		k = NBITS_CHAR;
		while ((k > 0) && (nb < Nbits))
		{
			k --;
			byte = stream_in[n] >> k;
			stream_out[nb] = byte & 0x0001;

			nb ++;
		};
	}

	/* ---------------------------------------------------------------------- */

    return ;

/* (CD) End of STR_bitstream_decompression () ------------------------------- */
}

/* DS --------------------------------------------------------------------------
  Name:				STR_bitstream_compression
  Purpose:			This function performs stream conversion from char-format to
					bit-format.
  Arguments:		1 - (char[]) stream_in: input bit stream.
					2 - (char[]) stream_out: output bit stream.
					3 - (int) Nbits: number of bits.
  Return:			R - none.
-------------------------------------------------------------------------- DE */

void STR_bitstream_compression (char stream_in[], char stream_out[], int Nbits)
{
/* (CD) --- Declaration of variables for STR_bitstream_compression () ------- */

	short		k, n, nb, Nbytes;
	char	byte;

	/* ---------------------------------------------------------------------- */

	Nbytes = (int)ceil((double)(Nbits) / (double)(NBITS_CHAR));

	nb = 0;
	for (n = 0; n < Nbytes; n ++)
	{
		byte = 0x0000;

		for (k = 0; k < NBITS_CHAR; k ++)
		{
			if (nb < Nbits)
			{
				byte <<= 1;
				byte += stream_in[nb];
				nb ++;
			}
			else
			{
				byte <<= 1;
			}
		}

		stream_out[n] = byte;
	}

	/* ---------------------------------------------------------------------- */

    return ;

/* (CD) End of STR_bitstream_compression () --------------------------------- */
}

/* -------------------------------------------------------------------------- */
