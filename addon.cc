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

  return Camera::Init(env, exports);
}

NODE_API_MODULE(addon, InitAll)
