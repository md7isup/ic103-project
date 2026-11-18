#include "signals.h"
#include "ui.h"

int main(void) {
    setup_signals();
    ui_run();
    return 0;
}
