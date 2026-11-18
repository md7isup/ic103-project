#ifndef BUDGET_H
#define BUDGET_H

typedef struct {
    char name[16];     /* "Coding", "Studying", "Exercise" */
    int  limit_min;
    int  warn_pct;
    int  used_sec;
    int  warned;
    int  exceeded;
} Budget;

int         budget_count(void);
int         budget_get(int index, Budget *out);  /* 0 ok, -1 bad index */
int         budget_start(const char *activity);  /* 0 ok, -1 unknown */
void        budget_pause(void);
void        budget_stop(void);
const char *budget_active(void);                 /* name, or NULL if none */
int         budget_is_running(void);

#endif
