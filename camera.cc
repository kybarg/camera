#include "camera.h"
#include <comdef.h>
#include <thread>

Napi::Object Camera::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func = DefineClass(env, "Camera", {
                                                       InstanceMethod("enumerateDevicesAsync", &Camera::EnumerateDevicesAsync)
                                                       //, InstanceMethod("claimDeviceAsync", &Camera::ClaimDeviceAsync), InstanceMethod("releaseDeviceAsync", &Camera::ReleaseDeviceAsync), InstanceMethod("startCaptureAsync", &Camera::StartCaptureAsync), InstanceMethod("stopCaptureAsync", &Camera::StopCaptureAsync), InstanceMethod("getDimensions", &Camera::GetDimensions), InstanceMethod("getSupportedFormatsAsync", &Camera::GetSupportedFormatsAsync), InstanceMethod("setDesiredFormatAsync", &Camera::SetDesiredFormatAsync)
                                                   });

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

Camera::~Camera() = default;

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
      HRESULT hr2 = list.GetAllDevices(devicesVec);
      if (SUCCEEDED(hr2)) {
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
        auto callback = [deferred = std::move(deferred), hr2](Napi::Env env, Napi::Function) mutable {
          _com_error err(hr2);
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

// Napi::Value Camera::ClaimDeviceAsync(const Napi::CallbackInfo& info) {
//   Napi::Env env = info.Env();

//   if (info.Length() < 1 || !info[0].IsString()) {
//     Napi::TypeError::New(env, "Expected device symbolic link as string").ThrowAsJavaScriptException();
//     return env.Null();
//   }

//   std::u16string symbolicLinkU16 = info[0].As<Napi::String>().Utf16Value();
//   std::wstring symbolicLink(symbolicLinkU16.begin(), symbolicLinkU16.end());

//   // Create a promise
//   auto deferred = Napi::Promise::Deferred::New(env);

//   // Create thread-safe function for the promise resolution
//   auto tsfnPromise = Napi::ThreadSafeFunction::New(
//       env,
//       Napi::Function(),
//       "ClaimDeviceAsync",
//       0,
//       1);

//   // Start async operation
//   std::thread([this, deferred = std::move(deferred), tsfnPromise = std::move(tsfnPromise), symbolicLink]() mutable {
//     try {
//       HRESULT hr = device.SelectDeviceBySymbolicLink(symbolicLink);

//       if (SUCCEEDED(hr)) {
//         auto callback = [deferred = std::move(deferred), symbolicLink](Napi::Env env, Napi::Function) mutable {
//           Napi::Object result = Napi::Object::New(env);
//           result.Set("success", Napi::Boolean::New(env, true));
//           result.Set("message", Napi::String::New(env, "Device claimed successfully"));
//           // Convert std::wstring to std::u16string for N-API
//           std::u16string symbolicU16(symbolicLink.begin(), symbolicLink.end());
//           result.Set("symbolicLink", Napi::String::New(env, symbolicU16));
//           deferred.Resolve(result);
//         };

//         tsfnPromise.BlockingCall(callback);
//       } else {
//         auto callback = [deferred = std::move(deferred), hr](Napi::Env env, Napi::Function) mutable {
//           _com_error err(hr);
//           LPCTSTR errMsg = err.ErrorMessage();
//           std::string message = errMsg;
//           deferred.Reject(Napi::Error::New(env, message).Value());
//         };

//         tsfnPromise.BlockingCall(callback);
//       }
//     } catch (const std::exception& e) {
//       auto callback = [deferred = std::move(deferred), message = std::string(e.what())](Napi::Env env, Napi::Function) mutable {
//         deferred.Reject(Napi::Error::New(env, message).Value());
//       };

//       tsfnPromise.BlockingCall(callback);
//     }

//     tsfnPromise.Release();
//   }).detach();

//   return deferred.Promise();
// }

// Napi::Value Camera::ReleaseDeviceAsync(const Napi::CallbackInfo& info) {
//   Napi::Env env = info.Env();

//   // Create a promise
//   auto deferred = Napi::Promise::Deferred::New(env);

//   // Create thread-safe function for the promise resolution
//   auto tsfnPromise = Napi::ThreadSafeFunction::New(
//       env,
//       Napi::Function(),
//       "ReleaseDeviceAsync",
//       0,
//       1);

//   // Start async operation
//   std::thread([this, deferred = std::move(deferred), tsfnPromise = std::move(tsfnPromise)]() mutable {
//     try {
//       HRESULT hr = device.ReleaseDevice();

//       if (SUCCEEDED(hr)) {
//         auto callback = [deferred = std::move(deferred)](Napi::Env env, Napi::Function) mutable {
//           Napi::Object result = Napi::Object::New(env);
//           result.Set("success", Napi::Boolean::New(env, true));
//           result.Set("message", Napi::String::New(env, "Device released successfully"));
//           deferred.Resolve(result);
//         };

//         tsfnPromise.BlockingCall(callback);
//       } else {
//         auto callback = [deferred = std::move(deferred), hr](Napi::Env env, Napi::Function) mutable {
//           _com_error err(hr);
//           LPCTSTR errMsg = err.ErrorMessage();
//           std::string message = errMsg;
//           deferred.Reject(Napi::Error::New(env, message).Value());
//         };

//         tsfnPromise.BlockingCall(callback);
//       }
//     } catch (const std::exception& e) {
//       auto callback = [deferred = std::move(deferred), message = std::string(e.what())](Napi::Env env, Napi::Function) mutable {
//         deferred.Reject(Napi::Error::New(env, message).Value());
//       };

//       tsfnPromise.BlockingCall(callback);
//     }

//     tsfnPromise.Release();
//   }).detach();

//   return deferred.Promise();
// }

// Napi::Value Camera::StopCaptureAsync(const Napi::CallbackInfo& info) {
//   Napi::Env env = info.Env();

//   // Create a promise
//   auto deferred = Napi::Promise::Deferred::New(env);

//   // Create thread-safe function for the promise resolution
//   auto tsfnPromise = Napi::ThreadSafeFunction::New(
//       env,
//       Napi::Function(),
//       "StopCaptureAsync",
//       0,
//       1);

//   // Start async operation
//   std::thread([this, deferred = std::move(deferred), tsfnPromise = std::move(tsfnPromise)]() mutable {
//     try {
//       HRESULT hr = device.StopCapture();

//       auto callback = [deferred = std::move(deferred), hr](Napi::Env env, Napi::Function) mutable {
//         if (SUCCEEDED(hr)) {
//           Napi::Object result = Napi::Object::New(env);
//           result.Set("success", Napi::Boolean::New(env, true));
//           result.Set("message", Napi::String::New(env, "Capture stopped successfully"));
//           deferred.Resolve(result);
//         } else {
//           _com_error err(hr);
//           LPCTSTR errMsg = err.ErrorMessage();
//           std::string message = errMsg;
//           deferred.Reject(Napi::Error::New(env, message).Value());
//         }
//       };

//       tsfnPromise.BlockingCall(callback);
//     } catch (const std::exception& e) {
//       auto callback = [deferred = std::move(deferred), message = std::string(e.what())](Napi::Env env, Napi::Function) mutable {
//         deferred.Reject(Napi::Error::New(env, message).Value());
//       };

//       tsfnPromise.BlockingCall(callback);
//     }

//     tsfnPromise.Release();
//   }).detach();

//   return deferred.Promise();
// }

// Napi::Value Camera::GetDimensions(const Napi::CallbackInfo& info) {
//   Napi::Env env = info.Env();

//   Napi::Object dimensions = Napi::Object::New(env);
//   dimensions.Set("width", Napi::Number::New(env, device.width));
//   dimensions.Set("height", Napi::Number::New(env, device.height));

//   return dimensions;
// }

// Napi::Value Camera::GetSupportedFormatsAsync(const Napi::CallbackInfo& info) {
//   Napi::Env env = info.Env();

//   // Create a promise
//   auto deferred = Napi::Promise::Deferred::New(env);

//   // Create thread-safe function for the promise resolution
//   auto tsfnPromise = Napi::ThreadSafeFunction::New(
//       env,
//       Napi::Function(),
//       "GetSupportedFormatsAsync",
//       0,
//       1);

//   // Start async operation
//   std::thread([this, deferred = std::move(deferred), tsfnPromise = std::move(tsfnPromise)]() mutable {
//     try {
//       auto formats = device.GetSupportedFormats();

//       auto callback = [deferred = std::move(deferred), formats = std::move(formats)](Napi::Env env, Napi::Function) mutable {
//         Napi::Array result = Napi::Array::New(env, formats.size());

//         for (size_t i = 0; i < formats.size(); i++) {
//           Napi::Object format = Napi::Object::New(env);
//           format.Set("width", Napi::Number::New(env, std::get<0>(formats[i])));
//           format.Set("height", Napi::Number::New(env, std::get<1>(formats[i])));
//           format.Set("frameRate", Napi::Number::New(env, std::get<2>(formats[i])));
//           result.Set(i, format);
//         }

//         deferred.Resolve(result);
//       };

//       tsfnPromise.BlockingCall(callback);
//     } catch (const std::exception& e) {
//       auto callback = [deferred = std::move(deferred), message = std::string(e.what())](Napi::Env env, Napi::Function) mutable {
//         deferred.Reject(Napi::Error::New(env, message).Value());
//       };

//       tsfnPromise.BlockingCall(callback);
//     }

//     tsfnPromise.Release();
//   }).detach();

//   return deferred.Promise();
// }

// Napi::Value Camera::SetDesiredFormatAsync(const Napi::CallbackInfo& info) {
//   Napi::Env env = info.Env();

//   if (info.Length() < 3 || !info[0].IsNumber() || !info[1].IsNumber() || !info[2].IsNumber()) {
//     Napi::TypeError::New(env, "Expected width, height, and frameRate as numbers").ThrowAsJavaScriptException();
//     return env.Null();
//   }

//   UINT32 width = info[0].As<Napi::Number>().Uint32Value();
//   UINT32 height = info[1].As<Napi::Number>().Uint32Value();
//   UINT32 frameRate = info[2].As<Napi::Number>().Uint32Value();

//   // Create a promise
//   auto deferred = Napi::Promise::Deferred::New(env);

//   // Create thread-safe function for the promise resolution
//   auto tsfnPromise = Napi::ThreadSafeFunction::New(
//       env,
//       Napi::Function(),
//       "SetDesiredFormatAsync",
//       0,
//       1);

//   // Start async operation
//   std::thread([this, deferred = std::move(deferred), tsfnPromise = std::move(tsfnPromise), width, height, frameRate]() mutable {
//     try {
//       HRESULT hr = device.SetDesiredFormat(width, height, frameRate);

//       auto callback = [deferred = std::move(deferred), hr, this](Napi::Env env, Napi::Function) mutable {
//         if (SUCCEEDED(hr)) {
//           Napi::Object result = Napi::Object::New(env);
//           result.Set("success", Napi::Boolean::New(env, true));
//           result.Set("actualWidth", Napi::Number::New(env, device.width));
//           result.Set("actualHeight", Napi::Number::New(env, device.height));
//           deferred.Resolve(result);
//         } else {
//           Napi::Object result = Napi::Object::New(env);
//           result.Set("success", Napi::Boolean::New(env, false));
//           result.Set("error", Napi::String::New(env, "Failed to set desired format"));
//           deferred.Reject(Napi::Error::New(env, "Failed to set desired format").Value());
//         }
//       };

//       tsfnPromise.BlockingCall(callback);
//     } catch (const std::exception& e) {
//       auto callback = [deferred = std::move(deferred), message = std::string(e.what())](Napi::Env env, Napi::Function) mutable {
//         deferred.Reject(Napi::Error::New(env, message).Value());
//       };

//       tsfnPromise.BlockingCall(callback);
//     }

//     tsfnPromise.Release();
//   }).detach();

//   return deferred.Promise();
// }

// Napi::Value Camera::StartCaptureAsync(const Napi::CallbackInfo& info) {
//   Napi::Env env = info.Env();

//   // Create a promise
//   auto deferred = Napi::Promise::Deferred::New(env);

//   // Create thread-safe function for the promise resolution
//   auto tsfnPromise = Napi::ThreadSafeFunction::New(
//       env,
//       Napi::Function(),
//       "StartCaptureAsync",
//       0,
//       1);

//   // Check if frame event emitter function was passed as parameter
//   Napi::ThreadSafeFunction tsfnFrame;
//   bool hasFrameCallback = false;
//   if (info.Length() > 0 && info[0].IsFunction()) {
//     tsfnFrame = Napi::ThreadSafeFunction::New(
//         env,
//         info[0].As<Napi::Function>(),
//         "FrameEvent",
//         0,
//         1);
//     hasFrameCallback = true;
//   }

//   // Start async capture operation
//   std::thread([this, deferred = std::move(deferred), tsfnPromise = std::move(tsfnPromise), tsfnFrame = std::move(tsfnFrame), hasFrameCallback]() mutable {
//     try {
//       HRESULT hr = S_OK;

//       if (device.m_pTransform == NULL) {
//         hr = device.CreateStream();
//       }

//       if (SUCCEEDED(hr)) {
//         auto frameCallbackLambda = [this, tsfnFrame, hasFrameCallback](IMFMediaBuffer* buf) mutable -> HRESULT {
//           if (!hasFrameCallback) {
//             return S_OK;
//           }

//           // Take an extra reference for passing the buffer to the JS thread
//           buf->AddRef();

//           // JS-side callback that will lock/unlock and release the IMFMediaBuffer
//           auto callback = [](Napi::Env env, Napi::Function jsCallback, IMFMediaBuffer* buffer) {
//             BYTE* bufData = nullptr;
//             DWORD bufLength = 0;
//             HRESULT hrLock = buffer->Lock(&bufData, nullptr, &bufLength);
//             bool locked = SUCCEEDED(hrLock);

//             if (locked && bufLength > 0) {
//               // Copy the buffer data; Node owns the copy
//               BYTE* ownedData = new BYTE[bufLength];
//               memcpy(ownedData, bufData, bufLength);

//               // Unlock after reading
//               buffer->Unlock();

//               // Create Node.js buffer with finalizer for automatic cleanup
//               Napi::Buffer<BYTE> bufferN = Napi::Buffer<BYTE>::New(
//                   env,
//                   ownedData,
//                   bufLength,
//                   [](Napi::Env /*env*/, BYTE* data) {
//                     delete[] data;
//                   });

//               // Call the JavaScript frame event emitter
//               jsCallback.Call({bufferN});
//             } else if (locked) {
//               // Nothing to send, just unlock
//               buffer->Unlock();
//             }

//             // Release the IMFMediaBuffer reference taken by the producer
//             buffer->Release();
//           };

//           // Try to queue the buffer to JS without blocking; drop if queue is full
//           napi_status status = tsfnFrame.NonBlockingCall(buf, callback);

//           if (status == napi_ok) {
//             return S_OK;
//           }

//           // If the queue is full or another error occurred, drop the frame and release our ref
//           buf->Release();
//           // Treat dropped frames as non-fatal for the capture pipeline
//           return S_OK;
//         };

//         // Setup capture (non-blocking)
//         hr = device.SetupCapture(frameCallbackLambda);

//         // Resolve the promise immediately after setup succeeds
//         auto callback = [deferred = std::move(deferred), hr](Napi::Env env, Napi::Function) mutable {
//           if (SUCCEEDED(hr)) {
//             Napi::Object result = Napi::Object::New(env);
//             result.Set("success", Napi::Boolean::New(env, true));
//             result.Set("message", Napi::String::New(env, "Capture started successfully"));
//             deferred.Resolve(result);
//           } else {
//             _com_error err(hr);
//             LPCTSTR errMsg = err.ErrorMessage();
//             std::string message = errMsg;
//             deferred.Reject(Napi::Error::New(env, message).Value());
//           }
//         };

//         tsfnPromise.BlockingCall(callback);

//         // If setup succeeded, start the blocking capture loop in a separate detached thread
//         if (SUCCEEDED(hr)) {
//           std::thread([this, frameCallbackLambda, tsfnFrame, hasFrameCallback]() mutable {
//             try {
//               // Now run the blocking capture loop
//               device.RunCaptureLoop(frameCallbackLambda);
//             } catch (...) {
//               // Capture loop failed - reset capturing flag
//               device.isCapturing = false;
//             }

//             // Clean up frame callback when capture loop ends
//             if (hasFrameCallback) {
//               tsfnFrame.Release();
//             }
//           }).detach();
//         }

//       } else {
//         auto callback = [deferred = std::move(deferred), hr](Napi::Env env, Napi::Function) mutable {
//           _com_error err(hr);
//           LPCTSTR errMsg = err.ErrorMessage();
//           std::string message = errMsg;
//           deferred.Reject(Napi::Error::New(env, message).Value());
//         };

//         tsfnPromise.BlockingCall(callback);
//       }
//     } catch (const std::exception& e) {
//       auto callback = [deferred = std::move(deferred), message = std::string(e.what())](Napi::Env env, Napi::Function) mutable {
//         deferred.Reject(Napi::Error::New(env, message).Value());
//       };

//       tsfnPromise.BlockingCall(callback);
//     }

//     tsfnPromise.Release();
//   }).detach();

//   return deferred.Promise();
// }
