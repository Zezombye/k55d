// Prints every input report from MI_02 (events) and unsolicited MI_01 reports for N seconds; keeps device alive with pings.
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
    int secs = argc > 1 ? atoi(argv[1]) : 60;
    HANDLE c = open_if("mi_01"), e = open_if("mi_02");
    if (c == INVALID_HANDLE_VALUE || e == INVALID_HANDLE_VALUE) { fprintf(stderr, "open failed\n"); return 1; }
    unsigned char ib[2][65]; OVERLAPPED o[2] = {0}; HANDLE hs[2] = { c, e };
    for (int i = 0; i < 2; i++) { o[i].hEvent = CreateEventA(NULL, TRUE, FALSE, NULL); ReadFile(hs[i], ib[i], 65, NULL, &o[i]); }
    HANDLE ev[2] = { o[0].hEvent, o[1].hEvent };
    OVERLAPPED wo = {0}; wo.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    DWORD start = GetTickCount(), lastping = 0, n;
    while (GetTickCount() - start < (DWORD)secs * 1000) {
        if (argc < 3 && GetTickCount() - lastping > 5000) {
            unsigned char p[65] = {0, 0x08, 0x12};
            WriteFile(c, p, 65, NULL, &wo); GetOverlappedResult(c, &wo, &n, TRUE);
            lastping = GetTickCount();
        }
        DWORD r = WaitForMultipleObjects(2, ev, FALSE, 200);
        if (r > WAIT_OBJECT_0 + 1) continue;
        int i = r - WAIT_OBJECT_0;
        GetOverlappedResult(hs[i], &o[i], &n, FALSE);
        if (!(i == 0 && ib[0][2] == 0x12)) {
            int last = n - 1; while (last > 1 && !ib[i][last]) last--;
            printf("%7.3f %s:", (GetTickCount() - start) / 1000.0, i ? "EVT" : "CMD");
            for (int k = 1; k <= last; k++) printf(" %02x", ib[i][k]);
            printf("\n"); fflush(stdout);
        }
        ResetEvent(o[i].hEvent); ReadFile(hs[i], ib[i], 65, NULL, &o[i]);
    }
    return 0;
}
