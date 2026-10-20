#include <windows.h>
#include "input.h"
#include "log.h"

// Value in dwExtraInfo of the events that k55d injects.
#define K55D_EXTRA_INFO 0x6B353564

static volatile LONG fn_held;
static volatile LONG win_down;

// G keys that k55d holds down, to release them when the keyboard disconnects.
static int g_down[G_KEY_COUNT];

// Scan codes (set 1) of the keys that k55d injected for Fn combinations, by key index. 0 = not injected.
static WORD fn_combo_down[256];

// Scan codes (set 1) for the key indexes 0x04 to 0x65. For these keys, the key index is the HID usage code.
// 0xE0xx is an extended key. Pause (0x48) has no simple scan code and is not in the table.
static const WORD usage_scan[0x66] = {
    [0x04] = 0x1E, [0x05] = 0x30, [0x06] = 0x2E, [0x07] = 0x20, [0x08] = 0x12, [0x09] = 0x21, [0x0A] = 0x22, [0x0B] = 0x23,
    [0x0C] = 0x17, [0x0D] = 0x24, [0x0E] = 0x25, [0x0F] = 0x26, [0x10] = 0x32, [0x11] = 0x31, [0x12] = 0x18, [0x13] = 0x19,
    [0x14] = 0x10, [0x15] = 0x13, [0x16] = 0x1F, [0x17] = 0x14, [0x18] = 0x16, [0x19] = 0x2F, [0x1A] = 0x11, [0x1B] = 0x2D,
    [0x1C] = 0x15, [0x1D] = 0x2C, [0x1E] = 0x02, [0x1F] = 0x03, [0x20] = 0x04, [0x21] = 0x05, [0x22] = 0x06, [0x23] = 0x07,
    [0x24] = 0x08, [0x25] = 0x09, [0x26] = 0x0A, [0x27] = 0x0B, [0x28] = 0x1C, [0x29] = 0x01, [0x2A] = 0x0E, [0x2B] = 0x0F,
    [0x2C] = 0x39, [0x2D] = 0x0C, [0x2E] = 0x0D, [0x2F] = 0x1A, [0x30] = 0x1B, [0x31] = 0x2B, [0x32] = 0x2B, [0x33] = 0x27,
    [0x34] = 0x28, [0x35] = 0x29, [0x36] = 0x33, [0x37] = 0x34, [0x38] = 0x35, [0x39] = 0x3A, [0x3A] = 0x3B, [0x3B] = 0x3C,
    [0x3C] = 0x3D, [0x3D] = 0x3E, [0x3E] = 0x3F, [0x3F] = 0x40, [0x40] = 0x41, [0x41] = 0x42, [0x42] = 0x43, [0x43] = 0x44,
    [0x44] = 0x57, [0x45] = 0x58, [0x46] = 0xE037, [0x47] = 0x46, [0x49] = 0xE052, [0x4A] = 0xE047, [0x4B] = 0xE049,
    [0x4C] = 0xE053, [0x4D] = 0xE04F, [0x4E] = 0xE051, [0x4F] = 0xE04D, [0x50] = 0xE04B, [0x51] = 0xE050, [0x52] = 0xE048,
    [0x53] = 0x45, [0x54] = 0xE035, [0x55] = 0x37, [0x56] = 0x4A, [0x57] = 0x4E, [0x58] = 0xE01C, [0x59] = 0x4F, [0x5A] = 0x50,
    [0x5B] = 0x51, [0x5C] = 0x4B, [0x5D] = 0x4C, [0x5E] = 0x4D, [0x5F] = 0x47, [0x60] = 0x48, [0x61] = 0x49, [0x62] = 0x52,
    [0x63] = 0x53, [0x64] = 0x56, [0x65] = 0xE05D,
};

static void send_input(INPUT *in)
{
    // SendInput fails if the foreground window has a higher integrity level (for example an administrator window) than k55d.
    if (SendInput(1, in, sizeof *in) != 1)
        log_msg("SendInput failed for key vk=0x%02x scan=0x%02x (error %lu)", in->ki.wVk, in->ki.wScan, GetLastError());
}

static void send_key(WORD vk, int up)
{
    INPUT in = { .type = INPUT_KEYBOARD };
    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
    in.ki.wVk = vk;
    in.ki.wScan = (WORD)(scan & 0xFF);
    in.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | ((scan & 0xFF00) == 0xE000 ? KEYEVENTF_EXTENDEDKEY : 0);
    in.ki.dwExtraInfo = K55D_EXTRA_INFO;
    send_input(&in);
}

// Injects a key by its scan code. Windows converts the scan code with the active keyboard layout, as for a physical key.
static void send_scan(WORD scan, int up)
{
    INPUT in = { .type = INPUT_KEYBOARD };
    in.ki.wScan = scan & 0xFF;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0) | ((scan & 0xFF00) == 0xE000 ? KEYEVENTF_EXTENDEDKEY : 0);
    in.ki.dwExtraInfo = K55D_EXTRA_INFO;
    send_input(&in);
}

static void release_win(void)
{
    if (InterlockedExchange(&win_down, 0))
        send_key(VK_LWIN, 1);
}

static int is_modifier(DWORD vk)
{
    return vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN;
}

static LRESULT CALLBACK keyboard_hook(int code, WPARAM wp, LPARAM lp)
{
    const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)lp;
    // While Fn is held, k55d injects the other keys from the event channel (see input_fn_combo).
    // If the keyboard also sends a key to Windows, the key would come two times: the physical key is blocked.
    if (code == HC_ACTION && fn_held && (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) && !(k->flags & LLKHF_INJECTED) && !is_modifier(k->vkCode))
        return 1;
    return CallNextHookEx(NULL, code, wp, lp);
}

void input_init(void)
{
    if (!SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_hook, GetModuleHandleW(NULL), 0))
        fatal("SetWindowsHookEx failed (error %lu)", GetLastError());
}

void input_fn(int pressed)
{
    InterlockedExchange(&fn_held, pressed);
    if (pressed) {
        InterlockedExchange(&win_down, 1);
        send_key(VK_LWIN, 0);
    } else {
        release_win();
    }
}

void input_fn_combo(int key, int pressed)
{
    if (key < 0 || key >= 256)
        return;
    if (!pressed) {
        if (fn_combo_down[key]) {
            send_scan(fn_combo_down[key], 1);
            fn_combo_down[key] = 0;
        }
        return;
    }
    if (!fn_held)
        return;
    WORD scan = key < (int)(sizeof usage_scan / sizeof usage_scan[0]) ? usage_scan[key] : 0;
    if (!scan) {
        log_msg("Fn + key index %d: no scan code for this key", key);
        return;
    }
    // Windows does not lock the session for Win+L when the Windows key is injected. Thus k55d locks the session itself.
    if (MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX) == 'L') {
        release_win();
        if (!LockWorkStation())
            log_msg("LockWorkStation failed (error %lu)", GetLastError());
        return;
    }
    send_scan(scan, 0);
    fn_combo_down[key] = scan;
}

void input_g(int g, int pressed)
{
    if (g_down[g] == pressed)
        return;
    g_down[g] = pressed;
    send_key((WORD)(VK_F13 + g), !pressed);
}

void input_release_all(void)
{
    for (int key = 0; key < 256; key++)
        input_fn_combo(key, 0);
    InterlockedExchange(&fn_held, 0);
    release_win();
    for (int g = 0; g < G_KEY_COUNT; g++)
        input_g(g, 0);
}
