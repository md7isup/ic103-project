#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include "file_manager.h"
#include "stopwatch.h"

static void ensure_dirs(void) { mkdir("data", 0755); mkdir("data/sessions", 0755); }
static void session_path(const char *name, char *buf, size_t n) { snprintf(buf, n, "data/sessions/%s.txt", name); }
static void csv_path(const char *name, char *buf, size_t n)     { snprintf(buf, n, "data/%s_laps.csv", name); }

int session_file_exists(const char *name) { char p[128]; session_path(name, p, sizeof p); return access(p, F_OK) == 0; }
int csv_file_exists(const char *name)     { char p[128]; csv_path(name, p, sizeof p);     return access(p, F_OK) == 0; }

int save_session(const char *name) {
    char p[128];
    ensure_dirs();
    session_path(name, p, sizeof p);
    FILE *f = fopen(p, "w");
    if (!f) return -1;
    fprintf(f, "#IC103 v1\nSESSION=%s\nLAPS=%d\n", name, lap_count());
    for (int i = 0; i < lap_count(); i++) {
        lap_t l = lap_get(i);
        fprintf(f, "LAP,%d,%ld,%ld\n", l.no, l.split_ms, l.total_ms);
    }
    return fclose(f) == 0 ? 0 : -1;
}
int load_session(const char *name) { return session_file_exists(name) ? 0 : -1; }
int export_laps_csv(const char *name) {
    char p[128];
    ensure_dirs();
    csv_path(name, p, sizeof p);
    FILE *f = fopen(p, "w");
    if (!f) return -1;
    fprintf(f, "lap_no,split_ms,total_ms\n");
    for (int i = 0; i < lap_count(); i++) {
        lap_t l = lap_get(i);
        fprintf(f, "%d,%ld,%ld\n", l.no, l.split_ms, l.total_ms);
    }
    return fclose(f) == 0 ? 0 : -1;
}
