#include <string.h>
#include <time.h>
#include "budget.h"

static Budget b[3] = {
    {"Coding",   60, 80, 0, 0, 0},
    {"Studying", 90, 80, 0, 0, 0},
    {"Exercise", 30, 80, 0, 0, 0},
};
static int active = -1, running = 0;
static double started = 0.0;

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
static void bank(void) {
    if (running && active >= 0) b[active].used_sec += (int)(now_seconds() - started);
    running = 0;
}
int budget_count(void) { return 3; }
int budget_get(int i, Budget *out) {
    if (i < 0 || i >= 3) return -1;
    *out = b[i];
    if (running && i == active) out->used_sec += (int)(now_seconds() - started);
    int limit = out->limit_min * 60;
    if (out->used_sec >= limit * out->warn_pct / 100) out->warned = 1;
    if (out->used_sec > limit) out->exceeded = 1;
    return 0;
}
int budget_start(const char *name) {
    for (int i = 0; i < 3; i++) {
        if (strcmp(b[i].name, name) == 0) {
            bank();
            active = i; running = 1; started = now_seconds();
            return 0;
        }
    }
    return -1;
}
void budget_pause(void) { bank(); }
void budget_stop(void)  { bank(); active = -1; }
const char *budget_active(void) { return active >= 0 ? b[active].name : NULL; }
int budget_is_running(void) { return running; }
