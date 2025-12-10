/* DS --------------------------------------------------------------------------
  PROJECT:			MELP 600 bit/sec
  FILE NAME:		main600.h
  VERSION:			v001
  CREATION:			07 aout 2001
  AUTHOR:			Francois Capman
  LANGUAGE:			C
  PURPOSE:			Generic Declaration
-------------------------------------------------------------------------- DE */

#ifndef __MAIN600_H
#define __MAIN600_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

/* Platform-specific includes */
#ifdef _WIN32
#include <malloc.h>
#include <conio.h>
#include <float.h>
#endif

/*------------------------------------------------------------------------------
	Definition of public constants
------------------------------------------------------------------------------*/

//#define	pi 3.1415926535897932384626433832795

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif	/* __MAIN600_H */
