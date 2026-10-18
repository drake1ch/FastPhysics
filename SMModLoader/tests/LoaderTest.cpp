// Test of SMModLoader.dll without the game. In a temporary folder it lays out
//   ScrapMechanic.exe   (a copy of this test, which loads SMModLoader.dll like the game loads vcruntime140_1.dll)
//   WorldBuilder.exe    (another copy: an editor, where the loader must do nothing)
//   SMModLoader.dll
//   DLLModules\TestModB.dll, TestModA.dll, Broken.dll (not a DLL)
// and checks that the mods are loaded once, in alphabetical order, that a broken file is skipped,
// that the log lists everything, and that nothing happens in other programs or without DLLModules.
// Usage: LoaderTest.exe <SMModLoader.dll> <TestMod.dll>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_failures = 0;
void Check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "OK  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::string ReadFileText(const std::wstring& path) {
    std::string text;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return text;
    char buf[4096];
    DWORD read = 0;
    while (ReadFile(f, buf, sizeof(buf), &read, nullptr) && read > 0) text.append(buf, read);
    CloseHandle(f);
    return text;
}

// Runs exe with "child" and waits for it; returns its exit code
DWORD Run(const std::wstring& exe) {
    std::wstring cmd = L"\"" + exe + L"\" child";
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return 0xFFFFFFFF;
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = 0xFFFFFFFF;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

// The child: loads the loader from its own folder twice (a second load must not load the mods again)
int Child() {
    wchar_t dir[MAX_PATH];
    GetModuleFileNameW(nullptr, dir, MAX_PATH);
    wcscpy_s(wcsrchr(dir, L'\\') + 1, 32, L"SMModLoader.dll");
    HMODULE loader = LoadLibraryW(dir);
    if (!loader) return 2;
    FreeLibrary(loader);
    loader = LoadLibraryW(dir);
    return loader ? 0 : 3;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc > 1 && wcscmp(argv[1], L"child") == 0) return Child();
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 3) {
        printf("Usage: LoaderTest.exe <SMModLoader.dll> <TestMod.dll>\n");
        return 1;
    }
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring dir = std::wstring(temp) + L"SMModLoaderTest";
    const std::wstring modules = dir + L"\\DLLModules";
    std::wstring self(MAX_PATH, L'\0');
    self.resize(GetModuleFileNameW(nullptr, &self[0], MAX_PATH));

    // A clean folder
    for (const wchar_t* f : {L"\\DLLModules\\TestModA.dll", L"\\DLLModules\\TestModB.dll", L"\\DLLModules\\Broken.dll",
                             L"\\ScrapMechanic.exe", L"\\WorldBuilder.exe", L"\\SMModLoader.dll", L"\\SMModLoader.log",
                             L"\\order.txt"}) {
        DeleteFileW((dir + f).c_str());
    }
    RemoveDirectoryW(modules.c_str());
    CreateDirectoryW(dir.c_str(), nullptr);
    bool ok = CopyFileW(self.c_str(), (dir + L"\\ScrapMechanic.exe").c_str(), FALSE) &&
              CopyFileW(self.c_str(), (dir + L"\\WorldBuilder.exe").c_str(), FALSE) &&
              CopyFileW(argv[1], (dir + L"\\SMModLoader.dll").c_str(), FALSE);
    Check(ok, "test folder laid out");

    printf("== No DLLModules folder\n");
    Check(Run(dir + L"\\ScrapMechanic.exe") == 0, "the game starts");
    std::string log = ReadFileText(dir + L"\\SMModLoader.log");
    Check(log.find("SM Mod Loader") != std::string::npos && log.find("No mods") != std::string::npos,
          "log: no mods");

    printf("== Two mods and a broken file\n");
    CreateDirectoryW(modules.c_str(), nullptr);
    ok = CopyFileW(argv[2], (modules + L"\\TestModB.dll").c_str(), FALSE) &&
         CopyFileW(argv[2], (modules + L"\\TestModA.dll").c_str(), FALSE);
    HANDLE broken = CreateFileW((modules + L"\\Broken.dll").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD written;
    WriteFile(broken, "not a dll", 9, &written, nullptr);
    CloseHandle(broken);
    Check(ok, "mods copied");
    Check(Run(dir + L"\\ScrapMechanic.exe") == 0, "the game starts");
    Check(ReadFileText(dir + L"\\order.txt") == "TestModA.dll\nTestModB.dll\n", "mods loaded once each, in alphabetical order");
    log = ReadFileText(dir + L"\\SMModLoader.log");
    Check(log.find("Loaded TestModA.dll") != std::string::npos && log.find("Loaded TestModB.dll") != std::string::npos,
          "log: both mods loaded");
    Check(log.find("Failed to load Broken.dll") != std::string::npos, "log: the broken file is reported and skipped");

    printf("== Another program loads the same DLL\n");
    DeleteFileW((dir + L"\\order.txt").c_str());
    DeleteFileW((dir + L"\\SMModLoader.log").c_str());
    Check(Run(dir + L"\\WorldBuilder.exe") == 0, "the editor starts");
    Check(ReadFileText(dir + L"\\order.txt").empty() && GetFileAttributesW((dir + L"\\SMModLoader.log").c_str()) == INVALID_FILE_ATTRIBUTES,
          "nothing loaded, no log");

    printf(g_failures ? "\n%d check(s) FAILED\n" : "\nAll checks passed%.0d\n", g_failures);
    return g_failures ? 1 : 0;
}
