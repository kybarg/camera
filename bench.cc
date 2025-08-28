#include <napi.h>
#include <vector>
#include <chrono>
#include <cstdint>
#include <immintrin.h>
#include <intrin.h>

using namespace Napi;

// Baseline per-byte BGRA->RGBA conversion
void baseline_rgb32_to_rgba(const uint8_t* src, uint8_t* dst, size_t pixels) {
  for (size_t i = 0; i < pixels; ++i) {
    size_t s = i * 4;
    size_t d = i * 4;
    uint8_t b = src[s + 0];
    uint8_t g = src[s + 1];
    uint8_t r = src[s + 2];
    dst[d + 0] = r;
    dst[d + 1] = g;
    dst[d + 2] = b;
    dst[d + 3] = 255;
  }
}

// Optimized 32-bit conversion (matching capture.cc)
void optimized_rgb32_to_rgba(const uint8_t* srcBytes, uint8_t* dstBytes, size_t pixels, size_t width, size_t height) {
  size_t maxPixels = width * height;
  size_t safePixelCount = pixels < maxPixels ? pixels : maxPixels;
  const uint32_t* src32 = reinterpret_cast<const uint32_t*>(srcBytes);
  uint32_t* dst32 = reinterpret_cast<uint32_t*>(dstBytes);

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
static bool has_avx2() {
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

static bool has_ssse3() {
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

static bool has_sse2() {
  int regs[4] = {0};
  __cpuidex(regs, 1, 0);
  return (regs[3] & (1 << 26)) != 0; // EDX bit 26 = SSE2
}

static bool has_sse3() {
  int regs[4] = {0};
  __cpuidex(regs, 1, 0);
  return (regs[2] & (1 << 0)) != 0; // ECX bit 0 = SSE3
}

static bool has_sse41() {
  int regs[4] = {0};
  __cpuidex(regs, 1, 0);
  return (regs[2] & (1 << 19)) != 0; // ECX bit 19 = SSE4.1
}

static bool has_avx() {
  int regs[4] = {0};
  __cpuidex(regs, 1, 0);
  return (regs[2] & (1 << 28)) != 0; // ECX bit 28 = AVX
}

static bool has_bmi2() {
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
void simd_rgb32_to_rgba(const uint8_t* srcBytes, uint8_t* dstBytes, size_t pixels) {
  if (pixels == 0) return;

  if (has_avx2()) {
    const size_t lanePixels = 8; // 8 pixels per 256-bit
    size_t vecCount = pixels / lanePixels;

    const __m256i alpha_mask = _mm256_set1_epi32(0xFF000000u);
    const __m256i shuffle_mask = _mm256_setr_epi8(
        2,1,0,3, 6,5,4,7, 10,9,8,11, 14,13,12,15,
        2,1,0,3, 6,5,4,7, 10,9,8,11, 14,13,12,15);

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
      d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255;
    }
    return;
  }

  if (has_ssse3()) {
    const size_t lanePixels = 4; // 4 pixels per 128-bit
    size_t vecCount = pixels / lanePixels;

    const __m128i alpha_mask = _mm_set1_epi32(0xFF000000u);
    const __m128i shuffle_mask = _mm_setr_epi8(2,1,0,3, 6,5,4,7, 10,9,8,11, 14,13,12,15);

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
      d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255;
    }
    return;
  }

  // Fallback to the optimized scalar/unrolled implementation
  optimized_rgb32_to_rgba(srcBytes, dstBytes, pixels, 0, 0);
}


// N-API wrapper to run both conversions many times and return timings (ms)
Value RunRgb32Bench(const CallbackInfo& info) {
  Env env = info.Env();
  if (info.Length() < 4) {
    TypeError::New(env, "expected width,height,iterations,repeat").ThrowAsJavaScriptException();
    return env.Null();
  }
  size_t width = info[0].As<Number>().Uint32Value();
  size_t height = info[1].As<Number>().Uint32Value();
  int iterations = info[2].As<Number>().Int32Value();
  int repeat = info[3].As<Number>().Int32Value();

  size_t pixels = width * height;
  size_t bytes = pixels * 4;

  std::vector<uint8_t> src(bytes);
  std::vector<uint8_t> dst(bytes);

  // fill src with pseudo-random data
  for (size_t i = 0; i < bytes; ++i) src[i] = static_cast<uint8_t>(i & 0xFF);

  // warm up
  optimized_rgb32_to_rgba(src.data(), dst.data(), pixels, width, height);
  baseline_rgb32_to_rgba(src.data(), dst.data(), pixels);

  double bestBaseline = 1e99, bestOpt = 1e99;
  for (int r = 0; r < repeat; ++r) {
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int it = 0; it < iterations; ++it) baseline_rgb32_to_rgba(src.data(), dst.data(), pixels);
    auto t1 = std::chrono::high_resolution_clock::now();
    double msBase = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (msBase < bestBaseline) bestBaseline = msBase;

    auto t2 = std::chrono::high_resolution_clock::now();
    for (int it = 0; it < iterations; ++it) optimized_rgb32_to_rgba(src.data(), dst.data(), pixels, width, height);
    auto t3 = std::chrono::high_resolution_clock::now();
    double msOpt = std::chrono::duration<double, std::milli>(t3 - t2).count();
    if (msOpt < bestOpt) bestOpt = msOpt;
  }

  // SIMD benchmark (warm-up + best of repeats)
  double bestSimd = 1e99;
  // warm up
  simd_rgb32_to_rgba(src.data(), dst.data(), pixels);
  for (int r = 0; r < repeat; ++r) {
    auto ts0 = std::chrono::high_resolution_clock::now();
    for (int it = 0; it < iterations; ++it) simd_rgb32_to_rgba(src.data(), dst.data(), pixels);
    auto ts1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(ts1 - ts0).count();
    if (ms < bestSimd) bestSimd = ms;
  }

  // Build cpu object first so it appears first when serialized in JS
  Object cpu = Object::New(env);
  cpu.Set("avx2", Boolean::New(env, has_avx2()));
  cpu.Set("ssse3", Boolean::New(env, has_ssse3()));
  cpu.Set("sse2", Boolean::New(env, has_sse2()));
  cpu.Set("sse3", Boolean::New(env, has_sse3()));
  cpu.Set("sse4_1", Boolean::New(env, has_sse41()));
  cpu.Set("avx", Boolean::New(env, has_avx()));
  cpu.Set("bmi2", Boolean::New(env, has_bmi2()));

  Object result = Object::New(env);
  result.Set("cpu", cpu);
  result.Set("width", Number::New(env, width));
  result.Set("height", Number::New(env, height));
  result.Set("pixels", Number::New(env, pixels));
  result.Set("baseline_ms", Number::New(env, bestBaseline));
  result.Set("optimized_ms", Number::New(env, bestOpt));
  result.Set("simd_ms", Number::New(env, bestSimd));
  return result;
}

// Removed legacy RunSimdRgb32Bench in favor of the merged RunRgb32Bench

Object BenchInit(Env env, Object exports) {
  exports.Set("runRgb32Bench", Function::New(env, RunRgb32Bench));
  return exports;
}
