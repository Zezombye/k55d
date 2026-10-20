#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <stdio.h>
#include <string.h>
#include "device.h"
#include "log.h"

// Bragi command ids (second byte of each output report, after the 0x08 prefix).
#define CMD_SET_PROPERTY 0x01
#define CMD_GET_PROPERTY 0x02
#define CMD_CLOSE_HANDLE 0x05
#define CMD_WRITE_FIRST 0x06
#define CMD_WRITE_NEXT 0x07
#define CMD_READ 0x08
#define CMD_FILE_INFO 0x09
#define CMD_OPEN_HANDLE 0x0D
#define CMD_PING 0x12

// Bragi properties.
#define PROP_MODE 0x03
#define PROP_KEY_EVENTS 0x4A
#define MODE_HARDWARE 1
#define MODE_SOFTWARE 2

// Bragi resources, opened with CMD_OPEN_HANDLE.
#define RES_LIGHTING 0x01
#define RES_KEY_ROUTING 0x02
#define KEY_ROUTING_SIZE 137
// Lighting effect that the keyboard shows in hardware mode. File 0x6D60 is the list of effects and contains only 0x6D61.
#define FILE_HW_LIGHTING 0x6D61
#define HW_LIGHTING_SIZE 33

#define HANDLE_LIGHTING 0
#define HANDLE_OTHER 1

#define REPORT_SIZE 65
#define REPLY_TIMEOUT_MS 1000

struct device {
    HANDLE cmd;
    HANDLE evt;
    OVERLAPPED evt_ov;
    uint8_t evt_buf[REPORT_SIZE];
    OVERLAPPED cmd_wov, cmd_rov;
};

static HANDLE open_interface(const char *usb_interface)
{
    GUID hid_guid;
    HidD_GetHidGuid(&hid_guid);
    HDEVINFO set = SetupDiGetClassDevsA(&hid_guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        fatal("SetupDiGetClassDevs failed (error %lu)", GetLastError());
    char wanted[64];
    snprintf(wanted, sizeof wanted, "vid_%04x&pid_%04x&%s", K55_VID, K55_PID, usb_interface);
    HANDLE h = INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DATA ifd = { .cbSize = sizeof ifd };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, NULL, &hid_guid, i, &ifd); i++) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailA(set, &ifd, NULL, 0, &size, NULL);
        SP_DEVICE_INTERFACE_DETAIL_DATA_A *detail = malloc(size);
        detail->cbSize = sizeof *detail;
        if (!SetupDiGetDeviceInterfaceDetailA(set, &ifd, detail, size, NULL, NULL))
            fatal("SetupDiGetDeviceInterfaceDetail failed (error %lu)", GetLastError());
        char *path = _strdup(detail->DevicePath);
        free(detail);
        if (strstr(_strlwr(path), wanted)) {
            h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
            if (h == INVALID_HANDLE_VALUE)
                log_msg("Cannot open %s (error %lu)", path, GetLastError());
            free(path);
            break;
        }
        free(path);
    }
    SetupDiDestroyDeviceInfoList(set);
    return h;
}

static int start_event_read(device *d)
{
    ResetEvent(d->evt_ov.hEvent);
    if (!ReadFile(d->evt, d->evt_buf, REPORT_SIZE, NULL, &d->evt_ov) && GetLastError() != ERROR_IO_PENDING) {
        log_msg("Event read failed (error %lu)", GetLastError());
        return -1;
    }
    return 0;
}

device *dev_open(void)
{
    HANDLE cmd = open_interface("mi_01");
    if (cmd == INVALID_HANDLE_VALUE)
        return NULL;
    HANDLE evt = open_interface("mi_02");
    if (evt == INVALID_HANDLE_VALUE) {
        CloseHandle(cmd);
        return NULL;
    }
    device *d = calloc(1, sizeof *d);
    d->cmd = cmd;
    d->evt = evt;
    d->evt_ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    d->cmd_wov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    d->cmd_rov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!d->evt_ov.hEvent || !d->cmd_wov.hEvent || !d->cmd_rov.hEvent)
        fatal("CreateEvent failed (error %lu)", GetLastError());
    if (start_event_read(d) < 0) {
        dev_close(d);
        return NULL;
    }
    return d;
}

void dev_close(device *d)
{
    CancelIoEx(d->cmd, NULL);
    CancelIoEx(d->evt, NULL);
    CloseHandle(d->cmd);
    CloseHandle(d->evt);
    CloseHandle(d->evt_ov.hEvent);
    CloseHandle(d->cmd_wov.hEvent);
    CloseHandle(d->cmd_rov.hEvent);
    free(d);
}

// Sends one command and waits for the reply with the same command id.
// req: the command bytes after the 0x08 prefix. reply (optional): receives the 61 data bytes after the status.
// Returns the Bragi status, or -1 if the keyboard does not reply.
static int dev_cmd(device *d, const uint8_t *req, int len, uint8_t *reply)
{
    uint8_t out[REPORT_SIZE] = { 0x00, 0x08 };
    memcpy(out + 2, req, len);
    DWORD n;
    ResetEvent(d->cmd_wov.hEvent);
    if (!WriteFile(d->cmd, out, REPORT_SIZE, NULL, &d->cmd_wov) && GetLastError() != ERROR_IO_PENDING) {
        log_msg("Command write failed (error %lu)", GetLastError());
        return -1;
    }
    if (!GetOverlappedResult(d->cmd, &d->cmd_wov, &n, TRUE)) {
        log_msg("Command write failed (error %lu)", GetLastError());
        return -1;
    }
    // Windows keeps input reports in a queue, thus the reply is not lost if it arrives before ReadFile.
    // Replies to other programs (for example iCUE) can also be in the queue: they are ignored if the command id is different.
    DWORD deadline = GetTickCount() + REPLY_TIMEOUT_MS;
    for (;;) {
        uint8_t in[REPORT_SIZE];
        ResetEvent(d->cmd_rov.hEvent);
        if (!ReadFile(d->cmd, in, REPORT_SIZE, NULL, &d->cmd_rov) && GetLastError() != ERROR_IO_PENDING) {
            log_msg("Reply read failed (error %lu)", GetLastError());
            return -1;
        }
        DWORD now = GetTickCount();
        DWORD wait = (int)(deadline - now) > 0 ? deadline - now : 0;
        if (WaitForSingleObject(d->cmd_rov.hEvent, wait) != WAIT_OBJECT_0) {
            CancelIoEx(d->cmd, &d->cmd_rov);
            GetOverlappedResult(d->cmd, &d->cmd_rov, &n, TRUE);
            log_msg("No reply to command 0x%02x", req[0]);
            return -1;
        }
        if (!GetOverlappedResult(d->cmd, &d->cmd_rov, &n, FALSE)) {
            log_msg("Reply read failed (error %lu)", GetLastError());
            return -1;
        }
        // Reply layout: [report id] [0x00] [command id] [status] [data...]
        if (in[2] != req[0])
            continue;
        if (reply)
            memcpy(reply, in + 4, REPORT_SIZE - 4);
        return in[3];
    }
}

static int set_property(device *d, uint8_t prop, uint8_t value)
{
    uint8_t req[] = { CMD_SET_PROPERTY, prop, 0x00, value };
    return dev_cmd(d, req, sizeof req, NULL);
}

static int get_property(device *d, uint8_t prop)
{
    uint8_t req[] = { CMD_GET_PROPERTY, prop, 0x00 };
    uint8_t reply[REPORT_SIZE];
    int st = dev_cmd(d, req, sizeof req, reply);
    if (st != BRAGI_OK)
        return -1;
    return reply[0];
}

static int open_handle(device *d, uint8_t handle, uint16_t resource)
{
    uint8_t req[] = { CMD_OPEN_HANDLE, handle, resource & 0xFF, resource >> 8 };
    return dev_cmd(d, req, sizeof req, NULL);
}

static int close_handle(device *d, uint8_t handle)
{
    uint8_t req[] = { CMD_CLOSE_HANDLE, 0x01, handle };
    return dev_cmd(d, req, sizeof req, NULL);
}

// Writes data to an open handle. The first packet holds the total length and 57 data bytes, next packets hold 61 data bytes.
static int write_handle(device *d, uint8_t handle, const uint8_t *data, uint32_t len)
{
    uint8_t req[63] = { CMD_WRITE_FIRST, handle, len & 0xFF, (len >> 8) & 0xFF, (len >> 16) & 0xFF, len >> 24 };
    uint32_t chunk = len < 57 ? len : 57;
    memcpy(req + 6, data, chunk);
    int st = dev_cmd(d, req, 6 + chunk, NULL);
    for (uint32_t off = chunk; st == BRAGI_OK && off < len; off += chunk) {
        chunk = len - off < 61 ? len - off : 61;
        req[0] = CMD_WRITE_NEXT;
        memcpy(req + 2, data + off, chunk);
        st = dev_cmd(d, req, 2 + chunk, NULL);
    }
    return st;
}

static int check(const char *what, int status)
{
    if (status == BRAGI_OK)
        return 0;
    if (status < 0)
        log_msg("%s: no reply", what);
    else
        log_msg("%s: keyboard returned status %d", what, status);
    return -1;
}

// Writes data to a file: open, file information, write, close (same sequence as iCUE).
// The handle must be opened again for each write, because the keyboard refuses (status 2) a write after a read or a write on the same handle.
static int write_file(device *d, uint16_t file, const uint8_t *data, uint32_t len)
{
    uint8_t info[] = { CMD_FILE_INFO, HANDLE_OTHER };
    if (check("Open file", open_handle(d, HANDLE_OTHER, file)) < 0)
        return -1;
    int rc = check("File information", dev_cmd(d, info, sizeof info, NULL));
    if (rc == 0)
        rc = check("Write file", write_handle(d, HANDLE_OTHER, data, len));
    if (check("Close file", close_handle(d, HANDLE_OTHER)) < 0 || rc < 0)
        return -1;
    return 0;
}

// The keyboard shows the hardware lighting file in hardware mode, and after it gets power again. The file is accessible only in software mode.
//
// Observed behavior of the keyboard (firmware 1.2.18):
// - At power-up, the keyboard loads only the per-zone format that the keyboard itself writes (5 colors for LEDs 1-5, 33 bytes).
//   With other formats (the 18-byte single-color format that iCUE writes, or 34 bytes) it shows the rainbow.
// - Only one save out of two is kept after the keyboard loses power. The cause is not known.
// Thus the save is a user action (tray menu): the user can save again if the keyboard shows the rainbow after it gets power again.
// Each save reverses the order of the LED list, thus the record always changes (the keyboard possibly ignores a write that does not change the file).
int dev_save_hardware_color(device *d, const uint8_t color[3])
{
    // Effect 0x207E (static color), format with one color per zone:
    // [7E 20 01 00 00 00] [color count: 5] 5 x [alpha, blue, green, red] [LED count: 5] [LED indexes]
    // Color i applies to LED i of the list, thus all colors are the same and the order of the list has no effect.
    uint8_t record[HW_LIGHTING_SIZE] = { 0x7E, 0x20, 0x01, 0x00, 0x00, 0x00, 0x05 };
    for (int i = 0; i < 5; i++) {
        uint8_t *c = record + 7 + 4 * i;
        c[0] = 0xFF;
        c[1] = color[2];
        c[2] = color[1];
        c[3] = color[0];
    }
    record[27] = 0x05;

    // File information reply: [2 unknown bytes] [size: 2 bytes LE]
    uint8_t info[] = { CMD_FILE_INFO, HANDLE_OTHER }, reply[REPORT_SIZE];
    uint8_t read[] = { CMD_READ, HANDLE_OTHER }, current[REPORT_SIZE];
    if (check("Open hardware lighting", open_handle(d, HANDLE_OTHER, FILE_HW_LIGHTING)) < 0)
        return -1;
    int rc = check("Hardware lighting information", dev_cmd(d, info, sizeof info, reply));
    if (rc == 0)
        rc = check("Read hardware lighting", dev_cmd(d, read, sizeof read, current));
    if (check("Close hardware lighting", close_handle(d, HANDLE_OTHER)) < 0 || rc < 0)
        return -1;
    int size = reply[2] | reply[3] << 8;
    // LED list 1-5, or 5-1 if the current record already has the list 1-5.
    int ascending = !(size == HW_LIGHTING_SIZE && current[28] == 0x01);
    for (int i = 0; i < 5; i++)
        record[28 + i] = (uint8_t)(ascending ? 1 + i : 5 - i);
    log_msg("Writing color %02x%02x%02x to the hardware lighting (old: %d bytes, LED list %s)", color[0], color[1], color[2], size, ascending ? "1-5" : "5-1");
    return write_file(d, FILE_HW_LIGHTING, record, sizeof record);
}

int dev_start_software_mode(device *d, const uint8_t colors[6][3], const int *hidden_keys)
{
    // A previous instance (or iCUE) can leave the handles open. Close returns an error status when the handle is not open:
    // this is expected and the status is not checked.
    if (close_handle(d, HANDLE_LIGHTING) < 0 || close_handle(d, HANDLE_OTHER) < 0)
        return -1;
    if (check("Set software mode", set_property(d, PROP_MODE, MODE_SOFTWARE)) < 0)
        return -1;
    if (check("Enable key events", set_property(d, PROP_KEY_EVENTS, 1)) < 0)
        return -1;

    // Key routing: one byte per key index, 1 = the keyboard sends the key to Windows, 0 = only the event channel receives it.
    uint8_t routing[KEY_ROUTING_SIZE];
    memset(routing, 1, sizeof routing);
    for (const int *k = hidden_keys; *k >= 0; k++)
        routing[*k] = 0;
    if (check("Open key routing", open_handle(d, HANDLE_OTHER, RES_KEY_ROUTING)) < 0)
        return -1;
    if (check("Write key routing", write_handle(d, HANDLE_OTHER, routing, sizeof routing)) < 0)
        return -1;
    if (check("Close key routing", close_handle(d, HANDLE_OTHER)) < 0)
        return -1;

    // Lighting buffer: all red values, then all green values, then all blue values.
    uint8_t rgb[18];
    for (int i = 0; i < 6; i++) {
        rgb[i] = colors[i][0];
        rgb[6 + i] = colors[i][1];
        rgb[12 + i] = colors[i][2];
    }
    if (check("Open lighting", open_handle(d, HANDLE_LIGHTING, RES_LIGHTING)) < 0)
        return -1;
    if (check("Write lighting", write_handle(d, HANDLE_LIGHTING, rgb, sizeof rgb)) < 0)
        return -1;
    return 0;
}

int dev_start_hardware_mode(device *d)
{
    if (close_handle(d, HANDLE_LIGHTING) < 0)
        return -1;
    // Give all keys back to Windows, because a key hidden in software mode must work in hardware mode.
    uint8_t routing[KEY_ROUTING_SIZE];
    memset(routing, 1, sizeof routing);
    if (check("Open key routing", open_handle(d, HANDLE_OTHER, RES_KEY_ROUTING)) < 0)
        return -1;
    if (check("Write key routing", write_handle(d, HANDLE_OTHER, routing, sizeof routing)) < 0)
        return -1;
    if (check("Close key routing", close_handle(d, HANDLE_OTHER)) < 0)
        return -1;
    return check("Set hardware mode", set_property(d, PROP_MODE, MODE_HARDWARE));
}

int dev_ping(device *d)
{
    uint8_t req[] = { CMD_PING };
    if (check("Ping", dev_cmd(d, req, sizeof req, NULL)) < 0)
        return -1;
    return get_property(d, PROP_MODE);
}

HANDLE dev_event_handle(device *d)
{
    return d->evt_ov.hEvent;
}

int dev_take_event(device *d, uint8_t bitmap[KEY_BITMAP_SIZE])
{
    DWORD n;
    if (!GetOverlappedResult(d->evt, &d->evt_ov, &n, FALSE)) {
        log_msg("Event read failed (error %lu)", GetLastError());
        return -1;
    }
    // Event layout: [report id] [0x00] [0x02] [key bitmap: bit i = key index i is pressed]
    int is_keys = n >= 3 && d->evt_buf[1] == 0x00 && d->evt_buf[2] == 0x02;
    if (is_keys)
        memcpy(bitmap, d->evt_buf + 3, KEY_BITMAP_SIZE);
    else
        log_msg("Unknown event report %02x %02x %02x %02x", d->evt_buf[1], d->evt_buf[2], d->evt_buf[3], d->evt_buf[4]);
    if (start_event_read(d) < 0)
        return -1;
    return is_keys;
}
