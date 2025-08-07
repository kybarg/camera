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
  Napi::ThreadSafeFunction tsfn;
  Napi::ThreadSafeFunction tsfnCapturing;
  std::thread nativeThread;

  Napi::Value EnumerateDevices(const Napi::CallbackInfo& info);
  Napi::Value SelectDevice(const Napi::CallbackInfo& info);
  Napi::Value StartCapture(const Napi::CallbackInfo& info);
  Napi::Value StopCapture(const Napi::CallbackInfo& info);
  Napi::Value GetWidth(const Napi::CallbackInfo& info);
  Napi::Value GetHeight(const Napi::CallbackInfo& info);
};

#endif
