##
## MAKEFILE FOR TURBO C 3.0
##
# TCC 3.0 Compiler flags
# ----------------------
#     -ml - large memory model      -f - floating point emulation
#     -K  - assume char unsigned    -w - enable warnings
#     -1  - generate 186/286 inst.  -G - generate for /speed/
#     -j8 - stop after 8 errors
#
CC       = tcc
CFLAGS	 = -DOPCS=1 -ml -f -w -1 -G -j8 -K
LFLAGS   =

# Default target
default: test-readline

### readline.obj module
###    Command line editing..
###
readline.pro: readline.c getproto.exe
	getproto < readline.c > readline.pro

readline.h: readline.pro

readline.obj: readline.h readline.c
	$(CC) $(CFLAGS) -c readline.c

###
### Readline test program - verify line editing
###
#
#   Due to DOS's 8.3 filename limit, we build test-readline
#   as 'readline.exe'
#
readline: readline.exe

readline.exe: readline.obj test-readline.c
	$(CC) $(CFLAGS) -c test-readline.c
	$(CC) $(CFLAGS) -ereadline.exe test-readline.obj readline.obj


