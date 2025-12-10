/* ================================================================== */
/*                                                                    */
/*    Microsoft Speech coder     ANSI-C Source Code                   */
/*    1200/2400 bps MELPe speech coder                                */
/*    Fixed Point Implementation      Version 8.0                     */
/*    Copyright (C) 2000-2001, Microsoft Corp.                        */
/*    All rights reserved.                                            */
/*                                                                    */
/* ================================================================== */

#ifndef _HARM_H_
#define _HARM_H_


void    set_fc(Shortword bpvc[], Shortword *fc);

void    harm_syn_pitch(Shortword amp[], Shortword signal[], Shortword fc,
                       Shortword length);


#endif


