#ifndef UI_H
#define UI_H

void ui_run(void);              /* runs the interface until the user quits */
void ui_cleanup(void);          /* restores the terminal; safe to call more than once */
void ui_alert(const char *msg); /* banner + beep for alarm / budget events */

#endif
