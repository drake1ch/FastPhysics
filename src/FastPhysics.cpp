// FastPhysics: swaps the Dantzig LCP solver of Scrap Mechanic's physics (btDantzigSolver::solveMLCP) for a faster
// build of the same algorithm. AVX2 kernels plus a warm start from the previous tick; the warm start is verified
// and repaired, so the answer is the exact one either way. Big creations (lots of bearings/pistons and parts
// pressing on each other in one island) spend 10-30 ms per tick in that solver, this brings it down to 1-3.
//
// Settings: FastPhysics.ini next to the DLL. Log: FastPhysics.log.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>

#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <vector>

#include "fastlcp.h"
#include "version.h"

namespace {

HANDLE g_log = INVALID_HANDLE_VALUE;

bool PathNextToModule(HMODULE module, const wchar_t* fileName, wchar_t (&out)[MAX_PATH]) {
    DWORD len = GetModuleFileNameW(module, out, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;
    wchar_t* slash = wcsrchr(out, L'\\');
    if (!slash) return false;
    size_t room = MAX_PATH - static_cast<size_t>(slash + 1 - out);
    return wcscpy_s(slash + 1, room, fileName) == 0;
}

void Log(const char* fmt, ...) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    char buf[1024];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int len = snprintf(buf, sizeof(buf), "[%02u:%02u:%02u] ", t.wHour, t.wMinute, t.wSecond);
    va_list args;
    va_start(args, fmt);
    len += vsnprintf(buf + len, sizeof(buf) - static_cast<size_t>(len) - 2, fmt, args);
    va_end(args);
    if (len < 0) return;
    if (len > static_cast<int>(sizeof(buf)) - 3) len = sizeof(buf) - 3;
    buf[len++] = '\r';
    buf[len++] = '\n';
    DWORD written;
    WriteFile(g_log, buf, static_cast<DWORD>(len), &written, nullptr);
}

struct Range {
    uint8_t* begin = nullptr;
    uint8_t* end = nullptr;
    bool Contains(const void* p) const { return p >= begin && p < end; }
};

uint8_t* g_base = nullptr;
Range g_text, g_rdata, g_data;

bool IsScrapMechanicExe() {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _wcsicmp(name, L"ScrapMechanic.exe") == 0;
}

bool InitSections(uint8_t* exeBase) {
    g_base = exeBase;
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(g_base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(g_base + dos->e_lfanew);
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        char name[IMAGE_SIZEOF_SHORT_NAME + 1] = {};
        memcpy(name, sec->Name, IMAGE_SIZEOF_SHORT_NAME);
        Range r{g_base + sec->VirtualAddress, g_base + sec->VirtualAddress + sec->Misc.VirtualSize};
        if (strcmp(name, ".text") == 0) g_text = r;
        else if (strcmp(name, ".rdata") == 0) g_rdata = r;
        else if (strcmp(name, ".data") == 0) g_data = r;
    }
    return g_text.begin && g_rdata.begin && g_data.begin;
}

template <typename T>
T Read(const void* p) {
    T value;
    memcpy(&value, p, sizeof(T));
    return value;
}

uint8_t* FindBytes(const Range& r, const void* needle, size_t size, uint8_t* from) {
    for (uint8_t* p = from; p + size <= r.end; ++p) {
        if (memcmp(p, needle, size) == 0) return p;
    }
    return nullptr;
}

// MSVC RTTI walk: type descriptor name -> complete object locator -> vtable.
void** FindVTable(const char* mangledName) {
    const size_t nameSize = strlen(mangledName) + 1;
    for (const Range* r : {&g_data, &g_rdata}) {
        for (uint8_t* name = FindBytes(*r, mangledName, nameSize, r->begin); name;
             name = FindBytes(*r, mangledName, nameSize, name + 1)) {
            const uint32_t tdRva = static_cast<uint32_t>(name - 0x10 - g_base);
            for (uint8_t* col = g_rdata.begin; col + 24 <= g_rdata.end; col += 4) {
                if (Read<uint32_t>(col) != 1 || Read<uint32_t>(col + 4) != 0) continue;
                if (Read<uint32_t>(col + 12) != tdRva) continue;
                if (Read<uint32_t>(col + 20) != static_cast<uint32_t>(col - g_base)) continue;
                for (uint8_t* slot = g_rdata.begin; slot + 16 <= g_rdata.end; slot += 8) {
                    if (Read<uint8_t*>(slot) == col) return reinterpret_cast<void**>(slot + 8);
                }
            }
        }
    }
    return nullptr;
}

bool WriteProtected(void* at, const void* bytes, size_t size) {
    DWORD oldProtect;
    if (!VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    memcpy(at, bytes, size);
    VirtualProtect(at, size, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), at, size);
    return true;
}

bool PatchPointer(void** slot, void* hook, void** original) {
    *original = *slot;
    return WriteProtected(slot, &hook, sizeof(hook));
}

LARGE_INTEGER g_freq;
LONGLONG Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
double Ms(LONGLONG ticks) { return 1000.0 * static_cast<double>(ticks) / static_cast<double>(g_freq.QuadPart); }

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
int g_enabled = 1, g_warmStart = 1, g_verify = 0, g_minRows = 0, g_logInterval = 5, g_dumpFailures = 0;

// ---------------------------------------------------------------------------
// Warm-start cache: active set of the last solve of the same problem. A bad guess costs time, never correctness.
// ---------------------------------------------------------------------------
struct CacheEntry {
    uint64_t key = 0;
    int n = 0;
    LONGLONG lastUse = 0;
    std::vector<unsigned char> interior;
};
CacheEntry g_cache[32];
SRWLOCK g_cacheLock = SRWLOCK_INIT;

uint64_t HashBytes(uint64_t h, const void* p, size_t size) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < size; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

// The bound values drift every tick (motor/piston limits get recomputed), so hash the structure only:
// row count, friction dependencies, kind of bounds per row.
uint64_t HashProblem(int n, const float* lo, const float* hi, const int* dep) {
    uint64_t h = 14695981039346656037ull;
    h = HashBytes(h, &n, sizeof n);
    h = HashBytes(h, dep, sizeof(int) * n);
    for (int i = 0; i < n; ++i) {
        unsigned char cls;
        if (dep[i] >= 0) cls = 4;
        else if (lo[i] <= -FLT_MAX && hi[i] >= FLT_MAX) cls = 0;
        else if (lo[i] == 0.0f) cls = 1;
        else if (hi[i] == 0.0f) cls = 3;
        else if (lo[i] < 0.0f && hi[i] > 0.0f) cls = 2;
        else cls = 5;
        h ^= cls; h *= 1099511628211ull;
    }
    return h ? h : 1;
}

bool CacheLookup(uint64_t key, int n, std::vector<unsigned char>& out) {
    AcquireSRWLockShared(&g_cacheLock);
    bool found = false;
    for (CacheEntry& e : g_cache) {
        if (e.key == key && e.n == n) { out = e.interior; found = true; break; }
    }
    ReleaseSRWLockShared(&g_cacheLock);
    return found;
}

void CacheStore(uint64_t key, int n, const std::vector<unsigned char>& interior) {
    LONGLONG now = Now();
    AcquireSRWLockExclusive(&g_cacheLock);
    CacheEntry* target = nullptr;
    for (CacheEntry& e : g_cache) if (e.key == key && e.n == n) { target = &e; break; }
    if (!target) {
        target = &g_cache[0];
        for (CacheEntry& e : g_cache) if (e.lastUse < target->lastUse) target = &e;
    }
    target->key = key; target->n = n; target->lastUse = now; target->interior = interior;
    ReleaseSRWLockExclusive(&g_cacheLock);
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------
CRITICAL_SECTION g_statsCs;
struct Stats {
    int calls = 0, fast = 0, fallback = 0, invalid = 0, skipped = 0, cacheHits = 0, maxRows = 0, retries = 0, evictions = 0;
    LONGLONG tFast = 0, tFastMax = 0, tOrig = 0;
    int origCalls = 0;
    void Reset() { *this = Stats(); }
} g_stats;
LONGLONG g_lastReport = 0;

void Report() {
    Stats s;
    EnterCriticalSection(&g_statsCs);
    s = g_stats; g_stats.Reset();
    LeaveCriticalSection(&g_statsCs);
    if (s.calls == 0) return;
    Log("solves %d: fast %d (avg %.3f ms, max %.3f ms, max rows %d, warm-start hits %d, cold retries %d), fallback to game solver %d, skipped %d, invalid layout %d%s",
        s.calls, s.fast, s.fast ? Ms(s.tFast) / s.fast : 0.0, Ms(s.tFastMax), s.maxRows, s.cacheHits, s.retries, s.fallback, s.skipped, s.invalid,
        s.origCalls ? " | verify" : "");
    if (s.origCalls) Log("  game solver for comparison: %d calls, avg %.3f ms", s.origCalls, Ms(s.tOrig) / s.origCalls);
}

// ---------------------------------------------------------------------------
// The hook. btDantzigSolver::solveMLCP(const btMatrixXu& A, const btVectorXu& b, btVectorXu& x, const btVectorXu& lo,
// const btVectorXu& hi, const btAlignedObjectArray<int>& limitDependency, int numIterations, bool useSparsity)
// btMatrixX: rows +0, cols +4, storage size +0x1c, storage data +0x28. btVectorX / btAlignedObjectArray: size +4, data +0x10.
// ---------------------------------------------------------------------------
using DantzigFn = bool (*)(void*, const uint8_t*, const uint8_t*, uint8_t*, const uint8_t*, const uint8_t*, const uint8_t*, int, uintptr_t);
DantzigFn g_orig = nullptr;
LONG g_verifyLeft = 0;

HANDLE g_failDump = INVALID_HANDLE_VALUE; LONG g_failDumpLeft = 10;
void DumpFailure(int n, const float* A, const float* b, const float* lo, const float* hi, const int* dep) {
    if (g_failDump == INVALID_HANDLE_VALUE || InterlockedDecrement(&g_failDumpLeft) < 0) return;
    EnterCriticalSection(&g_statsCs);
    int hdr[4] = {0x50434c4d, n, 0, 0};
    DWORD w;
    WriteFile(g_failDump, hdr, sizeof hdr, &w, nullptr);
    WriteFile(g_failDump, A, static_cast<DWORD>(sizeof(float) * n * n), &w, nullptr);
    WriteFile(g_failDump, b, sizeof(float) * n, &w, nullptr);
    WriteFile(g_failDump, lo, sizeof(float) * n, &w, nullptr);
    WriteFile(g_failDump, hi, sizeof(float) * n, &w, nullptr);
    WriteFile(g_failDump, dep, sizeof(int) * n, &w, nullptr);
    std::vector<float> zeros(n, 0.0f);
    WriteFile(g_failDump, zeros.data(), sizeof(float) * n, &w, nullptr);
    FlushFileBuffers(g_failDump);
    LeaveCriticalSection(&g_statsCs);
}

thread_local FastLcpScratch t_scratch;
thread_local std::vector<unsigned char> t_guess, t_interior;
thread_local std::vector<float> t_ours;

bool SolveHook(void* self, const uint8_t* A, const uint8_t* b, uint8_t* x, const uint8_t* lo, const uint8_t* hi, const uint8_t* dep, int numIterations, uintptr_t sparsity) {
    if (!g_enabled) return g_orig(self, A, b, x, lo, hi, dep, numIterations, sparsity);
    const int rows = Read<int>(A), cols = Read<int>(A + 4), storage = Read<int>(A + 0x1c);
    const float* Ad = Read<const float*>(A + 0x28);
    const float* bD = Read<const float*>(b + 0x10);
    float* xD = Read<float*>(x + 0x10);
    const float* loD = Read<const float*>(lo + 0x10);
    const float* hiD = Read<const float*>(hi + 0x10);
    const int* depD = Read<const int*>(dep + 0x10);
    const bool valid = rows > 0 && rows == cols && storage == rows * cols && Ad && bD && xD && loD && hiD && depD &&
                       Read<int>(b + 4) == rows && Read<int>(x + 4) == rows && Read<int>(lo + 4) == rows && Read<int>(hi + 4) == rows && Read<int>(dep + 4) == rows;
    if (!valid) {
        EnterCriticalSection(&g_statsCs); ++g_stats.calls; ++g_stats.invalid; LeaveCriticalSection(&g_statsCs);
        return g_orig(self, A, b, x, lo, hi, dep, numIterations, sparsity);
    }
    if (rows < g_minRows) {
        EnterCriticalSection(&g_statsCs); ++g_stats.calls; ++g_stats.skipped; LeaveCriticalSection(&g_statsCs);
        return g_orig(self, A, b, x, lo, hi, dep, numIterations, sparsity);
    }

    const unsigned char* guess = nullptr;
    uint64_t key = 0;
    bool hit = false;
    if (g_warmStart) {
        key = HashProblem(rows, loD, hiD, depD);
        if (CacheLookup(key, rows, t_guess) && static_cast<int>(t_guess.size()) == rows) { guess = t_guess.data(); hit = true; }
    }
    // Test mode: run the game's solver first on the untouched problem, keep its answer for comparison.
    bool verifyNow = g_verifyLeft > 0 && InterlockedDecrement(&g_verifyLeft) >= 0;
    LONGLONG tOrig = 0;
    bool okOrig = false;
    if (verifyNow) {
        LONGLONG t1 = Now();
        okOrig = g_orig(self, A, b, x, lo, hi, dep, numIterations, sparsity);
        tOrig = Now() - t1;
        t_ours.assign(xD, xD + rows);   // the game's x
    }

    t_interior.resize(rows);
    LONGLONG t0 = Now();
    // Dantzig can stall on a degenerate pivot (the game's own solver does too). Retry with different arithmetic /
    // pivot order before giving up: cold, scalar factorisation, then the game's row order.
    bool ok = FastSolveLcp(rows, Ad, bD, loD, hiD, depD, xD, t_scratch, guess, t_interior.data(), 0);
    bool retried = false;
    if (!ok && guess) { retried = true; ok = FastSolveLcp(rows, Ad, bD, loD, hiD, depD, xD, t_scratch, nullptr, t_interior.data(), 0); }
    if (!ok) { retried = true; ok = FastSolveLcp(rows, Ad, bD, loD, hiD, depD, xD, t_scratch, nullptr, t_interior.data(), 2); }   // scalar factorisation
    if (!ok) { retried = true; ok = FastSolveLcp(rows, Ad, bD, loD, hiD, depD, xD, t_scratch, nullptr, t_interior.data(), 8); }   // the game's row order
    if (ok) {   // the game rejects NaN solutions too (its solver falls back to the iterative one); mirror that
        for (int i = 0; i < rows; ++i) if (xD[i] != xD[i]) { ok = false; break; }
    }
    LONGLONG dt = Now() - t0;
    if (ok && g_warmStart) CacheStore(key, rows, t_interior);

    if (verifyNow) {
        double maxDiff = 0, maxX = 0;
        for (int i = 0; i < rows; ++i) {
            double d = fabs(static_cast<double>(t_ours[i]) - xD[i]);
            if (d > maxDiff) maxDiff = d;
            if (fabs(t_ours[i]) > maxX) maxX = fabs(t_ours[i]);
        }
        Log("verify: %d rows | ours %.3f ms (ok %d%s), game %.3f ms (ok %d) | max |x_ours - x_game| %.4g, max |x| %.4g, relative %.2e, warm start %s",
            rows, Ms(dt), ok ? 1 : 0, retried ? ", cold retry" : "", Ms(tOrig), okOrig ? 1 : 0, maxDiff, maxX, maxX > 0 ? maxDiff / maxX : 0.0, hit ? "hit" : "miss");
    }
    if (!ok) {
        DumpFailure(rows, Ad, bD, loD, hiD, depD);
        EnterCriticalSection(&g_statsCs); ++g_stats.calls; ++g_stats.fallback; if (retried) ++g_stats.retries; LeaveCriticalSection(&g_statsCs);
        return g_orig(self, A, b, x, lo, hi, dep, numIterations, sparsity);
    }

    bool report = false;
    EnterCriticalSection(&g_statsCs);
    ++g_stats.calls; ++g_stats.fast; g_stats.tFast += dt; if (dt > g_stats.tFastMax) g_stats.tFastMax = dt;
    if (rows > g_stats.maxRows) g_stats.maxRows = rows;
    if (hit) ++g_stats.cacheHits;
    if (retried) ++g_stats.retries;
    if (verifyNow) { ++g_stats.origCalls; g_stats.tOrig += tOrig; }
    LONGLONG now = Now();
    if (Ms(now - g_lastReport) >= 1000.0 * g_logInterval) { g_lastReport = now; report = true; }
    LeaveCriticalSection(&g_statsCs);
    if (report) Report();
    return true;
}

bool CpuSupportsAvx2Fma() {
    int info[4];
    __cpuid(info, 0);
    if (info[0] < 7) return false;
    __cpuid(info, 1);
    const bool fma = (info[2] & (1 << 12)) != 0, osxsave = (info[2] & (1 << 27)) != 0, avx = (info[2] & (1 << 28)) != 0;
    if (!fma || !osxsave || !avx) return false;
    const unsigned long long xcr0 = _xgetbv(0);
    if ((xcr0 & 6) != 6) return false;
    __cpuidex(info, 7, 0);
    return (info[1] & (1 << 5)) != 0;
}

void Init(HMODULE self) {
    if (!IsScrapMechanicExe()) return;
    wchar_t path[MAX_PATH];
    if (PathNextToModule(self, L"FastPhysics.log", path)) {
        g_log = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    InitializeCriticalSection(&g_statsCs);
    QueryPerformanceFrequency(&g_freq);
    g_lastReport = Now();
    wchar_t ini[MAX_PATH];
    if (PathNextToModule(self, L"FastPhysics.ini", ini)) {
        g_enabled = static_cast<int>(GetPrivateProfileIntW(L"FastPhysics", L"Enabled", 1, ini));
        g_warmStart = static_cast<int>(GetPrivateProfileIntW(L"FastPhysics", L"WarmStart", 1, ini));
        g_verify = static_cast<int>(GetPrivateProfileIntW(L"FastPhysics", L"Verify", 0, ini));
        g_minRows = static_cast<int>(GetPrivateProfileIntW(L"FastPhysics", L"MinRows", 0, ini));
        g_logInterval = static_cast<int>(GetPrivateProfileIntW(L"FastPhysics", L"LogIntervalSeconds", 5, ini));
        if (g_logInterval < 1) g_logInterval = 1;
        g_dumpFailures = static_cast<int>(GetPrivateProfileIntW(L"FastPhysics", L"DumpFailures", 0, ini));
    }
    if (g_dumpFailures && PathNextToModule(self, L"FastPhysics_fail.bin", path)) {
        g_failDump = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    g_verifyLeft = g_verify;
    Log("FastPhysics %s loaded (enabled %d, warm start %d, verify %d, min rows %d)", FP_VERSION_STRING, g_enabled, g_warmStart, g_verify, g_minRows);
    if (!g_enabled) return;
    if (!CpuSupportsAvx2Fma()) { Log("CPU without AVX2/FMA: FastPhysics disabled, the game keeps its own solver"); g_enabled = 0; return; }
    if (!InitSections(reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)))) { Log("sections not found"); return; }
    void** vt = FindVTable(".?AVbtDantzigSolver@@");
    if (!vt || !g_rdata.Contains(vt) || !g_text.Contains(vt[1])) { Log("btDantzigSolver vtable not found (%p), FastPhysics inactive", vt); return; }
    void* o;
    if (!PatchPointer(&vt[1], reinterpret_cast<void*>(&SolveHook), &o)) { Log("vtable patch failed"); return; }
    g_orig = reinterpret_cast<DantzigFn>(o);
    Log("btDantzigSolver::solveMLCP hooked (original %p)", o);
}

}  // namespace

#ifndef FP_SELFTEST
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        Init(module);
    }
    return TRUE;
}
#endif
