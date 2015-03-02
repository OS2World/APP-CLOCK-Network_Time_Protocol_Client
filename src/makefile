# makefile - build os2ntpd with OpenWatcom

# ***** BEGIN LICENSE BLOCK *****
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# Copyright (c) 1998 Bruce M. Penrod (Initial Developer)
# Portions Copyright (c) 2014 Steven H. Levine
# ***** END LICENSE BLOCK *****

# 2014-04-21 SHL Baseline - clone from elsewhere

# Required environment variables
# WATCOM
# VENDOR
# BUILDVER
# BUILDMACH
# BUILDTIME

# Supported environment variables
# Define DEBUG in environment for debug build
# Define EXCEPTQ in environment to build with exceptq support
# Define WARN to allow warnings

# Define TOOLKIT to override default location
# Define PORTIO to override default location

# Check required environment variables

!ifndef %WATCOM
!error WATCOM not defined
!endif

!ifndef %BUILDTIME
!error BUILDTIME not defined
!endif

!ifndef %BUILDVER
!error BUILDVER not defined
!endif

!ifndef %BUILDMACH
!error BUILDMACH not defined
!endif

!ifndef %VENDOR
!error VENDOR not defined
!endif

!ifndef TOOLKIT			# if not defined on wmake command line
!ifdef %TOOLKIT			# if defined in environment
TOOLKIT = $(%TOOLKIT)		# use value from environment
!else
TOOLKIT = d:\toolkit
!endif
!endif

!ifndef PORTIO			# if not defined on wmake command line
!ifdef %PORTIO			# if defined in environment
PORTIO = $(%PORTIO)		# use value from environment
!else
# Must use bcos2 dll because OpenWatcom does not support MS _fastcall
PORTIO = portio
!endif
!endif

# Force
%INCLUDE = $(TOOLKIT)\H;$(%WATCOM)\H;$(%WATCOM)\H\OS2;$(PORTIO)
%LIB = $(PORTIO);$(TOOLKIT)\LIB;$(%WATCOM)\LIB386\OS2;$(%WATCOM)\LIB386

.ERASE

NAME = os2_ntpd
DESC = NTP Daemon for OS/2
EXENAME = $(NAME).exe
MAKEFILE = makefile

CC = wcc386

!ifndef DEBUG			# if not defined on wmake command line
!ifdef %DEBUG			# if defined in environment
DEBUG = $(%DEBUG)		# use value from environment
!endif
!endif

!ifndef WARN		# if not defined on wmake command line
!ifdef %WARN		# if defined in environment
WARN = $(%WARN)	# use value from environment
!endif
!endif

!ifndef EXCEPTQ			# if not defined on wmake command line
!ifdef %EXCEPTQ			# if defined in environment
EXCEPTQ = $(%EXCEPTQ)		# use value from environment
!endif
!endif

# wcc/wpp flags
# -bm           multithread libs
# -bt=os2       target
# -d2           full debug
# -hd           dwarf
# -j		signed char
# -mf           flat
# -of           generate traceable stack frames
# -olinars      optimze loops, inline, e(n)able fp recip, relax (a)lias, reordering, space
# -s            disable stack checks
# -sg	        generate calls to grow the stack
# -wcd14	no reference to symbol
# -wcd138	missing EOL
# -wcd726	no reference to formal parameter
# -we		treat warnings as errors
# -zfp          disable fs use
# -zgp          disable gs use
# -zq           quiet

INCS = -i=$(PORTIO) -i=$(TOOLKIT)\h

# Debug flags are order dependent
CFLAGS1 = -bt=os2v2 -mf -bm -of $(INCS) -dTCPV40HDRS -olrs
CFLAGS2 = -s -j -wx -zfp -zgp -hd
!ifdef DEBUG
CFLAGS = $(CFLAGS1) -d2 $(CFLAGS2)
!else
CFLAGS = $(CFLAGS1) $(CFLAGS2)
!endif

# Order independent flags
CFLAGS += -zq
CFLAGS += -wcd14 -wcd138 -wcd726

!ifndef WARN
CFLAGS += -we
!endif

!ifdef EXCEPTQ
CFLAGS += -dEXCEPTQ
!endif

LINK = wlink

#################
# Inference rules
#################

.c.obj: .AUTODEPEND $(MAKEFILE)
     echo INCLUDE $(%INCLUDE)
     $(CC) $(CFLAGS) $*.c

##############
# Object files
##############

# objects built here
OBJS = $(NAME).obj ntp_menu.obj

# objects built elsewhere
EXTRA_OBJS =  

# all object files
ALL_OBJS = $(OBJS) $(EXTRA_OBJS)

# libraries built here
LIBS =

# libraries built elsewhere
EXTRA_LIBS = portio.lib &
	     tcp32dll.lib &
	     so32dll.lib &
	     os2386.lib

# all library files
ALL_LIBS = $(LIBS) $(EXTRA_LIBS)

#############
# Build rules
#############

all: $(EXENAME)

# Avoid lost spaces in @%write
FILL = xxxx

$(NAME).lrf: $(MAKEFILE)
   @%write $^@ system os2v2
   @%write $^@ option verbose
   @%write $^@ option cache
   @%write $^@ option alignment=1024
!ifdef DEBUG
    @%write $^@ debug dwarf all
!endif
   @%write $^@ option map
   @%write $^@ op desc '@$#$(%VENDOR):$(%BUILDVER)$#@$#$#1$#$# $(%BUILDTIME) $(FILL:x= ) $(%BUILDMACH)::EN:US:0:U:@@$(DESC)'
   @%write $^@ name $(NAME)
   for %f in ($(ALL_OBJS)) do @%append $^@ file %f
!ifdef EXCEPTQ
    @%write $^@ file exceptq.lib
!endif
#   @%write $^@ import _inp portio.'_inp', _outp portio.'_outp'
   for %f in ($(ALL_LIBS)) do @%append $^@ library %f

$(EXENAME): $(OBJS) $(MAKEFILE) $(LIBS) $(NAME).lrf
   echo LIB is $(%LIB)
   $(LINK) @$(NAME).lrf
   mapxqs $(NAME).map
   echo Built with OpenWatcom >$(NAME).lvl
   echo Using bcos2 portio.dll >>$(NAME).lvl
   echo. >>$(NAME).lvl
   bldlevel $@ | tee /a $(NAME).lvl
!ifndef %DEBUG
  lxlite -B- -X- $@
!endif

lxlite: $(EXENAME) .symbolic
    dir $<
    lxlite -X- -B- $<
    bldlevel $<
    dir $<

strip: .symbolic
   dir /km $(EXENAME)
   @echo.
   -wstrip $(EXENAME)
   lxlite -B- $(EXENAME)
   @echo.
   dir /km $(EXENAME)

distclean: .symbolic
  -del $(NAME).err
  -del $(NAME).lrf
  -del $(NAME).map
  -del $(NAME).mbr
  -del $(NAME).sym
  -del $(OBJS)

clean: distclean .symbolic
  -del $(EXENAME)

settings: .SYMBOLIC
  @echo MAKE is $MAKE
  @echo __VERSION__ is $__VERSION__
  @echo MAKEOPTS is $__MAKEOPTS__
  @echo MAKEFILES is $__MAKEFILES__
  @echo __LOADDLL__ is $__LOADDLL__
