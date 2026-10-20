// Lists the HID interfaces of Corsair devices (VID 1B1C) with their usage page, usage and report sizes.
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <stdio.h>
int main(void) {
    GUID g; HidD_GetHidGuid(&g);
    HDEVINFO di = SetupDiGetClassDevsA(&g, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA id = { sizeof id };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(di, NULL, &g, i, &id); i++) {
        DWORD sz = 0; SetupDiGetDeviceInterfaceDetailA(di, &id, NULL, 0, &sz, NULL);
        SP_DEVICE_INTERFACE_DETAIL_DATA_A *d = malloc(sz); d->cbSize = sizeof *d;
        SetupDiGetDeviceInterfaceDetailA(di, &id, d, sz, NULL, NULL);
        if (!strstr(d->DevicePath, "1b1c")) continue;
        HANDLE h = CreateFileA(d->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        PHIDP_PREPARSED_DATA pp; HIDP_CAPS c = {0};
        if (h != INVALID_HANDLE_VALUE && HidD_GetPreparsedData(h, &pp)) { HidP_GetCaps(pp, &c); HidD_FreePreparsedData(pp); }
        printf("%s\n  page=%04x usage=%04x in=%u out=%u feat=%u\n", d->DevicePath, c.UsagePage, c.Usage, c.InputReportByteLength, c.OutputReportByteLength, c.FeatureReportByteLength);
        CloseHandle(h);
    }
    return 0;
}
