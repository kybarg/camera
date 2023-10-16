#include "camera.h"
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
#include <windows.h>
#include <iostream>
#include <thread>

template <class T>
void SafeRelease(T** ppT) {
  if (*ppT) {
    (*ppT)->Release();
    *ppT = NULL;
  }
}

template <class T>
void SafeRelease(T*& pT) {
  if (pT != NULL) {
    pT->Release();
    pT = NULL;
  }
}

struct ResultData {
  UINT32 width;
  UINT32 height;
  IMFMediaBuffer* buffer;
};

Napi::Object Camera::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func = DefineClass(env, "Camera", {InstanceMethod("enumerateDevices", &Camera::EnumerateDevices), InstanceMethod("selectDevice", &Camera::SelectDevice), InstanceMethod("startCapture", &Camera::StartCapture)});

  Napi::FunctionReference* constructor = new Napi::FunctionReference();
  *constructor = Napi::Persistent(func);
  env.SetInstanceData(constructor);

  exports.Set("Camera", func);
  return exports;
}

Camera::Camera(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<Camera>(info) {
  Napi::Env env = info.Env();

  HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  if (FAILED(hr)) {
    _com_error err(hr);
    LPCTSTR errMsg = err.ErrorMessage();

    Napi::Error::New(env, errMsg).ThrowAsJavaScriptException();
  }

  // Initialize nativeThread
}

Camera::~Camera() {
  if (nativeThread.joinable()) {
    nativeThread.join();
  }
}

void Camera::ThreadFunction() {
  auto callback = [](Napi::Env env, Napi::Function jsCallback, void* data) {
    ResultData* resultData = static_cast<ResultData*>(data);

    BYTE* bufData = nullptr;
    DWORD bufLength;
    HRESULT hr = resultData->buffer->Lock(&bufData, nullptr, &bufLength);

    if (FAILED(hr)) {
      return;
    }

    // Wrap the allocated memory in a Buffer
    Napi::Buffer<BYTE> buffer = Napi::Buffer<BYTE>::New(env, bufData, bufLength);

    Napi::Object result = Napi::Object::New(env);
    result.Set("width", Napi::Number::New(env, resultData->width));
    result.Set("height", Napi::Number::New(env, resultData->height));
    result.Set("data", buffer);

    jsCallback.Call({env.Null(), result});

    // Unlock the buffer when done processing the data
    resultData->buffer->Unlock();

    // SafeRelease(resultData);
  };

  auto errorCallback = [](Napi::Env env, Napi::Function jsCallback, LPCTSTR errMsg) {
    jsCallback.Call({Napi::Error::New(env, "error").Value()});
  };

  HRESULT hr = S_OK;

  hr = MFCreateSourceReaderFromMediaSource(this->ppSource, NULL, &this->pVideoReader);
  if (FAILED(hr)) {
    _com_error err(hr);
    LPCTSTR errMsg = err.ErrorMessage();

    napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
    if (status != napi_ok) {
      // Handle error
      return;
    }
  }

  IMFMediaType* pSrcOutMediaType = NULL;

  hr = pVideoReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &pSrcOutMediaType);
  if (FAILED(hr)) {
    _com_error err(hr);
    LPCTSTR errMsg = err.ErrorMessage();

    napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
    if (status != napi_ok) {
      // Handle error
      return;
    }
  }

  IUnknown* colorConvTransformUnk = NULL;

  hr = CoCreateInstance(CLSID_CColorConvertDMO, NULL, CLSCTX_INPROC_SERVER, IID_IUnknown, (void**)&colorConvTransformUnk);
  if (FAILED(hr)) {
    _com_error err(hr);
    LPCTSTR errMsg = err.ErrorMessage();

    napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
    if (status != napi_ok) {
      // Handle error
      return;
    }
  }

  IMFTransform* pTransform = NULL;  //< this is RGB24 Encoder MFT

  hr = colorConvTransformUnk->QueryInterface(IID_PPV_ARGS(&pTransform));
  if (FAILED(hr)) {
    _com_error err(hr);
    LPCTSTR errMsg = err.ErrorMessage();

    napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
    if (status != napi_ok) {
      // Handle error
      return;
    }
  }

  IMFMediaType *pMFTInputMediaType = NULL, *pMFTOutputMediaType = NULL;

  MFCreateMediaType(&pMFTInputMediaType);

  hr = pSrcOutMediaType->CopyAllItems(pMFTInputMediaType);
  if (FAILED(hr)) {
    _com_error err(hr);
    LPCTSTR errMsg = err.ErrorMessage();

    napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
    if (status != napi_ok) {
      // Handle error
      return;
    }
  }

  hr = pTransform->SetInputType(0, pMFTInputMediaType, 0);
  if (FAILED(hr)) {
    _com_error err(hr);
    LPCTSTR errMsg = err.ErrorMessage();

    napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
    if (status != napi_ok) {
      // Handle error
      return;
    }
  }

  MFCreateMediaType(&pMFTOutputMediaType);

  hr = pSrcOutMediaType->CopyAllItems(pMFTOutputMediaType);
  if (FAILED(hr)) {
    _com_error err(hr);
    LPCTSTR errMsg = err.ErrorMessage();

    napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
    if (status != napi_ok) {
      // Handle error
      return;
    }
  }

  hr = pMFTOutputMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB24);
  if (FAILED(hr)) {
    _com_error err(hr);
    LPCTSTR errMsg = err.ErrorMessage();

    napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
    if (status != napi_ok) {
      // Handle error
      return;
    }
  }

  hr = pMFTOutputMediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
  if (FAILED(hr)) {
    _com_error err(hr);
    LPCTSTR errMsg = err.ErrorMessage();

    napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
    if (status != napi_ok) {
      // Handle error
      return;
    }
  }

  hr = pTransform->SetOutputType(0, pMFTOutputMediaType, 0);
  if (FAILED(hr)) {
    _com_error err(hr);
    LPCTSTR errMsg = err.ErrorMessage();

    napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
    if (status != napi_ok) {
      // Handle error
      return;
    }
  }

  DWORD mftStatus = 0;
  hr = pTransform->GetInputStatus(0, &mftStatus);
  if (FAILED(hr)) {
    _com_error err(hr);
    LPCTSTR errMsg = err.ErrorMessage();

    napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
    if (status != napi_ok) {
      // Handle error
      return;
    }
  }

  hr = pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL);
  if (FAILED(hr)) {
    _com_error err(hr);
    LPCTSTR errMsg = err.ErrorMessage();

    napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
    if (status != napi_ok) {
      // Handle error
      return;
    }
  }

  hr = pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL);
  if (FAILED(hr)) {
    _com_error err(hr);
    LPCTSTR errMsg = err.ErrorMessage();

    napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
    if (status != napi_ok) {
      // Handle error
      return;
    }
  }

  MFT_OUTPUT_DATA_BUFFER outputDataBuffer;
  DWORD processOutputStatus = 0;
  IMFSample* videoSample = NULL;
  DWORD streamIndex, flags;
  LONGLONG llVideoTimeStamp, llSampleDuration;
  MFT_OUTPUT_STREAM_INFO StreamInfo;
  IMFSample* mftOutSample = NULL;
  IMFMediaBuffer* pBuffer = NULL;
  int sampleCount = 0;
  // DWORD mftOutFlags;

  UINT32 pWidth = 0;
  UINT32 pHeight = 0;
  MFGetAttributeSize(pSrcOutMediaType, MF_MT_FRAME_SIZE, &pWidth, &pHeight);

  while (true) {
    hr = pVideoReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                  0,                  // Flags.
                                  &streamIndex,       // Receives the actual stream index.
                                  &flags,             // Receives status flags.
                                  &llVideoTimeStamp,  // Receives the time stamp.
                                  &videoSample        // Receives the sample or NULL.
    );
    if (FAILED(hr)) {
      _com_error err(hr);
      LPCTSTR errMsg = err.ErrorMessage();

      napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
      if (status != napi_ok) {
        // Handle error
        return;
      }
    }

    if (videoSample) {
      hr = videoSample->SetSampleTime(llVideoTimeStamp);

      if (FAILED(hr)) {
        _com_error err(hr);
        LPCTSTR errMsg = err.ErrorMessage();

        napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
        if (status != napi_ok) {
          // Handle error
          return;
        }
      }
      hr = videoSample->GetSampleDuration(&llSampleDuration);

      if (FAILED(hr)) {
        _com_error err(hr);
        LPCTSTR errMsg = err.ErrorMessage();

        napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
        if (status != napi_ok) {
          // Handle error
          return;
        }
      }

      // Pass the video sample to the H.264 transform.

      hr = pTransform->ProcessInput(0, videoSample, 0);

      if (FAILED(hr)) {
        _com_error err(hr);
        LPCTSTR errMsg = err.ErrorMessage();

        napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
        if (status != napi_ok) {
          // Handle error
          return;
        }
      }

      auto mftResult = S_OK;

      while (mftResult == S_OK) {
        hr = MFCreateSample(&mftOutSample);

        if (FAILED(hr)) {
          _com_error err(hr);
          LPCTSTR errMsg = err.ErrorMessage();

          napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
          if (status != napi_ok) {
            // Handle error
            return;
          }
        }
        hr = pTransform->GetOutputStreamInfo(0, &StreamInfo);

        if (FAILED(hr)) {
          _com_error err(hr);
          LPCTSTR errMsg = err.ErrorMessage();

          napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
          if (status != napi_ok) {
            // Handle error
            return;
          }
        }
        hr = MFCreateMemoryBuffer(StreamInfo.cbSize, &pBuffer);

        if (FAILED(hr)) {
          _com_error err(hr);
          LPCTSTR errMsg = err.ErrorMessage();

          napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
          if (status != napi_ok) {
            // Handle error
            return;
          }
        }
        hr = mftOutSample->AddBuffer(pBuffer);

        if (FAILED(hr)) {
          _com_error err(hr);
          LPCTSTR errMsg = err.ErrorMessage();

          napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
          if (status != napi_ok) {
            // Handle error
            return;
          }
        }
        outputDataBuffer.dwStreamID = 0;
        outputDataBuffer.dwStatus = 0;
        outputDataBuffer.pEvents = NULL;
        outputDataBuffer.pSample = mftOutSample;

        mftResult = pTransform->ProcessOutput(0, 1, &outputDataBuffer, &processOutputStatus);

        if (mftResult == S_OK) {
          hr = outputDataBuffer.pSample->SetSampleTime(llVideoTimeStamp);

          if (FAILED(hr)) {
            _com_error err(hr);
            LPCTSTR errMsg = err.ErrorMessage();

            napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
            if (status != napi_ok) {
              // Handle error
              return;
            }
          }
          hr = outputDataBuffer.pSample->SetSampleDuration(llSampleDuration);

          if (FAILED(hr)) {
            _com_error err(hr);
            LPCTSTR errMsg = err.ErrorMessage();

            napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
            if (status != napi_ok) {
              // Handle error
              return;
            }
          }

          IMFMediaBuffer* buf = NULL;

          hr = mftOutSample->ConvertToContiguousBuffer(&buf);

          if (FAILED(hr)) {
            _com_error err(hr);
            LPCTSTR errMsg = err.ErrorMessage();

            napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
            if (status != napi_ok) {
              // Handle error
              return;
            }
          }

          // hr =  videoSample->GetBufferByIndex(0, &buf);

          if (FAILED(hr)) {
            _com_error err(hr);
            LPCTSTR errMsg = err.ErrorMessage();

            napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
            if (status != napi_ok) {
              // Handle error
              return;
            }
          }
          // hr =  videoSample->ConvertToContiguousBuffer(&buf);

          if (FAILED(hr)) {
            _com_error err(hr);
            LPCTSTR errMsg = err.ErrorMessage();

            napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
            if (status != napi_ok) {
              // Handle error
              return;
            }
          }

          // hr =  buf->GetCurrentLength(&bufLength);

          if (FAILED(hr)) {
            _com_error err(hr);
            LPCTSTR errMsg = err.ErrorMessage();

            napi_status status = this->tsfn.BlockingCall(errMsg, errorCallback);
            if (status != napi_ok) {
              // Handle error
              return;
            }
          }

          ResultData resultData;

          resultData.width = pWidth;
          resultData.height = pHeight;
          resultData.buffer = buf;

          napi_status status = this->tsfn.BlockingCall(&resultData, callback);
          if (status != napi_ok) {
            // Handle error
            return;
          }
        }

        SafeRelease(pBuffer);
        SafeRelease(mftOutSample);
      }

      SafeRelease(&videoSample);

      if (mftResult != MF_E_TRANSFORM_NEED_MORE_INPUT) {
        // Error condition.
        printf("H264 encoder error on process output %.2X.", mftResult);
        // break;
        return;
      }
    }
  }
}

Napi::Value Camera::EnumerateDevices(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  IMFAttributes* pAttributes = NULL;

  HRESULT hr = MFCreateAttributes(&pAttributes, 1);

  if (FAILED(hr)) {
    Napi::TypeError::New(env, "MFCreateAttributes failed").ThrowAsJavaScriptException();
    return env.Null();
  }

  // Ask for source type = video capture devices
  hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

  if (FAILED(hr)) {
    Napi::TypeError::New(env, "SetGUID failed").ThrowAsJavaScriptException();
    SafeRelease(&pAttributes);
    return env.Null();
  }

  // Enumerate devices.
  UINT32 count;
  hr = MFEnumDeviceSources(pAttributes, &this->ppDevices, &count);
  if (FAILED(hr) || count == 0) {
    Napi::Error::New(env, "No video capture devices found").ThrowAsJavaScriptException();
    SafeRelease(&pAttributes);
    return env.Null();
  }

  // Create an array to store the device info objects.
  Napi::Array devices = Napi::Array::New(env);

  for (DWORD i = 0; i < count; i++) {
    Napi::Object deviceInfo = Napi::Object::New(env);

    WCHAR* szFriendlyName = NULL;
    hr = this->ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &szFriendlyName, NULL);
    if (SUCCEEDED(hr)) {
      deviceInfo.Set("friendlyName", Napi::String::New(env, reinterpret_cast<const char16_t*>(szFriendlyName)));
      CoTaskMemFree(szFriendlyName);
    } else {
      continue;
    }

    WCHAR* szSymbolicLink;
    hr = this->ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &szSymbolicLink, NULL);

    if (SUCCEEDED(hr)) {
      deviceInfo.Set("symbolicLink", Napi::String::New(env, reinterpret_cast<const char16_t*>(szSymbolicLink)));
      CoTaskMemFree(szSymbolicLink);
      devices.Set(i, deviceInfo);
      // SafeRelease(&this->this->ppDevices[i]);
    } else {
      // SafeRelease(&this->this->ppDevices[i]);
      continue;
    }
  }

  // env.Null();
  SafeRelease(&pAttributes);

  return devices;
}

Napi::Value Camera::SelectDevice(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1) {
    Napi::Error::New(env, "Expected device index").ThrowAsJavaScriptException();
    return env.Null();
  } else if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "Expected device index to be number").ThrowAsJavaScriptException();
    return env.Null();
  }

  int index = info[0].As<Napi::Number>().Int32Value();

  HRESULT hr = S_OK;

  this->ppSource = NULL;
  IMFMediaSource* pSource = NULL;

  // Create the media source object.
  hr = this->ppDevices[index]->ActivateObject(IID_PPV_ARGS(&pSource));
  if (FAILED(hr)) {
    _com_error err(hr);
    LPCTSTR errMsg = err.ErrorMessage();

    Napi::Error::New(env, errMsg).ThrowAsJavaScriptException();
    return env.Null();
  }

  this->ppSource = pSource;
  (this->ppSource)->AddRef();

  SafeRelease(&pSource);

  return Napi::Boolean::New(env, true);
}

void Camera::StartCapture(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (this->ppSource == NULL) {
    Napi::Error::New(env, "Need to select device first with \"SelectDevice\"").ThrowAsJavaScriptException();
    return;
  } else if (info.Length() < 1) {
    Napi::Error::New(env, "Callback function is expected").ThrowAsJavaScriptException();
    return;
  } else if (!info[0].IsFunction()) {
    Napi::TypeError::New(env, "Callback mus be a function").ThrowAsJavaScriptException();
    return;
  }

  Napi::Function cb = info[0].As<Napi::Function>();

  // Create a ThreadSafeFunction
  this->tsfn = Napi::ThreadSafeFunction::New(
      env,
      cb,                  // JavaScript function called asynchronously
      "Capture",           // Name
      0,                   // Unlimited queue
      1,                   // Only one thread will use this initially
      [this](Napi::Env) {  // Finalizer used to clean threads up
        if (this->nativeThread.joinable()) {
          this->nativeThread.join();
        }
      });

  this->nativeThread = std::thread(&Camera::ThreadFunction, this);
}
