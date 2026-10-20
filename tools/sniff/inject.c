// inject.exe <exe> <dll>: start exe suspended, load dll in it, resume.
#include <windows.h>
#include <stdio.h>
int main(int argc, char **argv) {
    STARTUPINFOA si = { sizeof si }; PROCESS_INFORMATION pi;
    char dir[MAX_PATH]; strcpy(dir, argv[1]); *strrchr(dir, 92) = 0;
    char cl[1024]; sprintf(cl, "\"%s\" %s", argv[1], argc > 3 ? argv[3] : ""); if (!CreateProcessA(argv[1], cl, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, dir, &si, &pi)) { printf("CreateProcess %lu\n", GetLastError()); return 1; }
    size_t len = strlen(argv[2]) + 1;
    void *mem = VirtualAllocEx(pi.hProcess, NULL, len, MEM_COMMIT, PAGE_READWRITE);
    WriteProcessMemory(pi.hProcess, mem, argv[2], len, NULL);
    HANDLE t = CreateRemoteThread(pi.hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"), mem, 0, NULL);
    if (!t) { printf("CreateRemoteThread %lu\n", GetLastError()); return 1; }
    WaitForSingleObject(t, INFINITE); DWORD code; GetExitCodeThread(t, &code);
    printf("LoadLibrary -> %lx\n", code);
    ResumeThread(pi.hThread);
    printf("pid %lu\n", pi.dwProcessId);
    return 0;
}
