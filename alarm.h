#ifndef ALARM_H
#define ALARM_H

struct Alarm { int id; int hour; int minute; int second; int enabled; };

int alarm_count(void);
int alarm_get(int index, struct Alarm *out);   /* 0 ok, -1 bad index */
int add_alarm(int hour, int minute, int second); /* new id, or -1 if full */
int delete_alarm(int id);                      /* 0 ok, -1 not found */
int enable_alarm(int id);
int disable_alarm(int id);

#endif
