#pragma once
#include <cstdint>
#include <cstddef>

// RGB32 (BGRA) -> RGBA conversion helpers
// RGB24 (BGR24) -> RGBA conversion helpers
void baseline_rgb24_to_rgba(const uint8_t* src, uint8_t* dst, size_t pixels);
void optimized_rgb24_to_rgba(const uint8_t* src, uint8_t* dst, size_t pixels);

// RGB32 (BGRA) -> RGBA conversion helpers
void baseline_rgb32_to_rgba(const uint8_t* src, uint8_t* dst, size_t pixels);
void optimized_rgb32_to_rgba(const uint8_t* src, uint8_t* dst, size_t pixels, size_t width, size_t height);
void simd_rgb32_to_rgba(const uint8_t* src, uint8_t* dst, size_t pixels);

// CPU feature queries
bool cpu_has_avx2();
bool cpu_has_ssse3();
bool cpu_has_sse2();
bool cpu_has_sse3();
bool cpu_has_sse41();
bool cpu_has_avx();
bool cpu_has_bmi2();
