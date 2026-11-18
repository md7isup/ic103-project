#ifndef STOPWATCH_H
#define STOPWATCH_H

typedef enum { SW_READY, SW_RUNNING, SW_PAUSED } sw_state_t;
typedef struct { int no; long split_ms; long total_ms; } lap_t;
typedef struct {
    int  count;
    int  fastest_no;  long fastest_ms;
    int  slowest_no;  long slowest_ms;
    long average_ms;
} lap_stats_t;

void       stopwatch_start(void);
void       stopwatch_pause(void);
void       stopwatch_resume(void);
void       stopwatch_reset(void);
double     sw_get_elapsed(void);      /* seconds */
sw_state_t sw_get_state(void);

int        lap_add(void);             /* 0 ok, -1 if not running or full */
int        lap_undo(void);            /* 0 ok, -1 if no laps */
int        lap_count(void);
lap_t      lap_get(int i);
int        lap_stats(lap_stats_t *out);   /* 0 ok, -1 if no laps */

#endif
