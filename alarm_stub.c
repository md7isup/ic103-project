#include "alarm.h"

#define MAX_ALARMS 10
static struct Alarm list[MAX_ALARMS];
static int n = 0, next_id = 1;

static int find(int id) {
    for (int i = 0; i < n; i++) if (list[i].id == id) return i;
    return -1;
}
int alarm_count(void) { return n; }
int alarm_get(int i, struct Alarm *out) {
    if (i < 0 || i >= n) return -1;
    *out = list[i];
    return 0;
}
int add_alarm(int h, int m, int s) {
    if (n >= MAX_ALARMS) return -1;
    list[n] = (struct Alarm){ next_id, h, m, s, 1 };
    n++;
    return next_id++;
}
int delete_alarm(int id) {
    int k = find(id);
    if (k < 0) return -1;
    for (int j = k; j < n - 1; j++) list[j] = list[j + 1];
    n--;
    return 0;
}
int enable_alarm(int id)  { int k = find(id); if (k < 0) return -1; list[k].enabled = 1; return 0; }
int disable_alarm(int id) { int k = find(id); if (k < 0) return -1; list[k].enabled = 0; return 0; }
