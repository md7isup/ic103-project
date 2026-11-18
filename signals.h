#ifndef SIGNALS_H
#define SIGNALS_H
#include <signal.h>

void             setup_signals(void);               /* install handlers, block signals */
int              signals_shutdown_requested(void);  /* 1 after SIGINT / SIGTERM */
const sigset_t  *signals_wait_mask(void);           /* mask to pass to pselect() */

#endif
