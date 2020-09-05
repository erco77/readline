#ifndef READLINE_H
#define READLINE_H

// In DOS I use a tool that keeps the prototypes up to date automatically,
// but it depends on this concept of using "Local" and "Public" to declare
// the functions..
#ifdef LINUX
  #define Local   static
  #define Public
#else
  #include "utype.h"	// uchar..
  #include "public.h"	// Public..
#endif

// Struct to manage readline history
typedef struct {
    int maxline;        // maximum line size
    int histsize;       // maximum history size
    char **history;     // history array
    int histpos;        // current up/down history position
    char *histsave;	// saved ptr for edit line during history nav
    char *undoline;     // copy of line used for 1 level undo
    int undocurpos;     // cursor position at time of undo save
    int curpos;         // current cursor position
    int promptx;        // prompt's X position on screen (0 based)
    int prompty;        // prompt's Y position on screen (0 based)
    char *prompt;       // prompt string
    int cursorx;        // onscreen cursor X position (0 based)
    int cursory;        // onscreen cursor Y position (0 based)
    int literal;        // 0|1 flag: =1 if ^V literal mode
    int scrn_w;         // screen width (default 80)
    int scrn_h;         // screen width (default 25)
    char hnav;          // FLAG: 1=history nav, 0=not hnav
    // linecancel flags
    char lcanmode;      // FLAG: 0=undo_save(), 1=undo_restore()
    char lcankey;       // FLAG: 0=non-line cancel, 1=lcan key
} Readline;

#include "readline.pro"

#endif
