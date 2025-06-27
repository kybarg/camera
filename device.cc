#pragma once

#include "device.h"
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

template <class T>
void SafeRelease(T** ppT) {
  if (*ppT) {
    (*ppT)->Release();
    *ppT = NULL;
  }
}

void SwapBGRAtoRGBA(uint8_t* data, size_t size) {
  for (size_t i = 0; i < size; i += 4) {
    std::swap(data[i], data[i + 2]);  // Swap Blue and Red
                                      // Now: [R, G, B, A]
  }
}

void CaptureDevice::Clear() {
  for (UINT32 i = 0; i < m_cDevices; i++) {
    SafeRelease(&m_ppDevices[i]);
  }
  CoTaskMemFree(m_ppDevices);
  m_ppDevices = NULL;

  m_cDevices = 0;
}

HRESULT CaptureDevice::EnumerateDevices() {
  HRESULT hr = S_OK;
  IMFAttributes* pAttributes = NULL;

  Clear();

  // Initialize an attribute store. We will use this to
  // specify the enumeration parameters.

  hr = MFCreateAttributes(&pAttributes, 1);

  // Ask for source type = video capture devices
  if (SUCCEEDED(hr)) {
    hr = pAttributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  }

  // Enumerate devices.
  if (SUCCEEDED(hr)) {
    hr = MFEnumDeviceSources(pAttributes, &m_ppDevices, &m_cDevices);
  }

  SafeRelease(&pAttributes);

  return hr;
}

std::vector<std::pair<std::wstring, std::wstring>> CaptureDevice::GetDevicesList() {
  std::vector<std::pair<std::wstring, std::wstring>> devices;

  for (UINT32 i = 0; i < this->Count(); i++) {
    WCHAR* pFriendlyName = nullptr;
    WCHAR* pSymbolicLink = nullptr;

    HRESULT hr = this->m_ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &pFriendlyName, nullptr);
    if (SUCCEEDED(hr)) {
      hr = this->m_ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &pSymbolicLink, nullptr);
      if (SUCCEEDED(hr)) {
        devices.emplace_back(std::make_pair(pFriendlyName, pSymbolicLink));
        CoTaskMemFree(pSymbolicLink);
      }
      CoTaskMemFree(pFriendlyName);
    }
  }

  return devices;
}

HRESULT CaptureDevice::SelectDevice(int index) {
  HRESULT hr = S_OK;

  // Release the existing source if it exists
  if (m_pSource) {
    m_pSource->Release();
    m_pSource = NULL;
  }

  // Initialize the COM library
  hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

  if (SUCCEEDED(hr)) {
    // Create the media source object
    hr = m_ppDevices[index]->ActivateObject(IID_PPV_ARGS(&m_pSource));
  }

  // If successful, add a reference to m_pSource
  if (SUCCEEDED(hr)) {
    m_pSource->AddRef();
  }

  return hr;
}

HRESULT CaptureDevice::CreateStream() {
  HRESULT hr = S_OK;
  IMFMediaType *pSrcOutMediaType = NULL, *pMFTInputMediaType = NULL, *pMFTOutputMediaType = NULL;
  IUnknown* colorConvTransformUnk = NULL;

  hr = MFCreateSourceReaderFromMediaSource(m_pSource, NULL, &m_pReader);
  if (FAILED(hr)) goto CleanUp;

  hr = m_pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &pSrcOutMediaType);
  if (FAILED(hr)) goto CleanUp;

  hr = MFGetAttributeSize(pSrcOutMediaType, MF_MT_FRAME_SIZE, &width, &height);
  if (FAILED(hr)) goto CleanUp;

  hr = CoCreateInstance(CLSID_CColorConvertDMO, NULL, CLSCTX_INPROC_SERVER, IID_IUnknown, (void**)&colorConvTransformUnk);
  if (FAILED(hr)) goto CleanUp;

  hr = colorConvTransformUnk->QueryInterface(IID_PPV_ARGS(&m_pTransform));
  if (FAILED(hr)) goto CleanUp;

  hr = MFCreateMediaType(&pMFTInputMediaType);
  if (FAILED(hr)) goto CleanUp;

  hr = pSrcOutMediaType->CopyAllItems(pMFTInputMediaType);
  if (FAILED(hr)) goto CleanUp;

  hr = m_pTransform->SetInputType(0, pMFTInputMediaType, 0);
  if (FAILED(hr)) goto CleanUp;

  hr = MFCreateMediaType(&pMFTOutputMediaType);
  if (FAILED(hr)) goto CleanUp;

  hr = pSrcOutMediaType->CopyAllItems(pMFTOutputMediaType);
  if (FAILED(hr)) goto CleanUp;

  hr = pMFTOutputMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB24);
  if (FAILED(hr)) goto CleanUp;

  hr = pMFTOutputMediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
  if (FAILED(hr)) goto CleanUp;

  hr = m_pTransform->SetOutputType(0, pMFTOutputMediaType, 0);
  if (FAILED(hr)) goto CleanUp;

CleanUp:
  if (pMFTOutputMediaType) pMFTOutputMediaType->Release();
  if (pMFTInputMediaType) pMFTInputMediaType->Release();
  if (pSrcOutMediaType) pSrcOutMediaType->Release();
  if (colorConvTransformUnk) colorConvTransformUnk->Release();
  if (FAILED(hr) && m_pTransform) {
    m_pTransform->Release();
    m_pTransform = NULL;
  }

  return hr;
}

HRESULT CaptureDevice::StartCapture(std::function<HRESULT(IMFMediaBuffer*)> callback) {
  HRESULT hr = S_OK;

  DWORD mftStatus = 0;
  DWORD processOutputStatus = 0;
  IMFSample* pSample = NULL;
  IMFSample* pOutSample = NULL;
  IMFMediaBuffer* pBuffer = NULL;
  DWORD streamIndex, flags;
  LONGLONG llTimeStamp, llDuration;
  MFT_OUTPUT_DATA_BUFFER outputDataBuffer = {0};
  MFT_OUTPUT_STREAM_INFO StreamInfo;

  isCapturing = true;

  hr = m_pTransform->GetInputStatus(0, &mftStatus);

  if (MFT_INPUT_STATUS_ACCEPT_DATA != mftStatus) {
    hr = m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL);
  }

  if (SUCCEEDED(hr)) {
    hr = m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL);
  }

  if (SUCCEEDED(hr)) {
    while (isCapturing) {
      hr = m_pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIndex, &flags, &llTimeStamp, &pSample);
      if (FAILED(hr)) break;  // Handle error

      if (pSample) {
        hr = pSample->SetSampleTime(llTimeStamp);
        if (FAILED(hr)) break;  // Handle error

        hr = pSample->GetSampleDuration(&llDuration);
        if (FAILED(hr)) break;  // Handle error

        hr = m_pTransform->ProcessInput(0, pSample, 0);
        if (FAILED(hr)) break;  // Handle error

        auto mftResult = S_OK;
        while (mftResult == S_OK) {
          hr = MFCreateSample(&pOutSample);
          if (FAILED(hr)) break;  // Handle error

          hr = m_pTransform->GetOutputStreamInfo(0, &StreamInfo);
          if (FAILED(hr)) break;  // Handle error

          hr = MFCreateMemoryBuffer(StreamInfo.cbSize, &pBuffer);
          if (FAILED(hr)) break;  // Handle error

          hr = pOutSample->AddBuffer(pBuffer);
          if (FAILED(hr)) break;  // Handle error

          outputDataBuffer.dwStreamID = 0;
          outputDataBuffer.dwStatus = 0;
          outputDataBuffer.pEvents = NULL;
          outputDataBuffer.pSample = pOutSample;

          mftResult = m_pTransform->ProcessOutput(0, 1, &outputDataBuffer, &processOutputStatus);
          // if (FAILED(hr)) break;  // Handle error

          // if (mftResult == S_OK) {
          //   hr = outputDataBuffer.pSample->SetSampleTime(llTimeStamp);
          //   if (FAILED(hr)) break;  // Handle error

          //   hr = outputDataBuffer.pSample->SetSampleDuration(llDuration);
          //   if (FAILED(hr)) break;  // Handle error

          //   IMFMediaBuffer* buf = NULL;
          //   hr = pOutSample->ConvertToContiguousBuffer(&buf);
          //   if (FAILED(hr)) break;  // Handle error

          //   if (isCapturing) {
          //     hr = callback(buf);
          //     if (FAILED(hr)) break;  // Handle error
          //   }
          // }

          if (mftResult == S_OK) {
            hr = outputDataBuffer.pSample->SetSampleTime(llTimeStamp);
            if (FAILED(hr)) break;

            hr = outputDataBuffer.pSample->SetSampleDuration(llDuration);
            if (FAILED(hr)) break;

            IMFMediaBuffer* buf = nullptr;
            hr = pOutSample->ConvertToContiguousBuffer(&buf);
            if (FAILED(hr)) break;

            if (isCapturing) {
              BYTE* pData = nullptr;
              DWORD maxLength = 0;
              DWORD currentLength = 0;

              // Lock the buffer to get pointer to raw data
              hr = buf->Lock(&pData, &maxLength, &currentLength);
              if (FAILED(hr)) {
                SafeRelease(&buf);
                break;
              }

              // In-place swizzle BGRA -> RGBA for correct colors
              for (DWORD i = 0; i + 3 < currentLength; i += 4) {
                std::swap(pData[i], pData[i + 2]);  // Swap B and R channels
              }

              // Unlock before calling callback (some APIs require unlocked buffer)
              hr = buf->Unlock();
              if (FAILED(hr)) {
                buf->Release();
                break;
              }

              // Call your Node.js callback with the IMFMediaBuffer*
              buf->AddRef();
              hr = callback(buf);

              if (FAILED(hr)) break;
            } else {
              SafeRelease(&buf);
            }
          }

          SafeRelease(&pOutSample);  // Release pOutSample after using it.
          SafeRelease(&pBuffer);     // Release pBuffer after using it.
        }

        SafeRelease(&pOutSample);
        SafeRelease(&pBuffer);
        SafeRelease(&pSample);

        if (mftResult != MF_E_TRANSFORM_NEED_MORE_INPUT) {
          // Handle error condition.
          break;
        }
      }
    }
  }

  // Release resources before returning
  SafeRelease(&pOutSample);
  SafeRelease(&pBuffer);
  SafeRelease(&pSample);

  return hr;
}

HRESULT CaptureDevice::StopCapture() {
  HRESULT hr = S_OK;

  hr = m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, NULL);

  if (SUCCEEDED(hr)) {
    hr = m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL);
  }

  isCapturing = false;

  return hr;
}
