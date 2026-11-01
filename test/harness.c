/*
 * Self-test for the plugin outside the game. Loads build\dbghelp.dll into this
 * process (which imports QueryPerformanceCounter and Sleep just like FTL does),
 * changes the speed through the test export and checks:
 *   - the hooked clock runs at the requested speed and never jumps,
 *   - Sleep() is shortened by the same factor,
 *   - dbghelp calls are forwarded to the real system dbghelp.dll.
 * With --host-check it instead checks that nothing is hooked outside FTLGame.exe.
 */
#include <windows.h>
#include <stdarg.h>

static BOOL (WINAPI *raw_qpc)(LARGE_INTEGER *);
static double qpf;
static int failures;

static void out(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    DWORD n, w;
    va_start(ap, fmt);
    n = wvsprintfA(buf, fmt, ap);
    va_end(ap);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, &w, NULL);
}

static LONGLONG raw_now(void)
{
    LARGE_INTEGER t;
    raw_qpc(&t);
    return t.QuadPart;
}

static LONGLONG game_now(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);         /* through our import table, like the game */
    return t.QuadPart;
}

/* Values in thousandths; ok when |got - want| <= tolerance. */
static void check(const char *what, int got, int want, int tolerance)
{
    int ok = got >= want - tolerance && got <= want + tolerance;
    out("  %-34s got %5d  want %5d  %s\r\n", what, got, want, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

static int clock_ratio(void)
{
    LONGLONG r0 = raw_now(), g0 = game_now(), r1, g1;
    while (raw_now() - r0 < (LONGLONG)(qpf / 2))
        ;
    r1 = raw_now();
    g1 = game_now();
    return (int)((double)(g1 - g0) / (double)(r1 - r0) * 1000 + 0.5);
}

static int sleep_ms(DWORD ms)
{
    LONGLONG r0 = raw_now();
    Sleep(ms);
    return (int)((double)(raw_now() - r0) * 1000 / qpf + 0.5);
}

void harness_main(void)
{
    static const int speeds[] = {1000, 3000, 8000, 500, 1000};
    char path[MAX_PATH], *p, *slash;
    HMODULE dll;
    LARGE_INTEGER f;
    void (*set_speed)(int);
    int host_check = 0, i;

    for (p = GetCommandLineA(); *p; p++)
        if (p[0] == '-' && p[1] == '-' && p[2] == 'h')
            host_check = 1;
    raw_qpc = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "QueryPerformanceCounter");
    QueryPerformanceFrequency(&f);
    qpf = (double)f.QuadPart;
    if (!host_check)
        SetEnvironmentVariableA("FTL_SPEED_ANY_HOST", "1");

    GetModuleFileNameA(NULL, path, MAX_PATH);
    for (slash = p = path; *p; p++)
        if (*p == '\\')
            slash = p + 1;
    lstrcpynA(slash, "dbghelp.dll", MAX_PATH - (int)(slash - path));
    dll = LoadLibraryA(path);
    if (!dll) {
        out("could not load %s (error %d)\r\n", path, (int)GetLastError());
        ExitProcess(2);
    }
    Sleep(500);                          /* let the plugin's thread hook us */
    set_speed = (void (*)(int))(void *)GetProcAddress(dll, "ftl_speed_set");
    if (!set_speed) {
        out("ftl_speed_set export missing\r\n");
        ExitProcess(2);
    }

    if (host_check) {
        out("Host check (not FTLGame.exe, so nothing should be hooked):\r\n");
        set_speed(4000);
        check("clock speed at 4x request", clock_ratio(), 1000, 20);
        check("Sleep(200) real ms", sleep_ms(200) * 1000, 200 * 1000, 20 * 1000);
        set_speed(1000);
    } else {
        for (i = 0; i < (int)(sizeof speeds / sizeof speeds[0]); i++) {
            LONGLONG before, after, raw_before, raw_after;
            int s = speeds[i], old = i ? speeds[i - 1] : 1000;
            out("Speed %d.%03dx:\r\n", s / 1000, s % 1000);
            raw_before = raw_now();
            before = game_now();
            set_speed(s);
            after = game_now();
            raw_after = raw_now();
            /* the gap around the change runs at the old speed; anything else is a jump */
            check("clock jump on change (ms)",
                  (int)(((double)(after - before) - (double)(raw_after - raw_before) * old / 1000)
                        * 1000000 / qpf), 0, 1000);
            check("clock speed", clock_ratio(), s, 20);
            /* ~16 ms Windows timer granularity */
            check("Sleep(300) real ms", sleep_ms(300) * 1000, 300 * 1000000 / s, 20 * 1000);
        }
        {
            BOOL (WINAPI *sym_init)(HANDLE, PCSTR, BOOL) = (void *)GetProcAddress(dll, "SymInitialize");
            BOOL (WINAPI *sym_cleanup)(HANDLE) = (void *)GetProcAddress(dll, "SymCleanup");
            void *(WINAPI *api_version)(void) = (void *)GetProcAddress(dll, "ImagehlpApiVersion");
            void *version = api_version ? api_version() : NULL;
            HMODULE owner = NULL;
            out("dbghelp forwarding:\r\n");
            check("SymInitialize returns TRUE", sym_init && sym_init(GetCurrentProcess(), NULL, FALSE), 1, 0);
            check("SymCleanup returns TRUE", sym_cleanup && sym_cleanup(GetCurrentProcess()), 1, 0);
            check("ImagehlpApiVersion non-NULL", version != NULL, 1, 0);
            /* the version struct must live in the real dbghelp, not in the plugin */
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)version, &owner);
            check("answered by system dbghelp", owner != NULL && owner != dll, 1, 0);
        }
    }
    Sleep(200);                          /* let the plugin flush its log */
    out(failures ? "%d check(s) FAILED\r\n" : "All checks passed\r\n", failures);
    ExitProcess(failures ? 1 : 0);
}
