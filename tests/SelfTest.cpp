// Self-test that does not run the game:
//  1) loads ScrapMechanic.exe as an image (without executing it) and checks that the Bullet Dantzig
//     solver the mod replaces is found exactly where the hook expects it;
//  2) solves generated LCP problems (joint-like unbounded rows, contacts, friction rows, two-sided
//     limits) with the fast solver, cold and with a warm start, in every kernel variant, and checks
//     the solutions against the LCP conditions independently;
//  3) checks the retry chain and the layout readers the hook relies on, on fake Bullet objects.

#define FP_SELFTEST
#include "../src/FastPhysics.cpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <random>
#include <string>

namespace {

int g_failures = 0;
void Check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "OK  " : "FAIL", what);
    if (!ok) ++g_failures;
}

// --- 2) generated problems -----------------------------------------------------------------
struct Lcp {
    int n = 0;
    std::vector<float> A, b, lo, hi;
    std::vector<int> dep;
};

// A = J M^-1 J^T-like: random sparse rectangular J (rows = constraints, 6 columns per "body"), plus a
// small diagonal. Rows: `nUnb` unbounded (joints), `nLim` two-sided limited (motors), `nContact`
// contact normals (lo = 0) each followed by two friction rows depending on it.
Lcp MakeProblem(std::mt19937& rng, int bodies, int nUnb, int nLim, int nContact, float diag) {
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    std::uniform_int_distribution<int> body(0, bodies - 1);
    const int n = nUnb + nLim + 3 * nContact;
    const int cols = 6 * bodies;
    std::vector<float> J((size_t)n * cols, 0.0f);
    auto fillRow = [&](int r, int b1, int b2) {
        for (int k = 0; k < 6; ++k) { J[(size_t)r * cols + 6 * b1 + k] = u(rng); if (b2 >= 0) J[(size_t)r * cols + 6 * b2 + k] = u(rng); }
    };
    Lcp p; p.n = n;
    p.lo.assign(n, 0.0f); p.hi.assign(n, 0.0f); p.dep.assign(n, -1); p.b.resize(n);
    int r = 0;
    for (int i = 0; i < nUnb; ++i, ++r) { fillRow(r, body(rng), body(rng)); p.lo[r] = -FLT_MAX; p.hi[r] = FLT_MAX; }
    for (int i = 0; i < nLim; ++i, ++r) { fillRow(r, body(rng), body(rng)); float lim = 10.0f + 90.0f * fabsf(u(rng)); p.lo[r] = -lim; p.hi[r] = lim; }
    for (int i = 0; i < nContact; ++i) {
        int b1 = body(rng), b2 = body(rng);
        fillRow(r, b1, b2); p.lo[r] = 0.0f; p.hi[r] = 1e10f; int normal = r++;
        for (int f = 0; f < 2; ++f, ++r) { fillRow(r, b1, b2); p.lo[r] = -0.8f; p.hi[r] = 0.8f; p.dep[r] = normal; }
    }
    p.A.assign((size_t)n * n, 0.0f);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j <= i; ++j) {
            double s = 0;
            for (int k = 0; k < cols; ++k) s += (double)J[(size_t)i * cols + k] * J[(size_t)j * cols + k];
            if (i == j) s += diag;
            p.A[(size_t)i * n + j] = p.A[(size_t)j * n + i] = (float)s;
        }
    for (int i = 0; i < n; ++i) p.b[i] = 5.0f * u(rng);
    return p;
}

// Violations of the LCP conditions: bounds, w ~ 0 inside, w >= 0 at lo, w <= 0 at hi. Friction rows are special:
// Dantzig's method fixes their bounds from the normal forces at the moment it reaches the first friction row, and
// the normal forces may still move afterwards, so for them only gross errors count (|x| beyond the largest force).
int Violations(const Lcp& p, const float* x) {
    int n = p.n, bad = 0;
    std::vector<float> lo(p.lo), hi(p.hi);
    float maxX = 0; for (int i = 0; i < n; ++i) if (p.dep[i] < 0 && fabsf(x[i]) > maxX) maxX = fabsf(x[i]);
    for (int i = 0; i < n; ++i) if (p.dep[i] >= 0) { float f = fabsf(hi[i] * x[p.dep[i]]); hi[i] = f; lo[i] = -f; }
    for (int i = 0; i < n; ++i) {
        if (x[i] != x[i]) { ++bad; continue; }
        if (p.dep[i] >= 0) { if (fabsf(x[i]) > fabsf(p.hi[i]) * maxX + 1e-3f) ++bad; continue; }
        double w = -p.b[i];
        for (int j = 0; j < n; ++j) w += (double)p.A[(size_t)i * n + j] * x[j];
        double tol = 1e-3 * (1.0 + fabs(p.b[i]));
        float el = 1e-5f * (1 + fabsf(lo[i])), eh = 1e-5f * (1 + fabsf(hi[i]));
        if (x[i] < lo[i] - el || x[i] > hi[i] + eh) { ++bad; continue; }
        bool atLo = x[i] <= lo[i] + el, atHi = x[i] >= hi[i] - eh;
        if (atLo && atHi) continue;
        if (atLo) { if (w < -tol) ++bad; }
        else if (atHi) { if (w > tol) ++bad; }
        else if (fabs(w) > tol) ++bad;
    }
    return bad;
}

void SolverTests() {
    printf("Generated LCP problems:\n");
    std::mt19937 rng(12345);
    FastLcpScratch scratch;
    int solved = 0, total = 0, badSolutions = 0, warmSolved = 0, warmTotal = 0, variantBad = 0;
    for (int t = 0; t < 12; ++t) {
        int bodies = 8 + t * 3;
        Lcp p = MakeProblem(rng, bodies, 10 + 6 * t, 2 + t, 6 + 4 * t, 0.05f);
        std::vector<float> x(p.n), x2(p.n);
        std::vector<unsigned char> interior(p.n), interior2(p.n);
        ++total;
        bool ok = FastSolveLcp(p.n, p.A.data(), p.b.data(), p.lo.data(), p.hi.data(), p.dep.data(), x.data(), scratch, nullptr, interior.data(), 0);
        if (ok) { ++solved; if (Violations(p, x.data()) > 0) ++badSolutions; }
        // warm start from the cold result (a perfect guess), and from a perturbed problem (a slightly wrong guess)
        ++warmTotal;
        bool ok2 = FastSolveLcp(p.n, p.A.data(), p.b.data(), p.lo.data(), p.hi.data(), p.dep.data(), x2.data(), scratch, interior.data(), interior2.data(), 0);
        if (ok2 && Violations(p, x2.data()) == 0) ++warmSolved;
        Lcp q = p;
        for (int i = 0; i < q.n; ++i) q.b[i] += 0.3f * (float)((i * 7919) % 13 - 6) / 6.0f;
        ++warmTotal;
        bool ok3 = FastSolveLcp(q.n, q.A.data(), q.b.data(), q.lo.data(), q.hi.data(), q.dep.data(), x2.data(), scratch, interior.data(), interior2.data(), 0);
        if (ok3 && Violations(q, x2.data()) == 0) ++warmSolved;
        // every kernel / ordering variant gives a valid solution too
        for (int v : {1, 2, 4, 7, 8}) {
            bool okv = FastSolveLcp(p.n, p.A.data(), p.b.data(), p.lo.data(), p.hi.data(), p.dep.data(), x2.data(), scratch, nullptr, nullptr, v);
            if (!okv || Violations(p, x2.data()) > 0) ++variantBad;
        }
    }
    char buf[160];
    snprintf(buf, sizeof buf, "cold start: %d of %d problems solved, %d invalid solutions", solved, total, badSolutions);
    Check(solved == total && badSolutions == 0, buf);
    snprintf(buf, sizeof buf, "warm start (exact and perturbed guess): %d of %d valid", warmSolved, warmTotal);
    Check(warmSolved == warmTotal, buf);
    snprintf(buf, sizeof buf, "scalar kernels and game-like ordering variants: %d invalid", variantBad);
    Check(variantBad == 0, buf);

    // a singular joint block (duplicated unbounded rows): the solver must either solve it or report failure, never hang or return NaN
    {
        Lcp p = MakeProblem(rng, 6, 12, 2, 4, 0.0f);
        for (int k = 0; k < p.n; ++k) { p.A[(size_t)1 * p.n + k] = p.A[(size_t)0 * p.n + k]; p.A[(size_t)k * p.n + 1] = p.A[(size_t)k * p.n + 0]; }
        p.b[1] = p.b[0];
        std::vector<float> x(p.n);
        bool ok = FastSolveLcp(p.n, p.A.data(), p.b.data(), p.lo.data(), p.hi.data(), p.dep.data(), x.data(), scratch, nullptr, nullptr, 0);
        bool nan = false; for (float v : x) if (v != v) nan = true;
        Check(!ok || !nan, "singular joint block: returns (no hang), no NaN when reported as solved");
    }
}

// --- 3) the hook's view of Bullet objects -------------------------------------------------------
// btMatrixXu / btVectorXu / btAlignedObjectArray<int> laid out as in the game (see SolveHook).
struct FakeMatrix { int rows, cols, ops, resizeOps, setElemOps; int pad; int allocPad; int size; int capacity; int pad2; float* data; };
struct FakeVector { int allocPad; int size; int capacity; int pad; float* data; };
struct FakeIntArray { int allocPad; int size; int capacity; int pad; int* data; };
static_assert(offsetof(FakeMatrix, size) == 0x1c, "btMatrixX storage size offset");
static_assert(offsetof(FakeMatrix, data) == 0x28, "btMatrixX storage data offset");
static_assert(offsetof(FakeVector, data) == 0x10, "btVectorX data offset");
static_assert(offsetof(FakeVector, size) == 4, "btVectorX size offset");

int g_origCalls = 0;
bool FakeOrig(void*, const uint8_t*, const uint8_t*, uint8_t* x, const uint8_t*, const uint8_t*, const uint8_t*, int, uintptr_t) {
    ++g_origCalls;
    const FakeVector* v = reinterpret_cast<const FakeVector*>(x);
    for (int i = 0; i < v->size; ++i) v->data[i] = -1.0f;
    return true;
}

void HookTests() {
    printf("Hook on fake Bullet objects:\n");
    g_orig = &FakeOrig;
    g_enabled = 1; g_warmStart = 1; g_verify = 0; g_verifyLeft = 0;
    InitializeCriticalSection(&g_statsCs);
    QueryPerformanceFrequency(&g_freq);
    std::mt19937 rng(777);
    Lcp p = MakeProblem(rng, 10, 20, 4, 10, 0.05f);
    std::vector<float> x(p.n, 0.0f);
    FakeMatrix A{p.n, p.n, 0, 0, 0, 0, 0, p.n * p.n, p.n * p.n, 0, p.A.data()};
    FakeVector b{0, p.n, p.n, 0, p.b.data()}, xv{0, p.n, p.n, 0, x.data()}, lo{0, p.n, p.n, 0, p.lo.data()}, hi{0, p.n, p.n, 0, p.hi.data()};
    FakeIntArray dep{0, p.n, p.n, 0, p.dep.data()};
    auto call = [&]() {
        return SolveHook(nullptr, reinterpret_cast<const uint8_t*>(&A), reinterpret_cast<const uint8_t*>(&b), reinterpret_cast<uint8_t*>(&xv),
                         reinterpret_cast<const uint8_t*>(&lo), reinterpret_cast<const uint8_t*>(&hi), reinterpret_cast<const uint8_t*>(&dep), 1440, 1);
    };
    bool ok = call();
    Check(ok && g_origCalls == 0 && Violations(p, x.data()) == 0, "a valid problem is solved by the fast solver, the game's is not called");
    bool ok2 = call();
    Check(ok2 && g_stats.cacheHits == 1 && Violations(p, x.data()) == 0, "the second tick of the same creation uses the warm start");
    A.size = p.n * p.n - 1;   // layout mismatch -> the game's solver
    call();
    Check(g_origCalls == 1 && x[0] == -1.0f, "an unexpected matrix layout goes to the game's solver untouched");
    A.size = p.n * p.n;
    g_enabled = 0;
    call();
    Check(g_origCalls == 2, "Enabled=0 passes everything to the game's solver");
    g_enabled = 1;
}

// --- 1) the game image ----------------------------------------------------------------------------
void ImageTests(const wchar_t* exePath) {
    printf("ScrapMechanic.exe image:\n");
    HMODULE exe = LoadLibraryExW(exePath, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!exe) { Check(false, "ScrapMechanic.exe loads as an image (pass its path as the first argument if the game is elsewhere)"); return; }
    Check(InitSections(reinterpret_cast<uint8_t*>(exe)), ".text/.rdata/.data sections found");
    void** vt = FindVTable(".?AVbtDantzigSolver@@");
    Check(vt != nullptr && g_rdata.Contains(vt), "btDantzigSolver vtable found through RTTI");
    if (vt) {
        Check(g_text.Contains(vt[0]) && g_text.Contains(vt[1]), "its destructor and solveMLCP point into .text");
    }
    Check(FindVTable(".?AVbtMLCPSolverInterface@@") != nullptr, "btMLCPSolverInterface (the solver's base class) found");
    void** world = FindVTable(".?AVTickDynamicsWorld@@");
    Check(world != nullptr, "TickDynamicsWorld (the game's dynamics world) found: this is Scrap Mechanic's physics");
    uint8_t* sweep = FindUnique(g_text, kProbeSweepSig, sizeof kProbeSweepSig);
    Check(sweep != nullptr, "reflection probe placement sweep: signature found exactly once");
    if (sweep) Check(Read<uint32_t>(sweep + 10) == 0x42000000 && Read<uint32_t>(sweep + 18) == 0x42000000, "both radius immediates are 32.0f");
    uint8_t* nav = FindUnique(g_text, kNavMeshSig, sizeof kNavMeshSig);
    Check(nav != nullptr, "navmesh duplicate-box search: signature found exactly once");
    FreeLibrary(exe);
}

}  // namespace

int main(int argc, char** argv) {
    wchar_t exePath[MAX_PATH] = L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Scrap Mechanic\\Release\\ScrapMechanic.exe";
    if (argc > 1) MultiByteToWideChar(CP_ACP, 0, argv[1], -1, exePath, MAX_PATH);
    printf("FastPhysics %s self-test\n", FP_VERSION_STRING);
    Check(CpuSupportsAvx2Fma(), "this CPU has AVX2 and FMA (needed by the solver kernels)");
    ImageTests(exePath);
    SolverTests();
    HookTests();
    printf(g_failures ? "\n%d check(s) FAILED\n" : "\nAll checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
