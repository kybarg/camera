#ifndef CAMERA_H
#define CAMERA_H

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <napi.h>
#include <thread>

class Camera : public Napi::ObjectWrap<Camera> {
 public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  Camera(const Napi::CallbackInfo& info);
  ~Camera();

 private:
  Napi::Value EnumerateDevices(const Napi::CallbackInfo& info);
  Napi::Value SelectDevice(const Napi::CallbackInfo& info);
  void StartCapture(const Napi::CallbackInfo& info);
  void ThreadFunction();  // Helper function for the thread

  std::thread nativeThread;
  Napi::ThreadSafeFunction tsfn;

  IMFMediaSource* ppSource = NULL;
  IMFActivate** ppDevices;
  IMFSourceReader* pVideoReader;

  bool capturing;
};

#endif