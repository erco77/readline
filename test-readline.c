#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "readline.h"

//
// test-readline.c - Test the "readline" module
//

// #include "regress1.c"   // regression tests

int main()
{
    // RegressionTest_delete_char();
    // RegressionTest_insert_char();
    char *s;
    Readline *rs = MakeReadline(255, 5);
    rs->prompt = "My Prompt>";
    strcpy(rs->history[0], "aaa");
    strcpy(rs->history[1], "bbb");
    strcpy(rs->history[2], "ccc");
    strcpy(rs->history[3], "ddd");
    strcpy(rs->history[4], "eee");
    printf("\033[2J\033[0;0H");  // cls, cursor to top
    show_history(rs);
    printf("Calling readline()..\n");
#ifdef LINUX
    system("stty raw -echo");
#endif
    s = readline(rs);
#ifdef LINUX
    system("stty -raw echo");
#endif
    printf("\rGOT: '%s'\n", s);
    show_history(rs);
    return 0;
}
