// SM Mod Loader - loads native (DLL) mods into Scrap Mechanic.
//
// The loader stands in for vcruntime140_1.dll, which ScrapMechanic.exe imports from its own
// folder: its three exports are forwarded to the renamed original vcruntime140_1_orig.dll
// (see SMModLoader.def). When the game starts, the loader loads every *.dll from
// Release\DLLModules in alphabetical order - the same folder SM-DLL-Injector uses, so mods
// made for either loader work with both.
//
// The loader does nothing in other programs that load the same DLL (the game's editors) and
// loads the mods only once per process. What it loaded is written to SMModLoader.log next to it.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

#include "version.h"

namespace {

constexpr int kMaxMods = 128;

HANDLE g_log = INVALID_HANDLE_VALUE;

// Path of fileName in the folder of the given module
bool PathNextToModule(HMODULE module, const wchar_t* fileName, wchar_t (&out)[MAX_PATH]) {
    DWORD len = GetModuleFileNameW(module, out, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;
    wchar_t* slash = wcsrchr(out, L'\\');
    if (!slash) return false;
    size_t room = MAX_PATH - static_cast<size_t>(slash + 1 - out);
    return wcscpy_s(slash + 1, room, fileName) == 0;
}

void OpenLog(HMODULE self) {
    wchar_t path[MAX_PATH];
    if (!PathNextToModule(self, L"SMModLoader.log", path)) return;
    g_log = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
}

void Log(const char* fmt, ...) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    char buf[1024];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int len = snprintf(buf, sizeof(buf), "[%02u:%02u:%02u] ", t.wHour, t.wMinute, t.wSecond);
    va_list args;
    va_start(args, fmt);
    int body = vsnprintf(buf + len, sizeof(buf) - len - 2, fmt, args);
    va_end(args);
    if (body < 0) body = 0;
    len += body;
    if (len > static_cast<int>(sizeof(buf)) - 3) len = static_cast<int>(sizeof(buf)) - 3;
    buf[len++] = '\r';
    buf[len++] = '\n';
    DWORD written;
    WriteFile(g_log, buf, static_cast<DWORD>(len), &written, nullptr);
}

bool IsScrapMechanicExe() {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _wcsicmp(name, L"ScrapMechanic.exe") == 0;
}

int CompareNames(const void* a, const void* b) {
    return _wcsicmp(static_cast<const wchar_t*>(a), static_cast<const wchar_t*>(b));
}

void LoadMods(HMODULE self) {
    wchar_t dir[MAX_PATH];
    if (!PathNextToModule(self, L"DLLModules", dir)) return;
    wchar_t pattern[MAX_PATH];
    if (swprintf(pattern, MAX_PATH, L"%ls\\*.dll", dir) < 0) return;

    static wchar_t names[kMaxMods][MAX_PATH];
    int count = 0;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE) {
        Log("No mods: the DLLModules folder is missing or empty");
        return;
    }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (count == kMaxMods) {
            Log("More than %d mods in DLLModules, the rest are skipped", kMaxMods);
            break;
        }
        wcscpy_s(names[count++], fd.cFileName);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    qsort(names, static_cast<size_t>(count), sizeof(names[0]), &CompareNames);

    for (int i = 0; i < count; ++i) {
        wchar_t path[MAX_PATH];
        if (swprintf(path, MAX_PATH, L"%ls\\%ls", dir, names[i]) < 0) continue;
        char name[MAX_PATH * 3] = {};
        WideCharToMultiByte(CP_UTF8, 0, names[i], -1, name, sizeof(name) - 1, nullptr, nullptr);
        // The mod's own dependencies are looked up next to it first
        HMODULE mod = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (mod) Log("Loaded %s", name);
        else Log("Failed to load %s (error %lu)", name, GetLastError());
    }
}

void Init(HMODULE self) {
    if (!IsScrapMechanicExe()) return;  // the game's editors load the same DLL from the Release folder

    wchar_t mutexName[64];
    swprintf(mutexName, 64, L"Local\\SM.ModLoader.%lu", GetCurrentProcessId());
    CreateMutexW(nullptr, FALSE, mutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) return;  // another copy already loaded the mods

    OpenLog(self);
    Log("SM Mod Loader %s", SMML_VERSION_STRING);
    LoadMods(self);
    if (g_log != INVALID_HANDLE_VALUE) {
        CloseHandle(g_log);
        g_log = INVALID_HANDLE_VALUE;
    }
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        Init(module);
    }
    return TRUE;
}
