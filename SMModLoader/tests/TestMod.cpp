// A mod for the loader test: when loaded, appends its file name to order.txt next to the exe.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cwchar>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    wchar_t self[MAX_PATH];
    wchar_t order[MAX_PATH];
    if (!GetModuleFileNameW(module, self, MAX_PATH) || !GetModuleFileNameW(nullptr, order, MAX_PATH)) return TRUE;
    const wchar_t* name = wcsrchr(self, L'\\') + 1;
    wcscpy_s(wcsrchr(order, L'\\') + 1, 64, L"order.txt");
    HANDLE f = CreateFileW(order, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return TRUE;
    char line[MAX_PATH];
    int len = WideCharToMultiByte(CP_UTF8, 0, name, -1, line, sizeof(line) - 2, nullptr, nullptr);
    if (len > 0) {
        line[len - 1] = '\n';
        DWORD written;
        WriteFile(f, line, static_cast<DWORD>(len), &written, nullptr);
    }
    CloseHandle(f);
    return TRUE;
}
