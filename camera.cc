#include "camera.h"
#include <comdef.h>
#include <windows.h>
#include <thread>
#include <cstring>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <vector>
#include <tuple>
#include <objbase.h>

// Helper: map common media subtype GUIDs to friendly encoder names
static std::string SubtypeGuidToName(const GUID& g) {
  // MEDIASUBTYPE_MJPG / MFVideoFormat_MJPG GUID value (bytes: 'MJPG')
  static const GUID MJPG_GUID = {0x47504A4D, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

  if (g == MFVideoFormat_RGB32) return "RGB32";
  if (g == MFVideoFormat_RGB24) return "RGB24";
  if (g == MFVideoFormat_NV12) return "NV12";
  if (g == MFVideoFormat_YUY2) return "YUY2";
  if (g == MFVideoFormat_UYVY) return "UYVY";
  if (g == MFVideoFormat_IYUV) return "IYUV";
  if (memcmp(&g, &MJPG_GUID, sizeof(GUID)) == 0) return "MJPEG";
  // MJPEG is usually defined as MEDIASUBTYPE_MJPG / MFVideoFormat_MJPG
#ifdef MFVideoFormat_MJPG
  if (g == MFVideoFormat_MJPG) return "MJPEG";
#endif
  // Fallback: convert GUID to string
  OLECHAR wsz[64];
  StringFromGUID2(g, wsz, ARRAYSIZE(wsz));
  int len = WideCharToMultiByte(CP_UTF8, 0, wsz, -1, NULL, 0, NULL, NULL);
  std::string guidStr;
  if (len > 0) {
    guidStr.assign(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wsz, -1, &guidStr[0], len, NULL, NULL);
    if (!guidStr.empty() && guidStr.back() == '\0') guidStr.pop_back();
  }
  return guidStr;
}

// Helper: convert a GUID to a UTF-8 string like "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}"
static std::string GuidToString(const GUID& g) {
  OLECHAR wsz[64];
  StringFromGUID2(g, wsz, ARRAYSIZE(wsz));
  int len = WideCharToMultiByte(CP_UTF8, 0, wsz, -1, NULL, 0, NULL, NULL);
  std::string guidStr;
  if (len > 0) {
    guidStr.assign(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wsz, -1, &guidStr[0], len, NULL, NULL);
    if (!guidStr.empty() && guidStr.back() == '\0') guidStr.pop_back();
  }
  return guidStr;
}

Napi::Object Camera::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func = DefineClass(env, "Camera", {InstanceMethod("claimDeviceAsync", &Camera::ClaimDeviceAsync), InstanceMethod("enumerateDevicesAsync", &Camera::EnumerateDevicesAsync), InstanceMethod("getDimensions", &Camera::GetDimensions), InstanceMethod("getSupportedFormatsAsync", &Camera::GetSupportedFormatsAsync), InstanceMethod("getCameraInfoAsync", &Camera::GetCameraInfoAsync), InstanceMethod("releaseDeviceAsync", &Camera::ReleaseDeviceAsync), InstanceMethod("setFormatAsync", &Camera::SetFormatAsync), InstanceMethod("setOutputFormatAsync", &Camera::SetOutputFormatAsync), InstanceMethod("startCaptureAsync", &Camera::StartCaptureAsync), InstanceMethod("stopCaptureAsync", &Camera::StopCaptureAsync)});

  Napi::FunctionReference* constructor = new Napi::FunctionReference();
  *constructor = Napi::Persistent(func);
  env.SetInstanceData(constructor);

  exports.Set("Camera", func);
  return exports;
}

Napi::Value Camera::GetCameraInfoAsync(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  auto deferred = Napi::Promise::Deferred::New(env);
  auto tsfnPromise = Napi::ThreadSafeFunction::New(env, Napi::Function(), "GetCameraInfoAsync", 0, 1);

  std::thread([this, deferred = std::move(deferred), tsfnPromise = std::move(tsfnPromise)]() mutable {
    try {
      if (!this->claimedActivate) {
        auto cb = [deferred = std::move(deferred)](Napi::Env env, Napi::Function) mutable {
          deferred.Reject(Napi::Error::New(env, "No claimed device. Call claimDeviceAsync first.").Value());
        };
        tsfnPromise.BlockingCall(cb);
        tsfnPromise.Release();
        return;
      }

      // Extract device-level attributes from claimedActivate
      WCHAR* pFriendly = nullptr;
      WCHAR* pSymbolic = nullptr;
      std::string friendlyUtf8, symbolicUtf8;

      HRESULT hr1 = this->claimedActivate->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &pFriendly, nullptr);
      HRESULT hr2 = this->claimedActivate->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &pSymbolic, nullptr);

      if (SUCCEEDED(hr1) && pFriendly) {
        int len = WideCharToMultiByte(CP_UTF8, 0, pFriendly, -1, NULL, 0, NULL, NULL);
        if (len > 0) {
          std::string tmp(len, '\0');
          WideCharToMultiByte(CP_UTF8, 0, pFriendly, -1, &tmp[0], len, NULL, NULL);
          if (!tmp.empty() && tmp.back() == '\0') tmp.pop_back();
          friendlyUtf8 = std::move(tmp);
        }
      }
      if (SUCCEEDED(hr2) && pSymbolic) {
        int len = WideCharToMultiByte(CP_UTF8, 0, pSymbolic, -1, NULL, 0, NULL, NULL);
        if (len > 0) {
          std::string tmp(len, '\0');
          WideCharToMultiByte(CP_UTF8, 0, pSymbolic, -1, &tmp[0], len, NULL, NULL);
          if (!tmp.empty() && tmp.back() == '\0') tmp.pop_back();
          symbolicUtf8 = std::move(tmp);
        }
      }

      if (pFriendly) CoTaskMemFree(pFriendly);
      if (pSymbolic) CoTaskMemFree(pSymbolic);

      // Ask CCapture for supported native types (includes subtype GUID)
      std::vector<std::tuple<GUID, UINT32, UINT32, double>> types;
      HRESULT hrTypes = S_OK;
      if (this->device) {
        hrTypes = this->device->GetSupportedFormats(types);
      }

      auto cb = [deferred = std::move(deferred), friendlyUtf8 = std::move(friendlyUtf8), symbolicUtf8 = std::move(symbolicUtf8), types = std::move(types), hrTypes](Napi::Env env, Napi::Function) mutable {
        if (FAILED(hrTypes)) {
          deferred.Reject(Napi::Error::New(env, "Failed to enumerate native types").Value());
          return;
        }

        Napi::Object out = Napi::Object::New(env);
        out.Set("friendlyName", Napi::String::New(env, friendlyUtf8));
        out.Set("symbolicLink", Napi::String::New(env, symbolicUtf8));

        // Map of encoders/formats supported (collect unique subtype GUIDs)
        Napi::Array enc = Napi::Array::New(env);
        std::vector<GUID> seenGuids;
        for (size_t i = 0; i < types.size(); ++i) {
          const GUID& g = std::get<0>(types[i]);
          bool found = false;
          for (const auto& sg : seenGuids) {
            if (memcmp(&sg, &g, sizeof(GUID)) == 0) {
              found = true;
              break;
            }
          }
          if (!found) {
            std::string name = SubtypeGuidToName(g);
            enc.Set(enc.Length(), Napi::String::New(env, name));
            seenGuids.push_back(g);
          }
        }

        out.Set("encoders", enc);

        // Return a flat array of CameraFormat objects: { subtype, width, height, frameRate }
        Napi::Array formatsArr = Napi::Array::New(env, static_cast<uint32_t>(types.size()));
        for (uint32_t i = 0; i < types.size(); ++i) {
          GUID g = std::get<0>(types[i]);
          UINT32 w = std::get<1>(types[i]);
          UINT32 h = std::get<2>(types[i]);
          double fr = std::get<3>(types[i]);

          // Use friendly short name for subtype if available and include GUID string
          std::string subtypeName = SubtypeGuidToName(g);
          std::string guidStr = GuidToString(g);

          Napi::Object entry = Napi::Object::New(env);
          entry.Set("subtype", Napi::String::New(env, subtypeName));
          entry.Set("guid", Napi::String::New(env, guidStr));
          entry.Set("width", Napi::Number::New(env, w));
          entry.Set("height", Napi::Number::New(env, h));
          // use only `frameRate` for frame rate information
          entry.Set("frameRate", Napi::Number::New(env, fr));

          formatsArr.Set(i, entry);
        }

        out.Set("formats", formatsArr);

        deferred.Resolve(out);
      };

      tsfnPromise.BlockingCall(cb);
    } catch (const std::exception& e) {
      auto cb = [deferred = std::move(deferred), message = std::string(e.what())](Napi::Env env, Napi::Function) mutable {
        deferred.Reject(Napi::Error::New(env, message).Value());
      };
      tsfnPromise.BlockingCall(cb);
    }
    tsfnPromise.Release();
  }).detach();

  return deferred.Promise();
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
      // Return claim result including the symbolic link (caller provided identifier may be friendly name or symbolic link).
      auto callback = [deferred = std::move(deferred), identifier](Napi::Env env, Napi::Function) mutable {
        Napi::Object result = Napi::Object::New(env);
        result.Set("success", Napi::Boolean::New(env, true));
        result.Set("message", Napi::String::New(env, "Device claimed successfully"));
        // Convert the stored wide string identifier to UTF-8 for JS return
        std::string symUtf8;
        if (!identifier.empty()) {
          int len = WideCharToMultiByte(CP_UTF8, 0, identifier.c_str(), -1, NULL, 0, NULL, NULL);
          if (len > 0) {
            std::string tmp(len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, identifier.c_str(), -1, &tmp[0], len, NULL, NULL);
            if (!tmp.empty() && tmp.back() == '\0') tmp.pop_back();
            symUtf8 = std::move(tmp);
          }
        }
        result.Set("symbolicLink", Napi::String::New(env, symUtf8));
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
      std::vector<std::tuple<GUID, UINT32, UINT32, double>> types;
      HRESULT hr = S_OK;

      if (this->device != nullptr) {
        hr = this->device->GetSupportedFormats(types);
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

      auto callback = [deferred = std::move(deferred), types = std::move(types)](Napi::Env env, Napi::Function) mutable {
        Napi::Array arr = Napi::Array::New(env, static_cast<uint32_t>(types.size()));
        for (uint32_t i = 0; i < types.size(); ++i) {
          Napi::Object obj = Napi::Object::New(env);
          GUID g = std::get<0>(types[i]);
          // Use short friendly name when possible (e.g., "MJPEG", "NV12")
          std::string subtypeName = SubtypeGuidToName(g);
          std::string guidStr = GuidToString(g);
          UINT32 w = std::get<1>(types[i]);
          UINT32 h = std::get<2>(types[i]);
          double fr = std::get<3>(types[i]);

          obj.Set("subtype", Napi::String::New(env, subtypeName));
          obj.Set("guid", Napi::String::New(env, guidStr));
          obj.Set("width", Napi::Number::New(env, w));
          obj.Set("height", Napi::Number::New(env, h));
          obj.Set("frameRate", Napi::Number::New(env, fr));
          // also set legacy frameRate for compatibility
          obj.Set("frameRate", Napi::Number::New(env, fr));

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

// Helper: map common subtype strings to GUIDs
static bool ParseSubtypeString(const std::string& s, GUID& out) {
  // Accept several common names (case-insensitive)
  std::string u = s;
  for (auto& c : u) c = (char)tolower(c);
  if (u == "nv12") {
    out = MFVideoFormat_NV12;
    return true;
  }
  if (u == "rgb24" || u == "bgr24") {
    out = MFVideoFormat_RGB24;
    return true;
  }
  if (u == "rgb32" || u == "bgra" || u == "rgba") {
    out = MFVideoFormat_RGB32;
    return true;
  }
  if (u == "yuy2" || u == "yuyv" || u == "yuv2" || u == "yuv") {
    out = MFVideoFormat_YUY2;
    return true;
  }
  if (u == "mjpg" || u == "mjpeg" || u == "mjepg" || u == "mjpg") {
    out = MFVideoFormat_MJPG;
    return true;
  }
  // Attempt to parse a GUID string of the form {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
  // Note: Simple parser; if parsing fails return false.
  if (u.size() >= 36) {
    // Use CLSIDFromString which accepts wide string
    wchar_t wbuf[64];
    size_t needed = 0;
    mbstowcs_s(&needed, wbuf, s.c_str(), _TRUNCATE);
    CLSID clsid;
    HRESULT hr = CLSIDFromString(wbuf, &clsid);
    if (SUCCEEDED(hr)) {
      out = clsid;
      return true;
    }
  }
  return false;
}

Napi::Value Camera::SetFormatAsync(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (!this->device) {
    Napi::TypeError::New(env, "Device not initialized").ThrowAsJavaScriptException();
    return env.Null();
  }

  // Accept only a single object: { subtype: string, width: number, height: number, frameRate: number }
  std::string subtypeStr;
  UINT32 width = 0, height = 0;
  double frameRate = 0.0;

  if (!(info.Length() == 1 && info[0].IsObject())) {
    Napi::TypeError::New(env, "Expected a single object: { subtype: string, width: number, height: number, frameRate: number }").ThrowAsJavaScriptException();
    return env.Null();
  }

  {
    Napi::Object obj = info[0].As<Napi::Object>();
    // Accept either a 'guid' string (preferred) or a 'subtype' friendly name
    if (obj.Has("guid") && obj.Get("guid").IsString()) {
      subtypeStr = "";  // leave subtypeStr empty and parse guid below
    } else if (!(obj.Has("subtype") && obj.Get("subtype").IsString())) {
      Napi::TypeError::New(env, "Format object must include either 'guid' string or 'subtype' string").ThrowAsJavaScriptException();
      return env.Null();
    }
    if (obj.Has("subtype") && obj.Get("subtype").IsString()) {
      subtypeStr = obj.Get("subtype").As<Napi::String>().Utf8Value();
    }

    if (!(obj.Has("width") && obj.Get("width").IsNumber())) {
      Napi::TypeError::New(env, "Format object missing required 'width' number").ThrowAsJavaScriptException();
      return env.Null();
    }
    width = obj.Get("width").As<Napi::Number>().Uint32Value();

    if (!(obj.Has("height") && obj.Get("height").IsNumber())) {
      Napi::TypeError::New(env, "Format object missing required 'height' number").ThrowAsJavaScriptException();
      return env.Null();
    }
    height = obj.Get("height").As<Napi::Number>().Uint32Value();

    if (!(obj.Has("frameRate") && obj.Get("frameRate").IsNumber())) {
      Napi::TypeError::New(env, "Format object missing required 'frameRate' number").ThrowAsJavaScriptException();
      return env.Null();
    }
    frameRate = obj.Get("frameRate").As<Napi::Number>().DoubleValue();
  }

  GUID subtypeGuid = {0};
  // If a GUID string was provided, try parsing it first
  if (info[0].As<Napi::Object>().Has("guid") && info[0].As<Napi::Object>().Get("guid").IsString()) {
    std::string guidInput = info[0].As<Napi::Object>().Get("guid").As<Napi::String>().Utf8Value();
    wchar_t wbuf[64];
    size_t needed = 0;
    mbstowcs_s(&needed, wbuf, guidInput.c_str(), _TRUNCATE);
    CLSID clsid;
    if (SUCCEEDED(CLSIDFromString(wbuf, &clsid))) {
      subtypeGuid = clsid;
    } else if (!subtypeStr.empty()) {
      if (!ParseSubtypeString(subtypeStr, subtypeGuid)) {
        Napi::TypeError::New(env, "Unknown subtype string or invalid GUID").ThrowAsJavaScriptException();
        return env.Null();
      }
    } else {
      Napi::TypeError::New(env, "Invalid 'guid' string and no 'subtype' provided").ThrowAsJavaScriptException();
      return env.Null();
    }
  } else {
    if (!ParseSubtypeString(subtypeStr, subtypeGuid)) {
      Napi::TypeError::New(env, "Unknown subtype string or invalid GUID").ThrowAsJavaScriptException();
      return env.Null();
    }
  }

  auto deferred = Napi::Promise::Deferred::New(env);

  auto tsfnPromise = Napi::ThreadSafeFunction::New(
      env,
      Napi::Function(),
      "SetFormatAsync",
      0,
      1);

  std::thread([this, deferred = std::move(deferred), tsfnPromise = std::move(tsfnPromise), subtypeGuid, width, height, frameRate]() mutable {
    try {
      // No validation against GetLastSupportedFormats here; forward request to
      // CCapture::SetFormat which will return an HRESULT indicating success or failure.

      HRESULT hr = this->device->SetFormat(subtypeGuid, width, height, frameRate);
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

Napi::Value Camera::SetOutputFormatAsync(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (!this->device) {
    Napi::TypeError::New(env, "Device not initialized").ThrowAsJavaScriptException();
    return env.Null();
  }

  auto deferred = Napi::Promise::Deferred::New(env);

  // Accept null/undefined to clear output format, or a string like "RGB32", "NV12", etc.
  GUID outputGuid = GUID_NULL;
  bool clearFormat = false;

  if (info.Length() == 0 || info[0].IsNull() || info[0].IsUndefined()) {
    clearFormat = true;
  } else if (info[0].IsString()) {
    std::string formatStr = info[0].As<Napi::String>().Utf8Value();
    if (!ParseSubtypeString(formatStr, outputGuid)) {
      Napi::TypeError::New(env, "Unknown output format. Use 'RGB32', 'RGB24', 'NV12', 'YUY2', or a GUID string.").ThrowAsJavaScriptException();
      return env.Null();
    }
  } else {
    Napi::TypeError::New(env, "Expected output format string or null/undefined to clear").ThrowAsJavaScriptException();
    return env.Null();
  }

  auto tsfnPromise = Napi::ThreadSafeFunction::New(env, Napi::Function(), "SetOutputFormatAsync", 0, 1);

  std::thread([this, deferred = std::move(deferred), tsfnPromise = std::move(tsfnPromise), outputGuid, clearFormat]() mutable {
    try {
      HRESULT hr = S_OK;

      if (clearFormat) {
        this->device->ClearOutputFormat();
      } else {
        hr = this->device->SetOutputFormat(outputGuid);
      }

      if (FAILED(hr)) {
        auto callback = [deferred = std::move(deferred), hr](Napi::Env env, Napi::Function) mutable {
          std::string msg = HResultToString(hr);
          deferred.Reject(Napi::Error::New(env, msg).Value());
        };
        tsfnPromise.BlockingCall(callback);
        tsfnPromise.Release();
        return;
      }

      auto callback = [deferred = std::move(deferred), clearFormat](Napi::Env env, Napi::Function) mutable {
        Napi::Object result = Napi::Object::New(env);
        result.Set("success", Napi::Boolean::New(env, true));
        result.Set("message", Napi::String::New(env, clearFormat ? "Output format cleared" : "Output format set"));
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

  // Create a TSFN to resolve/reject the start promise from the worker thread.
  auto tsfnPromise = Napi::ThreadSafeFunction::New(env, Napi::Function(), "StartCaptureAsync", 0, 1);

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

  // Move the actual StartCapture call to a worker thread that initializes COM.
  std::thread([this, deferred = std::move(deferred), tsfnPromise = std::move(tsfnPromise)]() mutable {
    HRESULT hrCo = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool coInitialized = SUCCEEDED(hrCo);

    HRESULT hr = S_OK;
    EncodingParameters params = {0, 0};

    if (!this->device) {
      hr = E_FAIL;
    } else {
      // about to call device->StartCapture
      hr = this->device->StartCapture(this->claimedActivate, params);
      // device->StartCapture returned (hr logged by caller if needed)
    }

    if (FAILED(hr)) {
      // On failure, cleanup the TSFN stored on the instance
      if (this->frameTsfn) {
        this->frameTsfn.Release();
        this->frameTsfn = Napi::ThreadSafeFunction();
      }

      auto callback = [deferred = std::move(deferred), hr](Napi::Env env, Napi::Function) mutable {
        deferred.Reject(Napi::Error::New(env, HResultToString(hr)).Value());
      };
      tsfnPromise.BlockingCall(callback);
    } else {
      // Success: mark capturing and resolve
      auto callback = [deferred = std::move(deferred), this](Napi::Env env, Napi::Function) mutable {
        this->isCapturing = true;
        Napi::Object res = Napi::Object::New(env);
        res.Set("success", Napi::Boolean::New(env, true));
        res.Set("message", Napi::String::New(env, "Capture started"));
        deferred.Resolve(res);
      };
      tsfnPromise.BlockingCall(callback);
    }

    if (coInitialized) {
      CoUninitialize();
    }
    tsfnPromise.Release();
  }).detach();

  return deferred.Promise();
}

Napi::Value Camera::StopCaptureAsync(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  auto deferred = Napi::Promise::Deferred::New(env);

  if (!this->device) {
    deferred.Reject(Napi::Error::New(env, "Device not initialized").Value());
    return deferred.Promise();
  }

  // Clear the device frame callback first so internal state is reset cleanly.
  this->device->SetFrameCallback(nullptr);
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
    Napi::Object res = Napi::Object::New(env);
    res.Set("success", Napi::Boolean::New(env, true));
    res.Set("message", Napi::String::New(env, "Capture stopped"));
    deferred.Resolve(res);
  }

  return deferred.Promise();
}
