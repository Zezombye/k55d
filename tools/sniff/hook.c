// Injected into iCUE: patches IATs of all modules so WriteFile/HidD_SetOutputReport/HidD_SetFeature/DeviceIoControl are logged.
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
static FILE *logf; static CRITICAL_SECTION cs; static DWORD t0;
static BOOL (WINAPI *oWriteFile)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOLEAN (WINAPI *oSetOut)(HANDLE, PVOID, ULONG);
static BOOLEAN (WINAPI *oSetFeat)(HANDLE, PVOID, ULONG);
static BOOL (WINAPI *oIoctl)(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static void dump(const char *tag, HANDLE h, const unsigned char *b, DWORD n) {
    EnterCriticalSection(&cs);
    fprintf(logf, "%9.3f %s h=%p n=%lu:", (GetTickCount() - t0) / 1000.0, tag, h, n);
    int last = (int)n - 1; while (last > 2 && !b[last]) last--;
    for (int i = 0; i <= last; i++) fprintf(logf, " %02x", b[i]);
    fprintf(logf, "\n"); fflush(logf);
    LeaveCriticalSection(&cs);
}
static BOOL WINAPI hWriteFile(HANDLE h, LPCVOID b, DWORD n, LPDWORD w, LPOVERLAPPED o) {
    if (n == 65 || n == 64 || n == 1025 || n == 1024) dump("WF", h, b, n);
    return oWriteFile(h, b, n, w, o);
}
static BOOLEAN WINAPI hSetOut(HANDLE h, PVOID b, ULONG n) { dump("SO", h, b, n); return oSetOut(h, b, n); }
static BOOLEAN WINAPI hSetFeat(HANDLE h, PVOID b, ULONG n) { dump("SF", h, b, n); return oSetFeat(h, b, n); }
static BOOL WINAPI hIoctl(HANDLE h, DWORD c, LPVOID ib, DWORD in, LPVOID ob, DWORD on, LPDWORD r, LPOVERLAPPED o) {
    // HID class IOCTLs: device type 0x0B
    if ((c >> 16) == 0x0B && in > 0 && in <= 1025) { char t[32]; sprintf(t, "IO%08lx", c); dump(t, h, ib, in); }
    return oIoctl(h, c, ib, in, ob, on, r, o);
}
static void patch(HMODULE m) {
    char mp[MAX_PATH]; GetModuleFileNameA(m, mp, MAX_PATH);
    if (!strstr(mp, "Corsair")) return;
    unsigned char *base = (unsigned char *)m;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dd.VirtualAddress) return;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (void *)(base + dd.VirtualAddress); d->Name; d++) {
        if (!d->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA *on = (void *)(base + d->OriginalFirstThunk), *ft = (void *)(base + d->FirstThunk);
        for (; on->u1.AddressOfData; on++, ft++) {
            if (IMAGE_SNAP_BY_ORDINAL(on->u1.Ordinal)) continue;
            const char *nm = ((IMAGE_IMPORT_BY_NAME *)(base + on->u1.AddressOfData))->Name;
            void *rep = NULL;
            if (!strcmp(nm, "WriteFile")) rep = hWriteFile;
            else if (!strcmp(nm, "HidD_SetOutputReport")) rep = hSetOut;
            else if (!strcmp(nm, "HidD_SetFeature")) rep = hSetFeat;
            else if (!strcmp(nm, "DeviceIoControl")) rep = hIoctl;
            if (!rep || (void *)ft->u1.Function == rep) continue;
            DWORD old; VirtualProtect(&ft->u1.Function, 8, PAGE_READWRITE, &old);
            ft->u1.Function = (ULONG_PTR)rep;
            VirtualProtect(&ft->u1.Function, 8, old, &old);
        }
    }
}
static FARPROC (WINAPI *oGetProc)(HMODULE, LPCSTR);
static DWORD WINAPI scanner(LPVOID p) {
    HMODULE self = p;
    for (;;) {
        HMODULE mods[1024]; DWORD need;
        EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &need);
        for (DWORD i = 0; i < need / sizeof(HMODULE); i++) if (mods[i] != self) patch(mods[i]);
        Sleep(20);
    }
}
BOOL WINAPI DllMain(HINSTANCE inst, DWORD r, LPVOID x) {
    if (r != DLL_PROCESS_ATTACH) return TRUE;
    InitializeCriticalSection(&cs); t0 = GetTickCount();
    char path[MAX_PATH]; GetModuleFileNameA(inst, path, MAX_PATH); strcpy(strrchr(path, 92) + 1, "sniff.log");
    logf = fopen(path, "a"); fprintf(logf, "=== start pid %lu\n", GetCurrentProcessId()); fflush(logf);
    HMODULE k = GetModuleHandleA("kernelbase.dll"), hid = LoadLibraryA("hid.dll");
    oWriteFile = (void *)GetProcAddress(k, "WriteFile"); oIoctl = (void *)GetProcAddress(k, "DeviceIoControl");
    oSetOut = (void *)GetProcAddress(hid, "HidD_SetOutputReport"); oSetFeat = (void *)GetProcAddress(hid, "HidD_SetFeature");
    HMODULE mods[1024]; DWORD need;
    EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &need);
    for (DWORD i = 0; i < need / sizeof(HMODULE); i++) if (mods[i] != inst) patch(mods[i]);
    CreateThread(NULL, 0, scanner, inst, 0, NULL);
    return TRUE;
}
