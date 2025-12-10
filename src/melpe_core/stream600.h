/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILENAME:			stream600.h - structure definition.
  VERSION:			version 1
  CREATION:			September 03th, 2002.
  AUTHOR:			F.Capman
  LANGUAGE:			C
  PURPOSE:			Encoded stream structures.
-------------------------------------------------------------------------- DE */

#ifndef __STREAM600_H
#define __STREAM600_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
	Definition of public structures
------------------------------------------------------------------------------*/

/* Number of bits per character ... */
#define		NBITS_CHAR		8

/* Synchro word for ITU softbit format ... */
#define		SYNC_WORD		(short)0x6B21

/* Hard bit format ... */
#define		HARD_ZERO		(char)0x00
#define		HARD_ONE		(char)0x01

/* Soft bit representation for ITU softbit format ... */
#define		SOFT_ZERO		(short)0x007F
#define		SOFT_ONE		(short)0x0081

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __STREAM600_H */
