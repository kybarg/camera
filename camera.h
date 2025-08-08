#ifndef CAMERA_H
#define CAMERA_H

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <napi.h>
#include <thread>
#include "device.h"

class Camera : public Napi::ObjectWrap<Camera> {
 public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  Camera(const Napi::CallbackInfo& info);
  ~Camera();

  CaptureDevice device;

  Napi::Value EnumerateDevicesAsync(const Napi::CallbackInfo& info);
  Napi::Value ClaimDeviceAsync(const Napi::CallbackInfo& info);
  Napi::Value StartCaptureAsync(const Napi::CallbackInfo& info);
  Napi::Value StopCaptureAsync(const Napi::CallbackInfo& info);
  Napi::Value GetDimensions(const Napi::CallbackInfo& info);
  Napi::Value GetSupportedFormatsAsync(const Napi::CallbackInfo& info);
  Napi::Value SetDesiredFormatAsync(const Napi::CallbackInfo& info);
};

#endif
