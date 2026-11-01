/*
 * FTL Speed - fast-forward for FTL: Faster Than Light (Steam, Windows, 32-bit).
 *
 * Ships as dbghelp.dll in the game folder. FTLGame.exe imports dbghelp.dll, so
 * Windows loads this file instead of the system copy; every dbghelp export is
 * forwarded to the real system dbghelp.dll by the stubs build.ps1 generates.
 *
 * How the speed-up works: each frame FTL advances the game by the time since
 * the previous frame, capped at 31.25 ms. So the game only runs N times faster
 * if its clock runs N times faster AND it gets enough frames. We hook the
 * game's clock (QueryPerformanceCounter) to run N times faster, shorten its
 * Sleep() calls by the same factor so its frame limiter lets more frames
 * through, and switch V-sync off while sped up so the monitor's refresh rate
 * does not cap the frame rate.
 */
#include <windows.h>
#include <stdarg.h>
#include "proxy_names.h"

#define PLUGIN_NAME "FTL Speed plugin 1.0"
#define MAX_STEPS 16

/* ------------------------------------------------------------------ */
/* Freestanding helpers (no C runtime is linked)                      */

void *memset(void *dst, int c, size_t n)
{
    unsigned char *p = dst;
    while (n--)
        *p++ = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

static int bytes_equal(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    while (n--)
        if (*x++ != *y++)
            return 0;
    return 1;
}

static char *str_end(char *s)
{
    while (*s)
        s++;
    return s;
}

static void str_append(char *dst, const char *src, size_t cap)
{
    size_t n = lstrlenA(dst);
    while (*src && n + 1 < cap)
        dst[n++] = *src++;
    dst[n] = 0;
}

/* ASCII-only, unlike lstrcmpiA, which follows the user's locale: with a
 * Turkish locale "GDI32.dll" and "gdi32.dll" compare as different. */
static int ascii_ieq(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
        char y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (x != y)
            return 0;
        if (!x)
            return 1;
    }
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char *trim(char *s)
{
    char *e;
    while (is_space(*s))
        s++;
    e = str_end(s);
    while (e > s && is_space(e[-1]))
        *--e = 0;
    return s;
}

/* Parses a non-negative decimal like "1.5"; returns -1 if there is no number. */
static double parse_number(const char **ps)
{
    const char *s = *ps;
    double v = 0, scale = 1;
    int any = 0;
    while (is_space(*s))
        s++;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s++ - '0');
        any = 1;
    }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            scale /= 10;
            v += (*s++ - '0') * scale;
            any = 1;
        }
    }
    *ps = s;
    return any ? v : -1;
}

/* "2" or "1.5" (one decimal). */
static void format_speed(char *out, double v)
{
    int tenths = (int)(v * 10 + 0.5);
    if (tenths % 10 == 0)
        wsprintfA(out, "%d", tenths / 10);
    else
        wsprintfA(out, "%d.%d", tenths / 10, tenths % 10);
}

/* ------------------------------------------------------------------ */
/* Logging                                                            */

static HMODULE g_self;
static char g_dir[MAX_PATH];             /* folder of this DLL, with trailing backslash */
static HANDLE g_log = INVALID_HANDLE_VALUE;

static void log_open(void)
{
    char path[MAX_PATH + 32];
    lstrcpynA(path, g_dir, MAX_PATH);
    str_append(path, "ftl_speed.log", sizeof path);
    g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
}

/* Lines are queued and written by the worker thread (log_flush): a file
 * write can take tens of ms, which would show as a stutter on the game's
 * render thread. */
static char g_logq[8192];
static DWORD g_logq_len;
static volatile LONG g_logq_lock;

static void log_line(const char *fmt, ...)
{
    char buf[1100];
    SYSTEMTIME t;
    va_list ap;
    DWORD n;
    GetLocalTime(&t);
    n = wsprintfA(buf, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_start(ap, fmt);
    n += wvsprintfA(buf + n, fmt, ap);
    va_end(ap);
    buf[n++] = '\r';
    buf[n++] = '\n';
    while (InterlockedCompareExchange(&g_logq_lock, 1, 0) != 0)
        YieldProcessor();
    if (g_logq_len + n <= sizeof g_logq) {
        memcpy(g_logq + g_logq_len, buf, n);
        g_logq_len += n;
    }
    InterlockedExchange(&g_logq_lock, 0);
}

static void log_flush(void)
{
    static char out[sizeof g_logq];
    DWORD n, written;
    while (InterlockedCompareExchange(&g_logq_lock, 1, 0) != 0)
        YieldProcessor();
    n = g_logq_len;
    memcpy(out, g_logq, n);
    g_logq_len = 0;
    InterlockedExchange(&g_logq_lock, 0);
    if (n && g_log != INVALID_HANDLE_VALUE)
        WriteFile(g_log, out, n, &written, NULL);
}

/* ------------------------------------------------------------------ */
/* Settings (ftl_speed.ini next to the DLL)                           */

static double g_steps[MAX_STEPS];
static int g_nsteps;
static int g_key_faster = VK_PRIOR, g_key_slower = VK_NEXT, g_key_toggle = VK_HOME;
static int g_overlay = 1;
static int g_corner = 5;                 /* 0 TL, 1 TC, 2 TR, 3 BL, 4 BC, 5 BR */
static int g_overlay_scale;              /* pixels per font dot, 0 = auto */
static int g_vsync_off = 1;

static const struct { const char *name; int vk; } g_key_names[] = {
    {"PageUp", VK_PRIOR}, {"PageDown", VK_NEXT}, {"Home", VK_HOME}, {"End", VK_END},
    {"Insert", VK_INSERT}, {"Delete", VK_DELETE}, {"Backspace", VK_BACK}, {"Tab", VK_TAB},
    {"Pause", VK_PAUSE}, {"ScrollLock", VK_SCROLL}, {"Up", VK_UP}, {"Down", VK_DOWN},
    {"Left", VK_LEFT}, {"Right", VK_RIGHT}, {"NumPlus", VK_ADD}, {"NumMinus", VK_SUBTRACT},
    {"NumMultiply", VK_MULTIPLY}, {"NumDivide", VK_DIVIDE}, {"NumDecimal", VK_DECIMAL},
    {"none", 0},
};

static int parse_key(char *s)
{
    unsigned i;
    s = trim(s);
    for (i = 0; i < sizeof g_key_names / sizeof g_key_names[0]; i++)
        if (ascii_ieq(s, g_key_names[i].name))
            return g_key_names[i].vk;
    if ((s[0] == 'F' || s[0] == 'f') && s[1] >= '1' && s[1] <= '9') {
        const char *p = s + 1;
        int n = (int)parse_number(&p);
        if (!*p && n >= 1 && n <= 24)
            return VK_F1 + n - 1;
    }
    if ((s[0] == 'N' || s[0] == 'n') && (s[1] == 'u' || s[1] == 'U') && (s[2] == 'm' || s[2] == 'M')
        && s[3] >= '0' && s[3] <= '9' && !s[4])
        return VK_NUMPAD0 + s[3] - '0';
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        int v = 0;
        for (s += 2; *s; s++) {
            char c = *s;
            v = v * 16 + (c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0');
        }
        return v & 0xff;
    }
    if (s[0] && !s[1]) {
        char c = s[0];
        if (c >= 'a' && c <= 'z')
            c -= 32;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            return c;
    }
    return -1;
}

static void parse_steps(const char *s)
{
    int i, j;
    g_nsteps = 0;
    for (;;) {
        double v = parse_number(&s);
        if (v >= 0.1 && v <= 32 && g_nsteps < MAX_STEPS)
            g_steps[g_nsteps++] = v;
        while (*s && *s != ',')
            s++;
        if (!*s++)
            break;
    }
    if (g_nsteps < MAX_STEPS)
        g_steps[g_nsteps++] = 1;         /* 1x is always available */
    for (i = 1; i < g_nsteps; i++)       /* sort ascending */
        for (j = i; j > 0 && g_steps[j - 1] > g_steps[j]; j--) {
            double t = g_steps[j];
            g_steps[j] = g_steps[j - 1];
            g_steps[j - 1] = t;
        }
    for (i = j = 0; i < g_nsteps; i++)   /* drop duplicates */
        if (j == 0 || (int)(g_steps[i] * 1000 + 0.5) != (int)(g_steps[j - 1] * 1000 + 0.5))
            g_steps[j++] = g_steps[i];
    g_nsteps = j;
}

static int read_key(const char *ini, const char *name, const char *def)
{
    char buf[64];
    int vk;
    GetPrivateProfileStringA("keys", name, def, buf, sizeof buf, ini);
    vk = parse_key(buf);
    if (vk < 0) {
        log_line("Unknown key name '%s' for %s, using %s", buf, name, def);
        vk = parse_key(lstrcpynA(buf, def, sizeof buf));
    }
    return vk;
}

static void load_config(void)
{
    static const char *corners[] = {"top-left", "top-center", "top-right",
                                    "bottom-left", "bottom-center", "bottom-right"};
    char ini[MAX_PATH + 32], buf[256];
    int i;
    lstrcpynA(ini, g_dir, MAX_PATH);
    str_append(ini, "ftl_speed.ini", sizeof ini);

    g_key_faster = read_key(ini, "faster", "PageUp");
    g_key_slower = read_key(ini, "slower", "PageDown");
    g_key_toggle = read_key(ini, "toggle", "Home");

    GetPrivateProfileStringA("speed", "steps", "1,1.5,2,3,4,6,8", buf, sizeof buf, ini);
    parse_steps(buf);

    g_overlay = GetPrivateProfileIntA("overlay", "show", 1, ini);
    g_overlay_scale = GetPrivateProfileIntA("overlay", "scale", 0, ini);
    GetPrivateProfileStringA("overlay", "position", "bottom-right", buf, sizeof buf, ini);
    for (i = 0; i < 6; i++)
        if (ascii_ieq(trim(buf), corners[i]))
            g_corner = i;

    g_vsync_off = GetPrivateProfileIntA("advanced", "disable_vsync_when_fast", 1, ini);
}

/* ------------------------------------------------------------------ */
/* Scaled clock                                                       */

static volatile LONG g_target = 1000;   /* requested speed x1000 */
static LONGLONG g_qpf;                  /* performance counter ticks per second */

typedef struct {
    volatile LONG lock;
    LONG speed;                          /* speed of the current segment x1000, 0 = unused */
    LONGLONG base_real, base_fake;
} Clock;

static Clock g_clock;

/* Maps a real counter value to the game's sped-up counter. On a speed change
 * the fake clock is re-based where it currently is, so it never jumps. */
static LONGLONG clock_fake(Clock *c, LONGLONG real)
{
    LONG target = g_target;
    LONGLONG fake;
    while (InterlockedCompareExchange(&c->lock, 1, 0) != 0)
        YieldProcessor();
    if (c->speed == 0) {
        c->speed = 1000;
        c->base_real = c->base_fake = real;
    }
    fake = c->base_fake + (LONGLONG)((double)(real - c->base_real) * c->speed / 1000.0);
    if (target != c->speed) {
        c->base_real = real;
        c->base_fake = fake;
        c->speed = target;
    }
    InterlockedExchange(&c->lock, 0);
    return fake;
}

/* ------------------------------------------------------------------ */
/* FTL internals (only used for the on-screen speed readout)          */

/* CFPS::speedFactor in FTLGame.exe: game time advanced by the last frame,
 * in 1/16 s. Only located on the build we checked (Steam 1.6.x, see
 * find_cfps); NULL otherwise, and the readout falls back to an estimate. */
static volatile float *g_cfps_speed;

static void find_cfps(HMODULE exe)
{
    static const BYTE store_speed[] = {0xF3, 0x0F, 0x11, 0x43, 0x18}; /* movss [ebx+18h], xmm0 */
    BYTE *base = (BYTE *)exe;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    DWORD size = nt->OptionalHeader.SizeOfImage;
    BYTE *mov_cfps = base + 0x2363;     /* mov ecx, offset CFPS ; then call CFPS::OnLoop */
    BYTE *store = base + 0x1ba113;      /* inside CFPS::OnLoop */
    DWORD cfps;
    if (size < 0x51a000)
        return;
    cfps = *(DWORD *)(mov_cfps + 1);
    if (mov_cfps[0] != 0xB9 || cfps != (DWORD)base + 0x5196a0 || !bytes_equal(store, store_speed, 5))
        return;
    g_cfps_speed = (volatile float *)(cfps + 0x18);
}

/* ------------------------------------------------------------------ */
/* Hooks                                                              */

static BOOL (WINAPI *real_QueryPerformanceCounter)(LARGE_INTEGER *);
static VOID (WINAPI *real_Sleep)(DWORD);
static BOOL (WINAPI *real_SwapBuffers)(HDC);

static BOOL WINAPI hook_QueryPerformanceCounter(LARGE_INTEGER *out)
{
    LARGE_INTEGER now;
    if (!out || !real_QueryPerformanceCounter(&now))
        return real_QueryPerformanceCounter(out);
    out->QuadPart = clock_fake(&g_clock, now.QuadPart);
    return TRUE;
}

static VOID WINAPI hook_Sleep(DWORD ms)
{
    LONG target = g_target;
    if (target != 1000 && ms != 0 && ms != INFINITE) {
        ms = (DWORD)(ms * 1000.0 / target + 0.5);
        if (ms == 0)
            ms = 1;                      /* keep it a real sleep, not a busy spin */
    }
    real_Sleep(ms);
}

/* OpenGL, resolved on the render thread the first time SwapBuffers runs. */
#define GL_SCISSOR_BOX            0x0C10
#define GL_SCISSOR_TEST           0x0C11
#define GL_COLOR_CLEAR_VALUE      0x0C22
#define GL_COLOR_WRITEMASK        0x0C23
#define GL_COLOR_BUFFER_BIT       0x4000
#define GL_FRAMEBUFFER_BINDING    0x8CA6
#define GL_FRAMEBUFFER            0x8D40

static struct {
    int tried, ok, ext_tried;
    void (WINAPI *Scissor)(int, int, int, int);
    void (WINAPI *ClearColor)(float, float, float, float);
    void (WINAPI *Clear)(unsigned);
    void (WINAPI *Enable)(unsigned);
    void (WINAPI *Disable)(unsigned);
    unsigned char (WINAPI *IsEnabled)(unsigned);
    void (WINAPI *GetIntegerv)(unsigned, int *);
    void (WINAPI *GetFloatv)(unsigned, float *);
    void (WINAPI *GetBooleanv)(unsigned, unsigned char *);
    void (WINAPI *ColorMask)(unsigned char, unsigned char, unsigned char, unsigned char);
    HGLRC (WINAPI *GetCurrentContext)(void);
    PROC (WINAPI *GetProcAddress)(LPCSTR);
    BOOL (WINAPI *SwapIntervalEXT)(int);
    int (WINAPI *GetSwapIntervalEXT)(void);
    void (WINAPI *BindFramebuffer)(unsigned, unsigned);
} gl;

static void *wgl_proc(const char *name)
{
    INT_PTR p = (INT_PTR)gl.GetProcAddress(name);
    return (p > 3 && p != -1) ? (void *)p : NULL;
}

/* Returns 1 when OpenGL calls can be made on this thread right now. */
static int gl_init(void)
{
    if (!gl.tried) {
        HMODULE m = GetModuleHandleA("opengl32.dll");
        if (!m)
            return 0;                    /* not loaded yet; try again next frame */
        gl.tried = 1;
#define GLP(field, name) *(FARPROC *)&gl.field = GetProcAddress(m, name)
        GLP(Scissor, "glScissor");
        GLP(ClearColor, "glClearColor");
        GLP(Clear, "glClear");
        GLP(Enable, "glEnable");
        GLP(Disable, "glDisable");
        GLP(IsEnabled, "glIsEnabled");
        GLP(GetIntegerv, "glGetIntegerv");
        GLP(GetFloatv, "glGetFloatv");
        GLP(GetBooleanv, "glGetBooleanv");
        GLP(ColorMask, "glColorMask");
        GLP(GetCurrentContext, "wglGetCurrentContext");
        GLP(GetProcAddress, "wglGetProcAddress");
#undef GLP
        gl.ok = gl.Scissor && gl.ClearColor && gl.Clear && gl.Enable && gl.Disable && gl.IsEnabled
             && gl.GetIntegerv && gl.GetFloatv && gl.GetBooleanv && gl.ColorMask
             && gl.GetCurrentContext && gl.GetProcAddress;
        if (!gl.ok)
            log_line("OpenGL functions not found; overlay and V-sync control disabled");
    }
    if (!gl.ok || !gl.GetCurrentContext())
        return 0;
    if (!gl.ext_tried) {
        /* extension entry points need a current context */
        gl.ext_tried = 1;
        *(void **)&gl.SwapIntervalEXT = wgl_proc("wglSwapIntervalEXT");
        *(void **)&gl.GetSwapIntervalEXT = wgl_proc("wglGetSwapIntervalEXT");
        *(void **)&gl.BindFramebuffer = wgl_proc("glBindFramebuffer");
        if (!gl.BindFramebuffer)
            *(void **)&gl.BindFramebuffer = wgl_proc("glBindFramebufferEXT");
        log_line("OpenGL ready, V-sync control %s",
                 gl.SwapIntervalEXT && gl.GetSwapIntervalEXT ? "available" : "unavailable");
    }
    return 1;
}

/* V-sync caps frames at the monitor refresh rate, which caps the speed-up,
 * so it is off while sped up and restored afterwards. */
static int g_vsync_saved, g_vsync_changed;

static void update_vsync(void)
{
    if (!g_vsync_off || !gl.SwapIntervalEXT || !gl.GetSwapIntervalEXT)
        return;
    if (g_target > 1000) {
        int cur = gl.GetSwapIntervalEXT();
        if (cur != 0) {
            g_vsync_saved = cur;
            g_vsync_changed = 1;
            gl.SwapIntervalEXT(0);
            log_line("V-sync off while sped up (was %d)", cur);
        }
    } else if (g_vsync_changed) {
        gl.SwapIntervalEXT(g_vsync_saved);
        g_vsync_changed = 0;
        log_line("V-sync restored to %d", g_vsync_saved);
    }
}

/* Measures what speed the game actually runs at, once per real second. */
static int g_fps;
static double g_actual = 1;              /* measured game speed */
static int g_actual_valid;

static void frame_stats(void)
{
    static LONGLONG win_start, last_real, last_fake;
    static double game_ticks;
    static int frames, windows;
    LARGE_INTEGER now;
    LONGLONG fake, dfake;
    real_QueryPerformanceCounter(&now);
    fake = clock_fake(&g_clock, now.QuadPart);
    if (!win_start || now.QuadPart - last_real > g_qpf / 2) {
        /* first frame, or the game stopped drawing for a while (minimized) */
        win_start = now.QuadPart;
        game_ticks = 0;
        frames = 0;
        g_actual_valid = 0;
    } else {
        dfake = fake - last_fake;
        if (g_cfps_speed)
            game_ticks += *g_cfps_speed / 16.0 * g_qpf;
        else
            game_ticks += (double)(dfake < g_qpf / 32 ? dfake : g_qpf / 32);
        frames++;
    }
    last_real = now.QuadPart;
    last_fake = fake;
    if (now.QuadPart - win_start >= g_qpf) {
        double span = (double)(now.QuadPart - win_start);
        g_fps = (int)(frames * (double)g_qpf / span + 0.5);
        g_actual = game_ticks / span;
        g_actual_valid = 1;
        if (g_target != 1000 && ++windows % 10 == 1)
            log_line("target %d.%02dx, running at %d.%02dx (%s), %d fps",
                     g_target / 1000, g_target % 1000 / 10,
                     (int)g_actual, (int)(g_actual * 100) % 100,
                     g_cfps_speed ? "measured" : "estimated", g_fps);
        win_start = now.QuadPart;
        game_ticks = 0;
        frames = 0;
    }
}

/* 5x7 font, one byte per row, bit 4 = leftmost column. */
static const struct { char c; BYTE rows[7]; } g_font[] = {
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {'x', {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11}},
    {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}},
    {'>', {0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10}},
    {'<', {0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01}},
};

static const BYTE *glyph(char c)
{
    unsigned i;
    for (i = 0; i < sizeof g_font / sizeof g_font[0]; i++)
        if (g_font[i].c == c)
            return g_font[i].rows;
    return NULL;                         /* space */
}

static volatile DWORD g_show_until;      /* keep showing "1x" briefly after returning to normal */

static void draw_overlay(HDC dc)
{
    char text[32], a[8], b[8];
    float color[3] = {0.92f, 0.96f, 1.0f};
    LONG target = g_target;
    HWND wnd;
    RECT rc;
    int W, H, px, len, i, x0, y0, box_w, box_h, pad;
    unsigned char sc_on, mask[4];
    int sc_box[4], fb = 0;
    float clear[4];

    if (target == 1000 && (!g_show_until || (LONG)(GetTickCount() - g_show_until) >= 0))
        return;
    wnd = WindowFromDC(dc);
    if (!wnd || !GetClientRect(wnd, &rc) || rc.right <= 0 || rc.bottom <= 0)
        return;
    W = rc.right;
    H = rc.bottom;

    format_speed(b, target / 1000.0);
    if (target > 1000 && g_actual_valid && g_actual * 1000 < target * 0.9) {
        /* the PC can't draw enough frames: show the real speed in amber */
        format_speed(a, g_actual);
        wsprintfA(text, ">> %s/%sx", a, b);
        color[0] = 1.0f; color[1] = 0.72f; color[2] = 0.25f;
    } else if (target > 1000) {
        wsprintfA(text, ">> %sx", b);
    } else if (target < 1000) {
        wsprintfA(text, "<< %sx", b);
    } else {
        wsprintfA(text, "%sx", b);
    }

    len = lstrlenA(text);
    px = g_overlay_scale > 0 ? g_overlay_scale : H / 240;
    if (px < 2)
        px = 2;
    pad = 2 * px;
    box_w = (len * 6 - 1) * px + 2 * pad;
    box_h = 7 * px + 2 * pad;
    x0 = g_corner % 3 == 0 ? 3 * px : g_corner % 3 == 1 ? (W - box_w) / 2 : W - box_w - 3 * px;
    y0 = g_corner < 3 ? 3 * px : H - box_h - 3 * px;   /* from the top */

    sc_on = gl.IsEnabled(GL_SCISSOR_TEST);
    gl.GetIntegerv(GL_SCISSOR_BOX, sc_box);
    gl.GetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    gl.GetBooleanv(GL_COLOR_WRITEMASK, mask);
    if (gl.BindFramebuffer) {
        gl.GetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
        if (fb)
            gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    gl.Enable(GL_SCISSOR_TEST);
    gl.ColorMask(1, 1, 1, 1);

    /* Draw with scissored clears: no shaders, textures or vertex state touched. */
    gl.ClearColor(0.06f, 0.07f, 0.10f, 1.0f);
    gl.Scissor(x0, H - y0 - box_h, box_w, box_h);
    gl.Clear(GL_COLOR_BUFFER_BIT);
    gl.ClearColor(color[0], color[1], color[2], 1.0f);
    for (i = 0; i < len; i++) {
        const BYTE *rows = glyph(text[i]);
        int r, gx = x0 + pad + i * 6 * px;
        if (!rows)
            continue;
        for (r = 0; r < 7; r++) {
            int col = 0;
            while (col < 5) {
                int start;
                if (!(rows[r] & (0x10 >> col))) {
                    col++;
                    continue;
                }
                start = col;
                while (col < 5 && (rows[r] & (0x10 >> col)))
                    col++;
                gl.Scissor(gx + start * px, H - (y0 + pad + (r + 1) * px), (col - start) * px, px);
                gl.Clear(GL_COLOR_BUFFER_BIT);
            }
        }
    }

    if (fb)
        gl.BindFramebuffer(GL_FRAMEBUFFER, fb);
    gl.ColorMask(mask[0], mask[1], mask[2], mask[3]);
    gl.ClearColor(clear[0], clear[1], clear[2], clear[3]);
    gl.Scissor(sc_box[0], sc_box[1], sc_box[2], sc_box[3]);
    if (!sc_on)
        gl.Disable(GL_SCISSOR_TEST);
}

static BOOL WINAPI hook_SwapBuffers(HDC dc)
{
    if (dc && gl_init()) {
        frame_stats();
        update_vsync();
        if (g_overlay)
            draw_overlay(dc);
    }
    return real_SwapBuffers(dc);
}

/* ------------------------------------------------------------------ */
/* Import-table patching of FTLGame.exe                               */

static struct {
    const char *dll, *func;
    void *hook;
    void **real;
    void **slot;
} g_hooks[] = {
    {"kernel32.dll", "QueryPerformanceCounter", (void *)hook_QueryPerformanceCounter,
     (void **)&real_QueryPerformanceCounter, NULL},
    {"kernel32.dll", "Sleep", (void *)hook_Sleep, (void **)&real_Sleep, NULL},
    {"gdi32.dll", "SwapBuffers", (void *)hook_SwapBuffers, (void **)&real_SwapBuffers, NULL},
};

static void **find_iat_slot(HMODULE mod, const char *dll, const char *func)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *imp;
    if (!dir->VirtualAddress)
        return NULL;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); imp->Name; imp++) {
        IMAGE_THUNK_DATA *names, *slots;
        if (!ascii_ieq((char *)(base + imp->Name), dll))
            continue;
        slots = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        if (!imp->OriginalFirstThunk) {
            /* no name table: match the resolved address instead */
            HMODULE m = GetModuleHandleA(dll);
            DWORD want = m ? (DWORD)GetProcAddress(m, func) : 0;
            for (; want && slots->u1.Function; slots++)
                if (slots->u1.Function == want)
                    return (void **)&slots->u1.Function;
            continue;
        }
        names = (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk);
        for (; names->u1.AddressOfData; names++, slots++) {
            IMAGE_IMPORT_BY_NAME *ibn;
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            ibn = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (lstrcmpA((char *)ibn->Name, func) == 0)
                return (void **)&slots->u1.Function;
        }
    }
    return NULL;
}

static void install_hooks(HMODULE exe, int first)
{
    unsigned i;
    for (i = 0; i < sizeof g_hooks / sizeof g_hooks[0]; i++) {
        DWORD old;
        void *cur;
        if (!g_hooks[i].slot)
            g_hooks[i].slot = find_iat_slot(exe, g_hooks[i].dll, g_hooks[i].func);
        if (!g_hooks[i].slot) {
            if (first)
                log_line("Hook %s: not imported by the game, skipped", g_hooks[i].func);
            continue;
        }
        cur = *g_hooks[i].slot;
        if (cur == g_hooks[i].hook)
            continue;
        if (!VirtualProtect(g_hooks[i].slot, sizeof(void *), PAGE_READWRITE, &old))
            continue;
        *g_hooks[i].real = cur;          /* set before the hook can run */
        InterlockedExchangePointer(g_hooks[i].slot, g_hooks[i].hook);
        VirtualProtect(g_hooks[i].slot, sizeof(void *), old, &old);
        log_line("Hook %s: %s", g_hooks[i].func, first ? "installed" : "re-applied");
    }
}

/* ------------------------------------------------------------------ */
/* Speed control                                                      */

static LONG g_last_fast;                 /* speed the toggle key returns to */

static void set_target(LONG milli, const char *why)
{
    char s[8];
    if (milli == g_target)
        return;
    if (milli != 1000)
        g_last_fast = milli;
    InterlockedExchange(&g_target, milli);
    g_show_until = GetTickCount() + 2000;
    format_speed(s, milli / 1000.0);
    log_line("Speed %sx (%s)", s, why);
}

static LONG step_milli(int i)
{
    return (LONG)(g_steps[i] * 1000 + 0.5);
}

static void step_speed(int dir)
{
    LONG cur = g_target;
    int i;
    if (dir > 0) {
        for (i = 0; i < g_nsteps; i++)
            if (step_milli(i) > cur) {
                set_target(step_milli(i), "faster key");
                return;
            }
    } else {
        for (i = g_nsteps - 1; i >= 0; i--)
            if (step_milli(i) < cur) {
                set_target(step_milli(i), "slower key");
                return;
            }
    }
}

static void toggle_speed(void)
{
    int i;
    if (g_target != 1000) {
        set_target(1000, "toggle key");
        return;
    }
    if (!g_last_fast)
        for (i = 0; i < g_nsteps && !g_last_fast; i++)
            if (step_milli(i) > 1000)
                g_last_fast = step_milli(i);
    if (g_last_fast)
        set_target(g_last_fast, "toggle key");
}

/* For the test harness. */
__declspec(dllexport) void ftl_speed_set(int milli)
{
    set_target(milli, "API");
}

static int game_has_focus(void)
{
    HWND w = GetForegroundWindow();
    DWORD pid = 0;
    if (!w)
        return 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

static DWORD WINAPI worker(LPVOID unused)
{
    HMODULE exe = GetModuleHandleA(NULL);
    LARGE_INTEGER f;
    int keys[3], down[3] = {0, 0, 0};
    unsigned n;
    char list[MAX_STEPS * 8] = "";
    (void)unused;

    QueryPerformanceFrequency(&f);
    g_qpf = f.QuadPart;
    log_open();
    log_line("%s loaded", PLUGIN_NAME);
    load_config();
    for (n = 0; n < (unsigned)g_nsteps; n++) {
        char s[8];
        format_speed(s, g_steps[n]);
        str_append(list, n ? ", " : "", sizeof list);
        str_append(list, s, sizeof list);
    }
    log_line("Speeds: %s. Keys (virtual-key codes): faster 0x%02X, slower 0x%02X, toggle 0x%02X",
             list, g_key_faster, g_key_slower, g_key_toggle);
    find_cfps(exe);
    log_line("Game build %s", g_cfps_speed ? "recognized (speed readout is measured)"
                                           : "not recognized (speed readout is estimated)");
    install_hooks(exe, 1);
    log_flush();

    keys[0] = g_key_faster;
    keys[1] = g_key_slower;
    keys[2] = g_key_toggle;
    for (n = 1;; n++) {
        int k, focus;
        Sleep(10);
        if (n % 200 == 0)
            install_hooks(exe, 0);       /* something replaced a hook: put it back */
        if (n % 10 == 0)
            log_flush();
        focus = game_has_focus();
        for (k = 0; k < 3; k++) {
            int now = focus && keys[k] && (GetAsyncKeyState(keys[k]) & 0x8000);
            if (now && !down[k]) {
                if (k == 0)
                    step_speed(+1);
                else if (k == 1)
                    step_speed(-1);
                else
                    toggle_speed();
            }
            down[k] = now;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* dbghelp proxy: stubs in proxy_stubs.S call this on first use       */

void *g_proxy_fn[PROXY_COUNT];
static HMODULE g_real_dbghelp;

void *proxy_resolve(int i)
{
    if (!g_real_dbghelp) {
        char path[MAX_PATH + 16];
        UINT n = GetSystemDirectoryA(path, MAX_PATH);
        if (!n || n >= MAX_PATH)
            return NULL;
        str_append(path, "\\dbghelp.dll", sizeof path);
        g_real_dbghelp = LoadLibraryA(path);
        if (!g_real_dbghelp)
            return NULL;
    }
    g_proxy_fn[i] = (void *)GetProcAddress(g_real_dbghelp, g_proxy_names[i]);
    return g_proxy_fn[i];
}

/* ------------------------------------------------------------------ */

static int should_run(void)
{
    char path[MAX_PATH], *name, *p;
    char env[8];
    if (GetEnvironmentVariableA("FTL_SPEED_ANY_HOST", env, sizeof env))
        return 1;
    if (!GetModuleFileNameA(NULL, path, sizeof path))
        return 0;
    for (name = p = path; *p; p++)
        if (*p == '\\' || *p == '/')
            name = p + 1;
    return ascii_ieq(name, "FTLGame.exe");
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        char *p, *slash;
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(inst, g_dir, sizeof g_dir);
        for (slash = p = g_dir; *p; p++)
            if (*p == '\\')
                slash = p + 1;
        *slash = 0;
        if (should_run()) {
            /* hooks go in from a thread: it starts once the loader is done */
            HANDLE t = CreateThread(NULL, 0, worker, NULL, 0, NULL);
            if (t)
                CloseHandle(t);
        }
    }
    return TRUE;
}
