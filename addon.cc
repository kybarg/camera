#include <napi.h>
#include "camera.h"

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  return Camera::Init(env, exports);
}

NODE_API_MODULE(addon, InitAll)