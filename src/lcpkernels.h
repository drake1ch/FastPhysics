// AVX2/FMA kernels for the Dantzig LCP solver: dot products, triangular solves and the LDL^T factorisation.
// Conventions follow Bullet/ODE: L is unit lower triangular stored by rows with leading dimension `skip`,
// d[] holds the RECIPROCALS of the diagonal D.
#pragma once
#include <immintrin.h>

static inline float dot8(const float* a, const float* b, int n) {
    __m256 s0 = _mm256_setzero_ps(), s1 = _mm256_setzero_ps();
    int i = 0;
    for (; i + 16 <= n; i += 16) {
        s0 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i), _mm256_loadu_ps(b + i), s0);
        s1 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 8), _mm256_loadu_ps(b + i + 8), s1);
    }
    if (i + 8 <= n) { s0 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i), _mm256_loadu_ps(b + i), s0); i += 8; }
    s0 = _mm256_add_ps(s0, s1);
    __m128 lo = _mm256_castps256_ps128(s0), hi = _mm256_extractf128_ps(s0, 1);
    lo = _mm_add_ps(lo, hi);
    lo = _mm_hadd_ps(lo, lo);
    lo = _mm_hadd_ps(lo, lo);
    float s = _mm_cvtss_f32(lo);
    for (; i < n; ++i) s += a[i] * b[i];
    return s;
}

// Dot product with double accumulation (used for pivots / Schur complements, where cancellation hurts).
static inline float dot8d(const float* a, const float* b, int n) {
    __m256d s0 = _mm256_setzero_pd(), s1 = _mm256_setzero_pd();
    int i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256 av = _mm256_loadu_ps(a + i), bv = _mm256_loadu_ps(b + i);
        s0 = _mm256_fmadd_pd(_mm256_cvtps_pd(_mm256_castps256_ps128(av)), _mm256_cvtps_pd(_mm256_castps256_ps128(bv)), s0);
        s1 = _mm256_fmadd_pd(_mm256_cvtps_pd(_mm256_extractf128_ps(av, 1)), _mm256_cvtps_pd(_mm256_extractf128_ps(bv, 1)), s1);
    }
    s0 = _mm256_add_pd(s0, s1);
    __m128d lo = _mm_add_pd(_mm256_castpd256_pd128(s0), _mm256_extractf128_pd(s0, 1));
    double r = _mm_cvtsd_f64(lo) + _mm_cvtsd_f64(_mm_unpackhi_pd(lo, lo));
    for (; i < n; ++i) r += (double)a[i] * (double)b[i];
    return (float)r;
}

// y[0..n) += a * x[0..n)
static inline void axpy8(float* y, const float* x, float a, int n) {
    __m256 av = _mm256_set1_ps(a);
    int i = 0;
    for (; i + 8 <= n; i += 8) _mm256_storeu_ps(y + i, _mm256_fmadd_ps(av, _mm256_loadu_ps(x + i), _mm256_loadu_ps(y + i)));
    for (; i < n; ++i) y[i] += a * x[i];
}

// Solve L X = B in place (unit lower triangular L, rows contiguous).
static inline void kSolveL1(const float* L, float* B, int n, int skip) {
    for (int i = 1; i < n; ++i) B[i] -= dot8(L + (size_t)i * skip, B, i);
}

// Solve L^T X = B in place: process rows bottom-up, each row of L is an axpy on the entries above.
static inline void kSolveL1T(const float* L, float* B, int n, int skip) {
    for (int i = n - 1; i > 0; --i) {
        float xi = B[i];
        if (xi != 0.0f) axpy8(B, L + (size_t)i * skip, -xi, i);
    }
}

// In-place LDL^T of the lower triangle stored in A (rows contiguous, leading dimension skip): A becomes L, d = 1/D.
// W is scratch of at least n floats.
static inline void kFactorLDLT(float* A, float* d, int n, int skip, float* W) {
    for (int i = 0; i < n; ++i) {
        float* Li = A + (size_t)i * skip;
        for (int j = 0; j < i; ++j) {
            const float* Lj = A + (size_t)j * skip;
            float v = (Li[j] - dot8(W, Lj, j)) * d[j];   // d[j] = 1/D_j
            Li[j] = v;
            W[j] = v / d[j];                              // = v * D_j
        }
        float Di = Li[i] - dot8(W, Li, i);
        d[i] = 1.0f / Di;
    }
}

// Four dot products sharing the right-hand vector b: s_r = a_r . b, r = 0..3.
static inline void dot8x4(const float* a0, const float* a1, const float* a2, const float* a3, const float* b, int n, float& s0, float& s1, float& s2, float& s3) {
    __m256 v0 = _mm256_setzero_ps(), v1 = _mm256_setzero_ps(), v2 = _mm256_setzero_ps(), v3 = _mm256_setzero_ps();
    int i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256 bv = _mm256_loadu_ps(b + i);
        v0 = _mm256_fmadd_ps(_mm256_loadu_ps(a0 + i), bv, v0);
        v1 = _mm256_fmadd_ps(_mm256_loadu_ps(a1 + i), bv, v1);
        v2 = _mm256_fmadd_ps(_mm256_loadu_ps(a2 + i), bv, v2);
        v3 = _mm256_fmadd_ps(_mm256_loadu_ps(a3 + i), bv, v3);
    }
    auto hsum = [](__m256 v) { __m128 lo = _mm_add_ps(_mm256_castps256_ps128(v), _mm256_extractf128_ps(v, 1)); lo = _mm_hadd_ps(lo, lo); lo = _mm_hadd_ps(lo, lo); return _mm_cvtss_f32(lo); };
    s0 = hsum(v0); s1 = hsum(v1); s2 = hsum(v2); s3 = hsum(v3);
    for (; i < n; ++i) { float bi = b[i]; s0 += a0[i] * bi; s1 += a1[i] * bi; s2 += a2[i] * bi; s3 += a3[i] * bi; }
}

// Same as kFactorLDLT but processes four rows at a time. W needs 4*n floats, Dv needs n floats (the diagonal D itself).
static inline void kFactorLDLT4(float* A, float* d, int n, int skip, float* W, float* Dv) {
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        float* L[4] = {A + (size_t)i * skip, A + (size_t)(i + 1) * skip, A + (size_t)(i + 2) * skip, A + (size_t)(i + 3) * skip};
        float* Wr[4] = {W, W + n, W + 2 * n, W + 3 * n};
        for (int j = 0; j < i; ++j) {
            const float* Lj = A + (size_t)j * skip;
            float s0, s1, s2, s3;
            dot8x4(Wr[0], Wr[1], Wr[2], Wr[3], Lj, j, s0, s1, s2, s3);
            const float dj = d[j], Dj = Dv[j];
            float v;
            v = (L[0][j] - s0) * dj; L[0][j] = v; Wr[0][j] = v * Dj;
            v = (L[1][j] - s1) * dj; L[1][j] = v; Wr[1][j] = v * Dj;
            v = (L[2][j] - s2) * dj; L[2][j] = v; Wr[2][j] = v * Dj;
            v = (L[3][j] - s3) * dj; L[3][j] = v; Wr[3][j] = v * Dj;
        }
        // the 4x4 diagonal block, row by row
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < r; ++c) {
                int j = i + c;
                float v = (L[r][j] - dot8(Wr[r], L[c], j)) * d[j];
                L[r][j] = v; Wr[r][j] = v * Dv[j];
            }
            int ii = i + r;
            float Di = L[r][ii] - dot8d(Wr[r], L[r], ii);
            Dv[ii] = Di; d[ii] = 1.0f / Di;
        }
    }
    for (; i < n; ++i) {
        float* Li = A + (size_t)i * skip;
        for (int j = 0; j < i; ++j) {
            const float* Lj = A + (size_t)j * skip;
            float v = (Li[j] - dot8(W, Lj, j)) * d[j];
            Li[j] = v; W[j] = v * Dv[j];
        }
        float Di = Li[i] - dot8d(W, Li, i);
        Dv[i] = Di; d[i] = 1.0f / Di;
    }
}
