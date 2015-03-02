/* ntp_menu.h -- header file for ntp_menu.c, user interface for os2_ntpd

  ***** BEGIN LICENSE BLOCK *****
  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.

  Copyright (c) 1998 Bruce M. Penrod (Initial Developer)
  Portions Copyright (c) 2014 Steven H. Levine
  ***** END LICENSE BLOCK *****

 */

#define WINDOW_ATTR     (USHORT)0x0900
#define MENU_ATTR       (USHORT)0x0200
#define STATUS_ATTR     (USHORT)0x0B00
#define NTP_PKT_ATTR    (USHORT)0x0E00
#define BUTTON_ATTR     (USHORT)0x4A00

#define BLINK_ON        (USHORT)0x7000
#define BLINK_OFF       (USHORT)0x0000

#define NOCURSOR        (CHAR)0
#define SOLIDCURSOR     (CHAR)1
#define NORMALCURSOR    (CHAR)2

#define MAX_MENU_DESC_LENGTH               12

#define CENTER          0
#define LEFT            -1
#define RIGHT           +1
#define RIGHTHALF       +2

typedef struct                          /* abbreviated mode data */
{
    USHORT length;
    UCHAR type;
    UCHAR color;
    USHORT col;
    USHORT row;
} AMD, *PAMD;

typedef struct
{
    UCHAR screenheight;
    UCHAR screenwidth;
} text_info;

extern PUSHORT screen_buffer;           // 2014-07-15 SHL
extern text_info textinfo;              // 2014-07-12 SHL
extern BOOL running_detached;           // 2014-07-12 SHL

VOID clrscr(VOID);

VOID modify_menu_desc(UINT item_number, PCSZ menu_desc, USHORT blink);

VOID ntp_menu_setup(PAMD screen_mode, PCSZ *menu_desc);

VOID put_status(const PCSZ status_line, UINT line_number, INT just);

VOID put_line_in_buffer(PCSZ line, UINT line_number, INT just);

VOID puttext(UINT left, UINT top, UINT right, UINT bottom, PVOID window_buffer);

// os2_ntpd.c

VOID put_info_status(PCSZ status_line, UINT line_number, INT just);

VOID report_fatal_error(PSZ fmt, ...);        // 2014-07-15 SHL

