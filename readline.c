// vim: autoindent tabstop=8 shiftwidth=4 expandtab softtabstop=4

// Copyright 2020 Greg Ercolano.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>

#ifdef LINUX
#define kbhit() 1
#else
#include <dos.h>
#include <conio.h>
#endif

#include "readline.h"

#define MAX(x,y)               (((x)>(y))?(x):(y))
#define MIN(x,y)               (((x)<(y))?(x):(y))

//
// readline - A DOS 16 bit readline for low memory use DOS 16 bit apps
//
//     Similar to GNU Readline, for simple line editing and a
//     command history, but implemented as a single .c file
//     written with the limits of DOS 640k applications in mind.
//
//           Up Arrow -- previous line in command history      (^P)
//           Dn Arrow -- next line in command history          (^N)
//           Lt Arrow -- move reverse one char on current line (^B)
//           Rt Arrow -- move forward one char on current line (^F)
//          Backspace -- backspace and delete                  (^H)
//          Delete    -- delete character                      (^D)
//           Home     -- move to start of current line         (^A)
//           End      -- move to end of current line           (^E)
//         Ctrl-Home  -- jump to top of command history
//         Ctrl-End   -- jump to bottom of command history (current line)
//         Ctrl-Left  -- word left
//         Ctrl-Right -- word right
//              ^K    -- clear to end of line
//              ^U    -- clear current line (and 'undo' clear if hit again)
//              ^V    -- Enter literal next character (like VI)
//              ESC   -- clear current line (and 'undo' clear if hit again)
//

// C types
typedef unsigned char   uchar;
typedef unsigned short  ushort;
typedef unsigned int    uint;
typedef unsigned long   ulong;

// CREATE A NEW Readline STRUCT
Public Readline* MakeReadline(int maxline,     // max chars per line
                              int histsize)    // max history of lines
{
    int t;
    Readline *rs = (Readline*)malloc(sizeof(Readline));
    rs->maxline  = maxline;
    rs->histsize = histsize;
    rs->history  = (char**)malloc(sizeof(char*) * histsize);
    // Pre-allocate history, to prevent mem fragmentation
    for ( t=0; t<histsize; t++ ) {
        rs->history[t] = (char*)malloc(maxline); // alloc line
        rs->history[t][0] = 0;                   // null terminate
    }
    rs->histsave= (char*)malloc(maxline);        // alloc line
    rs->histsave[0] = 0;
    rs->undoline= (char*)malloc(maxline);        // undo line
    rs->undoline[0] = 0;
    rs->undocurpos = 0;
    rs->curpos  = 0;
    rs->promptx = 0;    // 0=first char, (scrn_w-1)=last char
    rs->prompty = 23;   // 0=top line, (rs->scrn_h-1)=bottom line
    rs->prompt  = "PROMPT>";  // can be redefined by caller
    rs->cursorx = 0;
    rs->cursory = 0;
    rs->literal = 0;
    rs->scrn_w  = 80;   // can be redefined by caller
    rs->scrn_h  = 25;   // can be redefined by caller
    return rs;
}

// FREE A Readline STRUCT
Public void FreeReadline(Readline *rs)
{
    int t;
    for ( t=0; t<rs->histsize; t++ ) {
       free((void*)rs->history[t]);     // free history lines
       rs->history[t] = 0;
   }
   free((void*)rs->history);            // free history array
   free((void*)rs->histsave);           // free history save line
   free((void*)rs->undoline);           // free undo buffer
   free((void*)rs);                     // free struct allocation
}

#ifdef LINUX
Local char getch()
{
    char c;
    fread(&c, 1, 1, stdin);     // assume we're invoked in raw mode (stty raw)
    return c;
}
#endif

//UNUSED Local void beep(void)
//UNUSED {
//UNUSED    sound(1000);        // TC: speaker frequency (1khz)
//UNUSED    delay(250); // TC: time in msecs (1/4 sec)
//UNUSED    nosound();  // TC: stop sound
//UNUSED }

// POSITION CURSOR (ZERO BASED)
Local void cursor_pos(int x, int y)
{
    printf("\033[%d;%dH", y+1, x+1);    // 0,0 -> 1,1
}

// PLOT CHAR 'c' ATTRIBUTE 'attr' AT POSITION x,y (ZERO BASED)
Local void PlotAttr(int x, int y, uchar c, uchar attr)
{
#ifdef LINUX
    cursor_pos(x,y); printf("%c",c);
#else
    uchar far *mono = MK_FP(0xb000, (y*160)+(x*2));
    uchar far *cga  = MK_FP(0xb800, (y*160)+(x*2));
    *mono++ = c; *mono = attr;
    *cga++  = c; *cga  = attr;
#endif
}

// PLOT CHAR 'c' AT POSITION x,y (ZERO BASED)
Local void Plot(int x, int y, uchar c)
{
    PlotAttr(x, y, c, 0x07);    // 'normal' attribute
}

// CLEAR FROM (x,y) TO END OF SCREEN
//     ..using character 'c' as the clear character
//
Local void Cleos(Readline *rs, int x, int y, char c)
{
    // CLEAR TO EOS
    while ( y <= rs->scrn_h ) {
        Plot(x, y, c);
        if ( ++x > rs->scrn_w ) { x = 0; ++y; }
    }
}

// 80 //////////////////////////////////////////////////////////////////////////

// RETURN LENGTH OF STRING IN SCREEN UNITS TO CALCULATE SCREEN WRAPS
//
//    Returns the unclipped length of string from left edge of line
//    with prompt to end of line. This can be used to accurately
//    calculate how many screen wraps the line will make when drawn.
//
//    Like strlen(), but includes tab expansions.
//    'xoff' indicates x position of start of string
//    as this is needed to accurately calculate tab columns.
//
Local int screen_len(Readline *rs,	// used for screen dimensions
		     const char *line)  // line being measured
{
    int x = rs->promptx + strlen(rs->prompt);
    int len = x;
    while ( *line ) {
        if ( *line == 0x09 ) {   // tab char?
            do {
	        ++len;
		x = (x+1) % rs->scrn_w;
                if ( (x % 8) == 0 ) break;   // hit tab column? stop
            } while (1);
	} else {
	    ++len;
	    ++x;
	}
	++line;
    }
    return len;
}

// DRAW STRING AT (x,y) LEAVING (x,y) ADJUSTED
Local void Draw(Readline *rs, int *xp, int *yp, const char *s)
{
    int x = *xp, y = *yp;
    int cnt = 0;
    for ( ; *s && cnt<rs->maxline; s++,cnt++ ) {
        // Special case for TAB character
        //    Draw tab as spaces up to 8th column
        //
        if ( *s == 0x09 ) {
            do {
                Plot(x, y, ' ');
                if ( ++x > (rs->scrn_w-1) ) { x = 0; ++y; }
                if ( (x % 8) == 0 ) break;   // hit tab column? stop
            } while (1);
            continue;
        }
        Plot(x, y, *s);
        if ( ++x > (rs->scrn_w-1) ) { x = 0; ++y; }
    }
    *xp = x; *yp = y;
}

// FORCE PAGE TO SCROLL UP ONE LINE
Local void scroll_up(int lines)
{
    printf("\33[s"		// save cursor
           "\33[25;0H");	// go to bottom line
    while (lines-- > 0 ) printf("\n");
    printf("\33[u");            // restore cursor to where it was
}

// REDRAW LINE AT PROMPT
//    Draw directly to the screen to prevent cursor chatter
//
Local void redraw_line(Readline *rs)
{
    char *line  = rs->history[0];           // redraw current line
    int linelen = screen_len(rs, line);     // like strlen but includes tabs

    // IF LINE WOULD RUN OFF EDGE OF LAST LINE OF SCREEN, ADJUST PROMPTY
    //
    //   Once adjusted, subsequent calls won't retrigger this code
    //   because prompty is now /adjusted/, taking into account the
    //   scrolling that will happen when the line is actually printed.
    //
    int new_y = (rs->prompty + (linelen / rs->scrn_w));
    int max_y = (rs->scrn_h-1);
    if ( new_y > max_y ) {
        int diff = new_y - max_y;
        // Adjust prompty to be higher now that screen scrolled up
        rs->prompty -= diff;
	scroll_up(diff);
    }

    // DRAW ENTIRE LINE (INCLUDING PROMPT) TO EOS
    {
	int x = rs->promptx;
	int y = rs->prompty;
	Draw(rs, &x, &y, rs->prompt);     // DRAW PROMPT
	Draw(rs, &x, &y, rs->history[0]); // DRAW LINE BEING EDITED
	Cleos(rs, x, y, ' ');             // CLEAR TO EOS
    }

    // LEAVE CURSOR AT INSERT POINT
    {
        int x = rs->promptx + strlen(rs->prompt);
        int y = rs->prompty;
        int i = 0;

        // Find x,y for curpos
        //    Take into account tabs
        //
        while ( line[i] ) {
            if ( i == rs->curpos ) break;
            if ( line[i] == 0x09 ) {
                do { if ( (++x%8) == 0 ) break; } while(1);
            } else { ++x; }
            if ( ++i == rs->maxline ) break;
        }
        // Keep x/y within screen's w/h dimensions
        y += x/rs->scrn_w;
        x %= rs->scrn_w;
        cursor_pos(x, y);
        if ( rs->literal ) {
            Plot(x, y, '^');  // put caret under cursor
            cursor_pos(x, y);
        }
    }
}

////              ///////////////////////////////////////
//// LINE EDITING ///////////////////////////////////////
////              ///////////////////////////////////////

// DELETE CHAR AT CURRENT LINE + CURSOR POSITION
Local void delete_char(Readline *rs)
{
    int i;
    int maxline = rs->maxline;
    int curpos  = MIN(rs->curpos, maxline-2);   // leave room for char + NULL
    char *line  = rs->history[0];               // entire line being edited

    if ( line[curpos] == 0 ) return;            // dont delete NULL!
    for ( i=curpos; i<(maxline-1); i++ )
        { line[i] = line[i+1]; }
    line[maxline-1] = 0;        // ensure line terminated
}

// INSERT CHAR 'c' INTO CURRENT LINE + CURSOR POSITION
//     Where rs->history[0] is current line, rs->curpos is cursor pos'n
//
Local void insert_char(Readline *rs, char c)
{
    int i;
    int maxline = rs->maxline;
    int curpos  = MIN(rs->curpos, maxline-2);   // leave room for char + NULL
    char *line  = rs->history[0];               // entire line being edited

    //DEBUG printf("\33[10;0HMaxLine=%d CurPos=%d mincurpos=%d\n",
    //DEBUG     maxline, rs->curpos, curpos);

    // INSERT
    //     Start at eol, drag chars to right until cursor
    //
    for ( i=maxline-1; i>curpos; i-- )
        { line[i] = line[i-1]; }

    line[curpos] = c;           // drop in char
    line[maxline-1] = 0;        // ensure line terminated
}

// MOVE CURSOR TO LEFT (IF POSSIBLE)
//     Won't move into prompt.
//
Local void cursor_left(Readline *rs)
{
    if ( rs->curpos > 0 ) --rs->curpos;
}

// MOVE CURSOR TO RIGHT (IF POSSIBLE)
//     Won't advance beyond end of string or maxline
//
Local void cursor_right(Readline *rs)
{
    int max = MIN(strlen(rs->history[0]), (rs->maxline-1));
    if ( rs->curpos < max ) ++rs->curpos;
}

Local void cursor_sol(Readline *rs)
{
    rs->curpos = 0;
}

Local void cursor_eol(Readline *rs)
{
    int eol = MIN(strlen(rs->history[0]), (rs->maxline-1));
    rs->curpos = eol;
}

// MOVE TO FIRST LETTER IN EACH WORD
//     Look for a letter preceded by whitespace.
//
Local void word_right(Readline *rs)
{
    char *line = rs->history[0];
    int    end = strlen(line);
    cursor_right(rs);
    while ( rs->curpos < end ) {
        char lc = line[rs->curpos];
        cursor_right(rs);
        if ( lc == ' ' && line[rs->curpos] != ' ' )
            { break; }
    }
}

// MOVE TO FIRST LETTER IN EACH WORD
//     Look for a letter preceded by whitespace.
//
Local void word_left(Readline *rs, int del)
{
    int lcp = rs->curpos;
    char *line = rs->history[0];

    // Move left at *least* once
    cursor_left(rs);

    // Delete? If not first char, delete char
    if ( del && lcp != 0 ) delete_char(rs);

    // Word loop..
    while ( rs->curpos > 0 ) {
        char lc = line[rs->curpos];     // save last char
        int  lcp = rs->curpos;          // save last curpos
        cursor_left(rs);                // move left
        if ( line[rs->curpos] == ' ' && lc != ' ' ) // start of word?
            { cursor_right(rs); break; }            // move fwd, done
        if ( del && lcp != 0 )          // delete? if not first char..
            delete_char(rs);            // ..then delete char under cursor
    }
}

Local void backspace(Readline *rs)
{
    if ( rs->curpos > 0 )
        { cursor_left(rs); delete_char(rs); }
}

// APPEND A CHARACTER TO LINE AT CURRENT CURSOR POSITION
//    Insert character at cursor position, then move right
//
Local void append_char(Readline *rs, char c)
{
    insert_char(rs, c);
    cursor_right(rs);
}

// CLEAR ALL CHARACTERS TO EOL
Local void clear_eol(Readline *rs)
{
    char *line = rs->history[0];
    line[rs->curpos] = 0;       // truncate at cursor pos
    cursor_eol(rs);             // move to eol
}

// SAVE CURRENT LINE TO UNDO BUFFER
Local void undo_save(Readline *rs)
{
    rs->undocurpos = rs->curpos;                // remember cursor pos (^K)
    strcpy(rs->undoline, rs->history[0]);       // remember line
}

// RESTORE CURRENT LINE FROM COPY IN UNDO BUFFER
Local void undo_restore(Readline *rs)
{
    strcpy(rs->history[0], rs->undoline);       // restore line
    rs->curpos = MIN(rs->undocurpos, strlen(rs->history[0]));
}

////                 ////////////////////////////////////
//// COMMAND HISTORY ////////////////////////////////////
////                 ////////////////////////////////////

// MOVE UP TO REVEAL NEXT HISTORY LINE
//    If we're on first edit line, SAVE IT FIRST,
//    then copy down from history over it
// Returns:
//    1 -- Successfully moved up one line
//    0 -- Can't go higher
//
Local int history_up(Readline *rs)
{
    // Already at top? Do nothing
    if ( rs->histpos == (rs->histsize-1) ) { return 0; }

    // Next line up empty? Do nothing
    if ( rs->history[rs->histpos+1][0] == 0 ) { return 0; }

    // Save current edit line to restore for later
    if ( rs->histpos == 0 )
        strcpy(rs->histsave, rs->history[0]);

    // Visit next line in history
    if ( rs->histpos < (rs->histsize-1) )
        ++rs->histpos;

    // Copy that line to current edit line
    strcpy(rs->history[0], rs->history[rs->histpos]);

    // Leave cursor at eol
    rs->curpos = strlen(rs->history[0]);

    return 1;
}

// MOVE DOWN TO REVEAL PREVIOUS HISTORY LINE
//    If we're back to first edit line, RESTORE IT
//    (if not already); user may have visited history and returned.
//
Local void history_down(Readline *rs)
{
    // Already at bottom? Do nothing
    if ( rs->histpos == 0 ) return;

    // Move down
    --rs->histpos;

    // Returned to edit line?
    if ( rs->histpos == 0 ) {
        // Restore previous edit line
        strcpy(rs->history[0], rs->histsave);
    } else {
        // Revisit history line selected
        strcpy(rs->history[0], rs->history[rs->histpos]);
    }

    // Leave cursor at eol
    rs->curpos = strlen(rs->history[0]);
}

// MOVE TO TOP OF HISTORY
Local void history_top(Readline *rs)
{
    // Already at max top? Do nothing
    if ( rs->histpos == (rs->histsize-1) ) return;

    // Loop until we reach top
    //    Need a loop, because 'top' might be NULL.
    //
    while ( history_up(rs) )
        { }
}

// MOVE TO BOTTOM OF HISTORY (RETURN TO EDIT LINE)
Local void history_bot(Readline *rs)
{
    // If already at bottom, early exit
    if ( rs->histpos == 0 ) return;

    // Cheat: force histpos to 1, and go down
    //     This reuses logic to handle restoring edit line
    //
    rs->histpos = 1;
    history_down(rs);
}

// PUSH CURRENT COMMAND INTO HISTORY
//     BEFORE: a,b,c,d,e
//      AFTER: a,a,b,c,d
//             ^ current cmd, e.g. history[0]
//
Local void push_history(Readline *rs)
{
    int t;
    int top    = rs->histsize-1;
    char *htop = rs->history[top];  // save top; we overwrite it w/scroll

    // FIRST SCROLL HISTORY UP
    //    Just move the pointers around. Could have been a linked list,
    //    but working with an array is just plain easier to write and debug.
    //    This overwrites top, and leaves [0] and [1] dupes.
    //
    for ( t=top; t>0; t-- )
        { rs->history[t] = rs->history[t-1]; }

    // MOVE 'TOP' DOWN TO history[0]
    //    We don't care about top's string contents, but
    //    we don't want to lose top's memory allocation.
    //
    rs->history[0] = htop;

    // COPY EDIT LINE SCROLLED INTO history[1] back to history[0]
    strcpy(rs->history[0], rs->history[1]);
}

// SHOW CONTENTS OF HISTORY BUFFER ON STDOUT
Public void show_history(Readline *rs)
{
    int t, top = rs->histsize-1;
    for ( t=top; t>=0; t-- ) {
        printf("%02d) %s\033[K\n", t, rs->history[t]);
    }
}

// SEE IF LINE IS EMPTY (ALL BLANKS)
Local int is_empty(const char *s)
{
    // Stop at eol or first non-white char
    while ( *s && (*s==' ' || *s=='\t') ) s++;
    return *s ? 0 : 1;
}

// USER HIT ENTER
//    Conditionally save line in history, redraw line
//
Local void enter_key(Readline *rs)
{
    const char *last = rs->history[1];
    const char *line = rs->history[0];

    // Reset histpos to zero
    rs->histpos = 0;

    // Save copy of line to history
    //    Only if non-blanks and not same as last line
    //
    if ( !is_empty(line) && strcmp(last, line)!=0)
        push_history(rs);

    // Leave cursor on next line after eol
    cursor_eol(rs);
    redraw_line(rs);
    printf("\n");
}

Local void line_cancel(Readline *rs)
{
    if ( rs->lcanmode == 0 ) {
        undo_save(rs);
        rs->history[0][0] = 0;      // truncate line
        rs->curpos        = 0;      // cursor to sol
    } else {
        undo_restore(rs);
    }
    rs->lcanmode ^= 1;  // toggle between save/restore modes
    rs->lcankey   = 1;  // line cancel key was hit
}

// 80 //////////////////////////////////////////////////////////////////////////

// HANDLE ESC CHAR
//    On linux, this is the start of a multi-character terminal key code.
//    in DOS this is just the escape key, which is line cancel.
//
Local int handle_esc(Readline *rs)
{
#ifdef LINUX

  char c = getch();
  if ( c!= '[' ) return c;
  c = getch();
  switch (c) {
    case 'A': history_up(rs);   rs->hnav=1; return 256; // UP ARROW  ESC[A
    case 'B': history_down(rs); rs->hnav=1; return 256; // DN ARROW  ESC[B
    case 'C': cursor_right(rs);             return 256; // RT ARROW  ESC[C
    case 'D': cursor_left(rs);              return 256; // LT ARROW  ESC[D
    case 'H': cursor_sol(rs);               return 256; // HOME      ESC[H
    case 'F': cursor_eol(rs);               return 256; // END       ESC[F
    case '5':                      getch(); return 256; // PG UP     ESC[5~
    case '6':                      getch(); return 256; // PG DN     ESC[6~
    case '3': delete_char(rs);     getch(); return 256; // DEL       ESC[3~
    case '1':
      if ( (c = getch()) != ';' ) return c;
      if ( (c = getch()) != '5' ) return c;
      c = getch();
      switch (c) {
	case 'A': history_top(rs); rs->hnav=1; return 256; // CTRL-UP  ESC[1;5A
	case 'B': history_bot(rs); rs->hnav=1; return 256; // CTRL-DN  ESC[1;5B
	case 'C': word_right(rs);              return 256; // CTRL-RT  ESC[1;5C
	case 'D': word_left(rs,0);             return 256; // CTRL-LT  ESC[1;5D
	default: return c;
      }
      break;
  }
  return c;

#else

  // DOS: ESC cancels line
  line_cancel(rs);

#endif
}

// Read a line from the user
//     Handles line editing, command history.
//
Public char* readline(Readline *rs)
{
    int save_py = rs->prompty;   // save; we may adjust during scrolls
    uchar c;
    char cleolkey = 0;           // FLAG: 0=non-cleol, 1=cleol
    char cleolmode = 0;          // FLAG: 0=undo_save(), 1=undo_restore()
    char *line = rs->history[0]; // history[0] is always "current line"

    rs->curpos   = 0;            // current cursor position starts at 0
    rs->hnav     = 0;
    rs->lcanmode = 0;            // line cancel mode starts in save mode
    rs->lcankey  = 0;            // line cancel key not hit yet
    rs->histpos  = 0;            // reset history position to 0
    line[0]      = 0;            // start with an empty line

    while ( 1 ) {
//DEBUG cursor_pos(0, 0);       // DEBUG
//DEBUG show_history(rs);       // DEBUG

        rs->hnav    = 0;
        rs->lcankey = 0;
        cleolkey = 0;
        redraw_line(rs);
        c = getch();
        //cursor_pos(1,2); printf("GOTCHAR(%02x)\n", c);

        // Handle literal (^V) mode right away
        //     Whatever character user types next is inserted raw into line.
        //
        if ( rs->literal ) {
            rs->literal = 0;            // first disable mode
            if ( c == 0 ) continue;     // not allowed for multi-code keys
            append_char(rs, c);         // append raw character
            goto post;
        }
again:
        switch (c) {
	    // MULTI-CODE TERMINAL KEYS (LINUX)
	    case 0x1b:
	        if ( (c=handle_esc(rs)) == 256 ) break; // handled
		goto again;                             // not handled

            // MULTI-CODE KEYS
            case 0x00:
                c = getch();
                // cursor_pos(1,2); printf("GOTCHAR(%02x)\n", c);
                switch (c) {
                    case 0x3d: history_up(rs);               break; // F3
                    case 0x4b: cursor_left(rs);              break; // LT ARROW
                    case 0x4d: cursor_right(rs);             break; // RT ARROW
                    case 0x48: history_up(rs);   rs->hnav=1; break; // UP ARROW
                    case 0x50: history_down(rs); rs->hnav=1; break; // DN ARROW
                    case 0x47: cursor_sol(rs);               break; // HOME
                    case 0x4f: cursor_eol(rs);               break; // END
                    case 0x53: delete_char(rs);              break; // DEL
                    case 0x8d: history_top(rs);  rs->hnav=1; break; // CTRL-UP
                    case 0x91: history_bot(rs);  rs->hnav=1; break; // CTRL-DOWN
                    case 0x73: word_left(rs,0);              break; // CTRL-LT
                    case 0x74: word_right(rs);               break; // CTRL-RT
                }
                break;

            case 0x15:                          // ^U  -- line cancel/undo
	        line_cancel(rs);
                break;

            case 0x08: backspace(rs);   break;  // BACKSPACE
            case 0x7f: delete_char(rs); break;  // CTRL-BACKSPACE (DEL)
            // INS        -- enable/disable onscreen insert vs. overwrite mode
            // Ctrl-RIGHT -- word right
            // Ctrl-LEFT  -- word left
            // Alt-num    -- enter extended PC graphics characters in decimal
            // ^W         -- delete word left (del contiguous whitespace "word")
            // Ctrl-DEL   -- delete word right
            // ^L         -- clear screen, repaint current line
            case '\r':
            case '\n': enter_key(rs);                       // ENTER
                       rs->prompty = save_py;
                       return rs->history[0];
            case 0x01: cursor_sol(rs);               break; // ^A / HOME
            case 0x02: cursor_left(rs);              break; // ^B / LT ARROW
            case 0x03:                               break; // ^C nop
            case 0x04: delete_char(rs);              break; // ^D / DEL
            case 0x05: cursor_eol(rs);               break; // ^E / END
            case 0x06: cursor_right(rs);             break; // ^F / RT ARROW
            case 0x0e: history_down(rs); rs->hnav=1; break; // ^N / DN ARROW
            case 0x10: history_up(rs);   rs->hnav=1; break; // ^P / UP ARROW
            case 0x16: rs->literal ^= 1;             break; // ^V literal char
            case 0x0b:                                      // ^K clear to eol
                if ( cleolmode == 0 ) { undo_save(rs); clear_eol(rs); }
                else                  { undo_restore(rs); }
                cleolmode ^= 1; // toggle between save/restore modes
                cleolkey   = 1; // cleol key hit
                break;

// COMMENTED OUT UNTIL word_left(rs,1) works properly
// TODO:
//     If cursor is on a space, hitting ^W cancels entire line
//     because of how it hunts for a space/non-space.
//     Change this so char delete happens AFTER new position detected.
//     Maybe save curpos, do a word right, then do a delete_range()
//     from saved pos to curpos.
//
//
//          case 0x17: word_left(rs,1);  break;   // ^W delete word left

            default:
                // User typed some text, add it to string.
		//    Ignore all unhandled ctrl codes except Tab.
		//    If user wants to insert special chars (like ESC),
		//    they can use ^V to do it, e.g. (^V) (ESC).
		//
		if ( c == '\t' || c >= ' ' ) append_char(rs, c);
                break;
        }
post:
        // HISTORY NAVIGATION UNDO
        if ( ! rs->hnav ) {
            rs->histpos = 0;    // reset history pos unless navigating
        }
        // LINE CANCEL UNDO
        if ( ! rs->lcankey ) {
            rs->lcanmode = 0;       // reset to 'save' mode if not lcan key
        }
        // CLEOL UNDO
        if ( ! cleolkey ) {
            cleolmode = 0;      // reset to 'save' mode if not cleol key
        }
    }
}
