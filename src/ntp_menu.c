/* ntp_menu.c -- os2_ntpd user interface setup

  ***** BEGIN LICENSE BLOCK *****
  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.

  Copyright (c) 1998 Bruce M. Penrod (Initial Developer)
  Portions Copyright (c) 2014 Steven H. Levine
  ***** END LICENSE BLOCK *****

   2014-04-20 SHL Add alphanumeric hotkeys
   2014-07-12 SHL Support running detached
 */

#define INCL_VIO

#include <os2.h>

#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <string.h>
#include <time.h>                       // time_t time ctime

#include "ntp_menu.h"

/* Globals visible in other files */
text_info textinfo;                     /* holds the current display text mode info */
VIOMODEINFO tempVioMI;                  /* VioGet/SetMode structure */
ULONG LogicalVB_temp;                   /* Temp LVB pointer */
PCHAR LogicalVB;                        /* Logical Video Buffer pointer */
BOOL running_detached;                  // 2014-07-12 SHL
PUSHORT screen_buffer;                  /* pointer to screen buffer array
                                           not allocated if running detached
                                         */

VOID make_menu_buttons(PCSZ *menu_desc);

VOID __putline(UINT x, UINT y, PVOID line, UINT len);

VOID setcursortype(char cur_t);

static PCHAR __vptr(UINT x, UINT y);

VOID ntp_menu_setup(PAMD screen_mode, PCSZ *menu_desc)
{
    UINT col;
    UINT row;
    USHORT LVB_size;
    APIRET apiret;

    // 2014-07-12 SHL
    if (running_detached)
        return;

    /* set text mode */
    apiret = VioSetMode((PVIOMODEINFO)screen_mode, (HVIO)0);
    if (apiret)
        report_fatal_error("VioSetMode failed with error %u", apiret);

    tempVioMI.cb = sizeof(VIOMODEINFO);
    apiret = VioGetMode(&tempVioMI, (HVIO)0);
    if (apiret)
        report_fatal_error("VioGetMode failed with error %u", apiret);

    textinfo.screenwidth = (UCHAR)tempVioMI.col;
    textinfo.screenheight = (UCHAR)tempVioMI.row;

    printf("Screen Width = %u, Screen Height = %u\n",
           "Format Text Attribute = %u, N_Attributes = %u, Display_BufLength = %u\n",
           textinfo.screenwidth, textinfo.screenheight,
           tempVioMI.fmt_ID, tempVioMI.attrib, tempVioMI.buf_length);

    /* Get sel:offset address of the display Logical Video Buffer */
    apiret = VioGetBuf((PULONG)&LogicalVB_temp, &LVB_size, (HVIO)0);
    if (apiret)
        report_fatal_error("VioGetBuf failed with error %u", apiret);

    /* Convert to linear address */
    LogicalVB = (PCHAR)(((LogicalVB_temp >> 3) & 0xffff0000L) |
                        (LogicalVB_temp & 0xffff));

    /* Allocate working buffer for the current screen mode */
    screen_buffer = (PUSHORT)malloc(LVB_size);

    clrscr();
    setcursortype(NOCURSOR);

    /* Initialize screen array to spaces with blue fgnd on black bgnd */
    for (row = 0; row < textinfo.screenheight; row++) {
        for (col = 0; col < textinfo.screenwidth; col++) {
            /* lsb = char, msb = attr */
            screen_buffer[row * textinfo.screenwidth + col] = WINDOW_ATTR + (UCHAR)' ';
        }
    }

    /* Set top, row (rows-9), (rows-4) and bottom row to hor. double line */
    for (col = 0; col < textinfo.screenwidth; col++) {
        screen_buffer[col] =
            screen_buffer[(textinfo.screenheight - 11) * textinfo.screenwidth + col] =
            screen_buffer[(textinfo.screenheight - 7) * textinfo.screenwidth + col] =
            screen_buffer[(textinfo.screenheight - 4) * textinfo.screenwidth + col] =
            screen_buffer[(textinfo.screenheight - 1) * textinfo.screenwidth + col] =
                WINDOW_ATTR + (UCHAR)'Í';
    }

    /* Set left and right columns to vertical double line */
    for (row = 0; row < textinfo.screenheight; row++) {
        screen_buffer[row * textinfo.screenwidth] =
            screen_buffer[row * textinfo.screenwidth + textinfo.screenwidth - 1] =
                WINDOW_ATTR + (UCHAR)'º';
    }

    /* Set columns 13, 26, 39,40, 53, and 66 to vert. double line, row 19-23 */
    for (row = textinfo.screenheight - 6; row < textinfo.screenheight - 1; row++) {
        for (col = (textinfo.screenwidth - 2) / 6;
             col < textinfo.screenwidth / 2;
             col += (textinfo.screenwidth + 1) / 6) {
            screen_buffer[row * textinfo.screenwidth + col] =
                WINDOW_ATTR + (UCHAR)'º';
            screen_buffer[(textinfo.screenheight - 7) * textinfo.screenwidth + col] =
                WINDOW_ATTR + (UCHAR)'Ë';
            screen_buffer[(textinfo.screenheight - 4) * textinfo.screenwidth + col] =
                WINDOW_ATTR + (UCHAR)'Î';
            screen_buffer[(textinfo.screenheight - 1) * textinfo.screenwidth + col] =
                WINDOW_ATTR + (UCHAR)'Ê';
        }
        for (col = textinfo.screenwidth / 2;
             col < (5 * textinfo.screenwidth) / 6 + 1;
             col += (textinfo.screenwidth + 1) / 6) {
            screen_buffer[row * textinfo.screenwidth + col] =
                WINDOW_ATTR + (UCHAR)'º';
            screen_buffer[(textinfo.screenheight - 7) * textinfo.screenwidth + col] =
                WINDOW_ATTR + (UCHAR)'Ë';
            screen_buffer[(textinfo.screenheight - 4) * textinfo.screenwidth + col] =
                WINDOW_ATTR + (UCHAR)'Î';
            screen_buffer[(textinfo.screenheight - 1) * textinfo.screenwidth + col] =
                WINDOW_ATTR + (UCHAR)'Ê';
        }
    }
    /* Set corners to double corner character */
    screen_buffer[0] = WINDOW_ATTR + (UCHAR)'É';
    screen_buffer[textinfo.screenwidth - 1] =
        WINDOW_ATTR + (UCHAR)'»';
    screen_buffer[(textinfo.screenheight - 1) * textinfo.screenwidth] =
        WINDOW_ATTR + (UCHAR)'È';
    screen_buffer[(textinfo.screenheight - 1) * textinfo.screenwidth +
                  textinfo.screenwidth - 1] =
        WINDOW_ATTR + (UCHAR)'¼';
    /* Set left and right double T at rows (rows-7), (rows - 4) */
    screen_buffer[(textinfo.screenheight - 11) * textinfo.screenwidth] =
        screen_buffer[(textinfo.screenheight - 7) * textinfo.screenwidth] =
        screen_buffer[(textinfo.screenheight - 4) * textinfo.screenwidth] =
        WINDOW_ATTR + (UCHAR)'Ì';
    screen_buffer[(textinfo.screenheight - 11) * textinfo.screenwidth +
                  textinfo.screenwidth - 1] =
        screen_buffer[(textinfo.screenheight - 7) * textinfo.screenwidth +
                      textinfo.screenwidth - 1] =
        screen_buffer[(textinfo.screenheight - 4) * textinfo.screenwidth +
                      textinfo.screenwidth - 1] =
        WINDOW_ATTR + (UCHAR)'¹';

    make_menu_buttons(menu_desc);

    puttext(1, 1, textinfo.screenwidth, textinfo.screenheight, screen_buffer);

    return;
}

/**
 * Initialize menu buttons and descriptions on screen
 */

VOID make_menu_buttons(PCSZ *menu_desc)
{
    UINT button_row;
    UINT button_col;
    UINT char_ndx;
    size_t length;
    static PCSZ menu_button[12] = {
      " F1 ", " F2 ", " F3 ", " F4 ", " F5 ", " F6 ",
      " F7 ", " F8 ", " F9 ", " F10 ", " F11 ", " F12 "
    };

    PCSZ pzsMD;
    PCSZ pzsMB;

    for (button_row = 0; button_row < 2; button_row++) {
        for (button_col = 0; button_col < 6; button_col++) {
            pzsMD = menu_desc[button_row * 6 + button_col];
            length = strlen(pzsMD);
            /* If have button to display */
            if (length) {
                int offset;
                /* Write desc */
                for (char_ndx = 0, offset = 0; char_ndx < length; char_ndx++, offset++) {
                    int invert = *(pzsMD + char_ndx) == '~';
                    if (invert)
                      char_ndx++;
                    *(screen_buffer +
                      (textinfo.screenheight - 5) * textinfo.screenwidth +
                      button_row * 3 * textinfo.screenwidth +
                      (textinfo.screenwidth - 2) / 12 + 1 +
                      (int)(button_col * (textinfo.screenwidth / 6.0)) -
                      length / 2 + offset) =
                        (invert ? BUTTON_ATTR : MENU_ATTR) + *(pzsMD + char_ndx);
                }
                /* Write button text */
                pzsMB = menu_button[button_row * 6 + button_col];
                length = strlen(pzsMB);
                for (char_ndx = 0; char_ndx < length; char_ndx++) {
                    *(screen_buffer +
                     (textinfo.screenheight - 6) * textinfo.screenwidth +
                     button_row * 3 * textinfo.screenwidth +
                     (textinfo.screenwidth - 2) / 12 + 1 +
                     (int)(button_col * (textinfo.screenwidth / 6.0)) -
                     length / 2 + char_ndx) =
                       BUTTON_ATTR + *(pzsMB + char_ndx);
                }
            } /* if desc */
        } /* for button_col */
    } /* for button_row */
}

/**
 * Modify selected menu item description field
 * @param blink overrides default attribute (BLINK_ON, BLINK_OFF)
 */

VOID modify_menu_desc(UINT item_number, PCSZ menu_desc, USHORT blink)
{
    UINT button_row;
    UINT button_col;
    UINT char_ndx;
    size_t length;
    int offset;
    PCSZ pzsMD;

    /* two rows of 6 buttons */
    button_row = item_number / 6;
    button_col = item_number % 6;

    /* first restore description field to background attribute */
    length = MAX_MENU_DESC_LENGTH;
    for (char_ndx = 0; char_ndx < length; char_ndx++) {
        *(screen_buffer + (textinfo.screenheight - 5) * textinfo.screenwidth +
          button_row * 3 * textinfo.screenwidth + (textinfo.screenwidth - 2) / 12 +
          1 + (int)(button_col * (textinfo.screenwidth / 6.0)) - length / 2 + char_ndx) =
            MENU_ATTR + (UCHAR)' ';
    }

    /* write new description text with requested attribute */
    pzsMD = menu_desc;
    length = strlen(pzsMD);
    for (char_ndx = 0, offset = 0; char_ndx < length; char_ndx++, offset++) {
        int invert = *(pzsMD + char_ndx) == '~';
        if (invert)
          char_ndx++;
        *(screen_buffer + (textinfo.screenheight - 5) * textinfo.screenwidth +
          button_row * 3 * textinfo.screenwidth + (textinfo.screenwidth - 2) / 12 +
          1 + (int)(button_col * (textinfo.screenwidth / 6.0)) - length / 2 + offset) =
            (invert ? BUTTON_ATTR : (MENU_ATTR + blink)) + *(pzsMD + char_ndx);
    }
    puttext(1, 1, textinfo.screenwidth, textinfo.screenheight, screen_buffer);
}

/**
 * Output aligned status message at selected line
 * Output suppressed if running detached
 * @param line_number is line number within status area
 * @note status area starts 10 lines from botton of screen
 */

VOID put_status(PCSZ status_line, UINT line_number, INT just)
{
    int col;
    int ndx;
    PUSHORT status_pntr;                /* Points to status line in screen buffer */

    // 2014-07-12 SHL
    if (running_detached)
        return;

    status_pntr = screen_buffer +
                  (textinfo.screenheight - 10 + line_number) *
                  textinfo.screenwidth;

    if (just != RIGHTHALF) {
        for (col = 1; col < textinfo.screenwidth - 1; col++) {
            *(status_pntr + col) = WINDOW_ATTR + (UCHAR)' ';
        }
    }
    else {
        for (col = 40; col < textinfo.screenwidth - 1; col++) {
            *(status_pntr + col) = WINDOW_ATTR + (UCHAR)' ';
        }
    }

    if (just == CENTER) {
        col = textinfo.screenwidth - min(78, strlen(status_line));
        col /= 2;
    }
    else if (just == LEFT)
        col = 1;
    else if (just == RIGHT)
        col = textinfo.screenwidth - min(78, strlen(status_line)) - 1;
    else /* RIGHTHALF */
        col = 40;

    for (ndx = 0; ndx < min(78, strlen(status_line)); ndx++) {
        *(status_pntr + col + ndx) = STATUS_ATTR + status_line[ndx];
    }

    puttext(1,
            textinfo.screenheight - 9 + line_number,
            textinfo.screenwidth,
            textinfo.screenheight - 9 + line_number,
            (PCHAR)status_pntr);
}

/**
 * Insert formatted line at line in screen buffer
 */

VOID put_line_in_buffer(PCSZ line, UINT line_number, INT just)
{
    UINT col;
    UINT offset;
    PUSHORT screen_buffer_line;

    // 2014-07-12 SHL
    if (running_detached)
        return;

    screen_buffer_line =
        screen_buffer + (line_number - 1) * textinfo.screenwidth;

    if (just != RIGHTHALF) {
        for (col = 1; col < textinfo.screenwidth - 1; col++) {
            *(screen_buffer_line + col) = WINDOW_ATTR + (UCHAR)' ';
        }
    }
    else {
        for (col = 40; col < textinfo.screenwidth - 1; col++) {
            *(screen_buffer_line + col) = WINDOW_ATTR + (UCHAR)' ';
        }
    }

    if (just == CENTER) {
        col = textinfo.screenwidth - min(78, strlen(line));
        col /= 2;
    }
    else if (just == LEFT)
        col = 1;
    else if (just == RIGHT)
        col = textinfo.screenwidth - min(78, strlen(line)) - 1;
    else
        col = 40;

    for (offset = 0; offset < min(78, strlen(line)); offset++) {
        *(screen_buffer_line + col + offset) = NTP_PKT_ATTR + line[offset];
    }
}

/*************************************************************************
 The following are Borland Text Mode Functions which are not supported
 in the IBMCPP or OpenWatcom RTL, so they are emulated here so that I
 don't have to change my calling routines
 Some of the prototypes have been optimized (i.e int -> USHORT etc)
*************************************************************************/

/**
 * clear screen
 * Borland RTL CONIO Function
 */

VOID clrscr(VOID)
{
    char abCell[2];
    UINT row;
    APIRET apiret;

    // 2014-07-12 SHL
    if (running_detached)
        return;

    apiret = VioGetMode(&tempVioMI, (HVIO)0);
    if (apiret)
        report_fatal_error("VioGetMode failed with error %u", apiret);

    textinfo.screenwidth = (UCHAR)tempVioMI.col;
    textinfo.screenheight = (UCHAR)tempVioMI.row;

    abCell[0] = ' ';
    abCell[1] = 0x09;   // attribute
    // 2014-07-29 SHL FIXME to apiret
    for (row = 0; row < textinfo.screenheight; row++) {
        VioWrtNCell(abCell, textinfo.screenwidth, row, 0, (HVIO)0);
    }
}

/**
 * Set cursor shape
 * Borland RTL CONIO Function
 */

VOID setcursortype(char cur_t)
{
    VIOCURSORINFO CD;
    VIOMODEINFO MD;
    int height;

    // 2014-07-12 SHL
    if (running_detached)
        return;

    MD.cb = sizeof(MD);
    // 2014-07-29 SHL FIXME to apiret
    VioGetMode(&MD, (HVIO)0);
    height = MD.vres / MD.row;          /* calculate pixels per row */
    CD.cx = 0;                          /* use default cursor width */

    switch (cur_t) {
    case NOCURSOR:
        CD.attr = 0xffff;               /* -1 */
        CD.yStart = CD.cEnd = 0;
        break;
    case SOLIDCURSOR:
        CD.attr = 1;
        CD.yStart = 0;
        CD.cEnd = height - 1;
        break;
    case NORMALCURSOR:
        CD.attr = 1;
        CD.yStart = height <= 8 ? height - 1 : height - 2;
        CD.cEnd = height - 1;
    }
    // 2014-07-29 SHL FIXME to apiret
    VioSetCurType(&CD, (HVIO)0);
}

/**
 * Output text to screen window from window buffer
 * Borland RTL CONIO Function
 */

VOID puttext(UINT left, UINT top, UINT right, UINT bottom, PVOID window_buffer)
{
    UINT row;
    UINT size;

    // 2014-07-12 SHL
    if (running_detached)
        return;

    size = right - left + 1;            // Window width
    for (row = top; row <= bottom; row++) {
        __putline(left, row, window_buffer, size);
        window_buffer = (PVOID)((PCHAR)window_buffer + size * 2); // Next window line
    }
    return;
}

/**
 * Return pointer to screen buffer for x,y
 * Borland RTL Support Function
 */

static PCHAR __vptr(UINT x, UINT y)
{
    return LogicalVB + ((y - 1) * textinfo.screenwidth + (x - 1)) * 2;
}

/**
 * Output text to screen at selected location
 * Borland RTL Support Function
 */

VOID __putline(UINT x, UINT y, PVOID line, UINT len)
{
    PCHAR dst;

    // 2014-07-12 SHL
    if (running_detached)
        return;

    len *= 2;
    dst = __vptr(x, y);
    memmove(dst, line, len);
    // 2014-07-29 SHL FIXME to apiret
    VioShowBuf((USHORT)(dst - LogicalVB), len, (HVIO)0);
}
