/* ui.c - UI / Menu component
 *
 * One event loop (pselect, 50 ms) serves keypresses, signals and screen refresh.
 * The UI never measures time itself: it displays and controls the other modules.
 *
 * Sections:
 *   1. keys, modes and UI state
 *   2. terminal setup / restore
 *   3. formatting helpers
 *   4. frame building and screen drawing
 *   5. input: reading keys, key handlers per screen
 *   6. public functions: ui_run, ui_cleanup, ui_alert
 */
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>

#include "alarm.h"
#include "app.h"
#include "budget.h"
#include "file_manager.h"
#include "signals.h"
#include "stopwatch.h"
#include "ui.h"

#define PRINTF_LIKE(a, b) __attribute__((format(printf, a, b)))

/* ------------------------------------------------------------------ */
/* 1. keys, modes and UI state                                         */
/* ------------------------------------------------------------------ */

enum { KEY_EOF = -1, KEY_NONE = 0, KEY_ESC = 27,
       KEY_UP = 1000, KEY_DOWN, KEY_LEFT, KEY_RIGHT };

typedef enum {
    MODE_MAIN, MODE_LAPS, MODE_HELP, MODE_ALARMS, MODE_BUDGET, MODE_FILES,
    MODE_CONFIRM, MODE_INPUT
} ui_mode_t;

typedef enum {
    ACT_RESET, ACT_QUIT, ACT_DELETE_ALARM, ACT_SAVE_OVERWRITE, ACT_CSV_OVERWRITE
} confirm_act_t;

typedef enum { IN_ALARM_TIME, IN_SAVE_NAME, IN_LOAD_NAME, IN_CSV_NAME } input_kind_t;

#define NAME_LEN       24
#define REFRESH_MS     50
#define STATUS_SECONDS 2.0
#define EOL            "\033[K\n"      /* clear rest of line, then newline */

static struct {
    ui_mode_t     mode;
    ui_mode_t     return_mode;         /* where to go after confirm / input */
    int           quit;
    int           scroll;              /* lap list */
    int           cursor;              /* alarms / budgets */
    confirm_act_t confirm_act;
    char          confirm_text[80];
    char          pending_name[NAME_LEN + 1];
    int           pending_id;
    input_kind_t  input_kind;
    char          input_prompt[64];
    char          input_buf[NAME_LEN + 1];
    size_t        input_len;
    char          status[96];
    double        status_until;
    char          alert[128];
    int           alert_on;
} ui;

static double mono_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

PRINTF_LIKE(1, 2)
static void set_status(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ui.status, sizeof ui.status, fmt, ap);
    va_end(ap);
    ui.status_until = mono_now() + STATUS_SECONDS;
}

/* ------------------------------------------------------------------ */
/* 2. terminal setup / restore                                         */
/* ------------------------------------------------------------------ */

static struct termios saved_termios;
static int raw_on = 0;

/* Only async-signal-safe calls here (write, tcsetattr), and safe to call twice. */
void ui_cleanup(void) {
    if (!raw_on) return;
    raw_on = 0;
    static const char leave[] = "\033[0m\033[?25h\033[?1049l";   /* reset, show cursor, main screen */
    ssize_t ignored = write(STDOUT_FILENO, leave, sizeof leave - 1);
    (void)ignored;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
}

static void terminal_enter(void) {
    tcgetattr(STDIN_FILENO, &saved_termios);
    struct termios raw = saved_termios;
    raw.c_lflag &= ~(ICANON | ECHO);   /* ISIG stays ON so Ctrl+C still raises SIGINT */
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    raw_on = 1;
    fputs("\033[?1049h\033[?25l\033[2J\033[H", stdout);   /* alt screen, hide cursor, clear */
    fflush(stdout);
}

static int term_rows(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) return ws.ws_row;
    return 24;
}

/* ------------------------------------------------------------------ */
/* 3. formatting helpers                                               */
/* ------------------------------------------------------------------ */

static void fmt_ms(long total_ms, char *buf, size_t n) {
    if (total_ms < 0) total_ms = 0;
    long h  = total_ms / 3600000;
    long m  = (total_ms / 60000) % 60;
    long s  = (total_ms / 1000) % 60;
    long ms = total_ms % 1000;
    snprintf(buf, n, "%02ld:%02ld:%02ld.%03ld", h, m, s, ms);
}

static void format_time(double seconds, char *buf, size_t n) {
    fmt_ms((long)(seconds * 1000.0 + 0.5), buf, n);
}

static void fmt_delta(long delta_ms, char *buf, size_t n) {
    long a = delta_ms < 0 ? -delta_ms : delta_ms;
    snprintf(buf, n, "%c%ld.%03lds", delta_ms < 0 ? '-' : '+', a / 1000, a % 1000);
}

static void lap_line(int i, char *buf, size_t n) {
    lap_t l = lap_get(i);
    char split[32], total[32], delta[32], cmp[64] = "";
    fmt_ms(l.split_ms, split, sizeof split);
    fmt_ms(l.total_ms, total, sizeof total);
    if (i > 0) {
        long d = l.split_ms - lap_get(i - 1).split_ms;
        fmt_delta(d, delta, sizeof delta);
        snprintf(cmp, sizeof cmp, "%s %s", delta, d < 0 ? "faster" : (d > 0 ? "slower" : "same"));
    }
    snprintf(buf, n, "#%-3d split %s  total %s  %s", l.no, split, total, cmp);
}

/* ------------------------------------------------------------------ */
/* 4. frame building and screen drawing                                */
/* ------------------------------------------------------------------ */

/* Each screen is built in one buffer and written once per refresh: no flicker. */
static char   frame[8192];
static size_t flen;

PRINTF_LIKE(1, 2)
static void out(const char *fmt, ...) {
    if (flen >= sizeof frame - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(frame + flen, sizeof frame - flen, fmt, ap);
    va_end(ap);
    if (n > 0) {
        flen += (size_t)n;
        if (flen > sizeof frame - 1) flen = sizeof frame - 1;
    }
}

static void frame_begin(void) { flen = 0; out("\033[H"); }
static void frame_end(void) {
    out("\033[J");                       /* clear anything left below */
    fwrite(frame, 1, flen, stdout);
    fflush(stdout);
}

static void draw_main(void) {
    static const char *const names[] = { "READY", "RUNNING", "PAUSED" };
    char t[32];
    int n = lap_count();
    format_time(sw_get_elapsed(), t, sizeof t);

    out("  \033[1mDIGITAL STOPWATCH\033[0m" EOL EOL);
    out("      \033[1;36m%s\033[0m" EOL EOL, t);
    out("  Status: %-8s Laps: %d" EOL EOL, names[sw_get_state()], n);
    out("  Recent laps:" EOL);
    if (n == 0) out("    (none yet - press l while running)" EOL);
    for (int i = n > 3 ? n - 3 : 0; i < n; i++) {
        char line[160];
        lap_line(i, line, sizeof line);
        out("    %s" EOL, line);
    }
    out(EOL "  [s] start/pause/resume  [l] lap  [u] undo lap  [r] reset" EOL);
    out("  [v] lap list  [a] alarms  [b] budgets  [f] files  [h] help  [q] quit" EOL);
}

static void draw_laps(void) {
    int n = lap_count();
    int page = term_rows() - 9;
    if (page < 3) page = 3;
    int max_scroll = n > page ? n - page : 0;
    if (ui.scroll > max_scroll) ui.scroll = max_scroll;
    if (ui.scroll < 0) ui.scroll = 0;

    out("  \033[1mLAP LIST\033[0m  (%d laps)" EOL EOL, n);
    lap_stats_t st;
    if (lap_stats(&st) == 0) {
        char f[32], s[32], a[32];
        fmt_ms(st.fastest_ms, f, sizeof f);
        fmt_ms(st.slowest_ms, s, sizeof s);
        fmt_ms(st.average_ms, a, sizeof a);
        out("  fastest #%d %s   slowest #%d %s   average %s" EOL EOL,
            st.fastest_no, f, st.slowest_no, s, a);
    } else {
        out("  no laps recorded" EOL EOL);
    }
    for (int i = ui.scroll; i < n && i < ui.scroll + page; i++) {
        char line[160];
        lap_line(i, line, sizeof line);
        out("  %s" EOL, line);
    }
    out(EOL "  up/down or j/k: scroll    any other key: back" EOL);
}

static void draw_help(void) {
    out("  \033[1mHELP\033[0m" EOL EOL);
    out("  Main screen" EOL);
    out("    s  start / pause / resume       l  record a lap" EOL);
    out("    u  undo last lap                r  reset (asks first)" EOL);
    out("    v  lap list with statistics     q  quit (asks first)" EOL);
    out("    a  alarms   b  time budgets   f  files   h  this help" EOL EOL);
    out("  Lists: up/down or j/k move, q or Esc go back" EOL);
    out("  Ctrl+C always exits cleanly and restores the terminal" EOL);
    out(EOL "  any key: back" EOL);
}

static void draw_alarms(void) {
    int n = alarm_count();
    if (ui.cursor >= n) ui.cursor = n > 0 ? n - 1 : 0;
    out("  \033[1mALARMS\033[0m  (%d)" EOL EOL, n);
    if (n == 0) out("    no alarms - press n to add one" EOL);
    for (int i = 0; i < n; i++) {
        struct Alarm a;
        if (alarm_get(i, &a) != 0) continue;
        out("  %c %2d   %02d:%02d:%02d   %s" EOL,
            i == ui.cursor ? '>' : ' ', a.id, a.hour, a.minute, a.second,
            a.enabled ? "ON" : "off");
    }
    out(EOL "  up/down: select  [n] new  [e] enable/disable  [d] delete  [q] back" EOL);
}

static void draw_budget(void) {
    int n = budget_count();
    const char *active = budget_active();
    if (ui.cursor >= n) ui.cursor = n > 0 ? n - 1 : 0;
    out("  \033[1mTIME BUDGETS\033[0m" EOL EOL);
    for (int i = 0; i < n; i++) {
        Budget b;
        if (budget_get(i, &b) != 0) continue;
        int limit = b.limit_min * 60;
        int pct = limit > 0 ? (int)((long)b.used_sec * 100 / limit) : 0;
        char used[32], note[48] = "ok";
        fmt_ms((long)b.used_sec * 1000, used, sizeof used);
        if (b.exceeded) {
            snprintf(note, sizeof note, "EXCEEDED (+%d s over)", b.used_sec - limit);
        } else if (b.warned) {
            snprintf(note, sizeof note, "WARNING (%d%% reached)", b.warn_pct);
        }
        const char *run = "";
        if (active && strcmp(active, b.name) == 0) run = budget_is_running() ? " <running>" : " <paused>";
        out("  %c %-9s %.8s / %3d min  %3d%%  %s%s" EOL,
            i == ui.cursor ? '>' : ' ', b.name, used, b.limit_min, pct, note, run);
    }
    out(EOL "  up/down: select  [s] start  [p] pause  [x] stop  [q] back" EOL);
}

static void draw_files(void) {
    out("  \033[1mFILES\033[0m" EOL EOL);
    out("    1  save session" EOL);
    out("    2  load session" EOL);
    out("    3  export laps to CSV" EOL);
    out(EOL "  [q] back" EOL);
}

static void draw_body(ui_mode_t m) {
    switch (m) {
    case MODE_MAIN:   draw_main();   break;
    case MODE_LAPS:   draw_laps();   break;
    case MODE_HELP:   draw_help();   break;
    case MODE_ALARMS: draw_alarms(); break;
    case MODE_BUDGET: draw_budget(); break;
    case MODE_FILES:  draw_files();  break;
    default: break;
    }
}

static void draw(void) {
    frame_begin();
    if (ui.alert_on)
        out("\033[1;97;41m ALERT: %s  (press any key) \033[0m" EOL, ui.alert);

    if (ui.mode == MODE_CONFIRM) {
        draw_body(ui.return_mode);
        out(EOL "  \033[1;33m%s (y/n)\033[0m" EOL, ui.confirm_text);
    } else if (ui.mode == MODE_INPUT) {
        draw_body(ui.return_mode);
        out(EOL "  %s%s_" EOL, ui.input_prompt, ui.input_buf);
    } else {
        draw_body(ui.mode);
    }

    if (ui.status[0] && mono_now() < ui.status_until) out(EOL "  \033[33m%s\033[0m" EOL, ui.status);
    frame_end();
}

/* ------------------------------------------------------------------ */
/* 5. input                                                            */
/* ------------------------------------------------------------------ */

/* Waits up to ms for stdin. A signal (EINTR) or a timeout both return 0. */
static int stdin_ready(long ms) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    int r = pselect(STDIN_FILENO + 1, &fds, NULL, NULL, &ts, signals_wait_mask());
    return r > 0;
}

static int read_byte(void) {
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    return n == 1 ? c : -1;
}

/* Returns a plain character, or KEY_UP/DOWN/..., KEY_ESC, KEY_NONE, KEY_EOF. */
static int read_key(void) {
    int c = read_byte();
    if (c < 0) return KEY_EOF;
    if (c != KEY_ESC) return c;

    /* ESC: a lone Escape, or the start of an escape sequence such as ESC [ A */
    if (!stdin_ready(25)) return KEY_ESC;
    int c1 = read_byte();
    if (c1 != '[' && c1 != 'O') return KEY_ESC;
    if (!stdin_ready(25)) return KEY_ESC;
    int c2 = read_byte();
    switch (c2) {
    case 'A': return KEY_UP;
    case 'B': return KEY_DOWN;
    case 'C': return KEY_RIGHT;
    case 'D': return KEY_LEFT;
    default:
        /* longer sequence (e.g. ESC [ 3 ~): swallow the rest so no stray bytes remain */
        while (c2 >= 0 && !(c2 >= '@' && c2 <= '~') && stdin_ready(5)) c2 = read_byte();
        return KEY_NONE;
    }
}

PRINTF_LIKE(3, 4)
static void ask_confirm(confirm_act_t act, ui_mode_t back, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ui.confirm_text, sizeof ui.confirm_text, fmt, ap);
    va_end(ap);
    ui.confirm_act = act;
    ui.return_mode = back;
    ui.mode = MODE_CONFIRM;
}

static void begin_input(input_kind_t kind, ui_mode_t back, const char *prompt) {
    ui.input_kind = kind;
    ui.return_mode = back;
    ui.input_buf[0] = '\0';
    ui.input_len = 0;
    snprintf(ui.input_prompt, sizeof ui.input_prompt, "%s", prompt);
    ui.mode = MODE_INPUT;
}

static int valid_name(const char *s) {
    size_t n = strlen(s);
    if (n == 0 || n > NAME_LEN) return 0;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)s[i]) && s[i] != '_') return 0;
    return 1;
}

/* "HH:MM:SS" or "HH:MM"; returns 0 if valid */
static int parse_hms(const char *s, int *h, int *m, int *sec) {
    int a, b, c = 0;
    char extra;
    size_t len = strlen(s);
    if (len == 0 || s[len - 1] == ':') return -1;
    int n = sscanf(s, "%d:%d:%d%c", &a, &b, &c, &extra);
    if (n != 2 && n != 3) return -1;
    if (a < 0 || a > 23 || b < 0 || b > 59 || c < 0 || c > 59) return -1;
    *h = a; *m = b; *sec = c;
    return 0;
}

static void do_save(const char *name) {
    if (save_session(name) == 0) set_status("session '%s' saved", name);
    else                         set_status("could not save session '%s'", name);
}
static void do_csv(const char *name) {
    if (export_laps_csv(name) == 0) set_status("laps exported to data/%s_laps.csv", name);
    else                            set_status("could not export '%s'", name);
}

static void run_confirmed(void) {
    switch (ui.confirm_act) {
    case ACT_RESET:
        stopwatch_reset();
        set_status("stopwatch reset");
        break;
    case ACT_QUIT:
        ui.quit = 1;
        break;
    case ACT_DELETE_ALARM:
        if (delete_alarm(ui.pending_id) == 0) set_status("alarm deleted");
        else                                  set_status("could not delete alarm");
        break;
    case ACT_SAVE_OVERWRITE: do_save(ui.pending_name); break;
    case ACT_CSV_OVERWRITE:  do_csv(ui.pending_name);  break;
    }
}

static void submit_input(void) {
    const char *text = ui.input_buf;
    switch (ui.input_kind) {
    case IN_ALARM_TIME: {
        int h, m, s;
        if (parse_hms(text, &h, &m, &s) != 0) { set_status("invalid time - use HH:MM:SS"); return; }
        int id = add_alarm(h, m, s);
        if (id < 0) set_status("alarm list is full");
        else        set_status("alarm %02d:%02d:%02d added (id %d)", h, m, s, id);
        break;
    }
    case IN_SAVE_NAME:
        if (!valid_name(text)) { set_status("name: letters, digits and _ only"); return; }
        if (session_file_exists(text)) {
            snprintf(ui.pending_name, sizeof ui.pending_name, "%s", text);
            ask_confirm(ACT_SAVE_OVERWRITE, ui.return_mode, "Session '%s' exists. Overwrite?", ui.pending_name);
            return;
        }
        do_save(text);
        break;
    case IN_LOAD_NAME:
        if (!valid_name(text)) { set_status("name: letters, digits and _ only"); return; }
        if (!session_file_exists(text)) { set_status("no session named '%s'", text); break; }
        if (load_session(text) == 0) set_status("session '%s' loaded", text);
        else                         set_status("could not load '%s'", text);
        break;
    case IN_CSV_NAME:
        if (!valid_name(text)) { set_status("name: letters, digits and _ only"); return; }
        if (csv_file_exists(text)) {
            snprintf(ui.pending_name, sizeof ui.pending_name, "%s", text);
            ask_confirm(ACT_CSV_OVERWRITE, ui.return_mode, "File '%s_laps.csv' exists. Overwrite?", ui.pending_name);
            return;
        }
        do_csv(text);
        break;
    }
    ui.mode = ui.return_mode;
}

/* ---- one key handler per screen ---- */

static void handle_main(int key) {
    switch (key) {
    case 's':
        if (sw_get_state() == SW_READY)        stopwatch_start();
        else if (sw_get_state() == SW_RUNNING) stopwatch_pause();
        else                                   stopwatch_resume();
        break;
    case 'l':
        if (lap_add() != 0) set_status("laps need a running stopwatch");
        break;
    case 'u':
        if (lap_undo() != 0) set_status("no lap to undo");
        break;
    case 'r':
        if (sw_get_state() == SW_READY) set_status("already at zero");
        else ask_confirm(ACT_RESET, MODE_MAIN, "Reset the stopwatch and clear all laps?");
        break;
    case 'v': ui.scroll = 0; ui.mode = MODE_LAPS;   break;
    case 'a': ui.cursor = 0; ui.mode = MODE_ALARMS; break;
    case 'b': ui.cursor = 0; ui.mode = MODE_BUDGET; break;
    case 'f': ui.mode = MODE_FILES; break;
    case 'h': case '?': ui.mode = MODE_HELP; break;
    case 'q': ask_confirm(ACT_QUIT, MODE_MAIN, "Quit the stopwatch?"); break;
    default:
        if (key >= 32 && key < 127) set_status("unknown key '%c' - press h for help", key);
        break;
    }
}

static void handle_laps(int key) {
    switch (key) {
    case KEY_UP:   case 'k': ui.scroll--; break;
    case KEY_DOWN: case 'j': ui.scroll++; break;
    default: ui.mode = MODE_MAIN; break;
    }
}

static void handle_alarms(int key) {
    int n = alarm_count();
    struct Alarm a;
    switch (key) {
    case KEY_UP:   case 'k': if (ui.cursor > 0) ui.cursor--; break;
    case KEY_DOWN: case 'j': if (ui.cursor < n - 1) ui.cursor++; break;
    case 'n':
        begin_input(IN_ALARM_TIME, MODE_ALARMS, "New alarm time (HH:MM:SS): ");
        break;
    case 'e':
        if (n > 0 && alarm_get(ui.cursor, &a) == 0) {
            if (a.enabled) disable_alarm(a.id); else enable_alarm(a.id);
        }
        break;
    case 'd':
        if (n > 0 && alarm_get(ui.cursor, &a) == 0) {
            ui.pending_id = a.id;
            ask_confirm(ACT_DELETE_ALARM, MODE_ALARMS, "Delete alarm %02d:%02d:%02d?", a.hour, a.minute, a.second);
        }
        break;
    case 'q': case KEY_ESC: ui.mode = MODE_MAIN; break;
    default: break;
    }
}

static void handle_budget(int key) {
    int n = budget_count();
    Budget b;
    switch (key) {
    case KEY_UP:   case 'k': if (ui.cursor > 0) ui.cursor--; break;
    case KEY_DOWN: case 'j': if (ui.cursor < n - 1) ui.cursor++; break;
    case 's':
        if (budget_get(ui.cursor, &b) == 0) {
            if (budget_start(b.name) == 0) set_status("%s budget started", b.name);
            else                           set_status("could not start %s", b.name);
        }
        break;
    case 'p': budget_pause(); set_status("budget paused"); break;
    case 'x': budget_stop();  set_status("budget stopped"); break;
    case 'q': case KEY_ESC: ui.mode = MODE_MAIN; break;
    default: break;
    }
}

static void handle_files(int key) {
    switch (key) {
    case '1': begin_input(IN_SAVE_NAME, MODE_FILES, "Save session as: "); break;
    case '2': begin_input(IN_LOAD_NAME, MODE_FILES, "Load session: ");    break;
    case '3': begin_input(IN_CSV_NAME,  MODE_FILES, "Export CSV as: ");   break;
    case 'q': case KEY_ESC: ui.mode = MODE_MAIN; break;
    default: break;
    }
}

static void handle_confirm(int key) {
    if (key == 'y') run_confirmed();
    else            set_status("cancelled");
    ui.mode = ui.return_mode;
}

static int input_char_ok(int key) {
    if (ui.input_kind == IN_ALARM_TIME) return isdigit(key) || key == ':';
    return isalnum(key) || key == '_';
}

static void handle_input(int key) {
    size_t max = ui.input_kind == IN_ALARM_TIME ? 8 : NAME_LEN;
    if (key == KEY_ESC) {
        set_status("cancelled");
        ui.mode = ui.return_mode;
    } else if (key == '\n' || key == '\r') {
        submit_input();
    } else if (key == 127 || key == 8) {
        if (ui.input_len > 0) ui.input_buf[--ui.input_len] = '\0';
    } else if (key >= 32 && key < 127 && ui.input_len < max && input_char_ok(key)) {
        ui.input_buf[ui.input_len++] = (char)key;
        ui.input_buf[ui.input_len] = '\0';
    }
}

static void dispatch(int key) {
    if (ui.alert_on) { ui.alert_on = 0; return; }       /* first key only dismisses the alert */
    if (ui.mode != MODE_INPUT && key >= 'A' && key <= 'Z') key += 'a' - 'A';
    switch (ui.mode) {
    case MODE_MAIN:    handle_main(key);    break;
    case MODE_LAPS:    handle_laps(key);    break;
    case MODE_HELP:    ui.mode = MODE_MAIN; break;
    case MODE_ALARMS:  handle_alarms(key);  break;
    case MODE_BUDGET:  handle_budget(key);  break;
    case MODE_FILES:   handle_files(key);   break;
    case MODE_CONFIRM: handle_confirm(key); break;
    case MODE_INPUT:   handle_input(key);   break;
    }
}

/* ------------------------------------------------------------------ */
/* 6. public functions                                                 */
/* ------------------------------------------------------------------ */

void ui_alert(const char *msg) {
    snprintf(ui.alert, sizeof ui.alert, "%s", msg);
    ui.alert_on = 1;
    fputc('\a', stdout);
    fflush(stdout);
}

void ui_run(void) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fputs("ui: needs an interactive terminal\n", stderr);
        return;
    }
    memset(&ui, 0, sizeof ui);
    ui.mode = MODE_MAIN;
    atexit(ui_cleanup);
    terminal_enter();

    while (!ui.quit && !signals_shutdown_requested()) {
        app_poll_events();
        draw();
        if (!stdin_ready(REFRESH_MS)) continue;     /* timeout or signal: redraw / re-check flag */
        int key = read_key();
        if (key == KEY_EOF) break;
        if (key == KEY_NONE) continue;
        dispatch(key);
    }
    ui_cleanup();
    puts("stopwatch closed");
}
