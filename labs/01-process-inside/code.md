hello.c

#include <windows.h>
#include <stdio.h>

int main(void) {
    DWORD pid = GetCurrentProcessId();
    printf("agent v0, pid=%lu\n", pid);
    printf("Press Enter to exit\n");
    getchar();
    return 0;
}


hello.py

import ctypes

k32 = ctypes.windll.kernel32
k32.GetCurrentProcessId.restype = ctypes.c_ulong
print("agent v0 (python), pid=", k32.GetCurrentProcessId())
input("Press Enter to exit")

