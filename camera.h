#ifndef CAMERA_H
#define CAMERA_H

#include <napi.h>
#include "capture.h"

class Camera : public Napi::ObjectWrap<Camera> {
 public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  Camera(const Napi::CallbackInfo& info);
  ~Camera();

  CCapture* device;
  IMFActivate* claimedActivate = nullptr;
  std::wstring claimedTempFile;

  Napi::Value EnumerateDevicesAsync(const Napi::CallbackInfo& info);
  Napi::Value ClaimDeviceAsync(const Napi::CallbackInfo& info);
  // Napi::Value ReleaseDeviceAsync(const Napi::CallbackInfo& info);
  // Napi::Value StartCaptureAsync(const Napi::CallbackInfo& info);
  // Napi::Value StopCaptureAsync(const Napi::CallbackInfo& info);
  // Napi::Value GetDimensions(const Napi::CallbackInfo& info);
  // Napi::Value GetSupportedFormatsAsync(const Napi::CallbackInfo& info);
  // Napi::Value SetDesiredFormatAsync(const Napi::CallbackInfo& info);
};

#endif
