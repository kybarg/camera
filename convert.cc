#include "convert.h"
#include <immintrin.h>
#include <intrin.h>
#include <cstdint>
#include <cstddef>

// Baseline per-byte BGRA->RGBA conversion
void baseline_rgb32_to_rgba(const uint8_t* src, uint8_t* dst, size_t pixels) {
  for (size_t i = 0; i < pixels; ++i) {
    #include "convert.h"
    #include <immintrin.h>
    #include <intrin.h>
    #include <cstdint>
    #include <cstddef>

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

    // Optimized 32-bit conversion (matching capture.cc strategy)
    void optimized_rgb32_to_rgba(const uint8_t* src, uint8_t* dst, size_t pixels, size_t width, size_t height) {
      // Determine safe pixel count based on optional width/height
      size_t maxPixels = (width == 0 || height == 0) ? pixels : (width * height);
      size_t safePixelCount = pixels < maxPixels ? pixels : maxPixels;

      const uint32_t* src32 = reinterpret_cast<const uint32_t*>(src);
      uint32_t* dst32 = reinterpret_cast<uint32_t*>(dst);

      constexpr uint32_t ALPHA_MASK = 0xFF000000u;
      constexpr uint32_t GREEN_MASK = 0x0000FF00u;

      size_t i = 0;
      const size_t remainder = safePixelCount & 7u;
      const size_t vectorPixels = safePixelCount - remainder;

      for (; i < vectorPixels; i += 8) {
        uint32_t p0 = src32[i + 0]; uint32_t p1 = src32[i + 1];
        uint32_t p2 = src32[i + 2]; uint32_t p3 = src32[i + 3];
        uint32_t p4 = src32[i + 4]; uint32_t p5 = src32[i + 5];
        uint32_t p6 = src32[i + 6]; uint32_t p7 = src32[i + 7];

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
        return (regs[1] & (1 << 5)) != 0; // EBX bit 5 = AVX2
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
        return (regs[2] & (1 << 9)) != 0; // ECX bit 9 = SSSE3
      }
      return false;
    }

    bool cpu_has_sse2() {
      int regs[4] = {0};
      __cpuidex(regs, 1, 0);
      return (regs[3] & (1 << 26)) != 0; // EDX bit 26 = SSE2
    }

    bool cpu_has_sse3() {
      int regs[4] = {0};
      __cpuidex(regs, 1, 0);
      return (regs[2] & (1 << 0)) != 0; // ECX bit 0 = SSE3
    }

    bool cpu_has_sse41() {
      int regs[4] = {0};
      __cpuidex(regs, 1, 0);
      return (regs[2] & (1 << 19)) != 0; // ECX bit 19 = SSE4.1
    }

    bool cpu_has_avx() {
      int regs[4] = {0};
      __cpuidex(regs, 1, 0);
      return (regs[2] & (1 << 28)) != 0; // ECX bit 28 = AVX
    }

    bool cpu_has_bmi2() {
      int cpuInfo[4] = {0};
      __cpuid(cpuInfo, 0);
      int nIds = cpuInfo[0];
      if (nIds >= 7) {
        int regs[4] = {0};
        __cpuidex(regs, 7, 0);
        return (regs[1] & (1 << 8)) != 0; // EBX bit 8 = BMI2
      }
      return false;
    }

    // SIMD BGRA->RGBA using AVX2 (preferred) or SSSE3 fallback
    void simd_rgb32_to_rgba(const uint8_t* src, uint8_t* dst, size_t pixels) {
      if (pixels == 0) return;

      // AVX2 path: process 8 pixels (32 bytes) per loop
      if (cpu_has_avx2()) {
        const size_t lanePixels = 8; // 8 pixels per 256-bit
        size_t vecCount = pixels / lanePixels;

        const __m256i alpha_mask = _mm256_set1_epi32(0xFF000000u);
        const __m256i shuffle_mask = _mm256_setr_epi8(
            2,1,0,3, 6,5,4,7, 10,9,8,11, 14,13,12,15,
            2,1,0,3, 6,5,4,7, 10,9,8,11, 14,13,12,15);

        const __m256i* srcVec = reinterpret_cast<const __m256i*>(src);
        __m256i* dstVec = reinterpret_cast<__m256i*>(dst);

        for (size_t i = 0; i < vecCount; ++i) {
          __m256i v = _mm256_loadu_si256(srcVec + i);
          __m256i sh = _mm256_shuffle_epi8(v, shuffle_mask);
          __m256i out = _mm256_or_si256(sh, alpha_mask);
          _mm256_storeu_si256(dstVec + i, out);
        }

        size_t processed = vecCount * lanePixels;
        // remainder
        for (size_t i = processed; i < pixels; ++i) {
          const uint8_t* s = src + i * 4;
          uint8_t* d = dst + i * 4;
          d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255;
        }
        return;
      }

      // SSSE3 path: process 4 pixels (16 bytes) per loop
      if (cpu_has_ssse3()) {
        const size_t lanePixels = 4;
        size_t vecCount = pixels / lanePixels;

        const __m128i alpha_mask = _mm_set1_epi32(0xFF000000u);
        const __m128i shuffle_mask = _mm_setr_epi8(
            2,1,0,3, 6,5,4,7, 10,9,8,11, 14,13,12,15);

        const __m128i* srcVec = reinterpret_cast<const __m128i*>(src);
        __m128i* dstVec = reinterpret_cast<__m128i*>(dst);

        for (size_t i = 0; i < vecCount; ++i) {
          __m128i v = _mm_loadu_si128(srcVec + i);
          __m128i sh = _mm_shuffle_epi8(v, shuffle_mask);
          __m128i out = _mm_or_si128(sh, alpha_mask);
          _mm_storeu_si128(dstVec + i, out);
        }

        size_t processed = vecCount * lanePixels;
        for (size_t i = processed; i < pixels; ++i) {
          const uint8_t* s = src + i * 4;
          uint8_t* d = dst + i * 4;
          d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255;
        }
        return;
      }

      // Fallback to the optimized scalar path
      optimized_rgb32_to_rgba(src, dst, pixels, 0, 0);
    }
