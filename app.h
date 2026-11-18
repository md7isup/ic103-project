#ifndef APP_H
#define APP_H

/* Called once per UI refresh (about every 50 ms) from ui_run().
   Integration owners implement it: check the alarm_due flag, run budget_check(),
   and call ui_alert() for any event that fired. */
void app_poll_events(void);

#endif
