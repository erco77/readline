# readline
Small interactive command line editing module to link into applications that need a simple command line prompt, using this instead of fgets() to prompt user for commands, like a shell.

Allow MS-DOS applications that supply their own command prompt to have familiar interactive text editing functionality and a simple history mechanism for recalling previous lines.

Intended to be used as a MS-DOS module (.OBJ) linked into the main application that prompts for commands. The code is low overhead enough for DOS applications that don't have access to a lot of memory, and must operate within the 640k constraints of pre-Windows NT MS-DOS.

A simple test program demonstrates its use.

Written in Turbo C 3.0, but ported to allow building/testing on linux. (See Makefile.LINUX)
Linux applications shouldn't need this, as the GNU "readline" should be more appropriate.

