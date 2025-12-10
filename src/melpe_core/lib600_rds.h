/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		lib600_rds.h
  VERSION:			v001
  CREATION:			07 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Read Bit Stream Library.
-------------------------------------------------------------------------- DE */

#ifndef __LIB600_RDS_H
#define __LIB600_RDS_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Definition of public functions
------------------------------------------------------------------------------*/

void RDS_read_stream (char *stream_bit, quant_param600 *qpar);

static void RDS_read_mode0 (char *stream_char, quant_param600 *qpar);
static void RDS_read_mode1 (char *stream_char, quant_param600 *qpar);
static void RDS_read_mode2 (char *stream_char, quant_param600 *qpar);
static void RDS_read_mode3 (char *stream_char, quant_param600 *qpar);
static void RDS_read_mode4 (char *stream_char, quant_param600 *qpar);
static void RDS_read_mode5 (char *stream_char, quant_param600 *qpar);

static void RDS_restore_parameters (quant_param600 *qpar);

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __LIB600_RDS_H */
