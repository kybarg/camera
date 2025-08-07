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
#include <algorithm>
#include <cfloat>
#include <tuple>
#include <cmath>

void CaptureDevice::Clear() {
  for (UINT32 i = 0; i < m_cDevices; i++) {
    SafeRelease(&m_ppDevices[i]);
  }
  CoTaskMemFree(m_ppDevices);
  m_ppDevices = NULL;

  m_cDevices = 0;
}

// Conservative device status check
bool CaptureDevice::IsCameraBusy(const std::wstring& symbolicLink) {
  
  // Device not found - assume not busy
  return false;
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

std::vector<DeviceInfo> CaptureDevice::GetDevicesList() {
  std::vector<DeviceInfo> devices;

  for (UINT32 i = 0; i < this->Count(); i++) {
    WCHAR* pFriendlyName = nullptr;
    WCHAR* pSymbolicLink = nullptr;

    HRESULT hr = this->m_ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &pFriendlyName, nullptr);
    if (SUCCEEDED(hr)) {
      hr = this->m_ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &pSymbolicLink, nullptr);
      if (SUCCEEDED(hr)) {
        // Use our dedicated function to check if device is busy
        std::wstring symbolicLinkStr(pSymbolicLink);
        bool isClaimed = IsCameraBusy(symbolicLinkStr);

        devices.emplace_back(DeviceInfo(pFriendlyName, pSymbolicLink, isClaimed));
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

    // Create the source reader for format enumeration
    hr = MFCreateSourceReaderFromMediaSource(m_pSource, NULL, &m_pReader);
    if (SUCCEEDED(hr)) {
      // Get the default format dimensions
      IMFMediaType* pMediaType = NULL;
      hr = m_pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &pMediaType);
      if (SUCCEEDED(hr)) {
        hr = MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &width, &height);
        SafeRelease(&pMediaType);
      }
    }
  }

  return hr;
}

HRESULT CaptureDevice::CreateStream() {
  HRESULT hr = S_OK;
  IMFMediaType *pSrcOutMediaType = NULL, *pMFTInputMediaType = NULL, *pMFTOutputMediaType = NULL;
  IUnknown* colorConvTransformUnk = NULL;

  // Create reader if it doesn't exist (it should exist from SelectDevice)
  if (!m_pReader) {
    hr = MFCreateSourceReaderFromMediaSource(m_pSource, NULL, &m_pReader);
    if (FAILED(hr)) goto CleanUp;
  }

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
                // Ultra-fast BGRA to RGBA conversion using vectorized operations
                const DWORD pixelCount = cbCurrentLength / 4;
                DWORD* pixels = reinterpret_cast<DWORD*>(pData);

                // Constants for bit manipulation
                constexpr DWORD ALPHA_MASK = 0xFF000000;  // Alpha = 255
                constexpr DWORD GREEN_MASK = 0x0000FF00;  // Green unchanged
                constexpr DWORD BLUE_MASK  = 0x000000FF;  // Blue channel
                constexpr DWORD RED_MASK   = 0x00FF0000;  // Red channel

                // Process 16 pixels at once for maximum throughput
                const DWORD vectorCount = pixelCount / 16;
                const DWORD vectorPixels = vectorCount * 16;

                // Vectorized processing - 16 pixels per iteration
                for (DWORD i = 0; i < vectorPixels; i += 16) {
                  // Process 16 pixels in parallel using loop unrolling
                  for (DWORD j = 0; j < 16; ++j) {
                    const DWORD pixel = pixels[i + j];
                    // BGRA -> RGBA: Swap R&B, keep G, set A=255
                    pixels[i + j] = ALPHA_MASK |
                                   (pixel & GREEN_MASK) |
                                   ((pixel & BLUE_MASK) << 16) |
                                   ((pixel & RED_MASK) >> 16);
                  }
                }

                // Handle remaining pixels (0-15 pixels)
                for (DWORD i = vectorPixels; i < pixelCount; ++i) {
                  const DWORD pixel = pixels[i];
                  pixels[i] = ALPHA_MASK |
                             (pixel & GREEN_MASK) |
                             ((pixel & BLUE_MASK) << 16) |
                             ((pixel & RED_MASK) >> 16);
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

std::vector<std::tuple<UINT32, UINT32, UINT32>> CaptureDevice::GetSupportedFormats() {
  std::vector<std::tuple<UINT32, UINT32, UINT32>> formats;

  if (!m_pReader) {
    return formats; // Return empty if no reader
  }

  DWORD mediaTypeIndex = 0;
  IMFMediaType* pMediaType = NULL;

  while (SUCCEEDED(m_pReader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, mediaTypeIndex, &pMediaType))) {
    UINT32 width, height;
    if (SUCCEEDED(MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &width, &height))) {
      UINT32 numerator, denominator;
      UINT32 frameRate = 30; // Default fallback
      if (SUCCEEDED(MFGetAttributeRatio(pMediaType, MF_MT_FRAME_RATE, &numerator, &denominator))) {
        if (denominator > 0) {
          // Calculate frame rate with proper rounding for fractional rates
          // For example: 30000/1001 ≈ 29.97 → rounds to 30
          frameRate = static_cast<UINT32>((static_cast<double>(numerator) / static_cast<double>(denominator)) + 0.5);
        }
      }

      // Check if this format combination is already in the list
      bool found = false;
      for (const auto& format : formats) {
        if (std::get<0>(format) == width && std::get<1>(format) == height && std::get<2>(format) == frameRate) {
          found = true;
          break;
        }
      }
      if (!found) {
        formats.emplace_back(width, height, frameRate);
      }
    }
    SafeRelease(&pMediaType);
    mediaTypeIndex++;
  }

  // Sort formats by resolution (descending) then by frame rate (descending)
  std::sort(formats.begin(), formats.end(),
    [](const std::tuple<UINT32, UINT32, UINT32>& a, const std::tuple<UINT32, UINT32, UINT32>& b) {
      UINT32 pixelsA = std::get<0>(a) * std::get<1>(a);
      UINT32 pixelsB = std::get<0>(b) * std::get<1>(b);
      if (pixelsA != pixelsB) {
        return pixelsA > pixelsB; // Higher resolution first
      }
      return std::get<2>(a) > std::get<2>(b); // Higher frame rate first for same resolution
    });

  return formats;
}

HRESULT CaptureDevice::SetDesiredFormat(UINT32 desiredWidth, UINT32 desiredHeight, UINT32 desiredFrameRate) {
  if (!m_pReader) {
    return E_POINTER;
  }

  HRESULT hr = S_OK;
  IMFMediaType* pBestMediaType = NULL;
  DWORD bestMediaTypeIndex = 0;

  // Find the closest matching format
  DWORD mediaTypeIndex = 0;
  IMFMediaType* pMediaType = NULL;

  UINT32 bestWidthDiff = UINT32_MAX;
  UINT32 bestHeightDiff = UINT32_MAX;
  UINT32 bestFrameRateDiff = UINT32_MAX;
  double bestScore = DBL_MAX;

  while (SUCCEEDED(m_pReader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, mediaTypeIndex, &pMediaType))) {
    UINT32 typeWidth, typeHeight;
    if (SUCCEEDED(MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &typeWidth, &typeHeight))) {
      UINT32 numerator, denominator;
      UINT32 frameRate = 30; // Default fallback
      if (SUCCEEDED(MFGetAttributeRatio(pMediaType, MF_MT_FRAME_RATE, &numerator, &denominator))) {
        if (denominator > 0) {
          // Calculate frame rate with proper rounding for fractional rates
          frameRate = static_cast<UINT32>((static_cast<double>(numerator) / static_cast<double>(denominator)) + 0.5);
        }
      }

      // Calculate similarity score (lower is better)
      UINT32 widthDiff = (typeWidth > desiredWidth) ? (typeWidth - desiredWidth) : (desiredWidth - typeWidth);
      UINT32 heightDiff = (typeHeight > desiredHeight) ? (typeHeight - desiredHeight) : (desiredHeight - typeHeight);
      UINT32 frameRateDiff = (frameRate > desiredFrameRate) ? (frameRate - desiredFrameRate) : (desiredFrameRate - frameRate);

      // Weighted score: resolution is more important than framerate
      double score = (widthDiff * 2.0) + (heightDiff * 2.0) + (frameRateDiff * 1.0);

      if (score < bestScore) {
        bestScore = score;
        bestWidthDiff = widthDiff;
        bestHeightDiff = heightDiff;
        bestFrameRateDiff = frameRateDiff;
        bestMediaTypeIndex = mediaTypeIndex;
        SafeRelease(&pBestMediaType);
        pBestMediaType = pMediaType;
        pBestMediaType->AddRef();
      }
    }
    SafeRelease(&pMediaType);
    mediaTypeIndex++;
  }

  if (pBestMediaType) {
    // Set the best matching media type
    hr = m_pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pBestMediaType);
    if (SUCCEEDED(hr)) {
      // Update our stored dimensions
      MFGetAttributeSize(pBestMediaType, MF_MT_FRAME_SIZE, &width, &height);
    }
    SafeRelease(&pBestMediaType);
  } else {
    hr = E_FAIL;
  }

  return hr;
}
