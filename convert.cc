#include "convert.h"
#include <immintrin.h>
#include <intrin.h>
#include <cstdint>
#include <cstddef>

// CPU feature detection (Windows/MSVC)
static void query_cpuid(int leaf, int subleaf, int regs[4]) {
  __cpuidex(regs, leaf, subleaf);
}

bool cpu_has_avx2() {
  int cpuInfo[4] = {0};
  __cpuid(cpuInfo, 0);
  int nIds = cpuInfo[0];
  if (nIds >= 7) {
    int regs[4] = {0};
    __cpuidex(regs, 7, 0);
    return (regs[1] & (1 << 5)) != 0;  // EBX bit 5 = AVX2
  }
  return false;
}

bool cpu_has_ssse3() {
  int cpuInfo[4] = {0};
  __cpuid(cpuInfo, 0);
  int nIds = cpuInfo[0];
  if (nIds >= 1) {
    int regs[4] = {0};
    __cpuidex(regs, 1, 0);
    return (regs[2] & (1 << 9)) != 0;  // ECX bit 9 = SSSE3
  }
  return false;
}

bool cpu_has_sse2() {
  int regs[4] = {0};
  __cpuidex(regs, 1, 0);
  return (regs[3] & (1 << 26)) != 0;  // EDX bit 26 = SSE2
}

bool cpu_has_sse3() {
  int regs[4] = {0};
  __cpuidex(regs, 1, 0);
  return (regs[2] & (1 << 0)) != 0;  // ECX bit 0 = SSE3
}

bool cpu_has_sse41() {
  int regs[4] = {0};
  __cpuidex(regs, 1, 0);
  return (regs[2] & (1 << 19)) != 0;  // ECX bit 19 = SSE4.1
}

bool cpu_has_avx() {
  int regs[4] = {0};
  __cpuidex(regs, 1, 0);
  return (regs[2] & (1 << 28)) != 0;  // ECX bit 28 = AVX
}

bool cpu_has_bmi2() {
  int cpuInfo[4] = {0};
  __cpuid(cpuInfo, 0);
  int nIds = cpuInfo[0];
  if (nIds >= 7) {
    int regs[4] = {0};
    __cpuidex(regs, 7, 0);
    return (regs[1] & (1 << 8)) != 0;  // EBX bit 8 = BMI2
  }
  return false;
}

// Baseline per-byte RGB24 (BGR24) -> RGBA conversion
void baseline_rgb24_to_rgba(const uint8_t* src, uint8_t* dst, size_t pixels) {
  for (size_t i = 0; i < pixels; ++i) {
    const uint8_t* s = src + i * 3;
    uint8_t* d = dst + i * 4;
    uint8_t b = s[0];
    uint8_t g = s[1];
    uint8_t r = s[2];
    d[0] = r;
    d[1] = g;
    d[2] = b;
    d[3] = 255;
  }
}

// Optimized RGB24 -> RGBA: assemble 32-bit words and write sequentially
void optimized_rgb24_to_rgba(const uint8_t* src, uint8_t* dst, size_t pixels) {
  const uint8_t* s = src;
  uint32_t* d32 = reinterpret_cast<uint32_t*>(dst);
  size_t i = 0;
  const size_t remainder = pixels & 7u;
  const size_t vectorPixels = pixels - remainder;

  for (; i < vectorPixels; i += 8) {
    uint32_t b0 = s[0];
    uint32_t g0 = s[1];
    uint32_t r0 = s[2];
    uint32_t b1 = s[3];
    uint32_t g1 = s[4];
    uint32_t r1 = s[5];
    uint32_t b2 = s[6];
    uint32_t g2 = s[7];
    uint32_t r2 = s[8];
    uint32_t b3 = s[9];
    uint32_t g3 = s[10];
    uint32_t r3 = s[11];
    uint32_t b4 = s[12];
    uint32_t g4 = s[13];
    uint32_t r4 = s[14];
    uint32_t b5 = s[15];
    uint32_t g5 = s[16];
    uint32_t r5 = s[17];
    uint32_t b6 = s[18];
    uint32_t g6 = s[19];
    uint32_t r6 = s[20];
    uint32_t b7 = s[21];
    uint32_t g7 = s[22];
    uint32_t r7 = s[23];

    d32[i + 0] = (r0) | (g0 << 8) | (b0 << 16) | (0xFFu << 24);
    d32[i + 1] = (r1) | (g1 << 8) | (b1 << 16) | (0xFFu << 24);
    d32[i + 2] = (r2) | (g2 << 8) | (b2 << 16) | (0xFFu << 24);
    d32[i + 3] = (r3) | (g3 << 8) | (b3 << 16) | (0xFFu << 24);
    d32[i + 4] = (r4) | (g4 << 8) | (b4 << 16) | (0xFFu << 24);
    d32[i + 5] = (r5) | (g5 << 8) | (b5 << 16) | (0xFFu << 24);
    d32[i + 6] = (r6) | (g6 << 8) | (b6 << 16) | (0xFFu << 24);
    d32[i + 7] = (r7) | (g7 << 8) | (b7 << 16) | (0xFFu << 24);

    s += 24;  // 8 * 3
  }

  // remainder
  for (; i < pixels; ++i) {
    uint32_t b = s[0];
    uint32_t g = s[1];
    uint32_t r = s[2];
    d32[i] = (r) | (g << 8) | (b << 16) | (0xFFu << 24);
    s += 3;
  }
}

// Baseline per-byte BGRA->RGBA conversion
void baseline_rgb32_to_rgba(const uint8_t* src, uint8_t* dst, size_t pixels) {
  for (size_t i = 0; i < pixels; ++i) {
    const uint8_t* s = src + i * 4;
    uint8_t* d = dst + i * 4;
    uint8_t b = s[0];
    uint8_t g = s[1];
    uint8_t r = s[2];
    d[0] = r;
    d[1] = g;
    d[2] = b;
    d[3] = 255;
  }
}

// Optimized 32-bit conversion (matching capture.cc)
void optimized_rgb32_to_rgba(const uint8_t* srcBytes, uint8_t* dstBytes, size_t pixels, size_t width, size_t height) {
  size_t maxPixels = (width == 0 || height == 0) ? pixels : (width * height);
  size_t safePixelCount = pixels < maxPixels ? pixels : maxPixels;
  const uint32_t* src32 = reinterpret_cast<const uint32_t*>(srcBytes);
  uint32_t* dst32 = reinterpret_cast<uint32_t*>(dstBytes);

  constexpr uint32_t ALPHA_MASK = 0xFF000000u;
  constexpr uint32_t GREEN_MASK = 0x0000FF00u;

  size_t i = 0;
  const size_t remainder = safePixelCount & 7u;
  const size_t vectorPixels = safePixelCount - remainder;

  for (; i < vectorPixels; i += 8) {
    uint32_t p0 = src32[i + 0];
    uint32_t p1 = src32[i + 1];
    uint32_t p2 = src32[i + 2];
    uint32_t p3 = src32[i + 3];
    uint32_t p4 = src32[i + 4];
    uint32_t p5 = src32[i + 5];
    uint32_t p6 = src32[i + 6];
    uint32_t p7 = src32[i + 7];

    dst32[i + 0] = ALPHA_MASK | (p0 & GREEN_MASK) | ((p0 & 0xFFu) << 16) | ((p0 >> 16) & 0xFFu);
    dst32[i + 1] = ALPHA_MASK | (p1 & GREEN_MASK) | ((p1 & 0xFFu) << 16) | ((p1 >> 16) & 0xFFu);
    dst32[i + 2] = ALPHA_MASK | (p2 & GREEN_MASK) | ((p2 & 0xFFu) << 16) | ((p2 >> 16) & 0xFFu);
    dst32[i + 3] = ALPHA_MASK | (p3 & GREEN_MASK) | ((p3 & 0xFFu) << 16) | ((p3 >> 16) & 0xFFu);
    dst32[i + 4] = ALPHA_MASK | (p4 & GREEN_MASK) | ((p4 & 0xFFu) << 16) | ((p4 >> 16) & 0xFFu);
    dst32[i + 5] = ALPHA_MASK | (p5 & GREEN_MASK) | ((p5 & 0xFFu) << 16) | ((p5 >> 16) & 0xFFu);
    dst32[i + 6] = ALPHA_MASK | (p6 & GREEN_MASK) | ((p6 & 0xFFu) << 16) | ((p6 >> 16) & 0xFFu);
    dst32[i + 7] = ALPHA_MASK | (p7 & GREEN_MASK) | ((p7 & 0xFFu) << 16) | ((p7 >> 16) & 0xFFu);
  }

  for (; i < safePixelCount; ++i) {
    uint32_t p = src32[i];
    dst32[i] = ALPHA_MASK | (p & GREEN_MASK) | ((p & 0xFFu) << 16) | ((p >> 16) & 0xFFu);
  }
}

// SIMD BGRA->RGBA using AVX2 (preferred) or SSSE3 fallback
void simd_rgb32_to_rgba(const uint8_t* srcBytes, uint8_t* dstBytes, size_t pixels) {
  if (pixels == 0) return;

  if (cpu_has_avx2()) {
    const size_t lanePixels = 8;  // 8 pixels per 256-bit
    size_t vecCount = pixels / lanePixels;

    const __m256i alpha_mask = _mm256_set1_epi32(0xFF000000u);
    const __m256i shuffle_mask = _mm256_setr_epi8(
        2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15, 2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15);

    const __m256i* src = reinterpret_cast<const __m256i*>(srcBytes);
    __m256i* dst = reinterpret_cast<__m256i*>(dstBytes);

    for (size_t i = 0; i < vecCount; ++i) {
      __m256i v = _mm256_loadu_si256(src + i);
      __m256i sh = _mm256_shuffle_epi8(v, shuffle_mask);
      __m256i out = _mm256_or_si256(sh, alpha_mask);
      _mm256_storeu_si256(dst + i, out);
    }

    size_t processed = vecCount * lanePixels;
    // remainder
    for (size_t i = processed; i < pixels; ++i) {
      const uint8_t* s = srcBytes + i * 4;
      uint8_t* d = dstBytes + i * 4;
      d[0] = s[2];
      d[1] = s[1];
      d[2] = s[0];
      d[3] = 255;
    }
    return;
  }

  if (cpu_has_ssse3()) {
    const size_t lanePixels = 4;  // 4 pixels per 128-bit
    size_t vecCount = pixels / lanePixels;

    const __m128i alpha_mask = _mm_set1_epi32(0xFF000000u);
    const __m128i shuffle_mask = _mm_setr_epi8(2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15);

    const __m128i* src = reinterpret_cast<const __m128i*>(srcBytes);
    __m128i* dst = reinterpret_cast<__m128i*>(dstBytes);

    for (size_t i = 0; i < vecCount; ++i) {
      __m128i v = _mm_loadu_si128(src + i);
      __m128i sh = _mm_shuffle_epi8(v, shuffle_mask);
      __m128i out = _mm_or_si128(sh, alpha_mask);
      _mm_storeu_si128(dst + i, out);
    }

    size_t processed = vecCount * lanePixels;
    for (size_t i = processed; i < pixels; ++i) {
      const uint8_t* s = srcBytes + i * 4;
      uint8_t* d = dstBytes + i * 4;
      d[0] = s[2];
      d[1] = s[1];
      d[2] = s[0];
      d[3] = 255;
    }
    return;
  }

  // Fallback to the optimized scalar/unrolled implementation
  optimized_rgb32_to_rgba(srcBytes, dstBytes, pixels, 0, 0);
}
