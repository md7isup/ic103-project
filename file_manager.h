#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

/* "name" is already validated by the UI: 1-24 chars, letters/digits/underscore */
int save_session(const char *name);        /* 0 ok, -1 error */
int load_session(const char *name);
int export_laps_csv(const char *name);
int session_file_exists(const char *name); /* 1 yes, 0 no */
int csv_file_exists(const char *name);

#endif
