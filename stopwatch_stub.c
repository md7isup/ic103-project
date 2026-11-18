#include <time.h>
#include "stopwatch.h"

#define MAX_LAPS 100

static sw_state_t state = SW_READY;
static double accumulated = 0.0, started_at = 0.0;
static lap_t laps[MAX_LAPS];
static int nlaps = 0;

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void stopwatch_start(void) {
    accumulated = 0.0; nlaps = 0;
    started_at = now_seconds(); state = SW_RUNNING;
}
void stopwatch_pause(void) {
    if (state == SW_RUNNING) { accumulated += now_seconds() - started_at; state = SW_PAUSED; }
}
void stopwatch_resume(void) {
    if (state == SW_PAUSED) { started_at = now_seconds(); state = SW_RUNNING; }
}
void stopwatch_reset(void) { accumulated = 0.0; nlaps = 0; state = SW_READY; }

double sw_get_elapsed(void) {
    return state == SW_RUNNING ? accumulated + now_seconds() - started_at : accumulated;
}
sw_state_t sw_get_state(void) { return state; }

int lap_add(void) {
    if (state != SW_RUNNING || nlaps >= MAX_LAPS) return -1;
    long total = (long)(sw_get_elapsed() * 1000.0 + 0.5);
    long prev = nlaps ? laps[nlaps - 1].total_ms : 0;
    laps[nlaps].no = nlaps + 1;
    laps[nlaps].total_ms = total;
    laps[nlaps].split_ms = total - prev;
    nlaps++;
    return 0;
}
int lap_undo(void) { if (nlaps == 0) return -1; nlaps--; return 0; }
int lap_count(void) { return nlaps; }
lap_t lap_get(int i) {
    lap_t zero = {0, 0, 0};
    return (i >= 0 && i < nlaps) ? laps[i] : zero;
}
int lap_stats(lap_stats_t *out) {
    if (nlaps == 0) return -1;
    long sum = 0;
    out->count = nlaps;
    out->fastest_no = out->slowest_no = laps[0].no;
    out->fastest_ms = out->slowest_ms = laps[0].split_ms;
    for (int i = 0; i < nlaps; i++) {
        sum += laps[i].split_ms;
        if (laps[i].split_ms < out->fastest_ms) { out->fastest_ms = laps[i].split_ms; out->fastest_no = laps[i].no; }
        if (laps[i].split_ms > out->slowest_ms) { out->slowest_ms = laps[i].split_ms; out->slowest_no = laps[i].no; }
    }
    out->average_ms = sum / nlaps;
    return 0;
}
