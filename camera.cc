#include <Wmcodecdsp.h>
#include <assert.h>
#include <comdef.h>
#include <inttypes.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <napi.h>
#include <tchar.h>
#include <windows.h>
#include <chrono>
#include <iostream>
#include <thread>

#include "camera.h"
#include "device.h"

struct ResultData {
  UINT32 width;
  UINT32 height;
  IMFMediaBuffer* buffer;
};

Napi::Object Camera::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func = DefineClass(env, "Camera", {
    InstanceMethod("enumerateDevicesAsync", &Camera::EnumerateDevicesAsync),
    InstanceMethod("selectDeviceN", &Camera::SelectDevice),
    InstanceMethod("startCaptureN", &Camera::StartCapture),
    InstanceMethod("stopCaptureN", &Camera::StopCapture),
    InstanceMethod("getDimensions", &Camera::GetDimensions),
    InstanceMethod("getSupportedFormats", &Camera::GetSupportedFormats),
    InstanceMethod("setDesiredFormat", &Camera::SetDesiredFormat)
  });

  Napi::FunctionReference* constructor = new Napi::FunctionReference();
  *constructor = Napi::Persistent(func);
  env.SetInstanceData(constructor);

  exports.Set("Camera", func);
  return exports;
}

Camera::Camera(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<Camera>(info), device() {
  Napi::Env env = info.Env();
}

Camera::~Camera() {}

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
    1
  );
  
  // Start async operation
  std::thread([this, deferred = std::move(deferred), tsfnPromise = std::move(tsfnPromise)]() mutable {
    try {
      HRESULT hr = device.EnumerateDevices();
      
      if (SUCCEEDED(hr)) {
        auto deviceData = device.GetDevicesList();
        
        auto callback = [deferred = std::move(deferred), deviceData = std::move(deviceData)](Napi::Env env, Napi::Function) mutable {
          Napi::Array devices = Napi::Array::New(env);
          
          for (size_t i = 0; i < deviceData.size(); i++) {
            Napi::Object deviceInfo = Napi::Object::New(env);
            
            deviceInfo.Set("friendlyName", Napi::String::New(env, reinterpret_cast<const char16_t*>(deviceData[i].friendlyName.c_str())));
            deviceInfo.Set("symbolicLink", Napi::String::New(env, reinterpret_cast<const char16_t*>(deviceData[i].symbolicLink.c_str())));
            deviceInfo.Set("isClaimed", Napi::Boolean::New(env, deviceData[i].isClaimed));
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

Napi::Value Camera::SelectDevice(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1) {
    Napi::Error::New(env, "Expected device index").ThrowAsJavaScriptException();
    return env.Null();
  } else if (info.Length() < 2) {
    Napi::Error::New(env, "Expected callback function").ThrowAsJavaScriptException();
    return env.Null();
  } else if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "Expected device index to be number").ThrowAsJavaScriptException();
    return env.Null();
  } else if (!info[1].IsFunction()) {
    Napi::TypeError::New(env, "Callback should be a function").ThrowAsJavaScriptException();
    return env.Null();
  }

  int index = info[0].As<Napi::Number>().Int32Value();
  Napi::Function jsCallback = info[1].As<Napi::Function>();

  if (index > static_cast<int>(device.Count()) - 1) {
    Napi::RangeError::New(env, "Index is out of range").ThrowAsJavaScriptException();
    return env.Null();
  }

  tsfn = Napi::ThreadSafeFunction::New(
      info.Env(),
      jsCallback,
      "SelectDevice",
      0,
      1,
      [this](Napi::Env) {
        if (nativeThread.joinable()) {
          nativeThread.join();
        }
      });

  nativeThread = std::thread([this, index]() {
    try {
      HRESULT hr = device.SelectDevice(index);
      if (SUCCEEDED(hr)) {
        auto callback = [](Napi::Env env, Napi::Function jsCallback) {
          jsCallback.Call({env.Null(), Napi::Boolean::New(env, true)});
        };

        tsfn.BlockingCall(callback);
      } else {
        _com_error err(hr);
        LPCTSTR errMsg = err.ErrorMessage();
        std::string message = errMsg;

        auto callback = [message](Napi::Env env, Napi::Function jsCallback) {
          jsCallback.Call({Napi::Error::New(env, message).Value(), env.Null()});
        };

        tsfn.BlockingCall(callback);
      }
    } catch (const std::exception& e) {
      std::string message = e.what();

      auto callback = [message](Napi::Env env, Napi::Function jsCallback) {
        jsCallback.Call({Napi::Error::New(env, message).Value()});
      };

      tsfn.BlockingCall(callback);
    }

    tsfn.Release();
  });
  nativeThread.detach();

  return env.Undefined();
}

Napi::Value Camera::StartCapture(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 2 || !info[0].IsFunction() || !info[1].IsFunction()) {
    Napi::TypeError::New(env, "Expected callback function").ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Function jsCallback = info[1].As<Napi::Function>();
  Napi::Function jsCapturingCallback = info[0].As<Napi::Function>();

  tsfn = Napi::ThreadSafeFunction::New(
      info.Env(),
      jsCallback,
      "StartCapture",
      0,
      1);

  tsfnCapturing = Napi::ThreadSafeFunction::New(
      info.Env(),
      jsCapturingCallback,
      "Capturing",
      0,
      1,
      [this](Napi::Env) {
        if (nativeThread.joinable()) {
          nativeThread.join();
        }
      });

  nativeThread = std::thread([this]() {
    HRESULT hr = S_OK;

    try {
      if (device.m_pTransform == NULL) {
        hr = device.CreateStream();
      }

      if (SUCCEEDED(hr)) {
        tsfn.BlockingCall([](Napi::Env env, Napi::Function jsCallback) {
          jsCallback.Call({env.Null(), env.Null()});
        });

        tsfn.Release();

        auto callback = [](Napi::Env env, Napi::Function jsCallback, ResultData* resultData) {
          UINT32 width = resultData->width;
          UINT32 height = resultData->height;
          IMFMediaBuffer* buffer = resultData->buffer;

          BYTE* bufData = nullptr;
          DWORD bufLength;
          HRESULT hr = buffer->Lock(&bufData, nullptr, &bufLength);

          if (SUCCEEDED(hr)) {
            buffer->Unlock();
            Napi::Object result = Napi::Object::New(env);
            result.Set("width", Napi::Number::New(env, width));
            result.Set("height", Napi::Number::New(env, height));

            Napi::Buffer<BYTE> bufferN = Napi::Buffer<BYTE>::Copy(env, bufData, bufLength);
            result.Set("buffer", bufferN);

            // Call the JavaScript callback with the result object
            jsCallback.Call({env.Null(), result});
          }

          // Release the IMFMediaBuffer
          buffer->Release();
          resultData->buffer = nullptr;

          // Delete the ResultData object
          delete resultData;
        };

        hr = device.StartCapture([this, &callback](IMFMediaBuffer* buf) {
          std::unique_ptr<ResultData> result = std::make_unique<ResultData>();
          result->width = device.width;
          result->height = device.height;
          result->buffer = buf;

          napi_status status = tsfnCapturing.BlockingCall(result.release(), callback);

          if (status == napi_ok) {
            return S_OK;
          } else {
            return E_FAIL;
          }
        });
      }

      if (FAILED(hr)) {
        _com_error err(hr);
        LPCTSTR errMsg = err.ErrorMessage();
        std::string message = errMsg;

        auto callback = [message](Napi::Env env, Napi::Function jsCallback) {
          jsCallback.Call({Napi::Error::New(env, message).Value(), env.Null()});
        };

        tsfnCapturing.BlockingCall(callback);
        tsfnCapturing.Release();
      }

    } catch (const std::exception& e) {
      std::string message = e.what();
      tsfn.BlockingCall([&](Napi::Env env, Napi::Function jsCallback) {
        jsCallback.Call({Napi::Error::New(env, message).Value()});
      });

      tsfn.Release();
    }
  });

  nativeThread.detach();

  return env.Undefined();
}

Napi::Value Camera::StopCapture(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  // Creating a Promise
  Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(env);

  HRESULT hr = device.StopCapture();

  // Resolving the Promise with a value
  deferred.Resolve(Napi::String::New(env, "Promise resolved"));

  // Returning the Promise
  return deferred.Promise();
}

Napi::Value Camera::GetDimensions(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  Napi::Object dimensions = Napi::Object::New(env);
  dimensions.Set("width", Napi::Number::New(env, device.width));
  dimensions.Set("height", Napi::Number::New(env, device.height));

  return dimensions;
}

Napi::Value Camera::GetSupportedFormats(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  auto formats = device.GetSupportedFormats();
  Napi::Array result = Napi::Array::New(env, formats.size());

  for (size_t i = 0; i < formats.size(); i++) {
    Napi::Object format = Napi::Object::New(env);
    format.Set("width", Napi::Number::New(env, std::get<0>(formats[i])));
    format.Set("height", Napi::Number::New(env, std::get<1>(formats[i])));
    format.Set("frameRate", Napi::Number::New(env, std::get<2>(formats[i])));
    result.Set(i, format);
  }

  return result;
}

Napi::Value Camera::SetDesiredFormat(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 3 || !info[0].IsNumber() || !info[1].IsNumber() || !info[2].IsNumber()) {
    Napi::TypeError::New(env, "Expected width, height, and frameRate as numbers").ThrowAsJavaScriptException();
    return env.Null();
  }

  UINT32 width = info[0].As<Napi::Number>().Uint32Value();
  UINT32 height = info[1].As<Napi::Number>().Uint32Value();
  UINT32 frameRate = info[2].As<Napi::Number>().Uint32Value();

  HRESULT hr = device.SetDesiredFormat(width, height, frameRate);

  if (SUCCEEDED(hr)) {
    Napi::Object result = Napi::Object::New(env);
    result.Set("success", Napi::Boolean::New(env, true));
    result.Set("actualWidth", Napi::Number::New(env, device.width));
    result.Set("actualHeight", Napi::Number::New(env, device.height));
    return result;
  } else {
    Napi::Object result = Napi::Object::New(env);
    result.Set("success", Napi::Boolean::New(env, false));
    result.Set("error", Napi::String::New(env, "Failed to set desired format"));
    return result;
  }
}
