// stdin lines: hex bytes (sent after report id 0) ; "s <ms>" sleeps ; "#" comment. Prints replies (cmd byte matched).
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <stdio.h>
static HANDLE open_if(const char *mi) {
    GUID g; HidD_GetHidGuid(&g);
    HDEVINFO di = SetupDiGetClassDevsA(&g, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA id = { sizeof id };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(di, NULL, &g, i, &id); i++) {
        DWORD sz = 0; SetupDiGetDeviceInterfaceDetailA(di, &id, NULL, 0, &sz, NULL);
        SP_DEVICE_INTERFACE_DETAIL_DATA_A *d = malloc(sz); d->cbSize = sizeof *d;
        SetupDiGetDeviceInterfaceDetailA(di, &id, d, sz, NULL, NULL);
        if (strstr(d->DevicePath, "pid_1ba4") && strstr(d->DevicePath, mi))
            return CreateFileA(d->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    }
    return INVALID_HANDLE_VALUE;
}
static HANDLE h; static OVERLAPPED ro; static unsigned char in[65];
static int quiet;
int main(int argc, char **argv) {
    quiet = argc > 1;
    h = open_if("mi_01");
    if (h == INVALID_HANDLE_VALUE) { fprintf(stderr, "open failed %lu\n", GetLastError()); return 1; }
    ro.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    OVERLAPPED wo = {0}; wo.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    ReadFile(h, in, 65, NULL, &ro);
    char line[1024]; DWORD n;
    while (fgets(line, sizeof line, stdin)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (line[0] == 's') { Sleep(atoi(line + 1)); continue; }
        unsigned char out[65] = {0}; int k = 1; char *p = line, *e;
        while (k < 65) { unsigned long v = strtoul(p, &e, 16); if (e == p) break; out[k++] = (unsigned char)v; p = e; }
        if (!WriteFile(h, out, 65, NULL, &wo) && GetLastError() != ERROR_IO_PENDING) { fprintf(stderr, "write err %lu\n", GetLastError()); return 1; }
        GetOverlappedResult(h, &wo, &n, TRUE);
        for (;;) {
            if (WaitForSingleObject(ro.hEvent, 1000) != WAIT_OBJECT_0) { printf("TIMEOUT\n"); break; }
            GetOverlappedResult(h, &ro, &n, FALSE);
            int match = in[2] == out[2];
            if (!quiet || match) { printf("%s", match ? "" : "(other) "); for (DWORD i = 1; i < n; i++) printf("%02x ", in[i]); printf("\n"); }
            ResetEvent(ro.hEvent); ReadFile(h, in, 65, NULL, &ro);
            if (match) break;
        }
        fflush(stdout);
    }
    return 0;
}
