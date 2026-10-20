#include <windows.h>
#include <shellapi.h>
#include <string.h>
#include "config.h"
#include "device.h"
#include "input.h"
#include "log.h"

// Key indexes in the key bitmap of the K55 RGB PRO. For usual keys, the index is the HID usage code (A = 4).
#define KEY_FN 122

// K55D_VERSION comes from version.txt (see Makefile), without quotes.
#define WIDEN2(x) L ## #x
#define WIDEN(x) WIDEN2(x)
#define VERSION_W WIDEN(K55D_VERSION)
static const int key_g[G_KEY_COUNT] = { 131, 132, 133, 134, 135, 136 };

#define PING_INTERVAL_MS 5000
#define RETRY_INTERVAL_MS 2000

#define WM_TRAY (WM_APP + 1)
#define MENU_RELOAD 1
#define MENU_EXIT 2
#define MENU_SAVE 3
#define MENU_TITLE 4

static config cfg;
static CRITICAL_SECTION cfg_lock;
static HANDLE wake_event;
static volatile LONG want_restart, want_quit, want_save, connected;
static HANDLE device_thread, quit_event;
static NOTIFYICONDATAW tray;
static UINT taskbar_created_msg;

static int start(device *d)
{
    uint8_t colors[6][3] = { { 0 } };
    int hidden[2] = { -1, -1 };
    EnterCriticalSection(&cfg_lock);
    // LED 0 is the macro recording / Windows lock indicator: it stays off.
    for (int i = 1; i < 6; i++)
        memcpy(colors[i], cfg.color, 3);
    // The keyboard must not handle Fn itself when Fn acts as the Windows key.
    if (cfg.fn_as_win)
        hidden[0] = KEY_FN;
    LeaveCriticalSection(&cfg_lock);
    return dev_start_software_mode(d, colors, hidden);
}

static void notify(const wchar_t *text)
{
    NOTIFYICONDATAW n = tray;
    n.uFlags = NIF_INFO;
    wcscpy(n.szInfoTitle, L"k55d");
    wcsncpy(n.szInfo, text, sizeof n.szInfo / sizeof n.szInfo[0] - 1);
    n.dwInfoFlags = NIIF_INFO;
    if (!Shell_NotifyIconW(NIM_MODIFY, &n))
        log_msg("Cannot show the tray notification");
}

// Saves the configured color in the keyboard memory, then writes the software lighting again (the save turns it off).
static int save_hardware_color(device *d)
{
    uint8_t color[3];
    EnterCriticalSection(&cfg_lock);
    memcpy(color, cfg.color, 3);
    LeaveCriticalSection(&cfg_lock);
    if (dev_save_hardware_color(d, color) < 0) {
        alert("Cannot save the color in the keyboard memory. See k55d.log.");
        start(d);
        return -1;
    }
    notify(L"Color saved in the keyboard memory. If the keyboard shows the rainbow after you unplug it, save again.");
    return start(d);
}

static void handle_keys(const uint8_t *old, const uint8_t *now)
{
    for (int i = 0; i < KEY_BITMAP_SIZE * 8; i++) {
        int was = old[i / 8] >> (i % 8) & 1, is = now[i / 8] >> (i % 8) & 1;
        if (was == is)
            continue;
        EnterCriticalSection(&cfg_lock);
        int is_g = 0;
        for (int g = 0; g < G_KEY_COUNT; g++) {
            if (i == key_g[g]) {
                input_g(g, is);
                is_g = 1;
            }
        }
        if (i == KEY_FN && cfg.fn_as_win)
            input_fn(is);
        else if (!is_g && cfg.fn_as_win)
            input_fn_combo(i, is);
        LeaveCriticalSection(&cfg_lock);
    }
}

// Runs the keyboard: connects, applies the configuration, handles the key events, and reconnects after errors.
static DWORD WINAPI device_main(LPVOID unused)
{
    (void)unused;
    int waiting_logged = 0;
    while (!want_quit) {
        device *d = dev_open();
        if (!d) {
            if (!waiting_logged)
                log_msg("Keyboard not found, waiting for it");
            waiting_logged = 1;
            WaitForSingleObject(wake_event, RETRY_INTERVAL_MS);
            continue;
        }
        waiting_logged = 0;
        if (start(d) < 0) {
            log_msg("Keyboard initialization failed, retrying");
            // The initialization can stop after the keyboard is in software mode with Fn hidden: give control back to the keyboard.
            dev_start_hardware_mode(d);
            dev_close(d);
            WaitForSingleObject(wake_event, RETRY_INTERVAL_MS);
            continue;
        }
        log_msg("Keyboard ready");
        InterlockedExchange(&connected, 1);
        uint8_t keys[KEY_BITMAP_SIZE] = { 0 }, now[KEY_BITMAP_SIZE];
        DWORD last_ping = GetTickCount();
        for (;;) {
            DWORD since = GetTickCount() - last_ping;
            HANDLE events[2] = { dev_event_handle(d), wake_event };
            DWORD r = WaitForMultipleObjects(2, events, FALSE, since < PING_INTERVAL_MS ? PING_INTERVAL_MS - since : 0);
            if (r == WAIT_OBJECT_0) {
                int rc = dev_take_event(d, now);
                if (rc < 0)
                    break;
                if (rc == 1) {
                    handle_keys(keys, now);
                    memcpy(keys, now, sizeof keys);
                }
            } else if (r == WAIT_OBJECT_0 + 1) {
                if (want_quit) {
                    dev_start_hardware_mode(d);
                    break;
                }
                if (InterlockedExchange(&want_save, 0) && save_hardware_color(d) < 0)
                    break;
                if (InterlockedExchange(&want_restart, 0) && start(d) < 0)
                    break;
            } else if (r == WAIT_TIMEOUT) {
                last_ping = GetTickCount();
                int mode = dev_ping(d);
                if (mode < 0)
                    break;
                // The keyboard goes back to hardware mode after a reset, for example after the computer sleeps.
                if (mode != 2) {
                    log_msg("Keyboard is in mode %d, restarting software mode", mode);
                    if (start(d) < 0)
                        break;
                }
            } else {
                fatal("WaitForMultipleObjects failed (error %lu)", GetLastError());
            }
        }
        InterlockedExchange(&connected, 0);
        input_release_all();
        dev_close(d);
        if (!want_quit)
            log_msg("Keyboard connection lost");
    }
    return 0;
}

static void reload_config(void)
{
    config fresh;
    char err[512];
    if (config_load(&fresh, err, sizeof err) < 0) {
        alert("Configuration not reloaded: %s", err);
        return;
    }
    EnterCriticalSection(&cfg_lock);
    cfg = fresh;
    LeaveCriticalSection(&cfg_lock);
    log_msg("Configuration reloaded");
    InterlockedExchange(&want_restart, 1);
    SetEvent(wake_event);
}

static void stop_device_thread(void)
{
    InterlockedExchange(&want_quit, 1);
    SetEvent(wake_event);
    if (WaitForSingleObject(device_thread, 3000) != WAIT_OBJECT_0)
        log_msg("Device thread did not stop in 3 s");
}

static void add_tray_icon(HWND wnd)
{
    tray.cbSize = sizeof tray;
    tray.hWnd = wnd;
    tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray.uCallbackMessage = WM_TRAY;
    tray.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
    wcscpy(tray.szTip, L"k55d " VERSION_W L" - K55 RGB PRO");
    if (!Shell_NotifyIconW(NIM_ADD, &tray))
        log_msg("Cannot add the tray icon");
}

static LRESULT CALLBACK wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == taskbar_created_msg) {
        // Explorer restarted: the tray icon must be added again.
        add_tray_icon(wnd);
        return 0;
    }
    switch (msg) {
    case WM_TRAY:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING | MF_GRAYED, MENU_TITLE, L"k55d " VERSION_W);
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(menu, MF_STRING, MENU_RELOAD, L"Reload k55d.ini");
            AppendMenuW(menu, MF_STRING, MENU_SAVE, L"Save color to keyboard memory");
            AppendMenuW(menu, MF_STRING, MENU_EXIT, L"Exit");
            POINT p;
            GetCursorPos(&p);
            SetForegroundWindow(wnd);
            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, wnd, NULL);
            DestroyMenu(menu);
            if (cmd == MENU_RELOAD) {
                reload_config();
            } else if (cmd == MENU_SAVE) {
                if (!connected) {
                    alert("The keyboard is not connected.");
                } else {
                    InterlockedExchange(&want_save, 1);
                    SetEvent(wake_event);
                }
            } else if (cmd == MENU_EXIT) {
                DestroyWindow(wnd);
            }
        }
        return 0;
    case WM_ENDSESSION:
        if (wp)
            stop_device_thread();
        return 0;
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &tray);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    (void)prev, (void)show;
    // "k55d-<version>.exe --quit" stops the running instance. It runs before log_open, because log_open clears the log of the running instance.
    if (strstr(cmdline, "--quit")) {
        HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\k55d-quit");
        if (!ev) {
            MessageBoxA(NULL, "k55d is not running.", "k55d", MB_ICONERROR);
            return 1;
        }
        SetEvent(ev);
        return 0;
    }
    log_open();
    CreateMutexW(NULL, TRUE, L"Local\\k55d");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
        fatal("k55d is already running.");
    InitializeCriticalSection(&cfg_lock);
    char err[512];
    if (config_load(&cfg, err, sizeof err) < 0)
        fatal("Invalid configuration: %s", err);

    WNDCLASSW wc = { .lpfnWndProc = wnd_proc, .hInstance = inst, .lpszClassName = L"k55d" };
    if (!RegisterClassW(&wc))
        fatal("RegisterClass failed (error %lu)", GetLastError());
    // A hidden top-level window (not a message-only window), because only top-level windows receive WM_ENDSESSION and TaskbarCreated.
    HWND wnd = CreateWindowW(L"k55d", L"k55d", 0, 0, 0, 0, 0, NULL, NULL, inst, NULL);
    if (!wnd)
        fatal("CreateWindow failed (error %lu)", GetLastError());
    taskbar_created_msg = RegisterWindowMessageW(L"TaskbarCreated");
    add_tray_icon(wnd);
    input_init();

    wake_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    device_thread = CreateThread(NULL, 0, device_main, NULL, 0, NULL);
    if (!wake_event || !device_thread)
        fatal("Cannot start the device thread (error %lu)", GetLastError());

    quit_event = CreateEventW(NULL, TRUE, FALSE, L"Local\\k55d-quit");
    if (!quit_event)
        fatal("CreateEvent failed (error %lu)", GetLastError());
    MSG msg;
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, &quit_event, FALSE, INFINITE, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) {
            DestroyWindow(wnd);
        } else if (r != WAIT_OBJECT_0 + 1) {
            fatal("MsgWaitForMultipleObjects failed (error %lu)", GetLastError());
        }
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                goto done;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
done:
    stop_device_thread();
    log_msg("Exit");
    return 0;
}
