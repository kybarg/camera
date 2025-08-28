#include <napi.h>
#include "camera.h"
#include <mfapi.h>

static void MfCleanup() {
  // Best-effort shutdown of Media Foundation at process exit.
  MFShutdown();
}

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  HRESULT hr = MFStartup(MF_VERSION);
  if (SUCCEEDED(hr)) {
    // Register atexit cleanup to call MFShutdown when the process exits.
    atexit(MfCleanup);
  }

  Napi::Object camExports = Camera::Init(env, exports);

  // Register bench exports if available
  // bench.BenchInit is declared in bench.cc
  extern Napi::Object BenchInit(Napi::Env, Napi::Object);
  BenchInit(env, camExports);

  return camExports;
}

NODE_API_MODULE(addon, InitAll)
