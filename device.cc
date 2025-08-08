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
#include <functional>

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

std::vector<DeviceInfo> CaptureDevice::GetDevicesList() {
  std::vector<DeviceInfo> devices;

  for (UINT32 i = 0; i < this->Count(); i++) {
    WCHAR* pFriendlyName = nullptr;
    WCHAR* pSymbolicLink = nullptr;

    HRESULT hr = this->m_ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &pFriendlyName, nullptr);
    if (SUCCEEDED(hr)) {
      hr = this->m_ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &pSymbolicLink, nullptr);
      if (SUCCEEDED(hr)) {
        // Create device info without isClaimed logic
        devices.emplace_back(DeviceInfo(pFriendlyName, pSymbolicLink));
        CoTaskMemFree(pSymbolicLink);
      }
      CoTaskMemFree(pFriendlyName);
    }
  }

  return devices;
}

HRESULT CaptureDevice::SelectDeviceBySymbolicLink(const std::wstring& targetSymbolicLink) {
  HRESULT hr = S_OK;

  // Validate input
  if (targetSymbolicLink.empty()) {
    return E_INVALIDARG;
  }

  // Ensure devices are enumerated
  if (m_cDevices == 0 || !m_ppDevices) {
    hr = EnumerateDevices();
    if (FAILED(hr)) {
      return hr;
    }
  }

  // Release the existing source if it exists
  if (m_pSource) {
    m_pSource->Release();
    m_pSource = NULL;
  }

  // Release existing reader
  if (m_pReader) {
    m_pReader->Release();
    m_pReader = NULL;
  }

  // Initialize the COM library (ensure it's initialized)
  hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  // Note: S_FALSE means COM was already initialized, which is OK
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    return hr;
  }

  if (SUCCEEDED(hr) || hr == S_FALSE || hr == RPC_E_CHANGED_MODE) {
    // Find the device with matching symbolic link
    int deviceIndex = -1;
    for (UINT32 i = 0; i < m_cDevices; i++) {
      if (!m_ppDevices[i]) {
        continue; // Skip null devices
      }

      WCHAR* pSymbolicLink = nullptr;
      hr = m_ppDevices[i]->GetAllocatedString(
          MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
          &pSymbolicLink, nullptr);

      if (SUCCEEDED(hr) && pSymbolicLink) {
        bool isMatch = (targetSymbolicLink == std::wstring(pSymbolicLink));
        CoTaskMemFree(pSymbolicLink);

        if (isMatch) {
          deviceIndex = i;
          break;
        }
      } else if (pSymbolicLink) {
        CoTaskMemFree(pSymbolicLink);
      }
    }

    if (deviceIndex == -1) {
      return E_INVALIDARG; // Device not found
    }

    // Validate device before activation
    if (!m_ppDevices[deviceIndex]) {
      return E_POINTER;
    }

    // Create the media source object with retry logic
    const int MAX_ACTIVATION_ATTEMPTS = 3;
    for (int attempt = 0; attempt < MAX_ACTIVATION_ATTEMPTS; attempt++) {
      hr = m_ppDevices[deviceIndex]->ActivateObject(IID_PPV_ARGS(&m_pSource));

      if (SUCCEEDED(hr)) {
        break;
      } else if (attempt < MAX_ACTIVATION_ATTEMPTS - 1) {
        // Wait briefly before retry
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }

    if (SUCCEEDED(hr) && m_pSource) {
      // Validate the media source
      IMFPresentationDescriptor* pPD = nullptr;
      HRESULT validateHr = m_pSource->CreatePresentationDescriptor(&pPD);
      if (FAILED(validateHr)) {
        SafeRelease(&m_pSource);
        return validateHr;
      }
      SafeRelease(&pPD);

      // Create the source reader with retry logic
      const int MAX_READER_ATTEMPTS = 3;
      for (int attempt = 0; attempt < MAX_READER_ATTEMPTS; attempt++) {
        hr = MFCreateSourceReaderFromMediaSource(m_pSource, NULL, &m_pReader);

        if (SUCCEEDED(hr)) {
          break;
        } else if (attempt < MAX_READER_ATTEMPTS - 1) {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
      }

      if (SUCCEEDED(hr) && m_pReader) {
        // Validate the reader by trying to get a media type
        IMFMediaType* pMediaType = NULL;
        hr = m_pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &pMediaType);
        if (SUCCEEDED(hr) && pMediaType) {
          // Get dimensions with validation
          HRESULT dimHr = MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &width, &height);
          if (FAILED(dimHr) || width == 0 || height == 0) {
            // Set reasonable defaults if dimensions are invalid
            width = 640;
            height = 480;
          }
          SafeRelease(&pMediaType);
        } else {
          // Failed to get media type - cleanup and return error
          SafeRelease(&m_pReader);
          SafeRelease(&m_pSource);
          return hr;
        }
      } else {
        // Failed to create reader - cleanup source
        SafeRelease(&m_pSource);
        return hr;
      }
    } else {
      return hr;
    }
  }

  return hr;
}

HRESULT CaptureDevice::ReleaseDevice() {
  HRESULT hr = S_OK;

  // Stop capture first if it's running
  if (isCapturing) {
    HRESULT stopHr = StopCapture();
    // Don't fail the release if stop fails, but log the error
    if (FAILED(stopHr)) {
      hr = stopHr; // Remember the error but continue cleanup
    }
  }

  // Reset dimensions early
  width = 0;
  height = 0;

  // Release pre-allocated buffers first
  SafeRelease(&m_pReusableOutSample);
  SafeRelease(&m_pReusableBuffer);

  // Reset stream info initialization flag
  m_bStreamInfoInitialized = false;
  memset(&m_StreamInfo, 0, sizeof(m_StreamInfo));

  // Release transform before reader/source
  if (m_pTransform) {
    // Try to flush any pending data
    m_pTransform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL);
    m_pTransform->Release();
    m_pTransform = NULL;
  }

  // Release source reader
  if (m_pReader) {
    // Flush any pending reads
    m_pReader->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    m_pReader->Release();
    m_pReader = NULL;
  }

  // Release media source last
  if (m_pSource) {
    // Try to shutdown the source gracefully
    IMFMediaSource* pSource = m_pSource;
    m_pSource = NULL; // Clear pointer first to prevent race conditions

    // Shutdown source
    HRESULT shutdownHr = pSource->Shutdown();
    pSource->Release();

    if (FAILED(shutdownHr) && SUCCEEDED(hr)) {
      hr = shutdownHr;
    }
  }

  // Reset capture flag last
  isCapturing = false;

  return hr;
}

HRESULT CaptureDevice::CreateStream() {
  HRESULT hr = S_OK;
  IMFMediaType *pSrcOutMediaType = NULL, *pMFTInputMediaType = NULL, *pMFTOutputMediaType = NULL;
  IUnknown* colorConvTransformUnk = NULL;

  // Validate prerequisites
  if (!m_pSource) {
    return E_POINTER;
  }

  // Create reader if it doesn't exist (it should exist from SelectDevice)
  if (!m_pReader) {
    hr = MFCreateSourceReaderFromMediaSource(m_pSource, NULL, &m_pReader);
    if (FAILED(hr)) goto CleanUp;
  }

  // Validate reader
  if (!m_pReader) {
    hr = E_POINTER;
    goto CleanUp;
  }

  // Get source media type with validation
  hr = m_pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &pSrcOutMediaType);
  if (FAILED(hr)) goto CleanUp;

  if (!pSrcOutMediaType) {
    hr = E_POINTER;
    goto CleanUp;
  }

  // Validate and get frame size
  hr = MFGetAttributeSize(pSrcOutMediaType, MF_MT_FRAME_SIZE, &width, &height);
  if (FAILED(hr) || width == 0 || height == 0) {
    // Use fallback dimensions if invalid
    width = 640;
    height = 480;
    hr = S_OK; // Don't fail on dimension issues
  }

  // Verify color converter is available
  hr = CoCreateInstance(CLSID_CColorConvertDMO, NULL, CLSCTX_INPROC_SERVER, IID_IUnknown, (void**)&colorConvTransformUnk);
  if (FAILED(hr)) {
    // Try alternative color converter
    hr = CoCreateInstance(CLSID_VideoProcessorMFT, NULL, CLSCTX_INPROC_SERVER, IID_IUnknown, (void**)&colorConvTransformUnk);
    if (FAILED(hr)) goto CleanUp;
  }

  if (!colorConvTransformUnk) {
    hr = E_POINTER;
    goto CleanUp;
  }

  hr = colorConvTransformUnk->QueryInterface(IID_PPV_ARGS(&m_pTransform));
  if (FAILED(hr)) goto CleanUp;

  if (!m_pTransform) {
    hr = E_POINTER;
    goto CleanUp;
  }

  // Create and configure input media type
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

  // Validate prerequisites
  if (!callback) {
    return E_INVALIDARG;
  }

  if (!m_pTransform || !m_pReader) {
    return E_POINTER;
  }

  if (isCapturing) {
    return MF_E_INVALIDREQUEST; // Already capturing
  }

  DWORD mftStatus = 0;
  DWORD processOutputStatus = 0;
  IMFSample* pSample = NULL;
  IMFSample* pOutSample = NULL;
  IMFMediaBuffer* pBuffer = NULL;
  DWORD streamIndex, flags;
  LONGLONG llTimeStamp = 0, llDuration = 0;
  MFT_OUTPUT_DATA_BUFFER outputDataBuffer = {0};

  // Set capturing flag early but be prepared to reset on failure
  isCapturing = true;

  // Validate transform state
  hr = m_pTransform->GetInputStatus(0, &mftStatus);
  if (FAILED(hr)) {
    isCapturing = false;
    return hr;
  }

  if (MFT_INPUT_STATUS_ACCEPT_DATA != mftStatus) {
    hr = m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL);
    if (FAILED(hr)) {
      isCapturing = false;
      return hr;
    }
  }

  if (SUCCEEDED(hr)) {
    // Pre-allocate buffers once with validation
    if (!m_bStreamInfoInitialized) {
      hr = m_pTransform->GetOutputStreamInfo(0, &m_StreamInfo);
      if (SUCCEEDED(hr) && m_StreamInfo.cbSize > 0) {
        m_bStreamInfoInitialized = true;

        hr = MFCreateSample(&m_pReusableOutSample);
        if (SUCCEEDED(hr)) {
          hr = MFCreateMemoryBuffer(m_StreamInfo.cbSize, &m_pReusableBuffer);
          if (SUCCEEDED(hr)) {
            hr = m_pReusableOutSample->AddBuffer(m_pReusableBuffer);
          }
        }

        if (FAILED(hr)) {
          isCapturing = false;
          return hr;
        }
      } else {
        isCapturing = false;
        return FAILED(hr) ? hr : E_FAIL;
      }
    }
  } else {
    isCapturing = false;
    return hr;
  }

  // Main capture loop with enhanced error handling
  int consecutiveErrors = 0;
  const int MAX_CONSECUTIVE_ERRORS = 5;

  if (SUCCEEDED(hr)) {
    while (isCapturing) {
      // Early termination check to avoid blocking reads when stopping
      if (!isCapturing) break;

      hr = m_pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIndex, &flags, &llTimeStamp, &pSample);

      // Handle device disconnection gracefully
      if (hr == MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED ||
          hr == MF_E_VIDEO_RECORDING_DEVICE_PREEMPTED ||
          hr == HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED)) {
        break; // Device disconnected - exit gracefully
      }

      if (FAILED(hr)) {
        consecutiveErrors++;
        if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
          break; // Too many errors - exit
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }

      consecutiveErrors = 0; // Reset error count on success

      if (pSample) {
        hr = pSample->SetSampleTime(llTimeStamp);
        if (FAILED(hr)) {
          SafeRelease(&pSample);
          continue; // Skip this sample but continue
        }

        hr = pSample->GetSampleDuration(&llDuration);
        if (FAILED(hr)) {
          llDuration = 0; // Use default duration
          hr = S_OK;
        }

        hr = m_pTransform->ProcessInput(0, pSample, 0);
        if (FAILED(hr)) {
          SafeRelease(&pSample);
          continue; // Skip this sample but continue
        }

        auto mftResult = S_OK;
        while (mftResult == S_OK && isCapturing) {
          // Validate reusable buffer before use
          if (!m_pReusableBuffer || !m_pReusableOutSample) {
            mftResult = E_POINTER;
            break;
          }

          // Reset buffer length instead of removing/adding buffers
          if (m_pReusableBuffer) {
            m_pReusableBuffer->SetCurrentLength(0);
          }

          outputDataBuffer.dwStreamID = 0;
          outputDataBuffer.dwStatus = 0;
          outputDataBuffer.pEvents = NULL;
          outputDataBuffer.pSample = m_pReusableOutSample;

          mftResult = m_pTransform->ProcessOutput(0, 1, &outputDataBuffer, &processOutputStatus);
          // if (FAILED(hr)) break;  // Handle error

          if (mftResult == S_OK) {
            // Validate sample before processing
            if (!outputDataBuffer.pSample) {
              mftResult = E_POINTER;
              break;
            }

            hr = outputDataBuffer.pSample->SetSampleTime(llTimeStamp);
            if (FAILED(hr)) {
              // Don't break on timestamp errors - continue processing
              hr = S_OK;
            }

            hr = outputDataBuffer.pSample->SetSampleDuration(llDuration);
            if (FAILED(hr)) {
              // Don't break on duration errors - continue processing
              hr = S_OK;
            }

            IMFMediaBuffer* buf = nullptr;
            hr = outputDataBuffer.pSample->ConvertToContiguousBuffer(&buf);
            if (FAILED(hr) || !buf) {
              if (buf) SafeRelease(&buf);
              continue; // Skip this frame but continue
            }

            if (isCapturing && buf) {
              BYTE* pData = nullptr;
              DWORD cbMaxLength = 0;
              DWORD cbCurrentLength = 0;

              hr = buf->Lock(&pData, &cbMaxLength, &cbCurrentLength);
              if (SUCCEEDED(hr) && pData && cbCurrentLength >= 4) {
                // Validate data size
                const DWORD expectedSize = width * height * 4;
                if (cbCurrentLength >= expectedSize) {
                  // Ultra-fast BGRA to RGBA conversion using optimized bit operations
                  const DWORD pixelCount = cbCurrentLength >> 2; // Divide by 4 (faster than / 4)
                  DWORD* pixels = reinterpret_cast<DWORD*>(pData);

                  // Constants for bit manipulation
                  constexpr DWORD ALPHA_MASK = 0xFF000000;  // Alpha = 255
                  constexpr DWORD GREEN_MASK = 0x0000FF00;  // Green unchanged

                  // Optimized loop with better cache efficiency and bounds checking
                  const DWORD maxExpectedPixels = expectedSize >> 2;
                  const DWORD safePixelCount = (pixelCount < maxExpectedPixels) ? pixelCount : maxExpectedPixels;
                  const DWORD remainder = safePixelCount & 7; // safePixelCount % 8
                  const DWORD vectorPixels = safePixelCount - remainder;

                  // Main vectorized loop - 8 pixels per iteration
                  for (DWORD i = 0; i < vectorPixels; i += 8) {
                    // Unroll 8 pixels for maximum throughput
                    DWORD p0 = pixels[i];     DWORD p1 = pixels[i+1];
                    DWORD p2 = pixels[i+2];   DWORD p3 = pixels[i+3];
                    DWORD p4 = pixels[i+4];   DWORD p5 = pixels[i+5];
                    DWORD p6 = pixels[i+6];   DWORD p7 = pixels[i+7];

                    // BGRA -> RGBA: Swap R&B, keep G, set A=255
                    pixels[i]   = ALPHA_MASK | (p0 & GREEN_MASK) | ((p0 & 0xFF) << 16) | ((p0 >> 16) & 0xFF);
                    pixels[i+1] = ALPHA_MASK | (p1 & GREEN_MASK) | ((p1 & 0xFF) << 16) | ((p1 >> 16) & 0xFF);
                    pixels[i+2] = ALPHA_MASK | (p2 & GREEN_MASK) | ((p2 & 0xFF) << 16) | ((p2 >> 16) & 0xFF);
                    pixels[i+3] = ALPHA_MASK | (p3 & GREEN_MASK) | ((p3 & 0xFF) << 16) | ((p3 >> 16) & 0xFF);
                    pixels[i+4] = ALPHA_MASK | (p4 & GREEN_MASK) | ((p4 & 0xFF) << 16) | ((p4 >> 16) & 0xFF);
                    pixels[i+5] = ALPHA_MASK | (p5 & GREEN_MASK) | ((p5 & 0xFF) << 16) | ((p5 >> 16) & 0xFF);
                    pixels[i+6] = ALPHA_MASK | (p6 & GREEN_MASK) | ((p6 & 0xFF) << 16) | ((p6 >> 16) & 0xFF);
                    pixels[i+7] = ALPHA_MASK | (p7 & GREEN_MASK) | ((p7 & 0xFF) << 16) | ((p7 >> 16) & 0xFF);
                  }

                  // Handle remaining pixels (0-7 pixels)
                  for (DWORD i = vectorPixels; i < safePixelCount; ++i) {
                    const DWORD pixel = pixels[i];
                    pixels[i] = ALPHA_MASK | (pixel & GREEN_MASK) |
                               ((pixel & 0xFF) << 16) | ((pixel >> 16) & 0xFF);
                  }
                }

                hr = buf->Unlock();

                if (FAILED(hr)) {
                  SafeRelease(&buf);
                  continue; // Skip this frame but continue
                }
              } else {
                // Buffer lock failed or invalid data - skip this frame
                SafeRelease(&buf);
                continue;
              }

              // Call the callback with error handling
              if (buf && isCapturing) {
                HRESULT callbackHr = S_OK;
                try {
                  callbackHr = callback(buf);
                } catch (...) {
                  callbackHr = E_FAIL;
                }

                // Always release the buffer
                buf->Release();

                // Handle callback errors gracefully
                if (FAILED(callbackHr)) {
                  consecutiveErrors++;
                  if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
                    break; // Too many callback errors - exit
                  }
                  continue; // Skip but continue processing
                }
              } else if (buf) {
                buf->Release();
              }
            } else if (buf) {
              buf->Release();
            }
          }

          // Clean up current sample
          SafeRelease(&pSample);

          // Check if we need more input or if there was an error
          if (mftResult == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            break; // Normal case - need more input
          } else if (FAILED(mftResult)) {
            // Transform error - but don't exit capture loop
            break;
          }
        }
      }

      // Final cleanup of sample if still present
      SafeRelease(&pSample);
    }
  }

  // Efficient cleanup - only release if needed
  if (pSample) {
    pSample->Release();
  }

  return hr;
}

HRESULT CaptureDevice::StopCapture() {
  HRESULT hr = S_OK;

  // Set the flag first to stop the capture loop
  isCapturing = false;

  // Give the capture loop time to exit gracefully
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Only proceed if we have a valid transform
  if (m_pTransform) {
    // Flush any pending data first
    HRESULT flushHr = m_pTransform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL);
    // Don't fail if flush fails - it's not critical

    // Notify end of streaming
    hr = m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, NULL);
    if (FAILED(hr)) {
      // Try to continue with end of stream even if end streaming fails
      hr = S_OK;
    }

    // Notify end of stream
    HRESULT eosHr = m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL);
    if (FAILED(eosHr) && SUCCEEDED(hr)) {
      hr = eosHr;
    }
  }

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

bool CaptureDevice::IsDeviceValid() {
  // Check if we have a device source
  if (!m_pSource) {
    return false;
  }

  try {
    // Try to create a presentation descriptor to test if device is alive
    IMFPresentationDescriptor* pPD = nullptr;
    HRESULT hr = m_pSource->CreatePresentationDescriptor(&pPD);

    if (pPD) {
      pPD->Release();
    }

    // Also check if the reader is still valid
    if (SUCCEEDED(hr) && m_pReader) {
      IMFMediaType* pMediaType = nullptr;
      HRESULT readerHr = m_pReader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &pMediaType);
      if (pMediaType) {
        pMediaType->Release();
      }
      return SUCCEEDED(hr) && SUCCEEDED(readerHr);
    }

    return SUCCEEDED(hr);
  } catch (...) {
    // Any exception means device is not valid
    return false;
  }
}
