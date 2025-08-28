#include <napi.h>
#include <vector>
#include <chrono>
#include <cstdint>
#include "convert.h"

using namespace Napi;

// conversion helpers are implemented in convert.cc


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

  // RGB24 benchmarks
  size_t bytes24 = pixels * 3;
  std::vector<uint8_t> src24(bytes24);
  std::vector<uint8_t> dst24(bytes); // RGBA dest size
  for (size_t i = 0; i < bytes24; ++i) src24[i] = static_cast<uint8_t>(i & 0xFF);

  // warm up
  optimized_rgb24_to_rgba(src24.data(), dst24.data(), pixels);

  double best24Base = 1e99, best24Opt = 1e99;
  for (int r = 0; r < repeat; ++r) {
    auto t0_24 = std::chrono::high_resolution_clock::now();
    for (int it = 0; it < iterations; ++it) baseline_rgb24_to_rgba(src24.data(), dst24.data(), pixels);
    auto t1_24 = std::chrono::high_resolution_clock::now();
    double msBase24 = std::chrono::duration<double, std::milli>(t1_24 - t0_24).count();
    if (msBase24 < best24Base) best24Base = msBase24;

    auto t2_24 = std::chrono::high_resolution_clock::now();
    for (int it = 0; it < iterations; ++it) optimized_rgb24_to_rgba(src24.data(), dst24.data(), pixels);
    auto t3_24 = std::chrono::high_resolution_clock::now();
    double msOpt24 = std::chrono::duration<double, std::milli>(t3_24 - t2_24).count();
    if (msOpt24 < best24Opt) best24Opt = msOpt24;
  }

  // RGB24 SIMD benchmark (if available)
  double best24Simd = 1e99;
  // warm up
  simd_rgb24_to_rgba(src24.data(), dst24.data(), pixels);
  for (int r = 0; r < repeat; ++r) {
    auto t0s = std::chrono::high_resolution_clock::now();
    for (int it = 0; it < iterations; ++it) simd_rgb24_to_rgba(src24.data(), dst24.data(), pixels);
    auto t1s = std::chrono::high_resolution_clock::now();
    double msSimd24 = std::chrono::duration<double, std::milli>(t1s - t0s).count();
    if (msSimd24 < best24Simd) best24Simd = msSimd24;
  }

  // Build cpu object first so it appears first when serialized in JS
  Object cpu = Object::New(env);
  cpu.Set("avx2", Boolean::New(env, cpu_has_avx2()));
  cpu.Set("ssse3", Boolean::New(env, cpu_has_ssse3()));
  cpu.Set("sse2", Boolean::New(env, cpu_has_sse2()));
  cpu.Set("sse3", Boolean::New(env, cpu_has_sse3()));
  cpu.Set("sse4_1", Boolean::New(env, cpu_has_sse41()));
  cpu.Set("avx", Boolean::New(env, cpu_has_avx()));
  cpu.Set("bmi2", Boolean::New(env, cpu_has_bmi2()));

  Object result = Object::New(env);
  result.Set("cpu", cpu);
  result.Set("width", Number::New(env, width));
  result.Set("height", Number::New(env, height));
  result.Set("pixels", Number::New(env, pixels));
  result.Set("baseline_ms", Number::New(env, bestBaseline));
  result.Set("optimized_ms", Number::New(env, bestOpt));
  result.Set("simd_ms", Number::New(env, bestSimd));
  // RGB24 timings
  result.Set("rgb24_baseline_ms", Number::New(env, best24Base));
  result.Set("rgb24_optimized_ms", Number::New(env, best24Opt));
  result.Set("rgb24_simd_ms", Number::New(env, best24Simd));
  return result;
}

// Removed legacy RunSimdRgb32Bench in favor of the merged RunRgb32Bench

Object BenchInit(Env env, Object exports) {
  exports.Set("runRgb32Bench", Function::New(env, RunRgb32Bench));
  return exports;
}
