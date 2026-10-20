// usage: bragi.exe hexbyte... : sends one 64-byte command (after 0x00 report id) to MI_01 and prints the reply
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
int main(int argc, char **argv) {
    HANDLE h = open_if("mi_01");
    if (h == INVALID_HANDLE_VALUE) { fprintf(stderr, "open failed %lu\n", GetLastError()); return 1; }
    unsigned char out[65] = {0}, in[65];
    for (int i = 1; i < argc && i < 65; i++) out[i] = (unsigned char)strtoul(argv[i], NULL, 16);
    OVERLAPPED ov = {0}; ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    // start read first so the reply is not missed
    OVERLAPPED ro = {0}; ro.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    DWORD n;
    ReadFile(h, in, 65, NULL, &ro);
    if (!WriteFile(h, out, 65, NULL, &ov) && GetLastError() != ERROR_IO_PENDING) { fprintf(stderr, "write err %lu\n", GetLastError()); return 1; }
    if (!GetOverlappedResult(h, &ov, &n, TRUE)) { fprintf(stderr, "write err %lu\n", GetLastError()); return 1; }
    for (int k = 0; k < 3; k++) {
        if (WaitForSingleObject(ro.hEvent, 500) != WAIT_OBJECT_0) { printf("timeout\n"); break; }
        GetOverlappedResult(h, &ro, &n, FALSE);
        for (DWORD i = 0; i < n; i++) printf("%02x ", in[i]); printf("\n");
        if (in[1] == out[2]) break;
        ResetEvent(ro.hEvent); ReadFile(h, in, 65, NULL, &ro);
    }
    return 0;
}
