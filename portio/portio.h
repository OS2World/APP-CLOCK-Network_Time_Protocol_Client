/* portio.h -  header file for port access routines                 */
/********************************************************************/
/*                                                                  */
/*  Function prototypes for port access routines                    */
/*       inp(port)   - read single byte from I/O port               */
/*       inpw(port)  - read single word from I/O port pair          */
/*       outp(port)  - write single byte to I/O port                */
/*       outpw(port) - write single word to I/O port pair           */
/*                                                                  */
/*  For IBM C Set/2  Version 1.0                                    */
/*                                                                  */
/* DISCLAIMER:                                                      */
/* -------------------------                                        */
/* This code and the accompanying documentation is hereby placed in */
/* the public domain.  It is not part of any standard product and   */
/* is provided solely as an example for your private and/or         */
/* commercial use.  You may freely use or distribute this code in   */
/* derived works as long as you do not attempt to prevent others    */
/* from doing likewise.  Neither the author, nor his employer,      */
/* shall be liable for any damages arising from your use of this    */
/* code; it is provided solely ASIS with no warranty whatsoever.    */
/*                                                                  */
/* Author contact:   Michael Thompson                               */
/*                   tommy@msc.cornell.edu                          */
/********************************************************************/

#ifdef __IBMC__
#pragma checkout( suspend )
   #ifndef __CHKHDR__
      #pragma checkout( suspend )
   #endif
#pragma checkout( resume )
#endif

#ifndef __portio_h
   #define __portio_h

   unsigned short _cdecl _far16 inp(unsigned short port);
   unsigned short _cdecl _far16 inpw(unsigned short port);
   unsigned short _cdecl _far16 outp(unsigned short poort,
				unsigned short byte);
   unsigned short _cdecl _far16 outpw(unsigned short port,
				unsigned short word);
#endif

#ifdef __IBMC__
#pragma checkout( suspend )
   #ifndef __CHKHDR__
      #pragma checkout( resume )
   #endif
#pragma checkout( resume )
#endif
