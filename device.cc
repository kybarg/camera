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

  hr = pMFTOutputMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
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

  isCapturing = true;

  hr = m_pTransform->GetInputStatus(0, &mftStatus);

  if (MFT_INPUT_STATUS_ACCEPT_DATA != mftStatus) {
    hr = m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL);
  }

  if (SUCCEEDED(hr)) {
    // Pre-allocate buffers once
    hr = m_pTransform->GetOutputStreamInfo(0, &m_StreamInfo);
    if (SUCCEEDED(hr)) {
      m_bStreamInfoInitialized = true;

      hr = MFCreateSample(&m_pReusableOutSample);
      if (SUCCEEDED(hr)) {
        hr = MFCreateMemoryBuffer(m_StreamInfo.cbSize, &m_pReusableBuffer);
        if (SUCCEEDED(hr)) {
          hr = m_pReusableOutSample->AddBuffer(m_pReusableBuffer);
        }
      }
    }
  }

  if (SUCCEEDED(hr)) {
    while (isCapturing) {
      hr = m_pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIndex, &flags, &llTimeStamp, &pSample);
      if (FAILED(hr)) break;

      if (pSample) {
        hr = pSample->SetSampleTime(llTimeStamp);
        if (FAILED(hr)) break;

        hr = pSample->GetSampleDuration(&llDuration);
        if (FAILED(hr)) break;

        hr = m_pTransform->ProcessInput(0, pSample, 0);
        if (FAILED(hr)) break;

        auto mftResult = S_OK;
        while (mftResult == S_OK) {
          // Reuse pre-allocated sample and buffer
          m_pReusableOutSample->RemoveAllBuffers();
          hr = m_pReusableOutSample->AddBuffer(m_pReusableBuffer);
          if (FAILED(hr)) break;

          outputDataBuffer.dwStreamID = 0;
          outputDataBuffer.dwStatus = 0;
          outputDataBuffer.pEvents = NULL;
          outputDataBuffer.pSample = m_pReusableOutSample;

          mftResult = m_pTransform->ProcessOutput(0, 1, &outputDataBuffer, &processOutputStatus);
          // if (FAILED(hr)) break;  // Handle error

          if (mftResult == S_OK) {
            hr = outputDataBuffer.pSample->SetSampleTime(llTimeStamp);
            if (FAILED(hr)) break;

            hr = outputDataBuffer.pSample->SetSampleDuration(llDuration);
            if (FAILED(hr)) break;

            IMFMediaBuffer* buf = nullptr;
            hr = outputDataBuffer.pSample->ConvertToContiguousBuffer(&buf);
            if (FAILED(hr)) break;

            if (isCapturing) {
              BYTE* pData = nullptr;
              DWORD cbMaxLength = 0;
              DWORD cbCurrentLength = 0;

              hr = buf->Lock(&pData, &cbMaxLength, &cbCurrentLength);
              if (SUCCEEDED(hr)) {
                // High-performance BGRA to RGBA conversion
                const DWORD pixelCount = cbCurrentLength / 4;
                DWORD* pixels = reinterpret_cast<DWORD*>(pData);

                // Process 8 pixels at a time for maximum cache efficiency
                const DWORD alignedCount = (pixelCount / 8) * 8;

                // Unrolled loop for 8 pixels at once
                for (DWORD i = 0; i < alignedCount; i += 8) {
                  // Load 8 pixels
                  DWORD p0 = pixels[i];
                  DWORD p1 = pixels[i + 1];
                  DWORD p2 = pixels[i + 2];
                  DWORD p3 = pixels[i + 3];
                  DWORD p4 = pixels[i + 4];
                  DWORD p5 = pixels[i + 5];
                  DWORD p6 = pixels[i + 6];
                  DWORD p7 = pixels[i + 7];

                  // Convert BGRA (0xAABBGGRR) to RGBA (0xFFRRGGBB) using bit manipulation
                  // Extract: B=(p&0xFF), G=(p&0xFF00), R=(p&0xFF0000)
                  // Result: A=0xFF000000, R=(B<<16), G=(G), B=(R>>16)
                  pixels[i]     = 0xFF000000 | ((p0 & 0x0000FF00)) | ((p0 & 0x00FF0000) >> 16) | ((p0 & 0x000000FF) << 16);
                  pixels[i + 1] = 0xFF000000 | ((p1 & 0x0000FF00)) | ((p1 & 0x00FF0000) >> 16) | ((p1 & 0x000000FF) << 16);
                  pixels[i + 2] = 0xFF000000 | ((p2 & 0x0000FF00)) | ((p2 & 0x00FF0000) >> 16) | ((p2 & 0x000000FF) << 16);
                  pixels[i + 3] = 0xFF000000 | ((p3 & 0x0000FF00)) | ((p3 & 0x00FF0000) >> 16) | ((p3 & 0x000000FF) << 16);
                  pixels[i + 4] = 0xFF000000 | ((p4 & 0x0000FF00)) | ((p4 & 0x00FF0000) >> 16) | ((p4 & 0x000000FF) << 16);
                  pixels[i + 5] = 0xFF000000 | ((p5 & 0x0000FF00)) | ((p5 & 0x00FF0000) >> 16) | ((p5 & 0x000000FF) << 16);
                  pixels[i + 6] = 0xFF000000 | ((p6 & 0x0000FF00)) | ((p6 & 0x00FF0000) >> 16) | ((p6 & 0x000000FF) << 16);
                  pixels[i + 7] = 0xFF000000 | ((p7 & 0x0000FF00)) | ((p7 & 0x00FF0000) >> 16) | ((p7 & 0x000000FF) << 16);
                }

                // Handle remaining pixels (0-7 pixels)
                for (DWORD i = alignedCount; i < pixelCount; ++i) {
                  DWORD pixel = pixels[i];
                  pixels[i] = 0xFF000000 | ((pixel & 0x0000FF00)) | ((pixel & 0x00FF0000) >> 16) | ((pixel & 0x000000FF) << 16);
                }

                hr = buf->Unlock();

                if (FAILED(hr)) {
                  SafeRelease(&buf);
                  break;
                }
              }

              hr = callback(buf);

              if (FAILED(hr)) {
                SafeRelease(&buf);
                break;
              }
            } else {
              SafeRelease(&buf);
            }
          }

          SafeRelease(&pSample);

          if (mftResult != MF_E_TRANSFORM_NEED_MORE_INPUT) {
            // Handle error condition.
            break;
          }
        }
      }
    }
  }

  // Release resources before returning
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
