// Dantzig LCP solver. Same algorithm as Bullet/ODE btSolveDantzigLCP, with AVX2 kernels and a block warm start.
#pragma once
#include <vector>

struct FastLcpScratch {
    std::vector<float> A, b, lo, hi, w;
    std::vector<int> findex;
};

// Solves A x = b + w, lo <= x <= hi with complementarity on w (friction rows: findex[i] >= 0 scales lo/hi by x[findex[i]]).
// Inputs are not modified; x receives the solution. `guess` (optional, n bytes) marks rows expected to end strictly inside their
// bounds: they are factorised as one block and evicted if wrong, so the result is exact either way. `interiorOut` (optional,
// n bytes) receives that classification for the next call. Returns false on numerical failure (x is then unreliable).
bool FastSolveLcp(int n, const float* A, const float* b, const float* lo, const float* hi, const int* findex, float* x,
                  FastLcpScratch& s, const unsigned char* guess, unsigned char* interiorOut, int variant = 0);
// variant bits: 1 scalar triangular solves, 2 scalar factorisation, 4 scalar dots, 8 no unbounded block (game-like order).
void FastLcpPrintStats();
