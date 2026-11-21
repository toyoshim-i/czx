#include "quasi88.h"

#define quasi88_menu czx_unused_upstream_quasi88_menu
#define quasi88_quit czx_unused_upstream_quasi88_quit
#include "quasi88.c"
#undef quasi88_quit
#undef quasi88_menu

void quasi88_menu(void)
{
    set_mode(MENU);
}

void quasi88_quit(void)
{
    /* In libretro, confirming Quit in Q8TK returns cleanly to EXEC. */
    quasi88_event_flags &= ~EVENT_QUIT;
    quasi88_exec();
}
