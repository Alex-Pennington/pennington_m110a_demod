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
/* File:        "postfilt.h"                                        */
/*                                                                  */
/* Description:     header filt for postfilter and postprocessing   */
/*                                                                  */
/*------------------------------------------------------------------*/

#ifndef _POSTFILT_H_
#define _POSTFILT_H_


/* ========== Prototypes ========== */
void    postfilt(Shortword syn[], Shortword prev_lsf[], Shortword cur_lsf[]);


#endif

