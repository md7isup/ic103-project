#include <signal.h>
#include <stddef.h>
#include "signals.h"

static volatile sig_atomic_t shutdown_flag = 0;
static sigset_t orig_mask;

static void on_signal(int sig) { (void)sig; shutdown_flag = 1; }

void setup_signals(void) {
    struct sigaction sa;
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                       /* no SA_RESTART */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    sigset_t block;
    sigemptyset(&block);
    sigaddset(&block, SIGINT);
    sigaddset(&block, SIGTERM);
    sigprocmask(SIG_BLOCK, &block, &orig_mask);
}
int signals_shutdown_requested(void) { return shutdown_flag; }
const sigset_t *signals_wait_mask(void) { return &orig_mask; }
