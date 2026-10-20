// injectpid.exe <pid> <dll>: load dll into a running process.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    HANDLE p = OpenProcess(PROCESS_ALL_ACCESS, FALSE, strtoul(argv[1], NULL, 10));
    if (!p) { printf("OpenProcess %lu\n", GetLastError()); return 1; }
    size_t len = strlen(argv[2]) + 1;
    void *mem = VirtualAllocEx(p, NULL, len, MEM_COMMIT, PAGE_READWRITE);
    WriteProcessMemory(p, mem, argv[2], len, NULL);
    HANDLE t = CreateRemoteThread(p, NULL, 0, (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"), mem, 0, NULL);
    if (!t) { printf("CreateRemoteThread %lu\n", GetLastError()); return 1; }
    WaitForSingleObject(t, INFINITE); DWORD code; GetExitCodeThread(t, &code);
    printf("LoadLibrary -> %lx\n", code);
    return 0;
}
