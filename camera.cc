#include "camera.h"
#include <comdef.h>
#include <windows.h>
#include <thread>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <vector>
#include <tuple>
#include <objbase.h>

Napi::Object Camera::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func = DefineClass(env, "Camera", {InstanceMethod("claimDeviceAsync", &Camera::ClaimDeviceAsync), InstanceMethod("enumerateDevicesAsync", &Camera::EnumerateDevicesAsync), InstanceMethod("getDimensions", &Camera::GetDimensions), InstanceMethod("getSupportedFormatsAsync", &Camera::GetSupportedFormatsAsync), InstanceMethod("releaseDeviceAsync", &Camera::ReleaseDeviceAsync), InstanceMethod("setDesiredFormatAsync", &Camera::SetDesiredFormatAsync), InstanceMethod("startCaptureAsync", &Camera::StartCaptureAsync), InstanceMethod("stopCaptureAsync", &Camera::StopCaptureAsync)});

  Napi::FunctionReference* constructor = new Napi::FunctionReference();
  *constructor = Napi::Persistent(func);
  env.SetInstanceData(constructor);

  exports.Set("Camera", func);
  return exports;
}

Camera::Camera(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<Camera>(info), device(nullptr) {
  Napi::Env env = info.Env();
}

Camera::~Camera() {
  if (claimedActivate) {
    claimedActivate->Release();
    claimedActivate = nullptr;
  }
  if (device) {
    device->EndCaptureSession();
    device->Release();
    device = nullptr;
  }
  if (!claimedTempFile.empty()) {
    // Delete the temporary sink file created when claiming the device
    DeleteFileW(claimedTempFile.c_str());
    claimedTempFile.clear();
  }
}

Napi::Value Camera::EnumerateDevicesAsync(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  // Create a promise
  auto deferred = Napi::Promise::Deferred::New(env);

  // Create thread-safe function for the promise resolution
  auto tsfnPromise = Napi::ThreadSafeFunction::New(
      env,
      Napi::Function(),
      "EnumerateDevicesAsync",
      0,
      1);

  // Start async operation
  std::thread([this, deferred = std::move(deferred), tsfnPromise = std::move(tsfnPromise)]() mutable {
    try {
      // Get all devices using the capture DeviceList API (GetAllDevices now enumerates)
      DeviceList list;
      std::vector<std::pair<std::wstring, std::wstring>> devicesVec;
      HRESULT hr = list.GetAllDevices(devicesVec);
      if (SUCCEEDED(hr)) {
        auto callback = [deferred = std::move(deferred), devicesVec = std::move(devicesVec)](Napi::Env env, Napi::Function) mutable {
          Napi::Array devices = Napi::Array::New(env, static_cast<uint32_t>(devicesVec.size()));

          for (uint32_t i = 0; i < devicesVec.size(); ++i) {
            Napi::Object deviceInfo = Napi::Object::New(env);
            const auto& pair = devicesVec[i];

            // Convert std::wstring to std::u16string for N-API
            std::u16string friendlyU16(pair.first.begin(), pair.first.end());
            std::u16string symbolicU16(pair.second.begin(), pair.second.end());

            deviceInfo.Set("friendlyName", Napi::String::New(env, friendlyU16));
            deviceInfo.Set("symbolicLink", Napi::String::New(env, symbolicU16));

            devices.Set(i, deviceInfo);
          }

          deferred.Resolve(devices);
        };

        tsfnPromise.BlockingCall(callback);
      } else {
        auto callback = [deferred = std::move(deferred), hr](Napi::Env env, Napi::Function) mutable {
          _com_error err(hr);
          LPCTSTR errMsg = err.ErrorMessage();
          std::string message = errMsg;
          deferred.Reject(Napi::Error::New(env, message).Value());
        };

        tsfnPromise.BlockingCall(callback);
      }
    } catch (const std::exception& e) {
      auto callback = [deferred = std::move(deferred), message = std::string(e.what())](Napi::Env env, Napi::Function) mutable {
        deferred.Reject(Napi::Error::New(env, message).Value());
      };

      tsfnPromise.BlockingCall(callback);
    }

    tsfnPromise.Release();
  }).detach();

  return deferred.Promise();
}

Napi::Value Camera::ClaimDeviceAsync(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "Expected device identifier (friendlyName or symbolicLink) as string").ThrowAsJavaScriptException();
    return env.Null();
  }

  std::u16string idU16 = info[0].As<Napi::String>().Utf16Value();
  std::wstring identifier(idU16.begin(), idU16.end());

  auto deferred = Napi::Promise::Deferred::New(env);

  auto tsfnPromise = Napi::ThreadSafeFunction::New(
      env,
      Napi::Function(),
      "ClaimDeviceAsync",
      0,
      1);

  std::thread([this, deferred = std::move(deferred), tsfnPromise = std::move(tsfnPromise), identifier]() mutable {
    HRESULT hr = S_OK;
    // Initialize COM on this worker thread for Media Foundation/API calls.
    HRESULT hrCo = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool coInitialized = SUCCEEDED(hrCo);
    IMFActivate* pActivate = nullptr;

    DeviceList list;
    hr = list.GetDevice(identifier.c_str(), &pActivate);

    if (SUCCEEDED(hr) && pActivate) {
      // GetDevice returns an AddRef'd IMFActivate so we can store it directly.
      this->claimedActivate = pActivate;

      // Create a CCapture instance and initialize its reader from the
      // claimed activation so GetSupportedFormats can enumerate types.
      CCapture* cap = nullptr;
      HRESULT hrCreate = CCapture::CreateInstance(NULL, &cap);
      if (SUCCEEDED(hrCreate) && cap) {
        HRESULT hrInit = cap->InitFromActivate(pActivate);
        if (SUCCEEDED(hrInit)) {
          // Store device instance for subsequent operations
          this->device = cap;
        } else {
          // Init failed: cleanup
          cap->Release();
          // release claimed activate as initialization failed
          if (this->claimedActivate) {
            this->claimedActivate->Release();
            this->claimedActivate = nullptr;
          }
          hr = hrInit;
        }
      } else {
        // Could not create CCapture instance
        if (cap) cap->Release();
        if (this->claimedActivate) {
          this->claimedActivate->Release();
          this->claimedActivate = nullptr;
        }
        hr = hrCreate;
      }
    }

    if (coInitialized) {
      CoUninitialize();
    }

    if (SUCCEEDED(hr)) {
      auto callback = [deferred = std::move(deferred)](Napi::Env env, Napi::Function) mutable {
        Napi::Object result = Napi::Object::New(env);
        result.Set("success", Napi::Boolean::New(env, true));
        result.Set("message", Napi::String::New(env, "Device claimed successfully"));
        deferred.Resolve(result);
      };
      tsfnPromise.BlockingCall(callback);
    } else {
      auto callback = [deferred = std::move(deferred), hr](Napi::Env env, Napi::Function) mutable {
        _com_error err(hr);
        LPCTSTR errMsg = err.ErrorMessage();
        std::string messageBody;
#ifdef UNICODE
        if (errMsg) {
          int len = WideCharToMultiByte(CP_UTF8, 0, errMsg, -1, NULL, 0, NULL, NULL);
          if (len > 0) {
            std::string tmp(len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, errMsg, -1, &tmp[0], len, NULL, NULL);
            // remove trailing null
            if (!tmp.empty() && tmp.back() == '\0') tmp.pop_back();
            messageBody = tmp;
          }
        }
#else
        if (errMsg) messageBody = errMsg;
#endif
        char header[64];
        sprintf_s(header, sizeof(header), "HRESULT=0x%08X: ", (unsigned)hr);
        std::string message = std::string(header) + messageBody;
        deferred.Reject(Napi::Error::New(env, message).Value());
      };
      tsfnPromise.BlockingCall(callback);
    }

    tsfnPromise.Release();
  }).detach();

  return deferred.Promise();
}

// Helper: convert HRESULT to string
static std::string HResultToString(HRESULT hr) {
  _com_error err(hr);
  LPCTSTR errMsg = err.ErrorMessage();
  std::string messageBody;
#ifdef UNICODE
  if (errMsg) {
    int len = WideCharToMultiByte(CP_UTF8, 0, errMsg, -1, NULL, 0, NULL, NULL);
    if (len > 0) {
      std::string tmp(len, '\0');
      WideCharToMultiByte(CP_UTF8, 0, errMsg, -1, &tmp[0], len, NULL, NULL);
      if (!tmp.empty() && tmp.back() == '\0') tmp.pop_back();
      messageBody = tmp;
    }
  }
#else
  if (errMsg) messageBody = errMsg;
#endif
  char header[64];
  sprintf_s(header, sizeof(header), "HRESULT=0x%08X: ", (unsigned)hr);
  return std::string(header) + messageBody;
}

Napi::Value Camera::GetSupportedFormatsAsync(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  auto deferred = Napi::Promise::Deferred::New(env);

  auto tsfnPromise = Napi::ThreadSafeFunction::New(
      env,
      Napi::Function(),
      "GetSupportedFormatsAsync",
      0,
      1);

  std::thread([this, deferred = std::move(deferred), tsfnPromise = std::move(tsfnPromise)]() mutable {
    try {
      std::vector<std::tuple<UINT32, UINT32, double>> formats;
      HRESULT hr = S_OK;

      if (this->device != nullptr) {
        hr = this->device->GetSupportedFormats(formats);
      } else {
        auto callback = [deferred = std::move(deferred)](Napi::Env env, Napi::Function) mutable {
          deferred.Reject(Napi::Error::New(env, "No initialized device. Call claimDeviceAsync first to initialize the device before enumerating formats.").Value());
        };
        tsfnPromise.BlockingCall(callback);
        tsfnPromise.Release();
        return;
      }

      if (FAILED(hr)) {
        std::string errMsg = HResultToString(hr);
        auto callback = [deferred = std::move(deferred), errMsg](Napi::Env env, Napi::Function) mutable {
          deferred.Reject(Napi::Error::New(env, errMsg).Value());
        };
        tsfnPromise.BlockingCall(callback);
        tsfnPromise.Release();
        return;
      }

      auto callback = [deferred = std::move(deferred), formats = std::move(formats)](Napi::Env env, Napi::Function) mutable {
        Napi::Array arr = Napi::Array::New(env, static_cast<uint32_t>(formats.size()));
        for (uint32_t i = 0; i < formats.size(); ++i) {
          Napi::Object obj = Napi::Object::New(env);
          obj.Set("width", Napi::Number::New(env, std::get<0>(formats[i])));
          obj.Set("height", Napi::Number::New(env, std::get<1>(formats[i])));
          obj.Set("frameRate", Napi::Number::New(env, std::get<2>(formats[i])));
          // subtype removed; not exposing format subtype to JS
          arr.Set(i, obj);
        }
        deferred.Resolve(arr);
      };

      tsfnPromise.BlockingCall(callback);
    } catch (const std::exception& e) {
      auto callback = [deferred = std::move(deferred), message = std::string(e.what())](Napi::Env env, Napi::Function) mutable {
        deferred.Reject(Napi::Error::New(env, message).Value());
      };
      tsfnPromise.BlockingCall(callback);
    }
    tsfnPromise.Release();
  }).detach();

  return deferred.Promise();
}

Napi::Value Camera::SetDesiredFormatAsync(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 3 || !info[0].IsNumber() || !info[1].IsNumber() || !info[2].IsNumber()) {
    Napi::TypeError::New(env, "Expected width, height, and frameRate as numbers").ThrowAsJavaScriptException();
    return env.Null();
  }

  UINT32 width = info[0].As<Napi::Number>().Uint32Value();
  UINT32 height = info[1].As<Napi::Number>().Uint32Value();
  double frameRate = info[2].As<Napi::Number>().DoubleValue();

  auto deferred = Napi::Promise::Deferred::New(env);

  auto tsfnPromise = Napi::ThreadSafeFunction::New(
      env,
      Napi::Function(),
      "SetDesiredFormatAsync",
      0,
      1);

  std::thread([this, deferred = std::move(deferred), tsfnPromise = std::move(tsfnPromise), width, height, frameRate]() mutable {
    try {
      // Delegate to CCapture to set the desired format
      if (!this->device) {
        auto callback = [deferred = std::move(deferred)](Napi::Env env, Napi::Function) mutable {
          deferred.Reject(Napi::Error::New(env, "Device not initialized").Value());
        };
        tsfnPromise.BlockingCall(callback);
        tsfnPromise.Release();
        return;
      }

      // Validate requested format exists in the last enumerated formats cache on CCapture
      const auto& cache = this->device->GetLastSupportedFormats();
      bool found = false;
      for (const auto& f : cache) {
        if (std::get<0>(f) == width && std::get<1>(f) == height && std::abs(std::get<2>(f) - frameRate) < 1e-6) {
          found = true;
          break;
        }
      }

      if (!found) {
        auto callback = [deferred = std::move(deferred)](Napi::Env env, Napi::Function) mutable {
          deferred.Reject(Napi::Error::New(env, "Requested format is not in the last enumerated supported formats").Value());
        };
        tsfnPromise.BlockingCall(callback);
        tsfnPromise.Release();
        return;
      }

      HRESULT hr = this->device->SetDesiredFormat(width, height, frameRate);
      if (FAILED(hr)) {
        auto callback = [deferred = std::move(deferred), hr](Napi::Env env, Napi::Function) mutable {
          std::string msg = HResultToString(hr);
          deferred.Reject(Napi::Error::New(env, msg).Value());
        };
        tsfnPromise.BlockingCall(callback);
        tsfnPromise.Release();
        return;
      }

      auto callback = [deferred = std::move(deferred), width, height](Napi::Env env, Napi::Function) mutable {
        Napi::Object result = Napi::Object::New(env);
        result.Set("success", Napi::Boolean::New(env, true));
        result.Set("actualWidth", Napi::Number::New(env, width));
        result.Set("actualHeight", Napi::Number::New(env, height));
        deferred.Resolve(result);
      };

      tsfnPromise.BlockingCall(callback);
    } catch (const std::exception& e) {
      auto callback = [deferred = std::move(deferred), message = std::string(e.what())](Napi::Env env, Napi::Function) mutable {
        deferred.Reject(Napi::Error::New(env, message).Value());
      };
      tsfnPromise.BlockingCall(callback);
    }

    tsfnPromise.Release();
  }).detach();

  return deferred.Promise();
}

Napi::Value Camera::GetDimensions(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::Object result = Napi::Object::New(env);

  if (!this->device) {
    result.Set("width", env.Null());
    result.Set("height", env.Null());
    result.Set("frameRate", env.Null());
    return result;
  }

  UINT32 w = 0, h = 0;
  double fr = 0.0;
  HRESULT hr = this->device->GetCurrentDimensions(&w, &h, &fr);
  if (FAILED(hr)) {
    result.Set("width", env.Null());
    result.Set("height", env.Null());
    result.Set("frameRate", env.Null());
    return result;
  }

  result.Set("width", Napi::Number::New(env, w));
  result.Set("height", Napi::Number::New(env, h));
  result.Set("frameRate", Napi::Number::New(env, fr));
  return result;
}

Napi::Value Camera::ReleaseDeviceAsync(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  // Create a promise
  auto deferred = Napi::Promise::Deferred::New(env);

  // Create thread-safe function for the promise resolution
  auto tsfnPromise = Napi::ThreadSafeFunction::New(
      env,
      Napi::Function(),
      "ReleaseDeviceAsync",
      0,
      1);

  // Start async operation
  std::thread([this, deferred = std::move(deferred), tsfnPromise = std::move(tsfnPromise)]() mutable {
    try {
      HRESULT hr = device->ReleaseDevice();

      if (SUCCEEDED(hr)) {
        auto callback = [deferred = std::move(deferred)](Napi::Env env, Napi::Function) mutable {
          Napi::Object result = Napi::Object::New(env);
          result.Set("success", Napi::Boolean::New(env, true));
          result.Set("message", Napi::String::New(env, "Device released successfully"));
          deferred.Resolve(result);
        };

        tsfnPromise.BlockingCall(callback);
      } else {
        auto callback = [deferred = std::move(deferred), hr](Napi::Env env, Napi::Function) mutable {
          _com_error err(hr);
          LPCTSTR errMsg = err.ErrorMessage();
          std::string message = errMsg;
          deferred.Reject(Napi::Error::New(env, message).Value());
        };

        tsfnPromise.BlockingCall(callback);
      }
    } catch (const std::exception& e) {
      auto callback = [deferred = std::move(deferred), message = std::string(e.what())](Napi::Env env, Napi::Function) mutable {
        deferred.Reject(Napi::Error::New(env, message).Value());
      };

      tsfnPromise.BlockingCall(callback);
    }

    tsfnPromise.Release();
  }).detach();

  return deferred.Promise();
}

Napi::Value Camera::StartCaptureAsync(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (!this->device) {
    Napi::TypeError::New(env, "Device not initialized").ThrowAsJavaScriptException();
    return env.Null();
  }

  auto deferred = Napi::Promise::Deferred::New(env);

  // Expect a JS function to receive frames as first arg (optional)
  if (info.Length() > 0 && info[0].IsFunction()) {
    // Store the TSFN on the Camera instance for lifecycle management
    this->frameTsfn = Napi::ThreadSafeFunction::New(env, info[0].As<Napi::Function>(), "FrameCallback", 0, 1);
  } else {
    // Create a no-op TSFN stored on the Camera instance
    this->frameTsfn = Napi::ThreadSafeFunction::New(env, Napi::Function::New(env, [](const Napi::CallbackInfo&) {}), "FrameCallback", 0, 1);
  }

  // Register CCapture frame callback which forwards buffers via the stored TSFN
  Napi::ThreadSafeFunction tsfnLocal = this->frameTsfn;
  this->device->SetFrameCallback([tsfnLocal](std::vector<uint8_t>&& buf) mutable {
    // Move vector into heap and pass pointer to TSFN
    std::vector<uint8_t>* heapBuf = new std::vector<uint8_t>(std::move(buf));
    auto cb = [](Napi::Env env, Napi::Function jsCallback, std::vector<uint8_t>* data) {
      Napi::Buffer<uint8_t> nodeBuf = Napi::Buffer<uint8_t>::Copy(env, data->data(), data->size());
      jsCallback.Call({nodeBuf});
      delete data;
    };
    // Use non-blocking call to avoid deadlocks
    tsfnLocal.NonBlockingCall(heapBuf, cb);
  });

  // Start capture without file; frames will be delivered via callback
  EncodingParameters params = {0, 0};
  HRESULT hr = this->device->StartCapture(this->claimedActivate, params);
  if (FAILED(hr)) {
    // cleanup TSFN
    if (this->frameTsfn) {
      this->frameTsfn.Release();
      this->frameTsfn = Napi::ThreadSafeFunction();
    }
    deferred.Reject(Napi::Error::New(env, HResultToString(hr)).Value());
    return deferred.Promise();
  }

  this->isCapturing = true;
  deferred.Resolve(Napi::Boolean::New(env, true));
  return deferred.Promise();
}

Napi::Value Camera::StopCaptureAsync(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  auto deferred = Napi::Promise::Deferred::New(env);

  if (!this->device) {
    deferred.Reject(Napi::Error::New(env, "Device not initialized").Value());
    return deferred.Promise();
  }

  HRESULT hr = this->device->EndCaptureSession();
  if (FAILED(hr)) {
    deferred.Reject(Napi::Error::New(env, HResultToString(hr)).Value());
  } else {
    this->isCapturing = false;
    // If a TSFN was set up, release it
    if (this->frameTsfn) {
      this->frameTsfn.Release();
      this->frameTsfn = Napi::ThreadSafeFunction();
    }
    deferred.Resolve(Napi::Boolean::New(env, true));
  }

  return deferred.Promise();
}
