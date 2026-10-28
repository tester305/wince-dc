/*
 * Microsoft Windows CE 2.00 shell for the Sega Dreamcast (KallistiOS)
 *
 * A faithful recreation of the Windows CE 2.0 Handheld PC desktop:
 *   - "Microsoft Windows CE" boot splash
 *   - True overlapping window manager: navy/gray title bars with
 *     maximize + close buttons, window move, z-order cycling
 *   - Cascading Start menu: Programs > Accessories / Games,
 *     Settings > Control Panel, Run..., Shut Down...
 *   - Taskbar with window buttons, Input-Panel tray button and clock
 *   - Programs: My Computer, Command Prompt, Notepad, Calculator,
 *     Minesweeper, Paint, Clock (analog + calendar), Control Panel,
 *     Help, Run dialog, Shut Down dialog, About
 *   - Virtual "Program Files" folder: calc.exe, notepad.exe, cmd.exe,
 *     mines.exe, paint.exe, clock.exe, control.exe, explorer.exe,
 *     help.exe - opening any of them launches the program
 *   - On-screen Input Panel (SIP) for gamepad text entry
 *   - Auto-writable save: /ram, then /pc (SD/IDE), then /vmu/a0
 *   - AICA startup/click/error sounds (set USE_SOUND 0 if your KOS
 *     lacks <kos/snd_sfx.h>)
 *   - "Flying windows" screensaver after 60 s idle
 *
 * The shell can NOT run retail Windows CE binaries: CE was only ever
 * shipped embedded inside licensed Dreamcast games and Microsoft's SH4
 * BSP was never public. Everything here is native KallistiOS code.
 *
 * INPUT
 *   Keyboard (Maple keyboard):
 *       Ctrl+Esc  Start menu        Ctrl+Tab / Alt+Tab  next window
 *       Ctrl+W    close window      Ctrl+M    move window
 *       Ctrl+G    maximize/restore  Ctrl+S    save (Notepad/Paint)
 *       F1        Help              Esc       back / cancel
 *   Controller (no keyboard needed):
 *       D-pad / analog  move        A  Enter / OK
 *       B  Esc / back / flag          Y  Backspace / next tool
 *       X  maximize (Shift on Input Panel)
 *       Start  Start menu             L / R  previous / next window
 *       A+B+X+Y+Start together  exit to the BIOS menu
 *   The Input Panel appears by itself in Command Prompt, Notepad,
 *   Calculator and Run whenever no keyboard is plugged in (or via
 *   Start > Settings > Input Panel, or the tray button).
 *
 * Text uses the Dreamcast BIOS font (12x24), so it needs a real console
 * or an emulator that provides a BIOS image.
 */

#include <kos.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define USE_SOUND 1
#if USE_SOUND
#include <dc/spu.h>
#include <dc/sound/sfxmgr.h>
#endif

/* ------------------------------------------------------------------ */
/* constants                                                           */
/* ------------------------------------------------------------------ */

#define SW          640
#define SH          480
#define CHW         12
#define CHH         24
#define TITLE_H     28
#define TASKBAR_H   32
#define WORK_BOTTOM (SH - TASKBAR_H)

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define C_BLACK RGB(0, 0, 0)
#define C_WHITE RGB(255, 255, 255)
#define C_GRAY  RGB(192, 192, 192)
#define C_LIGHT RGB(224, 224, 224)
#define C_DGRAY RGB(128, 128, 128)
#define C_NAVY  RGB(0, 0, 128)
#define C_TEAL  RGB(0, 128, 128)
#define C_RED   RGB(255, 0, 0)
#define C_GREEN RGB(0, 160, 0)
#define C_YELL  RGB(255, 220, 0)

/* key symbols: printable ASCII is passed as-is, specials start at 256 */
enum {
    K_UP = 256, K_DOWN, K_LEFT, K_RIGHT, K_ENTER, K_BKSP, K_ESC, K_TAB, K_DEL,
    K_HOME, K_END, K_PGUP, K_PGDN,
    K_START, K_NEXT, K_PREV, K_CLOSE,
    K_MOVE, K_MAX, K_SAVE, K_HELP
};

enum {
    APP_EXPL, APP_CMD, APP_NOTE, APP_HELP, APP_CALC, APP_MINE,
    APP_PAINT, APP_CLOCK, APP_CTRL, APP_RUN, APP_SHUT, APP_ABOUT,
    APP_COUNT
};

static const char *app_title[APP_COUNT] = {
    "My Computer", "Command Prompt", "Notepad", "Help",
    "Calculator", "Minesweeper", "Paint", "Clock",
    "Control Panel", "Run", "Shut Down Windows", "About Windows CE"
};
static const char *app_short[APP_COUNT] = {
    "Computer", "Command", "Notepad", "Help",
    "Calc", "Mines", "Paint", "Clock",
    "Control", "Run", "Shut Down", "About"
};

#define NP_MAX    4096
#define NP_COLS   52
#define CMD_COLS  52
#define CMD_MAXL  200
#define EX_MAX    256
#define HIST_N    16

/* ------------------------------------------------------------------ */
/* forward declarations                                                */
/* ------------------------------------------------------------------ */

static void push_ev(int e);
static int  pop_ev(void);
static int  is_text_app(void);
static int  vk_visible(void);

static void clip_all(void);
static void clip_set(int x, int y, int w, int h);
static void fill(int x, int y, int w, int h, uint16_t c);
static void frame3d(int x, int y, int w, int h, int sunk);
static void box3d(int x, int y, int w, int h, int sunk, uint16_t face);
static void put_glyph(int x, int y, int c, uint16_t col);
static void txtn(int x, int y, const char *s, int n, uint16_t col);
static void txt(int x, int y, const char *s, uint16_t col);
static void txt2x(int x, int y, const char *s, uint16_t col);
static void txtb(int x, int y, const char *s, uint16_t col);
static int  text_w(const char *s);
static void wline(int x0, int y0, int x1, int y1, uint16_t c);
static void wdisc(int cx, int cy, int r, uint16_t c);
static void glyph_init(void);

static void path_resolve(const char *base, const char *in, char *out, size_t outsz);
static int  ent_is_dir(const dirent_t *e);
static int  read_file(const char *path, char *buf, int max);
static int  buf_write(const char *path, const char *buf, int len);
static int  save_auto(const char *name, const char *buf, int len, char *out, size_t osz);
static void fmt_size(char *out, size_t n, int sz);

static void win_rect(int a, int *rx, int *ry, int *rw, int *rh);
static void win_client(int a, int *rx, int *ry, int *rw, int *rh);
static int  active_app(void);
static int  top_dialog(void);
static int  is_dialog(int a);
static void open_app(int a);
static void close_app(int a);
static void switch_window(int dir);
static void begin_move(void);
static void move_key(int k);
static void toggle_max(void);
static const char *title_str(int a);

static int  np_layout(void);
static int  np_load(const char *path);
static void np_new(void);
static void note_key(int k);
static void note_save(void);
static void draw_note(int x, int y, int w, int h);

static int  ex_load(const char *path);
static void ex_up(void);
static void ex_open_sel(void);
static void expl_key(int k);
static void draw_expl(int x, int y, int w, int h);
static void make_program_files(void);

static void cmd_push(const char *s);
static void cmd_print(const char *s);
static void cmd_printf(const char *fmt, ...);
static const char *prompt_str(void);
static void cmd_dir(const char *arg);
static void cmd_exec(char *line);
static void cmd_submit(void);
static void cmd_runline(const char *s);
static void cmd_key(int k);
static void draw_cmd(int x, int y, int w, int h);
static int  match_prog(const char *s);
static void run_command(const char *s);

static void c_press(int key);
static void calc_key(int k);
static void draw_calc(int x, int y, int w, int h);

static void ms_reset(void);
static void mine_key(int k);
static void draw_mine(int x, int y, int w, int h);

static void paint_key(int k);
static void paint_reset(void);
static void paint_save(void);
static void draw_paint(int x, int y, int w, int h);

static void clock_key(int k);
static void draw_clock(int x, int y, int w, int h);

static void ctrl_key(int k);
static void draw_ctrl(int x, int y, int w, int h);
static void about_key(int k);
static void draw_about(int x, int y, int w, int h);
static void run_key(int k);
static void draw_run(int x, int y, int w, int h);
static void shut_key(int k);
static void draw_shut(int x, int y, int w, int h);

static void help_key(int k);
static void draw_help(int x, int y, int w, int h);

static void vk_move(int dx, int dy);
static void vk_press(void);
static void draw_vkb(void);

static void menu_key(int k);
static void menu_close_all(void);
static void draw_menu(void);

static void desk_key(int k);
static void draw_desktop(void);
static void draw_icon(int app, int x, int y);
static void draw_taskbar(void);
static void draw_window(int a);
static void draw_all(void);
static void dispatch(int k);

static void kbd_press(int k, int shift, int ctrl, int alt);
static void poll_kbd(void);
static void poll_pad(const void *vin);
static void handle_pad(const void *vin);

static void snd_init(void);
static void snd_click(void);
static void snd_err(void);
static void snd_start(void);

static void splash(void);
static void ss_reset(void);
static void draw_saver(void);
static void shell_init(void);
static void shell_frame(void);

/* ------------------------------------------------------------------ */
/* global state                                                        */
/* ------------------------------------------------------------------ */

static uint16_t fb[SW * SH] __attribute__((aligned(32)));

static int menu_open, m_lvl, m_id[3], m_sel[3];
static int desk_sel;
static int has_kbd, vkb_user = -1;    /* vkb_user: -1 auto, 0 off, 1 on */
static int caps;
static int vk_row, vk_col, vk_shift;
static int blink, dirty = 1;
static int powered_down;              /* "Windows CE has shut down" screen */
static unsigned frame_no, last_input;

#if USE_SOUND
static int snd_enabled = 1;
#endif

/* declared here, defined in their sections below (tentative defs) */
static char np_msg[48];
static unsigned np_msg_t;
static char paint_msg[48];
static unsigned paint_msg_t;
static char run_in[64];
static int run_len;
static int shut_sel;

/* desktop theme (Control Panel > Display) */
static uint16_t col_desktop = C_TEAL;
static const uint16_t theme_col[4] = {
    RGB(0, 128, 128), RGB(58, 110, 165), RGB(128, 128, 0), RGB(192, 192, 192)
};
static const char *theme_name[4] = { "Teal", "Windows Blue", "Olive", "Silver" };

/* event queue */
#define EVQ 64
static int evq[EVQ], ev_h, ev_t;

static void push_ev(int e)
{
    int n = (ev_t + 1) % EVQ;
    if (n == ev_h) return;
    evq[ev_t] = e;
    ev_t = n;
    dirty = 1;
}

static int pop_ev(void)
{
    int e;
    if (ev_h == ev_t) return 0;
    e = evq[ev_h];
    ev_h = (ev_h + 1) % EVQ;
    return e;
}

static int is_text_app(void)
{
    int a = active_app();
    return a == APP_CMD || a == APP_NOTE || a == APP_CALC || a == APP_RUN;
}

static int vk_visible(void)
{
    int want = (vkb_user < 0) ? !has_kbd : vkb_user;
    return is_text_app() && want;
}

/* ------------------------------------------------------------------ */
/* drawing primitives (all clipped)                                    */
/* ------------------------------------------------------------------ */

static int cl = 0, ct = 0, cr = SW, cb = SH;

static void clip_all(void) { cl = 0; ct = 0; cr = SW; cb = SH; }

static void clip_set(int x, int y, int w, int h)
{
    cl = x; ct = y; cr = x + w; cb = y + h;
    if (cl < 0) cl = 0;
    if (ct < 0) ct = 0;
    if (cr > SW) cr = SW;
    if (cb > SH) cb = SH;
}

static uint16_t glyph[95][CHH];       /* 12 bits per row, bit 11 = left */

static void glyph_init(void)
{
    static uint16_t tmp[24 * 24];
    char s[2] = { 0, 0 };
    int c, r, x;

    for (c = 32; c < 127; c++) {
        memset(tmp, 0, sizeof(tmp));
        s[0] = (char)c;
        bfont_draw_str(tmp, 24, 0, s);
        for (r = 0; r < CHH; r++) {
            uint16_t bits = 0;
            for (x = 0; x < CHW; x++)
                if (tmp[r * 24 + x]) bits |= (uint16_t)(0x800 >> x);
            glyph[c - 32][r] = bits;
        }
    }
}

static void fill(int x, int y, int w, int h, uint16_t c)
{
    int x2 = x + w, y2 = y + h, i, j;
    if (x < cl) x = cl;
    if (y < ct) y = ct;
    if (x2 > cr) x2 = cr;
    if (y2 > cb) y2 = cb;
    if (x >= x2 || y >= y2) return;
    for (j = y; j < y2; j++) {
        uint16_t *p = fb + j * SW + x;
        for (i = x; i < x2; i++) *p++ = c;
    }
}

static void frame3d(int x, int y, int w, int h, int sunk)
{
    uint16_t tl = sunk ? C_DGRAY : C_WHITE;
    uint16_t br = sunk ? C_WHITE : C_DGRAY;
    fill(x, y, w, 2, tl);
    fill(x, y, 2, h, tl);
    fill(x, y + h - 2, w, 2, br);
    fill(x + w - 2, y, 2, h, br);
}

static void box3d(int x, int y, int w, int h, int sunk, uint16_t face)
{
    fill(x, y, w, h, face);
    frame3d(x, y, w, h, sunk);
}

static void put_glyph(int x, int y, int c, uint16_t col)
{
    const uint16_t *g;
    int r, i;
    if (c < 32 || c > 126) c = '?';
    if (c == ' ') return;
    g = glyph[c - 32];
    for (r = 0; r < CHH; r++) {
        int yy = y + r;
        uint16_t bits = g[r];
        if (yy < ct || yy >= cb || !bits) continue;
        for (i = 0; i < CHW; i++) {
            int xx = x + i;
            if ((bits & (0x800 >> i)) && xx >= cl && xx < cr) fb[yy * SW + xx] = col;
        }
    }
}

static void txtn(int x, int y, const char *s, int n, uint16_t col)
{
    for (; *s && n > 0; s++, n--, x += CHW) put_glyph(x, y, (unsigned char)*s, col);
}

static void txt(int x, int y, const char *s, uint16_t col) { txtn(x, y, s, 1 << 20, col); }

/* double-size text for the boot splash */
static void txt2x(int x, int y, const char *s, uint16_t col)
{
    for (; *s; s++, x += CHW * 2) {
        int c = (unsigned char)*s, r, i;
        const uint16_t *g;
        if (c < 32 || c > 126 || c == ' ') continue;
        g = glyph[c - 32];
        for (r = 0; r < CHH; r++) {
            uint16_t bits = g[r];
            if (!bits) continue;
            for (i = 0; i < CHW; i++)
                if (bits & (0x800 >> i))
                    fill(x + i * 2, y + r * 2, 2, 2, col);
        }
    }
}

static void txtb(int x, int y, const char *s, uint16_t col)
{
    txt(x, y, s, col);
    txt(x + 1, y, s, col);
}

static int text_w(const char *s) { return (int)strlen(s) * CHW; }

static void wline(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    for (;;) {
        fill(x0, y0, 1, 1, c);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void wdisc(int cx, int cy, int r, uint16_t c)
{
    int dy;
    for (dy = -r; dy <= r; dy++) {
        int dx = (int)(sqrt((double)r * r - (double)dy * dy) + 0.5);
        fill(cx - dx, cy + dy, dx * 2 + 1, 1, c);
    }
}

/* ------------------------------------------------------------------ */
/* paths / files                                                       */
/* ------------------------------------------------------------------ */

static void path_resolve(const char *base, const char *in, char *out, size_t outsz)
{
    char tmp[256], *parts[32], *p;
    int cnt = 0, i;
    size_t len = 0;

    if (in[0] == '/' || in[0] == '\\') snprintf(tmp, sizeof(tmp), "%s", in);
    else snprintf(tmp, sizeof(tmp), "%s/%s", base, in);
    for (p = tmp; *p; p++) if (*p == '\\') *p = '/';

    p = tmp;
    while (*p) {
        char *tok;
        while (*p == '/') *p++ = 0;
        if (!*p) break;
        tok = p;
        while (*p && *p != '/') p++;
        if (*p) *p++ = 0;
        if (!strcmp(tok, ".")) continue;
        if (!strcmp(tok, "..")) { if (cnt > 0) cnt--; continue; }
        if (cnt < 32) parts[cnt++] = tok;
    }

    if (cnt == 0) { snprintf(out, outsz, "/"); return; }
    out[0] = 0;
    for (i = 0; i < cnt && len < outsz; i++)
        len += (size_t)snprintf(out + len, outsz - len, "/%s", parts[i]);
}

static int ent_is_dir(const dirent_t *e) { return (e->attr & O_DIR) || e->size < 0; }

static int read_file(const char *path, char *buf, int max)
{
    ssize_t n;
    file_t f = fs_open(path, O_RDONLY);
    if (f == FILEHND_INVALID) return -1;
    n = fs_read(f, buf, (size_t)max);
    fs_close(f);
    return (int)n;
}

static int buf_write(const char *path, const char *buf, int len)
{
    file_t f = fs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    ssize_t n;
    if (f == FILEHND_INVALID) return -1;
    n = fs_write(f, buf, (size_t)len);
    fs_close(f);
    return (n == len) ? 0 : -1;
}

/* try /name, then /ram, /pc, /vmu/a0; report where it landed */
static int save_auto(const char *name, const char *buf, int len, char *out, size_t osz)
{
    static const char *dirs[] = { "", "/ram/", "/pc/", "/vmu/a0/" };
    char p[160];
    size_t i;

    if (name[0] == '/') {
        if (buf_write(name, buf, len) == 0) { snprintf(out, osz, "%s", name); return 0; }
        return -1;
    }
    for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        snprintf(p, sizeof(p), "%s%s", dirs[i], name);
        if (buf_write(p, buf, len) == 0) { snprintf(out, osz, "%s", p); return 0; }
    }
    return -1;
}

static void fmt_size(char *out, size_t n, int sz)
{
    if (sz < 0) snprintf(out, n, "<DIR>");
    else if (sz < 10240) snprintf(out, n, "%d B", sz);
    else if (sz < 10 * 1024 * 1024) snprintf(out, n, "%d KB", sz / 1024);
    else snprintf(out, n, "%d MB", sz / (1024 * 1024));
}

/* ------------------------------------------------------------------ */
/* window manager                                                      */
/* ------------------------------------------------------------------ */

typedef struct { int open, max, x, y, w, h; } WinState;
static WinState ws[APP_COUNT];
static int zord[APP_COUNT], z_n;
static int moving, move_a;
static int casc;

static const struct { int w, h; } def_size[APP_COUNT] = {
    { 560, 340 },   /* My Computer  */
    { 600, 340 },   /* Command Prompt */
    { 600, 350 },   /* Notepad      */
    { 520, 340 },   /* Help         */
    { 340, 286 },   /* Calculator   */
    { 240, 292 },   /* Minesweeper  */
    { 470, 334 },   /* Paint        */
    { 420, 360 },   /* Clock        */
    { 420, 300 },   /* Control Panel */
    { 470, 150 },   /* Run          */
    { 430, 190 },   /* Shut Down    */
    { 470, 230 }    /* About        */
};

static int is_dialog(int a)
{
    return a == APP_RUN || a == APP_SHUT || a == APP_ABOUT;
}

static void win_rect(int a, int *rx, int *ry, int *rw, int *rh)
{
    if (ws[a].max) { *rx = 0; *ry = 0; *rw = SW; *rh = WORK_BOTTOM; }
    else { *rx = ws[a].x; *ry = ws[a].y; *rw = ws[a].w; *rh = ws[a].h; }
}

static void win_client(int a, int *rx, int *ry, int *rw, int *rh)
{
    int x, y, w, h;
    win_rect(a, &x, &y, &w, &h);
    *rx = x + 2; *ry = y + TITLE_H; *rw = w - 4; *rh = h - TITLE_H - 2;
}

static int active_app(void) { return z_n ? zord[0] : -1; }

static int top_dialog(void)
{
    return (z_n && is_dialog(zord[0])) ? zord[0] : -1;
}

static void z_remove(int a)
{
    int i;
    for (i = 0; i < z_n; i++)
        if (zord[i] == a) {
            memmove(&zord[i], &zord[i + 1], (size_t)(z_n - 1 - i) * sizeof(int));
            z_n--;
            return;
        }
}

static void z_pushfront(int a)
{
    z_remove(a);
    memmove(&zord[1], &zord[0], (size_t)z_n * sizeof(int));
    zord[0] = a;
    z_n++;
}

static void open_app(int a)
{
    if (!ws[a].open) {
        int n, w = def_size[a].w, h = def_size[a].h;
        ws[a].open = 1;
        ws[a].max = 0;
        ws[a].w = w;
        ws[a].h = h;
        if (is_dialog(a)) {
            ws[a].x = (SW - w) / 2;
            ws[a].y = (WORK_BOTTOM - h) / 2;
        } else {
            n = casc++ % 8;
            ws[a].x = 18 + n * 22;
            ws[a].y = 8 + n * 20;
            if (ws[a].x + w > SW - 4) ws[a].x = SW - 4 - w;
            if (ws[a].y + h > WORK_BOTTOM - 4) ws[a].y = WORK_BOTTOM - 4 - h;
            if (ws[a].x < 0) ws[a].x = 0;
            if (ws[a].y < 0) ws[a].y = 0;
        }
        if (a == APP_MINE) ms_reset();
        if (a == APP_PAINT) paint_reset();
        if (a == APP_RUN) { run_in[0] = 0; run_len = 0; }
        if (a == APP_SHUT) shut_sel = 0;
    }
    z_pushfront(a);
    menu_close_all();
    dirty = 1;
}

static void close_app(int a)
{
    z_remove(a);
    ws[a].open = 0;
    if (moving && move_a == a) moving = 0;
    dirty = 1;
}

static void switch_window(int dir)
{
    if (top_dialog() >= 0) return;
    if (z_n < 2) { dirty = 1; return; }
    if (dir > 0) {
        int f = zord[0];
        memmove(&zord[0], &zord[1], (size_t)(z_n - 1) * sizeof(int));
        zord[z_n - 1] = f;
    } else {
        int l = zord[z_n - 1];
        memmove(&zord[1], &zord[0], (size_t)(z_n - 1) * sizeof(int));
        zord[0] = l;
    }
    dirty = 1;
}

static void begin_move(void)
{
    int a = active_app();
    if (a < 0 || is_dialog(a) || ws[a].max) return;
    moving = 1;
    move_a = a;
    dirty = 1;
}

static void move_key(int k)
{
    WinState *w = &ws[move_a];
    switch (k) {
    case K_LEFT:  if (w->x > 0) w->x -= 4; break;
    case K_RIGHT: if (w->x + w->w < SW) w->x += 4; break;
    case K_UP:    if (w->y > 0) w->y -= 4; break;
    case K_DOWN:  if (w->y + w->h < WORK_BOTTOM) w->y += 4; break;
    default: moving = 0; break;      /* Enter, Esc, anything else ends move */
    }
    dirty = 1;
}

static void toggle_max(void)
{
    int a = active_app();
    if (a < 0 || is_dialog(a)) return;
    ws[a].max = !ws[a].max;
    dirty = 1;
}

static const char *title_str(int a)
{
    static char b[48];
    if (a == APP_NOTE && np_msg[0] && frame_no - np_msg_t < 180) {
        snprintf(b, sizeof(b), "Notepad - %s", np_msg);
        return b;
    }
    if (a == APP_PAINT && paint_msg[0] && frame_no - paint_msg_t < 180) {
        snprintf(b, sizeof(b), "Paint - %s", paint_msg);
        return b;
    }
    return app_title[a];
}

/* ------------------------------------------------------------------ */
/* Notepad                                                             */
/* ------------------------------------------------------------------ */

static char np[NP_MAX];
static int np_len, np_top, np_follow = 1;
static int np_ls[NP_MAX + 2], np_ll[NP_MAX + 2];
static char rbuf[NP_MAX];

static int np_layout(void)
{
    int n = 0, st = 0, col = 0, i;
    for (i = 0; i < np_len; i++) {
        if (np[i] == '\n') {
            np_ls[n] = st; np_ll[n] = i - st; n++;
            st = i + 1; col = 0;
        } else {
            if (col == NP_COLS) {
                np_ls[n] = st; np_ll[n] = i - st; n++;
                st = i; col = 0;
            }
            col++;
        }
    }
    np_ls[n] = st; np_ll[n] = np_len - st; n++;
    return n;
}

/* 0 ok, -1 cannot read, -2 binary file */
static int np_load(const char *path)
{
    int n, i, j = 0;
    n = read_file(path, rbuf, NP_MAX - 1);
    if (n < 0) return -1;
    for (i = 0; i < n; i++)
        if ((unsigned char)rbuf[i] < 9) return -2;
    for (i = 0; i < n; i++) {
        if (rbuf[i] == '\r') continue;
        np[j++] = (rbuf[i] == '\t') ? ' ' : rbuf[i];
    }
    np_len = j;
    np_top = 0;
    np_follow = 0;
    return 0;
}

static void np_new(void)
{
    np_len = 0;
    np_top = 0;
    np_follow = 1;
    np_msg[0] = 0;
}

static void note_save(void)
{
    char path[64];
    if (save_auto("untitled.txt", np, np_len, path, sizeof(path)) == 0) {
        snprintf(np_msg, sizeof(np_msg), "%s", path);
        np_msg_t = frame_no;
    } else {
        snprintf(np_msg, sizeof(np_msg), "cannot save");
        np_msg_t = frame_no;
    }
    dirty = 1;
}

static void note_key(int k)
{
    if (k >= 32 && k < 127) {
        if (np_len < NP_MAX - 1) np[np_len++] = (char)k;
        np_follow = 1;
    } else if (k == K_ENTER) {
        if (np_len < NP_MAX - 1) np[np_len++] = '\n';
        np_follow = 1;
    } else if (k == K_TAB) {
        if (np_len < NP_MAX - 2) { np[np_len++] = ' '; np[np_len++] = ' '; }
        np_follow = 1;
    } else if (k == K_BKSP) {
        if (np_len > 0) np_len--;
        np_follow = 1;
    } else if (k == K_UP)   { np_follow = 0; np_top--; }
    else if (k == K_DOWN)   { np_follow = 0; np_top++; }
    else if (k == K_PGUP)   { np_follow = 0; np_top -= 8; }
    else if (k == K_PGDN)   { np_follow = 0; np_top += 8; }
    else if (k == K_HOME)   { np_follow = 0; np_top = 0; }
    else if (k == K_END)    { np_follow = 1; }
    if (np_top < 0) np_top = 0;
}

static void draw_note(int x, int y, int w, int h)
{
    int rows = (h - 8) / CHH, n, i, yy, maxtop;

    fill(x, y, w, h, C_WHITE);
    n = np_layout();
    maxtop = n - rows;
    if (maxtop < 0) maxtop = 0;
    if (np_follow) np_top = maxtop;
    if (np_top > maxtop) np_top = maxtop;
    if (np_top < 0) np_top = 0;

    yy = y + 4;
    for (i = np_top; i < n && i < np_top + rows; i++, yy += CHH)
        txtn(x + 8, yy, np + np_ls[i], np_ll[i], C_BLACK);

    if (blink && np_follow) {
        int row = n - 1 - np_top, col = np_ll[n - 1];
        if (col > NP_COLS) col = NP_COLS;
        if (row >= 0 && row < rows)
            fill(x + 8 + col * CHW, y + 4 + row * CHH + 20, 10, 3, C_BLACK);
    }
}

/* ------------------------------------------------------------------ */
/* program table: virtual "runnable exes"                              */
/* ------------------------------------------------------------------ */

static const struct { const char *n; int a; } prog_map[] = {
    { "calc",        APP_CALC  }, { "calculator", APP_CALC },
    { "notepad",     APP_NOTE  },
    { "cmd",         APP_CMD   }, { "command",    APP_CMD },
    { "explorer",    APP_EXPL  }, { "mycomputer", APP_EXPL },
    { "mines",       APP_MINE  }, { "winmine",    APP_MINE },
    { "minesweeper", APP_MINE  },
    { "paint",       APP_PAINT }, { "pbrush",     APP_PAINT },
    { "clock",       APP_CLOCK },
    { "control",     APP_CTRL  },
    { "help",        APP_HELP  },
    { NULL, 0 }
};

static int match_prog(const char *s)
{
    char b[32];
    char *e;
    size_t i;

    while (*s == ' ') s++;
    snprintf(b, sizeof(b), "%s", s);
    for (i = 0; b[i]; i++) b[i] = (char)tolower((unsigned char)b[i]);
    e = strstr(b, ".exe");
    if (e && e[4] == 0) *e = 0;
    for (i = (int)strlen(b); i > 0 && b[i - 1] == ' '; i--) b[i - 1] = 0;
    for (i = 0; prog_map[i].n; i++)
        if (!strcmp(prog_map[i].n, b)) return prog_map[i].a;
    return -1;
}

static void run_command(const char *s)
{
    int a = match_prog(s);
    if (a >= 0) {
        open_app(a);
    } else {
        /* everything else goes to the Command Prompt */
        open_app(APP_CMD);
        cmd_runline(s);
    }
}

/* create /ram/Program Files with the launcher "exes" */
static void make_program_files(void)
{
    static const char *exes[] = {
        "calc.exe", "notepad.exe", "cmd.exe", "mines.exe", "paint.exe",
        "clock.exe", "control.exe", "explorer.exe", "help.exe", NULL
    };
    const char *sub = "/ram/Program Files";
    file_t d;
    int i;

    d = fs_open(sub, O_RDONLY | O_DIR);
    if (d == FILEHND_INVALID) {
        if (fs_mkdir(sub) < 0) return;
    } else {
        fs_close(d);
    }
    for (i = 0; exes[i]; i++) {
        char p[64];
        file_t f;
        snprintf(p, sizeof(p), "%s/%s", sub, exes[i]);
        f = fs_open(p, O_WRONLY | O_CREAT | O_TRUNC);
        if (f != FILEHND_INVALID) fs_close(f);
    }
}

/* ------------------------------------------------------------------ */
/* Explorer (My Computer)                                              */
/* ------------------------------------------------------------------ */

typedef struct { char name[48]; int isdir; int size; } Ent;
static Ent ex_ent[EX_MAX];
static int ex_n, ex_sel, ex_top;
static char ex_path[128] = "/";
static char ex_msg[64];

static int ex_add(const dirent_t *e, int want_dir)
{
    int isd = ent_is_dir(e);
    if (isd != want_dir || ex_n >= EX_MAX) return 0;
    snprintf(ex_ent[ex_n].name, sizeof(ex_ent[0].name), "%s", e->name);
    ex_ent[ex_n].isdir = isd;
    ex_ent[ex_n].size = isd ? -1 : e->size;
    ex_n++;
    return 1;
}

static int ex_load(const char *path)
{
    file_t d = fs_open(path, O_RDONLY | O_DIR);
    dirent_t *e;
    if (d == FILEHND_INVALID) return -1;

    ex_n = 0;
    if (strcmp(path, "/") != 0) {
        snprintf(ex_ent[0].name, sizeof(ex_ent[0].name), "..");
        ex_ent[0].isdir = 1;
        ex_ent[0].size = -1;
        ex_n = 1;
    }
    while ((e = fs_readdir(d)) != NULL) ex_add(e, 1);
    fs_close(d);

    d = fs_open(path, O_RDONLY | O_DIR);
    if (d != FILEHND_INVALID) {
        while ((e = fs_readdir(d)) != NULL) ex_add(e, 0);
        fs_close(d);
    }

    snprintf(ex_path, sizeof(ex_path), "%s", path);
    ex_sel = ex_top = 0;
    ex_msg[0] = 0;
    dirty = 1;
    return 0;
}

static void ex_up(void)
{
    char p[128];
    path_resolve(ex_path, "..", p, sizeof(p));
    if (ex_load(p) < 0) snprintf(ex_msg, sizeof(ex_msg), "Cannot open folder");
}

static void ex_open_sel(void)
{
    char name[48], full[200];
    int isdir, r;
    if (ex_n == 0) return;
    snprintf(name, sizeof(name), "%s", ex_ent[ex_sel].name);
    isdir = ex_ent[ex_sel].isdir;
    if (isdir && !strcmp(name, "..")) { ex_up(); return; }
    if (!isdir) {
        int app = match_prog(name);            /* "runnable exe" launch */
        if (app >= 0) { open_app(app); return; }
    }
    path_resolve(ex_path, name, full, sizeof(full));
    if (isdir) {
        if (ex_load(full) < 0) snprintf(ex_msg, sizeof(ex_msg), "Cannot open folder");
    } else {
        r = np_load(full);
        if (r == 0) open_app(APP_NOTE);
        else if (r == -2) snprintf(ex_msg, sizeof(ex_msg), "Not a text file");
        else snprintf(ex_msg, sizeof(ex_msg), "Cannot read file");
    }
}

static void expl_key(int k)
{
    if (k == K_UP) { if (ex_sel > 0) ex_sel--; }
    else if (k == K_DOWN) { if (ex_sel < ex_n - 1) ex_sel++; }
    else if (k == K_PGUP) { ex_sel -= 10; if (ex_sel < 0) ex_sel = 0; }
    else if (k == K_PGDN) { ex_sel += 10; if (ex_sel > ex_n - 1) ex_sel = ex_n - 1; if (ex_sel < 0) ex_sel = 0; }
    else if (k == K_HOME) ex_sel = 0;
    else if (k == K_END) { ex_sel = ex_n ? ex_n - 1 : 0; }
    else if (k == K_ENTER) ex_open_sel();
    else if (k == K_BKSP) ex_up();
    else if (k == K_ESC) {
        if (!strcmp(ex_path, "/")) close_app(APP_EXPL);
        else ex_up();
    }
}

static void draw_expl(int x, int y, int w, int h)
{
    int rows, ly, i, yy;
    char sz[24], status[80];

    fill(x, y, w, h, C_WHITE);
    box3d(x + 4, y + 4, w - 8, 28, 1, C_WHITE);
    txtn(x + 10, y + 6, ex_path, (w - 24) / CHW, C_BLACK);

    ly = y + 38;
    rows = (h - 38 - 30) / CHH;
    if (rows < 1) rows = 1;
    if (ex_sel < ex_top) ex_top = ex_sel;
    if (ex_sel >= ex_top + rows) ex_top = ex_sel - rows + 1;
    if (ex_top < 0) ex_top = 0;

    for (i = ex_top, yy = ly; i < ex_n && i < ex_top + rows; i++, yy += CHH) {
        int sel = (i == ex_sel);
        uint16_t fg = sel ? C_WHITE : C_BLACK;
        if (sel) fill(x, yy, w, CHH, C_NAVY);
        if (ex_ent[i].isdir) {
            fill(x + 10, yy + 7, 18, 12, C_YELL);
            fill(x + 10, yy + 5, 8, 3, C_YELL);
        } else {
            fill(x + 13, yy + 4, 13, 16, sel ? C_WHITE : C_LIGHT);
            frame3d(x + 13, yy + 4, 13, 16, 0);
        }
        txtn(x + 38, yy, ex_ent[i].name, (w - 100) / CHW, fg);
        if (!ex_ent[i].isdir) {
            fmt_size(sz, sizeof(sz), ex_ent[i].size);
            txt(x + w - 8 - text_w(sz), yy, sz, fg);
        }
    }

    fill(x, y + h - 28, w, 28, C_GRAY);
    frame3d(x + 2, y + h - 26, w - 4, 24, 1);
    if (ex_msg[0]) snprintf(status, sizeof(status), "%s", ex_msg);
    else snprintf(status, sizeof(status), "%d object(s)", ex_n > 0 && strcmp(ex_path, "/") ? ex_n - 1 : ex_n);
    txtn(x + 8, y + h - 26, status, (w - 20) / CHW, C_BLACK);
}

/* ------------------------------------------------------------------ */
/* Command Prompt                                                      */
/* ------------------------------------------------------------------ */

static char cmd_lines[CMD_MAXL][CMD_COLS + 1];
static int cmd_nlines;
static char cmd_in[128];
static int cmd_len;
static char cwd[128] = "/";
static char hist[HIST_N][128];
static int hist_cnt, hist_idx;

static void cmd_push(const char *s)
{
    if (cmd_nlines == CMD_MAXL) {
        memmove(cmd_lines[0], cmd_lines[1], (size_t)(CMD_MAXL - 1) * (CMD_COLS + 1));
        cmd_nlines--;
    }
    snprintf(cmd_lines[cmd_nlines], CMD_COLS + 1, "%s", s);
    cmd_nlines++;
}

/* print text, wrapping at CMD_COLS, honouring \n */
static void cmd_print(const char *s)
{
    char line[CMD_COLS + 1];
    int n = 0;
    for (;;) {
        char c = *s;
        if (c == 0 || c == '\n') {
            line[n] = 0;
            cmd_push(line);
            n = 0;
            if (!c) break;
            s++;
            continue;
        }
        if (c == '\r') { s++; continue; }
        if (c == '\t') c = ' ';
        if (n == CMD_COLS) { line[n] = 0; cmd_push(line); n = 0; }
        line[n++] = (c >= 32 && c < 127) ? c : '.';
        s++;
    }
}

static void cmd_printf(const char *fmt, ...)
{
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    cmd_print(b);
}

static const char *prompt_str(void)
{
    static char pr[48];
    size_t cl2 = strlen(cwd);
    snprintf(pr, sizeof(pr), "%s> ", cl2 > 30 ? cwd + cl2 - 30 : cwd);
    return pr;
}

static void cmd_dir(const char *arg)
{
    char p[128];
    file_t d;
    dirent_t *e;
    int nf = 0, nd = 0;

    path_resolve(cwd, *arg ? arg : ".", p, sizeof(p));
    d = fs_open(p, O_RDONLY | O_DIR);
    if (d == FILEHND_INVALID) { cmd_print("The system cannot find the path specified."); return; }
    cmd_printf("Directory of %s", p);
    cmd_print("");
    while ((e = fs_readdir(d)) != NULL) {
        char sz[16];
        if (ent_is_dir(e)) { nd++; snprintf(sz, sizeof(sz), "<DIR>"); }
        else { nf++; snprintf(sz, sizeof(sz), "%d", e->size); }
        cmd_printf("%-34.34s %10s", e->name, sz);
    }
    fs_close(d);
    cmd_printf("%d file(s), %d dir(s)", nf, nd);
}

static void cmd_exec(char *line)
{
    char *arg;
    size_t al;
    time_t t;
    struct tm *tm;
    char b[64];

    while (*line == ' ') line++;
    if (!*line) return;
    arg = line;
    while (*arg && *arg != ' ') arg++;
    if (*arg) { *arg++ = 0; while (*arg == ' ') arg++; }
    al = strlen(arg);
    while (al && arg[al - 1] == ' ') arg[--al] = 0;

    if (!strcasecmp(line, "help") || !strcmp(line, "?")) {
        cmd_print("help       this list");
        cmd_print("ver        version");
        cmd_print("cls        clear the screen");
        cmd_print("echo TEXT  print text");
        cmd_print("dir [P]    list a folder (also: ls)");
        cmd_print("cd [P]     change folder");
        cmd_print("type FILE  show a text file (also: cat)");
        cmd_print("edit FILE  open/create a file in Notepad");
        cmd_print("save FILE  save the Notepad text to FILE");
        cmd_print("start APP  launch: calc notepad cmd mines");
        cmd_print("           paint clock control explorer");
        cmd_print("date/time  show date and time");
        cmd_print("explorer   open My Computer");
        cmd_print("reboot     restart the Dreamcast");
        cmd_print("bios       back to the BIOS menu");
        cmd_print("exit       close this window");
    } else if (!strcasecmp(line, "ver")) {
        cmd_print("Microsoft(R) Windows CE Version 2.00 (Build 2128)");
        cmd_print("for Sega Dreamcast - running on KallistiOS");
    } else if (!strcasecmp(line, "cls")) {
        cmd_nlines = 0;
    } else if (!strcasecmp(line, "echo")) {
        cmd_print(arg);
    } else if (!strcasecmp(line, "dir") || !strcasecmp(line, "ls")) {
        cmd_dir(arg);
    } else if (!strcasecmp(line, "cd") || !strcasecmp(line, "chdir")) {
        if (!*arg) {
            cmd_print(cwd);
        } else {
            char p[128];
            file_t d;
            path_resolve(cwd, arg, p, sizeof(p));
            d = fs_open(p, O_RDONLY | O_DIR);
            if (d == FILEHND_INVALID) {
                cmd_print("The system cannot find the path specified.");
            } else {
                fs_close(d);
                snprintf(cwd, sizeof(cwd), "%s", p);
            }
        }
    } else if (!strcasecmp(line, "type") || !strcasecmp(line, "cat")) {
        if (!*arg) {
            cmd_print("Usage: type FILE");
        } else {
            char p[128];
            int n;
            path_resolve(cwd, arg, p, sizeof(p));
            n = read_file(p, rbuf, 2000);
            if (n < 0) cmd_print("File not found.");
            else { rbuf[n] = 0; cmd_print(rbuf); }
        }
    } else if (!strcasecmp(line, "edit")) {
        if (!*arg) {
            np_new();
            open_app(APP_NOTE);
        } else {
            char p[128];
            int r;
            path_resolve(cwd, arg, p, sizeof(p));
            r = np_load(p);
            if (r == 0) {
                open_app(APP_NOTE);
            } else if (r == -1) {
                np_new();                    /* create a new file */
                open_app(APP_NOTE);
                cmd_print("(New file - use: save FILE)");
            } else {
                cmd_print("Not a text file.");
            }
        }
    } else if (!strcasecmp(line, "save")) {
        if (!*arg) {
            cmd_print("Usage: save FILE");
        } else {
            char p[64], full[128];
            path_resolve(cwd, arg, p, sizeof(p));
            if (save_auto(p, np, np_len, full, sizeof(full)) == 0)
                cmd_printf("Saved %d bytes to %s", np_len, full);
            else
                cmd_print("Cannot write there (try /ram).");
        }
    } else if (!strcasecmp(line, "start") || !strcasecmp(line, "run")) {
        if (!*arg) {
            cmd_print("Usage: start APP   (calc notepad cmd mines");
            cmd_print("                    paint clock control explorer)");
        } else {
            int a = match_prog(arg);
            if (a >= 0) open_app(a);
            else cmd_printf("Cannot find '%.20s'.", arg);
        }
    } else if (!strcasecmp(line, "date") || !strcasecmp(line, "time")) {
        t = time(NULL);
        tm = localtime(&t);
        if (tm) {
            strftime(b, sizeof(b), !strcasecmp(line, "date") ? "%Y-%m-%d" : "%H:%M:%S", tm);
            cmd_print(b);
        }
    } else if (!strcasecmp(line, "explorer")) {
        open_app(APP_EXPL);
    } else if (!strcasecmp(line, "reboot")) {
        arch_reboot();
    } else if (!strcasecmp(line, "bios")) {
        arch_menu();
    } else if (!strcasecmp(line, "exit")) {
        close_app(APP_CMD);
    } else {
        snd_err();
        cmd_printf("Bad command or file name: %.20s", line);
    }
}

static void cmd_submit(void)
{
    char echo[220], line[128];

    snprintf(echo, sizeof(echo), "%s%s", prompt_str(), cmd_in);
    cmd_print(echo);

    if (cmd_len && (hist_cnt == 0 || strcmp(hist[hist_cnt - 1], cmd_in))) {
        if (hist_cnt == HIST_N) {
            memmove(hist[0], hist[1], (size_t)(HIST_N - 1) * sizeof(hist[0]));
            hist_cnt--;
        }
        snprintf(hist[hist_cnt++], sizeof(hist[0]), "%s", cmd_in);
    }
    hist_idx = hist_cnt;

    snprintf(line, sizeof(line), "%s", cmd_in);
    cmd_len = 0;
    cmd_in[0] = 0;
    cmd_exec(line);
}

/* feed a whole command line as if typed (Run dialog, `start`) */
static void cmd_runline(const char *s)
{
    snprintf(cmd_in, sizeof(cmd_in), "%s", s);
    cmd_len = (int)strlen(cmd_in);
    cmd_submit();
}

static void cmd_key(int k)
{
    if (k >= 32 && k < 127) {
        if (cmd_len < (int)sizeof(cmd_in) - 1) { cmd_in[cmd_len++] = (char)k; cmd_in[cmd_len] = 0; }
    } else if (k == K_BKSP) {
        if (cmd_len) cmd_in[--cmd_len] = 0;
    } else if (k == K_ESC) {
        cmd_len = 0; cmd_in[0] = 0;
    } else if (k == K_ENTER) {
        cmd_submit();
    } else if (k == K_UP) {
        if (hist_idx > 0) {
            hist_idx--;
            snprintf(cmd_in, sizeof(cmd_in), "%s", hist[hist_idx]);
            cmd_len = (int)strlen(cmd_in);
        }
    } else if (k == K_DOWN) {
        if (hist_idx < hist_cnt) {
            hist_idx++;
            if (hist_idx == hist_cnt) { cmd_len = 0; cmd_in[0] = 0; }
            else { snprintf(cmd_in, sizeof(cmd_in), "%s", hist[hist_idx]); cmd_len = (int)strlen(cmd_in); }
        }
    }
}

static void draw_cmd(int x, int y, int w, int h)
{
    int rows = (h - 8) / CHH, avail, start, nshow, i, yy, len, off;
    char line[220];

    fill(x, y, w, h, C_BLACK);
    avail = rows - 1;
    if (avail < 1) avail = 1;
    start = cmd_nlines - avail;
    if (start < 0) start = 0;
    nshow = cmd_nlines - start;
    yy = y + 4;
    for (i = start; i < cmd_nlines; i++, yy += CHH) txt(x + 8, yy, cmd_lines[i], C_GRAY);

    snprintf(line, sizeof(line), "%s%s", prompt_str(), cmd_in);
    len = (int)strlen(line);
    off = len > CMD_COLS - 1 ? len - (CMD_COLS - 1) : 0;
    yy = y + 4 + nshow * CHH;
    txt(x + 8, yy, line + off, C_WHITE);
    if (blink) fill(x + 8 + (len - off) * CHW, yy + 20, 10, 3, C_WHITE);
}

/* ------------------------------------------------------------------ */
/* Calculator                                                          */
/* ------------------------------------------------------------------ */

static double c_acc;
static char c_disp[24] = "0";
static int c_op;          /* 0 none, else '+','-','*','/' */
static int c_new = 1;     /* next digit starts a new number */
static int c_err;
static int c_row, c_col;  /* gamepad cursor */
static int c_moved;       /* cursor was moved: Enter presses it */

static const char *c_lbl[5][4] = {
    { "C", "CE", "%", "sqrt" },
    { "7", "8",  "9", "/" },
    { "4", "5",  "6", "*" },
    { "1", "2",  "3", "-" },
    { "0", ".",  "=", "+" }
};

static void c_clear_all(void)
{
    c_acc = 0.0;
    c_op = 0;
    c_new = 1;
    c_err = 0;
    strcpy(c_disp, "0");
}

static void c_setd(double v)
{
    snprintf(c_disp, sizeof(c_disp), "%.10g", v);
    if (strlen(c_disp) > 16) snprintf(c_disp, sizeof(c_disp), "%g", v);
    c_new = 1;
}

static void c_calc(void)
{
    double bb = atof(c_disp);
    switch (c_op) {
    case '+': c_acc += bb; break;
    case '-': c_acc -= bb; break;
    case '*': c_acc *= bb; break;
    case '/': if (bb == 0.0) { c_err = 1; c_acc = 0; } else c_acc /= bb; break;
    default:  c_acc = bb;  break;
    }
    if (!c_err) c_setd(c_acc);
    else strcpy(c_disp, "Error");
}

static void c_press(int key)
{
    if (c_err && key != 'c') { c_clear_all(); }

    if (key >= '0' && key <= '9') {
        if (c_new) { c_disp[0] = 0; c_new = 0; }
        if (strlen(c_disp) < 15) {
            int l = (int)strlen(c_disp);
            c_disp[l] = (char)key;
            c_disp[l + 1] = 0;
        }
    } else if (key == '.') {
        if (c_new) { strcpy(c_disp, "0"); c_new = 0; }
        if (!strchr(c_disp, '.') && strlen(c_disp) < 15) {
            int l = (int)strlen(c_disp);
            c_disp[l] = '.';
            c_disp[l + 1] = 0;
        }
    } else if (key == '+' || key == '-' || key == '*' || key == '/') {
        if (c_op && !c_new) c_calc();
        else c_acc = atof(c_disp);
        c_op = key;
        c_new = 1;
    } else if (key == '=') {
        if (c_op && !c_new) { c_calc(); c_op = 0; }
    } else if (key == 'c') {
        c_clear_all();
    } else if (key == 'e') {
        strcpy(c_disp, "0");
        c_new = 1;
    } else if (key == 's') {
        double v = atof(c_disp);
        if (v < 0) { c_err = 1; strcpy(c_disp, "Error"); }
        else c_setd(sqrt(v));
    } else if (key == '%') {
        c_setd(c_acc * atof(c_disp) / 100.0);
    }
}

static void calc_key(int k)
{
    if (k == K_ENTER) {
        if (c_moved) {
            const char *lb = c_lbl[c_row][c_col];
            int key = lb[0];
            if (key == 'C' && lb[1] == 0) key = 'c';
            else if (key == 'C') key = 'e';             /* CE */
            else if (key == 's') key = 's';             /* sqrt */
            c_press(key);
            dirty = 1;
        } else {
            c_press('=');
            dirty = 1;
        }
        return;
    }
    if (k == K_ESC) k = 'c';
    else if (k == K_BKSP) k = 'e';

    if (k >= 32 && k < 127) {
        c_press(k);
        dirty = 1;
        return;
    }
    switch (k) {
    case K_UP:    c_row = (c_row + 4) % 5; c_moved = 1; break;
    case K_DOWN:  c_row = (c_row + 1) % 5; c_moved = 1; break;
    case K_LEFT:  c_col = (c_col + 3) % 4; c_moved = 1; break;
    case K_RIGHT: c_col = (c_col + 1) % 4; c_moved = 1; break;
    default: return;
    }
    dirty = 1;
}

static void draw_calc(int x, int y, int w, int h)
{
    int bx, by, r, cc;
    int bw = 72, bh = 34, gap = 6;
    int gx = x + (w - (4 * bw + 3 * gap)) / 2;
    int gy = y + 54;

    fill(x, y, w, h, C_GRAY);

    /* display */
    fill(x + 8, y + 8, w - 16, 36, C_WHITE);
    frame3d(x + 8, y + 8, w - 16, 36, 1);
    txtn(x + 8, y + 34, "", 1, C_BLACK);    /* keeps glyph in range */
    {
        int tw = text_w(c_disp);
        int lim = w - 28;
        const char *d = c_disp;
        if (tw > lim) { d += (tw - lim) / CHW; tw = text_w(d); }
        txt(x + w - 16 - tw, y + 14, d, C_BLACK);
    }

    for (r = 0; r < 5; r++) {
        for (cc = 0; cc < 4; cc++) {
            const char *lb = c_lbl[r][cc];
            int sel = (r == c_row && cc == c_col);
            bx = gx + cc * (bw + gap);
            by = gy + r * (bh + gap);
            if (by + bh > y + h) break;
            if (sel) {
                fill(bx, by, bw, bh, C_NAVY);
                frame3d(bx, by, bw, bh, 0);
                txt(bx + (bw - text_w(lb)) / 2, by + (bh - CHH) / 2, lb, C_WHITE);
            } else {
                box3d(bx, by, bw, bh, 0, C_GRAY);
                txt(bx + (bw - text_w(lb)) / 2, by + (bh - CHH) / 2, lb, C_BLACK);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Minesweeper                                                         */
/* ------------------------------------------------------------------ */

#define MS_N 9
#define MS_MINES 10
#define MS_CELLS (MS_N * MS_N)

static uint8_t ms_mine[MS_CELLS], ms_st[MS_CELLS];  /* st bit0 open, bit1 flag */
static int ms_adj[MS_CELLS];
static int ms_built, ms_dead, ms_won, ms_left, ms_opened;
static int ms_time, ms_cx, ms_cy;

static void ms_reset(void)
{
    memset(ms_mine, 0, sizeof(ms_mine));
    memset(ms_st, 0, sizeof(ms_st));
    memset(ms_adj, 0, sizeof(ms_adj));
    ms_built = ms_dead = ms_won = ms_opened = 0;
    ms_left = MS_MINES;
    ms_time = 0;
    ms_cx = ms_cy = MS_N / 2;
    dirty = 1;
}

static int ms_count_adj(int i)
{
    int r = i / MS_N, c = i % MS_N, n = 0, dr, dc;
    for (dr = -1; dr <= 1; dr++)
        for (dc = -1; dc <= 1; dc++) {
            int rr = r + dr, ccc = c + dc;
            if ((dr || dc) && rr >= 0 && rr < MS_N && ccc >= 0 && ccc < MS_N && ms_mine[rr * MS_N + ccc])
                n++;
        }
    return n;
}

static void ms_place(int safe)
{
    int placed = 0;
    while (placed < MS_MINES) {
        int i = rand() % MS_CELLS;
        if (!ms_mine[i] && i != safe) {
            ms_mine[i] = 1;
            placed++;
        }
    }
    {
        int i;
        for (i = 0; i < MS_CELLS; i++) ms_adj[i] = ms_count_adj(i);
    }
    ms_built = 1;
}

static void ms_reveal(int start)
{
    int stack[MS_CELLS], sp = 0;
    stack[sp++] = start;
    while (sp > 0) {
        int i = stack[--sp];
        int r = i / MS_N, c = i % MS_N, dr, dc;
        if (ms_st[i] & 1 || ms_st[i] & 2) continue;
        ms_st[i] |= 1;
        ms_opened++;
        if (ms_mine[i]) {
            ms_dead = 1;
            snd_err();
            return;
        }
        if (ms_adj[i] == 0) {
            for (dr = -1; dr <= 1; dr++)
                for (dc = -1; dc <= 1; dc++) {
                    int rr = r + dr, cc = c + dc;
                    if ((dr || dc) && rr >= 0 && rr < MS_N && cc >= 0 && cc < MS_N) {
                        int j = rr * MS_N + cc;
                        if (!(ms_st[j] & 1) && !(ms_st[j] & 2) && sp < MS_CELLS)
                            stack[sp++] = j;
                    }
                }
        }
    }
}

static void mine_key(int k)
{
    int i = ms_cy * MS_N + ms_cx;

    if (ms_dead || ms_won) {
        if (k == K_ENTER || k == K_ESC) close_app(APP_MINE);
        return;
    }
    switch (k) {
    case K_UP:    if (ms_cy > 0) ms_cy--; break;
    case K_DOWN:  if (ms_cy < MS_N - 1) ms_cy++; break;
    case K_LEFT:  if (ms_cx > 0) ms_cx--; break;
    case K_RIGHT: if (ms_cx < MS_N - 1) ms_cx++; break;
    case K_ENTER:
        if (!ms_built) ms_place(i);
        if (!(ms_st[i] & 2)) {
            ms_reveal(i);
            if (!ms_dead && ms_opened == MS_CELLS - MS_MINES) { ms_won = 1; snd_click(); }
        }
        break;
    case K_ESC:
        close_app(APP_MINE);
        return;
    case K_BKSP:                             /* B button / Y: toggle flag */
        if (!(ms_st[i] & 1)) {
            ms_st[i] ^= 2;
            ms_left += (ms_st[i] & 2) ? -1 : 1;
        }
        break;
    default:
        return;
    }
    dirty = 1;
}

static const uint16_t ms_numcol[9] = {
    0, RGB(0, 0, 255), RGB(0, 128, 0), RGB(255, 0, 0), RGB(0, 0, 128),
    RGB(128, 0, 0), RGB(0, 128, 128), C_BLACK, C_DGRAY
};

static void draw_mine(int x, int y, int w, int h)
{
    int gx = x + (w - MS_N * 24) / 2, gy = y + 34, r, c;
    char b[16];

    fill(x, y, w, h, C_GRAY);

    /* header: mine counter, face, timer */
    fill(x + 8, y + 4, w - 16, 26, C_BLACK);
    frame3d(x + 8, y + 4, w - 16, 26, 1);
    snprintf(b, sizeof(b), "M:%03d", ms_left < 0 ? 0 : ms_left);
    txt(x + 14, y + 5, b, C_RED);
    txt(x + w / 2 - 24, y + 5, ms_dead ? "X-(" : (ms_won ? "8-)" : ":-)"), C_YELL);
    snprintf(b, sizeof(b), "T:%03d", ms_time > 999 ? 999 : ms_time);
    txt(x + w - 62, y + 5, b, C_RED);

    for (r = 0; r < MS_N; r++) {
        for (c = 0; c < MS_N; c++) {
            int i = r * MS_N + c;
            int cx0 = gx + c * 24, cy0 = gy + r * 24;
            int open = (ms_st[i] & 1) || ms_dead;
            int sel = (r == ms_cy && c == ms_cx && !ms_dead && !ms_won);

            if (sel) fill(cx0, cy0, 24, 24, C_LIGHT);

            if (open && ms_mine[i] && !(ms_st[i] & 2)) {
                fill(cx0 + 1, cy0 + 1, 22, 22, C_DGRAY);
                wdisc(cx0 + 12, cy0 + 12, 7, C_BLACK);
                fill(cx0 + 9, cy0 + 8, 3, 3, C_WHITE);
            } else if (open) {
                fill(cx0 + 1, cy0 + 1, 22, 22, C_WHITE);
                if (ms_st[i] & 2) {
                    /* flag survived (should not happen on open) */
                    fill(cx0 + 9, cy0 + 5, 7, 6, C_RED);
                    fill(cx0 + 8, cy0 + 5, 1, 12, C_BLACK);
                } else if (ms_adj[i] > 0) {
                    char nb[2] = { (char)('0' + ms_adj[i]), 0 };
                    txt(cx0 + 6, cy0, nb, ms_numcol[ms_adj[i]]);
                }
            } else {
                box3d(cx0, cy0, 24, 24, 0, C_GRAY);
                if (ms_st[i] & 2) {
                    fill(cx0 + 9, cy0 + 5, 7, 6, C_RED);
                    fill(cx0 + 8, cy0 + 5, 1, 12, C_BLACK);
                } else if ((ms_dead || ms_won) && ms_mine[i]) {
                    wdisc(cx0 + 12, cy0 + 12, 6, C_BLACK);
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Paint                                                               */
/* ------------------------------------------------------------------ */

#define PCW 440
#define PCH 236

enum { P_PENCIL, P_LINE, P_RECT, P_FRECT, P_FILL, P_ERA, P_TOOLS };
static const char *paint_tool[P_TOOLS] = { "Pen", "Line", "Box", "Fill", "FRect", "Era" };

static const uint16_t paint_pal[16] = {
    RGB(0, 0, 0), RGB(128, 0, 0), RGB(0, 128, 0), RGB(128, 128, 0),
    RGB(0, 0, 128), RGB(128, 0, 128), RGB(0, 128, 128), RGB(192, 192, 192),
    RGB(128, 128, 128), RGB(255, 0, 0), RGB(0, 255, 0), RGB(255, 255, 0),
    RGB(0, 0, 255), RGB(255, 0, 255), RGB(0, 255, 255), RGB(255, 255, 255)
};
static const uint8_t paint_pal24[16][3] = {
    { 0, 0, 0 }, { 128, 0, 0 }, { 0, 128, 0 }, { 128, 128, 0 },
    { 0, 0, 128 }, { 128, 0, 128 }, { 0, 128, 128 }, { 192, 192, 192 },
    { 128, 128, 128 }, { 255, 0, 0 }, { 0, 255, 0 }, { 255, 255, 0 },
    { 0, 0, 255 }, { 255, 0, 255 }, { 0, 255, 255 }, { 255, 255, 255 }
};

static uint8_t pcv[PCW * PCH];
static int pc_col, pc_tool, pc_x, pc_y;
static int pc_shape;              /* waiting for second point */
static int pc_sx, pc_sy;

static void paint_reset(void)
{
    memset(pcv, 15, sizeof(pcv));       /* white canvas */
    pc_col = 0;
    pc_tool = P_PENCIL;
    pc_x = PCW / 2;
    pc_y = PCH / 2;
    pc_shape = 0;
}

static void pset(int x, int y, int c)
{
    if (x >= 0 && x < PCW && y >= 0 && y < PCH) pcv[y * PCW + x] = (uint8_t)c;
}

static void pline(int x0, int y0, int x1, int y1, int c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    for (;;) {
        pset(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void pfill_rect(int x0, int y0, int x1, int y1, int c)
{
    int x, y;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) pset(x, y, c);
}

static void paint_flood(int x, int y, int nc)
{
    static uint16_t q[PCW * PCH];
    int head = 0, tail = 0, tc;

    if (x < 0 || x >= PCW || y < 0 || y >= PCH) return;
    tc = pcv[y * PCW + x];
    if (tc == nc) return;
    q[tail++] = (uint16_t)(y * PCW + x);
    while (head < tail) {
        int i = q[head++];
        int xx = i % PCW, yy = i / PCW;
        if (pcv[i] != tc) continue;
        pcv[i] = (uint8_t)nc;
        if (xx > 0)       q[tail++] = (uint16_t)(i - 1);
        if (xx < PCW - 1) q[tail++] = (uint16_t)(i + 1);
        if (yy > 0)       q[tail++] = (uint16_t)(i - PCW);
        if (yy < PCH - 1) q[tail++] = (uint16_t)(i + PCW);
    }
}

static void paint_commit(void)
{
    switch (pc_tool) {
    case P_LINE:  pline(pc_sx, pc_sy, pc_x, pc_y, pc_col); break;
    case P_RECT: {
        int x0 = pc_sx < pc_x ? pc_sx : pc_x, x1 = pc_sx < pc_x ? pc_x : pc_sx;
        int y0 = pc_sy < pc_y ? pc_sy : pc_y, y1 = pc_sy < pc_y ? pc_y : pc_sy;
        pline(x0, y0, x1, y0, pc_col); pline(x1, y0, x1, y1, pc_col);
        pline(x1, y1, x0, y1, pc_col); pline(x0, y1, x0, y0, pc_col);
        break;
    }
    case P_FRECT: pfill_rect(pc_sx, pc_sy, pc_x, pc_y, pc_col); break;
    default: break;
    }
}

static void paint_act(void)
{
    switch (pc_tool) {
    case P_PENCIL:
        pset(pc_x, pc_y, pc_col); pset(pc_x + 1, pc_y, pc_col);
        pset(pc_x, pc_y + 1, pc_col); pset(pc_x + 1, pc_y + 1, pc_col);
        pc_shape = 0;
        break;
    case P_ERA:
        pfill_rect(pc_x - 3, pc_y - 3, pc_x + 3, pc_y + 3, 15);
        pc_shape = 0;
        break;
    case P_FILL:
        paint_flood(pc_x, pc_y, pc_col);
        pc_shape = 0;
        break;
    default:
        if (!pc_shape) { pc_sx = pc_x; pc_sy = pc_y; pc_shape = 1; }
        else { paint_commit(); pc_shape = 0; }
        break;
    }
}

/* visible canvas size inside the current client rect */
static void paint_dims(int *vw, int *vh)
{
    int cx, cy, cw, ch;
    win_client(APP_PAINT, &cx, &cy, &cw, &ch);
    *vw = cw - 12; if (*vw > PCW) *vw = PCW;
    *vh = ch - 64; if (*vh > PCH) *vh = PCH;
    if (*vw < 40) *vw = 40;
    if (*vh < 40) *vh = 40;
}

static void paint_key(int k)
{
    int vw, vh;
    paint_dims(&vw, &vh);

    switch (k) {
    case K_UP:    pc_y -= 4; break;
    case K_DOWN:  pc_y += 4; break;
    case K_LEFT:  pc_x -= 4; break;
    case K_RIGHT: pc_x += 4; break;
    case K_ENTER: paint_act(); break;
    case K_BKSP:  pc_tool = (pc_tool + 1) % P_TOOLS; pc_shape = 0; break;
    case K_ESC:   close_app(APP_PAINT); return;
    case K_DEL:   memset(pcv, 15, sizeof(pcv)); pc_shape = 0; break;
    default:
        if (k == 'c' || k == 'C') { pc_col = (pc_col + 1) % 16; break; }
        if (k == 't' || k == 'T') { pc_tool = (pc_tool + 1) % P_TOOLS; pc_shape = 0; break; }
        return;
    }
    if (pc_x < 0) pc_x = 0;
    if (pc_y < 0) pc_y = 0;
    if (pc_x > vw - 1) pc_x = vw - 1;
    if (pc_y > vh - 1) pc_y = vh - 1;
    dirty = 1;
}

static void paint_save(void)
{
    static uint8_t bmp[54 + PCW * PCH * 3 + PCW * 4];
    int len, x, y;
    char path[64];
    uint8_t *p;

    /* BITMAPFILEHEADER */
    bmp[0] = 'B'; bmp[1] = 'M';
    len = 54 + (PCW * 3 + ((PCW * 3) & 3 ? 4 - ((PCW * 3) & 3) : 0)) * PCH;
    bmp[2] = (uint8_t)len; bmp[3] = (uint8_t)(len >> 8);
    bmp[4] = (uint8_t)(len >> 16); bmp[5] = (uint8_t)(len >> 24);
    bmp[10] = 54;
    /* BITMAPINFOHEADER */
    bmp[14] = 40;
    bmp[18] = (uint8_t)PCW; bmp[19] = (uint8_t)(PCW >> 8);
    bmp[20] = (uint8_t)(PCW >> 16); bmp[21] = (uint8_t)(PCW >> 24);
    bmp[22] = (uint8_t)PCH; bmp[23] = (uint8_t)(PCH >> 8);
    bmp[24] = (uint8_t)(PCH >> 16); bmp[25] = (uint8_t)(PCH >> 24);
    bmp[26] = 1; bmp[28] = 24;

    p = bmp + 54;
    for (y = PCH - 1; y >= 0; y--) {
        for (x = 0; x < PCW; x++) {
            uint8_t ci = pcv[y * PCW + x] & 15;
            *p++ = paint_pal24[ci][2];
            *p++ = paint_pal24[ci][1];
            *p++ = paint_pal24[ci][0];
        }
        while (((uintptr_t)p - 54) & 3) *p++ = 0;
    }
    len = (int)(p - bmp);
    if (save_auto("paint.bmp", (char *)bmp, len, path, sizeof(path)) == 0) {
        snprintf(paint_msg, sizeof(paint_msg), "%s", path);
        paint_msg_t = frame_no;
    } else {
        snprintf(paint_msg, sizeof(paint_msg), "cannot save");
        paint_msg_t = frame_no;
    }
    dirty = 1;
}

static void draw_paint(int x, int y, int w, int h)
{
    int t, i, cx, cy;
    int vw = w - 12, vh = h - 64;
    if (vw > PCW) vw = PCW;
    if (vh > PCH) vh = PCH;
    if (vw < 40) vw = 40;
    if (vh < 40) vh = 40;
    cx = x + 6;
    cy = y + 32;

    fill(x, y, w, h, C_GRAY);

    /* toolbar */
    for (t = 0; t < P_TOOLS; t++) {
        int bx = x + 6 + t * 46;
        if (t == pc_tool) {
            fill(bx, y + 4, 44, 20, C_NAVY);
            frame3d(bx, y + 4, 44, 20, 0);
            txt(bx + (44 - text_w(paint_tool[t])) / 2, y + 2, paint_tool[t], C_WHITE);
        } else {
            box3d(bx, y + 4, 44, 20, 0, C_GRAY);
            txt(bx + (44 - text_w(paint_tool[t])) / 2, y + 2, paint_tool[t], C_BLACK);
        }
    }

    /* canvas (respects the window clip) */
    fill(cx - 2, cy - 2, vw + 4, vh + 4, C_DGRAY);
    for (i = 0; i < vh; i++) {
        int yy2 = cy + i, x0 = cx, x1 = cx + vw;
        uint8_t *src = pcv + i * PCW;
        int xx;
        if (yy2 < ct || yy2 >= cb) continue;
        if (x0 < cl) { src += cl - x0; x0 = cl; }
        if (x1 > cr) x1 = cr;
        for (xx = x0; xx < x1; xx++)
            fb[yy2 * SW + xx] = paint_pal[src[xx - cx] & 15];
    }

    /* shape preview */
    if (pc_shape) {
        int ax = cx + pc_sx, ay = cy + pc_sy, bx2 = cx + pc_x, by2 = cy + pc_y;
        uint16_t cc = paint_pal[pc_col];
        if (pc_tool == P_LINE) wline(ax, ay, bx2, by2, cc);
        else {
            int rx0 = ax < bx2 ? ax : bx2, rx1 = ax < bx2 ? bx2 : ax;
            int ry0 = ay < by2 ? ay : by2, ry1 = ay < by2 ? by2 : ay;
            fill(rx0, ry0, rx1 - rx0 + 1, 1, cc);
            fill(rx0, ry1, rx1 - rx0 + 1, 1, cc);
            fill(rx0, ry0, 1, ry1 - ry0 + 1, cc);
            fill(rx1, ry0, 1, ry1 - ry0 + 1, cc);
        }
    }

    /* cursor */
    {
        int px = cx + pc_x, py = cy + pc_y;
        uint16_t cc = paint_pal[pc_col ^ 15];
        fill(px - 4, py, 9, 1, cc);
        fill(px, py - 4, 1, 9, cc);
    }

    /* palette strip */
    for (i = 0; i < 16; i++) {
        int sx = x + 6 + i * 22;
        fill(sx, y + h - 24, 20, 18, paint_pal[i]);
        frame3d(sx, y + h - 24, 20, 18, pc_col == i ? 1 : 0);
    }
    fill(x + 6 + 16 * 22 + 6, y + h - 24, 24, 18, paint_pal[pc_col]);
    frame3d(x + 6 + 16 * 22 + 6, y + h - 24, 24, 18, 0);
}

/* ------------------------------------------------------------------ */
/* Clock (analog + calendar)                                           */
/* ------------------------------------------------------------------ */

static void clock_key(int k)
{
    if (k == K_ESC) close_app(APP_CLOCK);
}

static void draw_clock(int x, int y, int w, int h)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    int acx = x + 105, acy = y + h / 2 - 10, r = 76;
    static const char *mon[12] = { "January", "February", "March", "April", "May", "June",
                                   "July", "August", "September", "October", "November", "December" };

    fill(x, y, w, h, C_GRAY);

    if (tm) {
        double sec = tm->tm_sec, min = tm->tm_min + sec / 60.0, hr = (tm->tm_hour % 12) + min / 60.0;
        char b[24];
        int i;

        wdisc(acx, acy, r, C_BLACK);
        wdisc(acx, acy, r - 2, C_WHITE);
        for (i = 0; i < 12; i++) {
            double a = i * 3.14159265 / 6.0;
            wline(acx + (int)((r - 8) * sin(a)), acy - (int)((r - 8) * cos(a)),
                  acx + (int)((r - 3) * sin(a)), acy - (int)((r - 3) * cos(a)), C_BLACK);
        }
        wline(acx, acy, acx + (int)(40 * sin(hr * 3.14159265 / 6)), acy - (int)(40 * cos(hr * 3.14159265 / 6)), C_BLACK);
        wline(acx, acy, acx + (int)(60 * sin(min * 3.14159265 / 30)), acy - (int)(60 * cos(min * 3.14159265 / 30)), C_BLACK);
        wline(acx, acy, acx + (int)(66 * sin(sec * 3.14159265 / 30)), acy - (int)(66 * cos(sec * 3.14159265 / 30)), C_RED);
        wdisc(acx, acy, 3, C_BLACK);

        strftime(b, sizeof(b), "%H:%M:%S", tm);
        txt(acx - text_w(b) / 2, y + h - 62, b, C_BLACK);
        strftime(b, sizeof(b), "%a %d %b %Y", tm);
        txt(acx - text_w(b) / 2, y + h - 34, b, C_BLACK);

        /* month calendar */
        {
            struct tm f = *tm;
            int dim, first, row, col, day;
            int cxx = x + w / 2 + 30, cww = w / 2 - 44;

            f.tm_mday = 1;
            mktime(&f);
            dim = (tm->tm_mon == 1)
                ? (28 + ((tm->tm_year % 4 == 0 && (tm->tm_year % 100 != 0 || tm->tm_year % 400 == 0)) ? 1 : 0))
                : (tm->tm_mon % 7 % 2 ? 30 : 31);
            if (tm->tm_mon == 3 || tm->tm_mon == 5 || tm->tm_mon == 8 || tm->tm_mon == 10) dim = 30;
            if (tm->tm_mon == 1) dim = (tm->tm_year % 4 == 0 && (tm->tm_year % 100 != 0 || tm->tm_year % 400 == 0)) ? 29 : 28;

            snprintf(b, sizeof(b), "%.9s %d", mon[tm->tm_mon], 1900 + tm->tm_year);
            txt(cxx + (cww - text_w(b)) / 2, y + 12, b, C_NAVY);
            txt(cxx + 2, y + 40, "Su Mo Tu We Th Fr Sa", C_DGRAY);
            first = f.tm_wday;
            for (day = 1; day <= dim; day++) {
                int pos = first + day - 1;
                row = pos / 7;
                col = pos % 7;
                snprintf(b, sizeof(b), "%2d", day);
                if (day == tm->tm_mday) {
                    fill(cxx + col * 32, y + 70 + row * CHH, 26, CHH, C_NAVY);
                    txt(cxx + col * 32 + 2, y + 70 + row * CHH, b, C_WHITE);
                } else {
                    txt(cxx + col * 32 + 2, y + 70 + row * CHH, b, C_BLACK);
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Control Panel / About / Run / Shut Down dialogs                     */
/* ------------------------------------------------------------------ */

#define CTRL_N 5
static const char *ctrl_lbl[CTRL_N] = {
    "Display", "Date and Time", "Input Panel", "Sounds", "System"
};
static int ctrl_sel;

static void ctrl_key(int k)
{
    switch (k) {
    case K_UP:   ctrl_sel = (ctrl_sel + CTRL_N - 1) % CTRL_N; break;
    case K_DOWN: ctrl_sel = (ctrl_sel + 1) % CTRL_N; break;
    case K_ESC:  close_app(APP_CTRL); return;
    case K_ENTER:
        switch (ctrl_sel) {
        case 0: {                                       /* cycle theme */
            int i, found = 0;
            for (i = 0; i < 4; i++) if (theme_col[i] == col_desktop) { found = i; break; }
            col_desktop = theme_col[(found + 1) % 4];
            break;
        }
        case 1: open_app(APP_CLOCK); break;
        case 2: {
            int cur = (vkb_user < 0) ? !has_kbd : vkb_user;
            vkb_user = !cur;
            break;
        }
        case 3:
#if USE_SOUND
            snd_enabled = !snd_enabled;
#endif
            break;
        case 4: open_app(APP_ABOUT); break;
        }
        break;
    default: return;
    }
    dirty = 1;
}

static void draw_ctrl(int x, int y, int w, int h)
{
    int i;
    fill(x, y, w, h, C_GRAY);
    for (i = 0; i < CTRL_N; i++) {
        int iy = y + 8 + i * 34;
        if (i == ctrl_sel) fill(x + 4, iy, w - 8, 32, C_NAVY);
        fill(x + 12, iy + 10, 12, 12, i == ctrl_sel ? C_WHITE : C_NAVY);
        txt(x + 34, iy + 4, ctrl_lbl[i], i == ctrl_sel ? C_WHITE : C_BLACK);
        switch (i) {
        case 0: {
            int tsel, found = 0;
            for (tsel = 0; tsel < 4; tsel++) if (theme_col[tsel] == col_desktop) { found = tsel; break; }
            txt(x + 190, iy + 4, theme_name[found], i == ctrl_sel ? C_WHITE : C_DGRAY);
            break;
        }
        case 1: txt(x + 190, iy + 4, "RTC", i == ctrl_sel ? C_WHITE : C_DGRAY); break;
        case 2: {
            int cur = (vkb_user < 0) ? !has_kbd : vkb_user;
            txt(x + 190, iy + 4, cur ? "shown" : "hidden", i == ctrl_sel ? C_WHITE : C_DGRAY);
            break;
        }
        case 3:
#if USE_SOUND
            txt(x + 190, iy + 4, snd_enabled ? "On" : "Off", i == ctrl_sel ? C_WHITE : C_DGRAY);
#else
            txt(x + 190, iy + 4, "n/a", i == ctrl_sel ? C_WHITE : C_DGRAY);
#endif
            break;
        default: break;
        }
    }
    txtn(x + 8, y + h - 26, "Enter: open/change   Esc: close", (w - 16) / CHW, C_DGRAY);
}

static void about_key(int k)
{
    if (k == K_ESC || k == K_ENTER) { close_app(APP_ABOUT); dirty = 1; }
}

static void draw_about(int x, int y, int w, int h)
{
    fill(x, y, w, h, C_GRAY);
    fill(x + 24, y + 16, 22, 22, C_RED);
    fill(x + 48, y + 16, 22, 22, C_GREEN);
    fill(x + 24, y + 40, 22, 22, RGB(0, 0, 255));
    fill(x + 48, y + 40, 22, 22, C_YELL);
    txt(x + 84, y + 14, "Microsoft", C_DGRAY);
    txtb(x + 84, y + 34, "Windows CE", C_BLACK);
    txt(x + 84, y + 62, "Dreamcast Edition", C_TEAL);
    txt(x + 24, y + 100, "Version 2.00 (Build 2128)", C_BLACK);
    txt(x + 24, y + 124, "Shell running on KallistiOS", C_BLACK);
    txt(x + 24, y + 148, "Hitachi SH-4 - 200 MHz - 16 MB RAM", C_BLACK);
    txt(x + 24, y + h - 28, "Press Esc to close", C_DGRAY);
}

static char run_in[64];
static int run_len;

static void run_key(int k)
{
    if (k >= 32 && k < 127) {
        if (run_len < (int)sizeof(run_in) - 1) { run_in[run_len++] = (char)k; run_in[run_len] = 0; }
        dirty = 1;
    } else if (k == K_BKSP) {
        if (run_len) run_in[--run_len] = 0;
        dirty = 1;
    } else if (k == K_ESC) {
        close_app(APP_RUN);
    } else if (k == K_ENTER) {
        char line[64];
        snprintf(line, sizeof(line), "%s", run_in);
        snd_click();
        close_app(APP_RUN);
        if (line[0]) run_command(line);
    }
}

static void draw_run(int x, int y, int w, int h)
{
    fill(x, y, w, h, C_GRAY);
    txt(x + 12, y + 8, "Type the name of a program,", C_BLACK);
    txt(x + 12, y + 32, "and press Enter:", C_BLACK);
    fill(x + 12, y + 62, w - 24, 30, C_WHITE);
    frame3d(x + 12, y + 62, w - 24, 30, 1);
    txt(x + 18, y + 66, run_in, C_BLACK);
    if (blink) fill(x + 18 + run_len * CHW, y + 86, 10, 3, C_BLACK);
    txtn(x + 12, y + h - 30,
         "calc notepad cmd mines paint clock control help",
         (w - 24) / CHW, C_DGRAY);
}

static int shut_sel;

static void shut_key(int k)
{
    switch (k) {
    case K_UP:   shut_sel = (shut_sel + 2) % 3; break;
    case K_DOWN: shut_sel = (shut_sel + 1) % 3; break;
    case K_ESC:  close_app(APP_SHUT); break;
    case K_ENTER:
        close_app(APP_SHUT);
        if (shut_sel == 0) arch_reboot();
        else if (shut_sel == 1) arch_menu();
        else { powered_down = 1; snd_err(); }
        break;
    default: return;
    }
    dirty = 1;
}

static void draw_shut(int x, int y, int w, int h)
{
    static const char *opts[3] = {
        "Restart the Dreamcast",
        "Return to the BIOS menu",
        "Shut down the shell"
    };
    int i;
    fill(x, y, w, h, C_GRAY);
    txtb(x + 12, y + 8, "Are you sure you want to:", C_BLACK);
    for (i = 0; i < 3; i++) {
        int iy = y + 44 + i * 36;
        if (i == shut_sel) fill(x + 8, iy, w - 16, 32, C_NAVY);
        txt(x + 24, iy + 4, opts[i], i == shut_sel ? C_WHITE : C_BLACK);
    }
    txt(x + 12, y + h - 28, "Up/Down + Enter, Esc cancels", C_DGRAY);
}

/* ------------------------------------------------------------------ */
/* Help                                                                */
/* ------------------------------------------------------------------ */

static const char *help_lines[] = {
    "Windows CE 2.00 for Dreamcast - Help",
    "",
    "GAMEPAD",
    " D-pad/Stick  move        A  Enter / OK",
    " B  Esc / back / flag     Y  Backspace / tool",
    " X  maximize (Shift on Input Panel)",
    " Start  Start menu        L / R  prev / next window",
    " A+B+X+Y+Start  exit to BIOS",
    "",
    "KEYBOARD",
    " Ctrl+Esc  Start menu     Ctrl+Tab  next window",
    " Ctrl+W  close window     Ctrl+M  move window",
    " Ctrl+G  maximize         Ctrl+S  save (Notepad/Paint)",
    " F1  Help                 Esc  back / cancel",
    "",
    "PROGRAMS",
    " My Computer   browse /ram /pc /cd /vmu and",
    "               Program Files (open *.exe to run)",
    " Command       dir cd type edit save start",
    " Notepad       Ctrl+S saves untitled.txt",
    " Calculator    keys, gamepad or Input Panel",
    " Minesweeper   A reveal, B(Y) flag, Esc quit",
    " Paint         arrows move, Enter draws,",
    "               Y/c/t tools+color, Ctrl+S = bmp",
    " Clock         analog clock and calendar",
    " Control Panel theme, clock, Input Panel,",
    "               sounds, about",
    "",
    "Run dialog: type calc, notepad, cmd, mines,",
    "paint, clock, control, help - anything else",
    "runs as a Command Prompt command.",
    NULL
};

static int help_top;

static void help_key(int k)
{
    switch (k) {
    case K_UP:  help_top--; break;
    case K_DOWN: help_top++; break;
    case K_PGUP: help_top -= 8; break;
    case K_PGDN: help_top += 8; break;
    case K_HOME: help_top = 0; break;
    case K_ESC:  close_app(APP_HELP); return;
    default: return;
    }
    if (help_top < 0) help_top = 0;
    dirty = 1;
}

static void draw_help(int x, int y, int w, int h)
{
    int rows = (h - 8) / CHH, i, n, yy = y + 4;

    for (n = 0; help_lines[n]; n++) ;
    if (help_top > n - rows) help_top = (n > rows) ? n - rows : 0;
    fill(x, y, w, h, C_WHITE);
    for (i = help_top; i < n && i < help_top + rows; i++, yy += CHH)
        txt(x + 10, yy, help_lines[i],
            i == 0 ? C_NAVY : (help_lines[i][0] && !strstr(help_lines[i], " ")) ? C_NAVY : C_BLACK);
}

/* ------------------------------------------------------------------ */
/* on-screen keyboard (Input Panel)                                    */
/* ------------------------------------------------------------------ */

#define VK_KW 52
#define VK_KH 32
#define VK_H  (5 * VK_KH + 8)

static const char *VK_N[4] = { "1234567890-=", "qwertyuiop[]", "asdfghjkl;'\\", "zxcvbnm,./`" };
static const char *VK_S[4] = { "!@#$%^&*()_+", "QWERTYUIOP{}", "ASDFGHJKL:\"|", "ZXCVBNM<>?~" };

/* bottom row: cols 0-1 Shift, 2-8 Space, 9-10 Enter, 11 Hide */
static int vk4_id(int col) { return col <= 1 ? 0 : col <= 8 ? 1 : col <= 10 ? 2 : 3; }

static void vk_move(int dx, int dy)
{
    if (dy) vk_row = (vk_row + dy + 5) % 5;
    if (dx) {
        if (vk_row == 4) {
            int id = vk4_id(vk_col), guard = 0;
            do vk_col = (vk_col + dx + 12) % 12; while (vk4_id(vk_col) == id && ++guard < 12);
        } else {
            vk_col = (vk_col + dx + 12) % 12;
        }
    }
    dirty = 1;
}

static void vk_press(void)
{
    if (vk_row < 4) {
        const char *lay;
        if (vk_row == 3 && vk_col == 11) { push_ev(K_BKSP); return; }
        lay = vk_shift ? VK_S[vk_row] : VK_N[vk_row];
        push_ev((unsigned char)lay[vk_col]);
        vk_shift = 0;
    } else {
        switch (vk4_id(vk_col)) {
        case 0: vk_shift = !vk_shift; break;
        case 1: push_ev(' '); break;
        case 2: push_ev(K_ENTER); break;
        default: vkb_user = 0; break;
        }
    }
    dirty = 1;
}

static void vk_key(int x, int y, int w, int h, const char *label, int sel, int sunk)
{
    uint16_t fg = sel ? C_WHITE : C_BLACK;
    if (sel) { fill(x, y, w, h, C_NAVY); frame3d(x, y, w, h, 0); }
    else box3d(x, y, w, h, sunk, C_GRAY);
    txt(x + (w - text_w(label)) / 2, y + (h - CHH) / 2, label, fg);
}

static void draw_vkb(void)
{
    int y0 = WORK_BOTTOM - VK_H, row, col, y;
    char s[2] = { 0, 0 };

    fill(0, y0, SW, VK_H, C_GRAY);
    fill(0, y0, SW, 2, C_WHITE);
    for (row = 0; row < 4; row++) {
        const char *lay = vk_shift ? VK_S[row] : VK_N[row];
        for (col = 0; col < 12; col++) {
            int sel = (row == vk_row && col == vk_col);
            int x = 6 + col * VK_KW;
            y = y0 + 4 + row * VK_KH;
            if (row == 3 && col == 11) {
                vk_key(x, y, VK_KW - 2, VK_KH - 2, "<-", sel, 0);
            } else {
                s[0] = lay[col];
                vk_key(x, y, VK_KW - 2, VK_KH - 2, s, sel, 0);
            }
        }
    }
    y = y0 + 4 + 4 * VK_KH;
    {
        int id = (vk_row == 4) ? vk4_id(vk_col) : -1;
        vk_key(6, y, 2 * VK_KW - 2, VK_KH - 2, "Shift", id == 0, vk_shift);
        vk_key(6 + 2 * VK_KW, y, 7 * VK_KW - 2, VK_KH - 2, "Space", id == 1, 0);
        vk_key(6 + 9 * VK_KW, y, 2 * VK_KW - 2, VK_KH - 2, "Enter", id == 2, 0);
        vk_key(6 + 11 * VK_KW, y, VK_KW - 2, VK_KH - 2, "v", id == 3, 0);
    }
}

/* ------------------------------------------------------------------ */
/* Start menu                                                          */
/* ------------------------------------------------------------------ */

enum { M_MAIN, M_PROGRAMS, M_ACCESS, M_GAMES, M_SETTINGS, M_MCOUNT };
enum { MI_OPEN_APP, MI_SUB, MI_SEP, MI_SIP, MI_MOVE, MI_MAX, MI_CLOSE, MI_RUN, MI_SHUTDOWN };

typedef struct { const char *label; int type; int arg; } MEnt;

static const MEnt menu_main[] = {
    { "Programs",       MI_SUB,       M_PROGRAMS },
    { "Settings",       MI_SUB,       M_SETTINGS },
    { "",               MI_SEP,       0 },
    { "Help",           MI_OPEN_APP,  APP_HELP },
    { "Run...",         MI_RUN,       0 },
    { "",               MI_SEP,       0 },
    { "Shut Down...",   MI_SHUTDOWN,  0 },
    { NULL, 0, 0 }
};
static const MEnt menu_programs[] = {
    { "Accessories",    MI_SUB,       M_ACCESS },
    { "Games",          MI_SUB,       M_GAMES },
    { "",               MI_SEP,       0 },
    { "My Computer",    MI_OPEN_APP,  APP_EXPL },
    { "Command Prompt", MI_OPEN_APP,  APP_CMD },
    { NULL, 0, 0 }
};
static const MEnt menu_access[] = {
    { "Calculator",       MI_OPEN_APP, APP_CALC },
    { "Notepad",          MI_OPEN_APP, APP_NOTE },
    { "Paint",            MI_OPEN_APP, APP_PAINT },
    { "Clock",            MI_OPEN_APP, APP_CLOCK },
    { "",                 MI_SEP, 0 },
    { "Windows Explorer", MI_OPEN_APP, APP_EXPL },
    { NULL, 0, 0 }
};
static const MEnt menu_games[] = {
    { "Minesweeper",    MI_OPEN_APP,  APP_MINE },
    { NULL, 0, 0 }
};
static const MEnt menu_settings[] = {
    { "Control Panel",      MI_OPEN_APP, APP_CTRL },
    { "Input Panel",        MI_SIP,      0 },
    { "",                   MI_SEP,      0 },
    { "Move Window",        MI_MOVE,     0 },
    { "Maximize / Restore", MI_MAX,      0 },
    { "Close Window",       MI_CLOSE,    0 },
    { NULL, 0, 0 }
};

static const MEnt *menu_get(int id)
{
    switch (id) {
    case M_PROGRAMS: return menu_programs;
    case M_ACCESS:   return menu_access;
    case M_GAMES:    return menu_games;
    case M_SETTINGS: return menu_settings;
    default:         return menu_main;
    }
}

static void menu_close_all(void) { menu_open = 0; }

static void menu_open_start(void)
{
    menu_open = 1;
    m_lvl = 0;
    m_id[0] = M_MAIN;
    m_sel[0] = 0;
    snd_click();
}

static void menu_activate(void)
{
    const MEnt *m = menu_get(m_id[m_lvl]);
    const MEnt *it = &m[m_sel[m_lvl]];

    switch (it->type) {
    case MI_SUB:
        m_lvl++;
        m_id[m_lvl] = it->arg;
        m_sel[m_lvl] = 0;
        break;
    case MI_OPEN_APP:
        menu_open = 0;
        snd_click();
        open_app(it->arg);
        break;
    case MI_SIP: {
        int cur = (vkb_user < 0) ? !has_kbd : vkb_user;
        menu_open = 0;
        vkb_user = !cur;
        break;
    }
    case MI_MOVE:  menu_open = 0; begin_move(); break;
    case MI_MAX:   menu_open = 0; toggle_max(); break;
    case MI_CLOSE:
        menu_open = 0;
        if (active_app() >= 0) close_app(active_app());
        break;
    case MI_RUN:      menu_open = 0; open_app(APP_RUN); break;
    case MI_SHUTDOWN: menu_open = 0; open_app(APP_SHUT); break;
    }
    dirty = 1;
}

static void menu_key(int k)
{
    const MEnt *m = menu_get(m_id[m_lvl]);
    int n = 0, s = m_sel[m_lvl];
    while (m[n].label || m[n].type == MI_SEP) n++;

    switch (k) {
    case K_UP:
        do s = (s + n - 1) % n; while (m[s].type == MI_SEP);
        m_sel[m_lvl] = s;
        break;
    case K_DOWN:
        do s = (s + 1) % n; while (m[s].type == MI_SEP);
        m_sel[m_lvl] = s;
        break;
    case K_RIGHT:
        if (m[s].type == MI_SUB) menu_activate();
        break;
    case K_LEFT:
        if (m_lvl > 0) m_lvl--;
        break;
    case K_ENTER:
        menu_activate();
        break;
    case K_ESC:
        if (m_lvl > 0) m_lvl--;
        else menu_open = 0;
        break;
    default:
        return;
    }
    dirty = 1;
}

#define MENU_W 196
#define MENU_IH 26

static int menu_count(const MEnt *m)
{
    int n = 0;
    while (m[n].label || m[n].type == MI_SEP) n++;
    return n;
}

static void draw_menu(void)
{
    int lvl;
    for (lvl = 0; lvl <= m_lvl; lvl++) {
        const MEnt *m = menu_get(m_id[lvl]);
        int n = menu_count(m);
        int h = n * MENU_IH + 6;
        int x = 2 + lvl * (MENU_W + 6);
        int y = WORK_BOTTOM - h - 2;
        int i;

        box3d(x, y, MENU_W, h, 0, C_GRAY);
        if (lvl == 0) fill(x + 3, y + 3, 24, h - 6, C_NAVY);
        for (i = 0; i < n; i++) {
            int iy = y + 3 + i * MENU_IH;
            if (m[i].type == MI_SEP) {
                fill(x + (lvl == 0 ? 30 : 8), iy + MENU_IH / 2, MENU_W - (lvl == 0 ? 36 : 16), 1, C_DGRAY);
                continue;
            }
            if (i == m_sel[lvl]) {
                fill(x + (lvl == 0 ? 26 : 4), iy, MENU_W - (lvl == 0 ? 30 : 8), MENU_IH, C_NAVY);
                txt(x + (lvl == 0 ? 34 : 12), iy + 1, m[i].label, C_WHITE);
            } else {
                txt(x + (lvl == 0 ? 34 : 12), iy + 1, m[i].label, C_BLACK);
            }
            if (m[i].type == MI_SUB)
                txt(x + MENU_W - 16, iy + 1, ">", i == m_sel[lvl] ? C_WHITE : C_BLACK);
        }
    }
}

/* ------------------------------------------------------------------ */
/* desktop                                                             */
/* ------------------------------------------------------------------ */

static const struct { const char *label; int app; } desk_icons[8] = {
    { "My Computer", APP_EXPL }, { "Command", APP_CMD },
    { "Notepad", APP_NOTE },     { "Calculator", APP_CALC },
    { "Mines", APP_MINE },       { "Paint", APP_PAINT },
    { "Clock", APP_CLOCK },      { "Help", APP_HELP }
};

static void desk_key(int k)
{
    switch (k) {
    case K_UP:    desk_sel = (desk_sel + 6) % 8; break;
    case K_DOWN:  desk_sel = (desk_sel + 2) % 8; break;
    case K_LEFT:  if (desk_sel & 1) desk_sel--; break;
    case K_RIGHT: if (!(desk_sel & 1)) desk_sel++; break;
    case K_ENTER: open_app(desk_icons[desk_sel].app); break;
    default: return;
    }
    dirty = 1;
}

static void draw_icon(int app, int x, int y)
{
    int j;
    switch (app) {
    case APP_EXPL:
        box3d(x + 2, y, 36, 26, 0, C_GRAY);
        fill(x + 7, y + 5, 26, 16, RGB(0, 170, 170));
        fill(x + 16, y + 26, 8, 4, C_DGRAY);
        box3d(x + 8, y + 30, 24, 5, 0, C_GRAY);
        break;
    case APP_CMD:
        fill(x, y, 40, 32, C_BLACK);
        frame3d(x, y, 40, 32, 1);
        txt(x + 8, y + 4, "C:", C_LIGHT);
        break;
    case APP_NOTE:
        box3d(x + 6, y, 28, 36, 0, C_WHITE);
        for (j = 0; j < 5; j++) fill(x + 11, y + 7 + j * 6, 18, 2, RGB(0, 0, 160));
        break;
    case APP_CALC:
        box3d(x + 4, y, 32, 38, 0, C_GRAY);
        fill(x + 9, y + 5, 22, 8, C_WHITE);
        for (j = 0; j < 6; j++)
            fill(x + 9 + (j % 3) * 8, y + 17 + (j / 3) * 8, 6, 6, C_DGRAY);
        break;
    case APP_MINE:
        box3d(x + 2, y + 2, 36, 36, 0, C_GRAY);
        for (j = 0; j < 4; j++) {
            int gx = x + 7 + (j % 2) * 14, gy = y + 7 + (j / 2) * 14;
            fill(gx, gy, 12, 12, C_LIGHT);
            if (j == 0) wdisc(gx + 6, gy + 6, 4, C_BLACK);
        }
        break;
    case APP_PAINT:
        box3d(x + 4, y + 2, 34, 36, 0, C_WHITE);
        fill(x + 9, y + 7, 8, 8, C_RED);
        fill(x + 20, y + 7, 8, 8, RGB(0, 0, 255));
        fill(x + 9, y + 22, 8, 8, C_YELL);
        fill(x + 20, y + 22, 8, 8, C_GREEN);
        break;
    case APP_CLOCK:
        wdisc(x + 20, y + 19, 18, C_NAVY);
        wdisc(x + 20, y + 19, 16, C_WHITE);
        wline(x + 20, y + 19, x + 20, y + 9, C_BLACK);
        wline(x + 20, y + 19, x + 27, y + 22, C_RED);
        break;
    default:
        box3d(x + 4, y, 32, 34, 0, C_YELL);
        txt(x + 14, y + 5, "?", C_BLACK);
        break;
    }
}

static void draw_desktop(void)
{
    int i;
    for (i = 0; i < 8; i++) {
        int ix = 56 + (i % 2) * 128, iy = 10 + (i / 2) * 104;
        const char *lb = desk_icons[i].label;
        int lw = text_w(lb), lx = ix + 20 - lw / 2, ly = iy + 42;
        draw_icon(desk_icons[i].app, ix, iy);
        if (i == desk_sel) {
            fill(lx - 4, ly, lw + 8, CHH, C_NAVY);
            txt(lx, ly, lb, C_WHITE);
        } else {
            txt(lx, ly, lb, C_WHITE);
        }
    }
    txtb(SW - 170, WORK_BOTTOM - 84, "Windows CE", RGB(120, 210, 210));
    txt(SW - 206, WORK_BOTTOM - 58, "for Dreamcast", RGB(120, 210, 210));
}

/* ------------------------------------------------------------------ */
/* taskbar                                                             */
/* ------------------------------------------------------------------ */

static void draw_taskbar(void)
{
    int y = SH - TASKBAR_H, i, n = 0;
    char clk[16];
    time_t t;
    struct tm *tm;

    fill(0, y, SW, TASKBAR_H, C_GRAY);
    fill(0, y, SW, 2, C_WHITE);

    box3d(3, y + 4, 98, TASKBAR_H - 6, menu_open, C_GRAY);
    fill(10, y + 9, 8, 8, C_RED);
    fill(19, y + 9, 8, 8, C_GREEN);
    fill(10, y + 18, 8, 8, RGB(0, 0, 255));
    fill(19, y + 18, 8, 8, C_YELL);
    txtb(32, y + 5, "Start", C_BLACK);

    /* window buttons (non-dialog apps, up to 3) */
    for (i = z_n - 1; i >= 0 && n < 3; i--) {
        int a = zord[i];
        int press = (a == active_app());
        if (is_dialog(a)) continue;
        box3d(106 + n * 112, y + 4, 110, TASKBAR_H - 6, press, press ? C_LIGHT : C_GRAY);
        txt(106 + n * 112 + 8, y + 5, app_short[a], C_BLACK);
        n++;
    }

    /* Input Panel tray button */
    box3d(526, y + 4, 30, TASKBAR_H - 6, vk_visible(), C_GRAY);
    {
        int r, c;
        for (r = 0; r < 2; r++)
            for (c = 0; c < 4; c++)
                fill(531 + c * 5, y + 11 + r * 6, 3, 3, C_BLACK);
    }

    frame3d(560, y + 4, 76, TASKBAR_H - 6, 1);
    t = time(NULL);
    tm = localtime(&t);
    if (tm) snprintf(clk, sizeof(clk), "%02d:%02d", tm->tm_hour, tm->tm_min);
    else snprintf(clk, sizeof(clk), "--:--");
    txt(568, y + 5, clk, C_BLACK);
}

/* ------------------------------------------------------------------ */
/* window rendering                                                    */
/* ------------------------------------------------------------------ */

static void tb_btn(int x, int y, int kind)
{
    box3d(x, y, 22, 18, 0, C_GRAY);
    if (kind == 0) {                       /* maximize */
        fill(x + 5, y + 4, 12, 1, C_BLACK);
        fill(x + 5, y + 4, 1, 9, C_BLACK);
        fill(x + 16, y + 4, 1, 9, C_BLACK);
        fill(x + 5, y + 12, 12, 1, C_BLACK);
    } else {                               /* close */
        int i;
        for (i = 0; i < 9; i++) {
            fill(x + 6 + i, y + 4 + i, 2, 2, C_BLACK);
            fill(x + 15 - i, y + 4 + i, 2, 2, C_BLACK);
        }
    }
}

static void draw_app_client(int a, int x, int y, int w, int h)
{
    switch (a) {
    case APP_EXPL:  draw_expl(x, y, w, h); break;
    case APP_CMD:   draw_cmd(x, y, w, h); break;
    case APP_NOTE:  draw_note(x, y, w, h); break;
    case APP_HELP:  draw_help(x, y, w, h); break;
    case APP_CALC:  draw_calc(x, y, w, h); break;
    case APP_MINE:  draw_mine(x, y, w, h); break;
    case APP_PAINT: draw_paint(x, y, w, h); break;
    case APP_CLOCK: draw_clock(x, y, w, h); break;
    case APP_CTRL:  draw_ctrl(x, y, w, h); break;
    case APP_RUN:   draw_run(x, y, w, h); break;
    case APP_SHUT:  draw_shut(x, y, w, h); break;
    default:        draw_about(x, y, w, h); break;
    }
}

static void draw_window(int a)
{
    int x, y, w, h, cx, cy, cw, ch;
    const char *t = title_str(a);
    int act = (a == active_app());
    uint16_t tbg = act ? C_NAVY : C_DGRAY;
    uint16_t tfg = act ? C_WHITE : C_GRAY;
    char tb[40];
    int maxch;

    win_rect(a, &x, &y, &w, &h);
    frame3d(x, y, w, h, 0);
    fill(x + 2, y + 2, w - 4, TITLE_H - 4, tbg);

    maxch = (w - 96) / CHW;
    if (maxch < 1) maxch = 1;
    if (maxch > (int)sizeof(tb) - 1) maxch = (int)sizeof(tb) - 1;
    {
        int i;
        for (i = 0; i < maxch && t[i]; i++) tb[i] = t[i];
        tb[i] = 0;
    }
    txtb(x + 8, y + 4, tb, tfg);

    tb_btn(x + w - 52, y + 4, 0);
    tb_btn(x + w - 27, y + 4, 1);

    win_client(a, &cx, &cy, &cw, &ch);
    clip_set(cx, cy, cw, ch);
    draw_app_client(a, cx, cy, cw, ch);
    clip_all();
}

static void draw_all(void)
{
    int i;
    clip_all();
    fill(0, 0, SW, SH, col_desktop);
    if (z_n == 0) draw_desktop();
    else for (i = z_n - 1; i >= 0; i--) draw_window(zord[i]);
    if (vk_visible()) draw_vkb();
    draw_taskbar();
    if (menu_open) draw_menu();
}

/* ------------------------------------------------------------------ */
/* input: dispatch                                                     */
/* ------------------------------------------------------------------ */

static void dispatch(int k)
{
    int d = top_dialog();

    if (d >= 0) {
        switch (d) {
        case APP_RUN:   run_key(k); break;
        case APP_SHUT:  shut_key(k); break;
        default:        about_key(k); break;
        }
        return;
    }
    if (k == K_START) {
        if (menu_open) menu_open = 0;
        else menu_open_start();
        dirty = 1;
        return;
    }
    if (menu_open) { menu_key(k); return; }
    if (moving) { move_key(k); return; }
    if (k == K_HELP) { open_app(APP_HELP); return; }
    if (k == K_NEXT) { switch_window(1); return; }
    if (k == K_PREV) { switch_window(-1); return; }
    if (k == K_CLOSE) { if (active_app() >= 0) close_app(active_app()); return; }
    if (k == K_MOVE) { begin_move(); return; }
    if (k == K_MAX)  { toggle_max(); return; }
    if (k == K_SAVE) {
        int a = active_app();
        if (a == APP_NOTE) note_save();
        else if (a == APP_PAINT) paint_save();
        return;
    }

    switch (active_app()) {
    case -1:        desk_key(k); break;
    case APP_EXPL:  expl_key(k); break;
    case APP_CMD:   cmd_key(k); break;
    case APP_NOTE:  note_key(k); break;
    case APP_CALC:  calc_key(k); break;
    case APP_MINE:  mine_key(k); break;
    case APP_PAINT: paint_key(k); break;
    case APP_CTRL:  ctrl_key(k); break;
    case APP_CLOCK: clock_key(k); break;
    default:        help_key(k); break;    /* Help window */
    }
}

/* ------------------------------------------------------------------ */
/* input: keyboard                                                     */
/* ------------------------------------------------------------------ */

static int hid_ascii(int k, int shift)
{
    static const char n12[] = "-=[]\\#;'`,./";
    static const char s12[] = "_+{}|~:\"~<>?";
    static const char sn[]  = "!@#$%^&*()";
    int up = shift ^ caps;
    if (k >= 0x04 && k <= 0x1D) return (up ? 'A' : 'a') + (k - 0x04);
    if (k >= 0x1E && k <= 0x26) return shift ? sn[k - 0x1E] : '1' + (k - 0x1E);
    if (k == 0x27) return shift ? ')' : '0';
    if (k == 0x2C) return ' ';
    if (k >= 0x2D && k <= 0x38) return shift ? s12[k - 0x2D] : n12[k - 0x2D];
    return 0;
}

static void kbd_press(int k, int shift, int ctrl, int alt)
{
    int c;
    if (k == 0x39) { caps = !caps; return; }
    if (ctrl || alt) {
        if (k == 0x29) push_ev(K_START);
        else if (k == 0x2B) push_ev(shift ? K_PREV : K_NEXT);
        else if (k == 0x1A) push_ev(K_CLOSE);              /* W */
        else if (k == 0x10) push_ev(K_MOVE);               /* M */
        else if (k == 0x0A) push_ev(K_MAX);                /* G */
        else if (k == 0x13) push_ev(K_SAVE);               /* S */
        return;
    }
    if (k == 0x3A) { push_ev(K_HELP); return; }            /* F1 */
    switch (k) {
    case 0x28: case 0x58: push_ev(K_ENTER); return;
    case 0x29: push_ev(K_ESC); return;
    case 0x2A: push_ev(K_BKSP); return;
    case 0x2B: push_ev(K_TAB); return;
    case 0x4C: push_ev(K_DEL); return;
    case 0x4A: push_ev(K_HOME); return;
    case 0x4B: push_ev(K_PGUP); return;
    case 0x4D: push_ev(K_END); return;
    case 0x4E: push_ev(K_PGDN); return;
    case 0x4F: push_ev(K_RIGHT); return;
    case 0x50: push_ev(K_LEFT); return;
    case 0x51: push_ev(K_DOWN); return;
    case 0x52: push_ev(K_UP); return;
    default: break;
    }
    c = hid_ascii(k, shift);
    if (c) push_ev(c);
}

static uint8_t kb_prev[256];
static int rep_k, rep_t;

static void poll_kbd(void)
{
    maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_KEYBOARD);
    kbd_state_t *ks = dev ? (kbd_state_t *)maple_dev_status(dev) : NULL;
    int k, sh, ctrl, alt;

    if (!ks) {
        if (has_kbd) { has_kbd = 0; vkb_user = -1; dirty = 1; }   /* unplugged: auto again */
        rep_k = 0;
        return;
    }
    if (!has_kbd) {                       /* just plugged in: ignore keys already down */
        has_kbd = 1;
        vkb_user = -1;                    /* plugged in: hide on-screen keyboard */
        dirty = 1;
        for (k = 0; k < 256; k++) kb_prev[k] = ks->matrix[k] ? 1 : 0;
        return;
    }

    sh   = (ks->shift_keys & (KBD_MOD_LSHIFT | KBD_MOD_RSHIFT)) != 0;
    ctrl = (ks->shift_keys & (KBD_MOD_LCTRL | KBD_MOD_RCTRL)) != 0;
    alt  = (ks->shift_keys & (KBD_MOD_LALT | KBD_MOD_RALT)) != 0;

    for (k = 4; k < 0x65; k++) {
        int down = ks->matrix[k] != 0;
        if (down && !kb_prev[k]) {
            kbd_press(k, sh, ctrl, alt);
            if (k != 0x39 && !ctrl && !alt) { rep_k = k; rep_t = 0; }
        }
        kb_prev[k] = (uint8_t)down;
    }

    if (rep_k && ks->matrix[rep_k]) {     /* key repeat */
        rep_t++;
        if (rep_t > 24 && ((rep_t - 24) % 4) == 0) kbd_press(rep_k, sh, ctrl, alt);
    } else {
        rep_k = 0;
    }
}

/* ------------------------------------------------------------------ */
/* input: controller                                                   */
/* ------------------------------------------------------------------ */

enum { PB_A = 1, PB_B = 2, PB_X = 4, PB_Y = 8, PB_START = 16, PB_L = 32, PB_R = 64 };
typedef struct { int nx, ny; unsigned pressed; } PadIn;

static unsigned pad_prev;
static int nav_code = 4, nav_t;

static void poll_pad(const void *vin)
{
    PadIn *in = (PadIn *)(void *)(uintptr_t)vin;
    unsigned raw = 0, btn = 0;
    int joyx = 0, joyy = 0, dx = 0, dy = 0, code, fire = 0, i;

    for (i = 0; i < 4; i++) {
        maple_device_t *d = maple_enum_type(i, MAPLE_FUNC_CONTROLLER);
        cont_state_t *st;
        if (!d) break;
        st = (cont_state_t *)maple_dev_status(d);
        if (!st) continue;
        raw |= st->buttons;
        if (abs(st->joyx) > abs(joyx)) joyx = st->joyx;
        if (abs(st->joyy) > abs(joyy)) joyy = st->joyy;
        if (st->ltrig > 128) btn |= PB_L;
        if (st->rtrig > 128) btn |= PB_R;
    }

    if ((raw & (CONT_A | CONT_B | CONT_X | CONT_Y | CONT_START)) ==
        (CONT_A | CONT_B | CONT_X | CONT_Y | CONT_START))
        arch_menu();

    if (raw & CONT_A) btn |= PB_A;
    if (raw & CONT_B) btn |= PB_B;
    if (raw & CONT_X) btn |= PB_X;
    if (raw & CONT_Y) btn |= PB_Y;
    if (raw & CONT_START) btn |= PB_START;

    if ((raw & CONT_DPAD_LEFT) || joyx < -50) dx = -1;
    if ((raw & CONT_DPAD_RIGHT) || joyx > 50) dx = 1;
    if ((raw & CONT_DPAD_UP) || joyy < -50) dy = -1;      /* stick up = negative */
    if ((raw & CONT_DPAD_DOWN) || joyy > 50) dy = 1;
    if (dx && dy) { if (abs(joyx) >= abs(joyy)) dy = 0; else dx = 0; }

    code = (dx + 1) * 3 + (dy + 1);
    if (code == 4) nav_t = 0;
    else if (code != nav_code) { fire = 1; nav_t = 0; }
    else { nav_t++; if (nav_t >= 20 && ((nav_t - 20) % 5) == 0) fire = 1; }
    nav_code = code;

    in->nx = fire ? dx : 0;
    in->ny = fire ? dy : 0;
    in->pressed = btn & ~pad_prev;
    pad_prev = btn;
}

static void handle_pad(const void *vin)
{
    const PadIn *in = (const PadIn *)vin;
    if (in->pressed & PB_L) push_ev(K_PREV);
    if (in->pressed & PB_R) push_ev(K_NEXT);
    if (in->pressed & PB_START) push_ev(K_START);

    if (menu_open || !vk_visible()) {
        if (in->ny < 0) push_ev(K_UP);
        if (in->ny > 0) push_ev(K_DOWN);
        if (in->nx < 0) push_ev(K_LEFT);
        if (in->nx > 0) push_ev(K_RIGHT);
        if (in->pressed & PB_A) push_ev(K_ENTER);
        if (in->pressed & PB_B) push_ev(K_ESC);
        if (in->pressed & PB_Y) push_ev(K_BKSP);
        if (in->pressed & PB_X) {
            int d = top_dialog();
            if (!d) {
                if (is_text_app()) vkb_user = 1;
                else if (active_app() >= 0) toggle_max();
            }
        }
    } else {
        if (in->nx || in->ny) vk_move(in->nx, in->ny);
        if (in->pressed & PB_A) vk_press();
        if (in->pressed & PB_B) push_ev(K_BKSP);
        if (in->pressed & PB_Y) push_ev(' ');
        if (in->pressed & PB_X) { vk_shift = !vk_shift; dirty = 1; }
    }
}

/* ------------------------------------------------------------------ */
/* sounds (AICA square-wave beeps via WAV + snd_sfx)                   */
/* ------------------------------------------------------------------ */

#if USE_SOUND

static sfxhnd_t sfx_click, sfx_err, sfx_start;
static int snd_ok;

static void wav_write(const char *path, const uint8_t *samples, int n)
{
    uint8_t h[44];
    uint32_t dsz = (uint32_t)n, rsz = dsz + 36;
    file_t f;

    memset(h, 0, sizeof(h));
    h[0] = 'R'; h[1] = 'I'; h[2] = 'F'; h[3] = 'F';
    h[4] = (uint8_t)rsz; h[5] = (uint8_t)(rsz >> 8); h[6] = (uint8_t)(rsz >> 16); h[7] = (uint8_t)(rsz >> 24);
    h[8] = 'W'; h[9] = 'A'; h[10] = 'V'; h[11] = 'E';
    h[12] = 'f'; h[13] = 'm'; h[14] = 't'; h[15] = ' ';
    h[16] = 16; h[20] = 1; h[22] = 1;                   /* PCM, mono */
    h[24] = 0x22; h[25] = 0x56;                         /* 22050 Hz */
    h[28] = 0x22; h[29] = 0x56;                         /* byte rate */
    h[34] = 8;                                          /* 8 bit */
    h[36] = 'd'; h[37] = 'a'; h[38] = 't'; h[39] = 'a';
    h[40] = (uint8_t)dsz; h[41] = (uint8_t)(dsz >> 8); h[42] = (uint8_t)(dsz >> 16); h[43] = (uint8_t)(dsz >> 24);

    f = fs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (f == FILEHND_INVALID) return;
    fs_write(f, h, 44);
    fs_write(f, samples, (size_t)n);
    fs_close(f);
}

static void mk_tone(uint8_t *b, int n, int hz, int amp)
{
    int i;
    for (i = 0; i < n; i++)
        b[i] = (uint8_t)(((i * hz) % 22050) < 11025 ? 128 + amp : 128 - amp);
}

static void snd_init(void)
{
    static uint8_t t1[2400], t2[9000], t3[14900];

    spu_init();

    mk_tone(t1, 2205, 1600, 50);                        /* click 100 ms */
    mk_tone(t2, 8820, 420, 60);                         /* error 400 ms */
    mk_tone(t3, 14700, 660, 55);                        /* startup 667 ms */
    wav_write("/ram/ce_click.wav", t1, 2205);
    wav_write("/ram/ce_error.wav", t2, 8820);
    wav_write("/ram/ce_start.wav", t3, 14700);

    sfx_click = snd_sfx_load("/ram/ce_click.wav");
    sfx_err = snd_sfx_load("/ram/ce_error.wav");
    sfx_start = snd_sfx_load("/ram/ce_start.wav");
    snd_ok = (sfx_click != SFXHND_INVALID && sfx_err != SFXHND_INVALID &&
              sfx_start != SFXHND_INVALID);
}

static void snd_play(sfxhnd_t h)
{
    if (snd_ok && snd_enabled) snd_sfx_play(h, 160, 0);
}

static void snd_click(void) { snd_play(sfx_click); }
static void snd_err(void)   { snd_play(sfx_err); }
static void snd_start(void) { snd_play(sfx_start); }

#else

static void snd_init(void) {}
static void snd_click(void) {}
static void snd_err(void) {}
static void snd_start(void) {}

#endif

/* ------------------------------------------------------------------ */
/* boot splash                                                         */
/* ------------------------------------------------------------------ */

static void splash(void)
{
    int f;

    for (f = 0; f < 170; f++) {
        int dots = (f / 25) % 4;
        clip_all();
        fill(0, 0, SW, SH, C_BLACK);
        fill(SW / 2 - 44, 120, 42, 42, C_RED);
        fill(SW / 2 + 2, 120, 42, 42, C_GREEN);
        fill(SW / 2 - 44, 164, 42, 42, RGB(0, 80, 255));
        fill(SW / 2 + 2, 164, 42, 42, C_YELL);
        txt(SW / 2 - text_w("Microsoft") / 2, 226, "Microsoft", C_GRAY);
        txt2x(SW / 2 - 120, 252, "Windows CE", C_WHITE);
        txt(SW / 2 - text_w("Dreamcast Edition") / 2, 310, "Dreamcast Edition", C_TEAL);
        txt(SW / 2 - text_w("Version 2.00 (Build 2128)") / 2, 340, "Version 2.00 (Build 2128)", C_DGRAY);
        txt(SW / 2 - text_w("Starting Windows CE") / 2, 430, "Starting Windows CE", C_GRAY);
        fill(SW / 2 + text_w("Starting Windows CE") / 2 + 6, 438, dots * 10, 6, C_GRAY);
        memcpy(vram_s, fb, sizeof(fb));
    }
}

/* ------------------------------------------------------------------ */
/* screensaver: flying windows                                         */
/* ------------------------------------------------------------------ */

#define SS_AFTER 3600            /* 60 s at 60 fps */

typedef struct { int x, y, dx, dy; } SSw;
static SSw ssw[16];
static int ss_on;

static void ss_reset(void)
{
    int i;
    for (i = 0; i < 16; i++) {
        ssw[i].x = rand() % SW;
        ssw[i].y = rand() % WORK_BOTTOM;
        do ssw[i].dx = rand() % 5 - 2; while (ssw[i].dx == 0);
        do ssw[i].dy = rand() % 5 - 2; while (ssw[i].dy == 0);
    }
}

static void draw_saver(void)
{
    int i;
    clip_all();
    fill(0, 0, SW, SH, C_BLACK);
    for (i = 0; i < 16; i++) {
        SSw *s = &ssw[i];
        s->x += s->dx;
        s->y += s->dy;
        if (s->x < -30) s->x = SW;
        if (s->x > SW) s->x = -30;
        if (s->y < -24) s->y = WORK_BOTTOM;
        if (s->y > WORK_BOTTOM) s->y = -24;
        fill(s->x, s->y, 30, 22, C_DGRAY);
        frame3d(s->x, s->y, 30, 22, 0);
        fill(s->x + 2, s->y + 2, 26, 4, C_NAVY);
        fill(s->x + 2, s->y + 6, 26, 14, C_TEAL);
    }
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

static void shell_init(void)
{
    vid_set_mode(DM_640x480, PM_RGB565);
    glyph_init();
    snd_init();
    srand((unsigned)time(NULL));
    splash();
    snd_start();

    cmd_print("Microsoft(R) Windows CE Version 2.00 (Build 2128)");
    cmd_print("for Sega Dreamcast. All rights reserved.");
    cmd_print("Type HELP for a list of commands.");
    cmd_print("");
    make_program_files();
    ex_load("/");
    hist_idx = hist_cnt = 0;
    desk_sel = 0;
    last_input = 0;
    dirty = 1;
}

static void shell_frame(void)
{
    PadIn in;
    int e, any = 0, b;

    vid_waitvbl();
    frame_no++;

    poll_kbd();
    poll_pad(&in);

    if (powered_down) {
        while ((e = pop_ev()) != 0)
            if (e == K_START) arch_reboot();
        clip_all();
        fill(0, 0, SW, SH, C_BLACK);
        txt(SW / 2 - text_w("Windows CE has been shut down.") / 2, 200,
            "Windows CE has been shut down.", C_GRAY);
        txt(SW / 2 - text_w("Press Start to restart.") / 2, 240,
            "Press Start to restart.", C_DGRAY);
        memcpy(vram_s, fb, sizeof(fb));
        return;
    }

    handle_pad(&in);
    while ((e = pop_ev()) != 0) { any = 1; dispatch(e); }
    if (any) { dirty = 1; last_input = frame_no; }
    if (in.pressed || in.nx || in.ny) last_input = frame_no;

    if (ss_on) {
        if (any || in.pressed || in.nx || in.ny) { ss_on = 0; dirty = 1; return; }
        draw_saver();
        memcpy(vram_s, fb, sizeof(fb));
        return;
    }
    if (!menu_open && top_dialog() < 0 && !moving && z_n >= 0 &&
        (frame_no - last_input) > SS_AFTER) {
        ss_on = 1;
        ss_reset();
    }

    b = (int)((frame_no / 30) & 1);
    if (b != blink) {
        blink = b;
        if (is_text_app()) dirty = 1;
    }
    if ((frame_no % 30) == 0) dirty = 1;      /* keeps the clock fresh */

    /* minesweeper timer */
    if (active_app() == APP_MINE && ms_built && !ms_dead && !ms_won &&
        (frame_no % 60) == 0) {
        ms_time++;
        dirty = 1;
    }

    if (dirty) {
        draw_all();
        memcpy(vram_s, fb, sizeof(fb));
        dirty = 0;
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    shell_init();
    for (;;) shell_frame();
    return 0;
}
