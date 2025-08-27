//////////////////////////////////////////////////////////////////////////
//
// capture.cpp: Manages video capture.
//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
//////////////////////////////////////////////////////////////////////////

#include <windows.h>
#include <Dbt.h>
#include <Wmcodecdsp.h>
#include <assert.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shlwapi.h>
#include <string>
#include <algorithm>
#include <cmath>
#include <new>

#include "capture.h"

// Use SDK QISearch implementation (link to SDK libs via binding.gyp)

HRESULT CopyAttribute(IMFAttributes* pSrc, IMFAttributes* pDest, const GUID& key);
// Forward declaration for ConfigureSourceReader so StartCapture can call it.
HRESULT ConfigureSourceReader(IMFSourceReader* pReader);
// Forward declaration for helper used to deliver samples to frame callback
static HRESULT DeliverSampleToCallback(IMFSample* pSample, std::function<void(std::vector<uint8_t>&&)>& callback);

void DeviceList::Clear() {
  for (UINT32 i = 0; i < m_cDevices; i++) {
    SafeRelease(&m_ppDevices[i]);
  }
  CoTaskMemFree(m_ppDevices);
  m_ppDevices = NULL;

  m_cDevices = 0;
}

// Note: EnumerateDevices was removed from the public API. GetAllDevices
// performs enumeration internally to simplify the DeviceList interface.

// ... index-based GetDevice removed; use GetDevice(identifier, ppActivate) instead.
HRESULT DeviceList::GetDevice(const WCHAR* identifier, IMFActivate** ppActivate) {
  if (!identifier || !ppActivate) return E_POINTER;

  *ppActivate = nullptr;

  // Enumerate devices on demand
  if (m_cDevices == 0) {
    IMFAttributes* pAttributes = nullptr;
    HRESULT hr = MFCreateAttributes(&pAttributes, 1);
    if (SUCCEEDED(hr)) hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (SUCCEEDED(hr)) hr = MFEnumDeviceSources(pAttributes, &m_ppDevices, &m_cDevices);
    SafeRelease(&pAttributes);
    if (FAILED(hr)) {
      m_cDevices = 0;
      m_ppDevices = nullptr;
      return hr;
    }
  }

  if (!m_ppDevices || m_cDevices == 0) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

  const UINT32 MAX_DEVICES = 256;
  UINT32 limit = (m_cDevices < MAX_DEVICES) ? m_cDevices : MAX_DEVICES;

  for (UINT32 i = 0; i < limit; ++i) {
    WCHAR* pFriendly = nullptr;
    WCHAR* pSymbolic = nullptr;

    HRESULT hr1 = m_ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &pFriendly, nullptr);
    HRESULT hr2 = m_ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &pSymbolic, nullptr);

    bool match = (SUCCEEDED(hr1) && pFriendly && _wcsicmp(pFriendly, identifier) == 0) || (SUCCEEDED(hr2) && pSymbolic && _wcsicmp(pSymbolic, identifier) == 0);

    if (pFriendly) {
      CoTaskMemFree(pFriendly);
      pFriendly = nullptr;
    }
    if (pSymbolic) {
      CoTaskMemFree(pSymbolic);
      pSymbolic = nullptr;
    }

    if (match) {
      *ppActivate = m_ppDevices[i];
      (*ppActivate)->AddRef();
      return S_OK;
    }
  }

  return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

HRESULT DeviceList::GetAllDevices(std::vector<std::pair<std::wstring, std::wstring>>& outDevices) {
  outDevices.clear();

  // Fresh enumeration each call keeps caller code simple.
  Clear();

  IMFAttributes* pAttributes = nullptr;
  HRESULT hr = MFCreateAttributes(&pAttributes, 1);
  if (SUCCEEDED(hr)) {
    hr = pAttributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  }

  if (SUCCEEDED(hr)) {
    hr = MFEnumDeviceSources(pAttributes, &m_ppDevices, &m_cDevices);
  }

  SafeRelease(&pAttributes);

  if (FAILED(hr)) {
    // keep internal state consistent on failure
    m_cDevices = 0;
    m_ppDevices = NULL;
    return hr;
  }

  if (m_cDevices == 0 || m_ppDevices == nullptr) {
    return S_OK;  // no devices
  }

  const UINT32 MAX_DEVICES = 256;  // safety cap
  UINT32 toProcess = (m_cDevices < MAX_DEVICES) ? m_cDevices : MAX_DEVICES;

  // Minimal loop: get strings, copy to std::wstring, free, push result.
  for (UINT32 i = 0; i < toProcess; ++i) {
    WCHAR* pFriendly = nullptr;
    WCHAR* pSymbolic = nullptr;

    HRESULT hr1 = m_ppDevices[i]->GetAllocatedString(
        MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &pFriendly, nullptr);
    HRESULT hr2 = m_ppDevices[i]->GetAllocatedString(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &pSymbolic, nullptr);

    std::wstring friendly;
    std::wstring symbolic;

    if (SUCCEEDED(hr1) && pFriendly) {
      friendly.assign(pFriendly);
    }
    if (pFriendly) CoTaskMemFree(pFriendly);

    if (SUCCEEDED(hr2) && pSymbolic) {
      symbolic.assign(pSymbolic);
    }
    if (pSymbolic) CoTaskMemFree(pSymbolic);

    outDevices.emplace_back(std::move(friendly), std::move(symbolic));
  }

  return S_OK;
}
//-------------------------------------------------------------------
//  CreateInstance
//
//  Static class method to create the CCapture object.
//-------------------------------------------------------------------

HRESULT CCapture::CreateInstance(
    HWND hwnd,            // Handle to the window to receive events
    CCapture** ppCapture  // Receives a pointer to the CCapture object.
) {
  if (ppCapture == NULL) {
    return E_POINTER;
  }

  CCapture* pCapture = new (std::nothrow) CCapture(hwnd);

  if (pCapture == NULL) {
    return E_OUTOFMEMORY;
  }

  // The CCapture constructor sets the ref count to 1.
  *ppCapture = pCapture;

  return S_OK;
}

//-------------------------------------------------------------------
//  constructor
//-------------------------------------------------------------------

CCapture::CCapture(HWND hwnd) : m_pReader(NULL),
                                m_pWriter(NULL),
                                m_hwndEvent(hwnd),
                                m_nRefCount(1),
                                m_bFirstSample(FALSE),
                                m_llBaseTime(0),
                                m_pwszSymbolicLink(NULL) {
  InitializeCriticalSection(&m_critsec);
}

//-------------------------------------------------------------------
//  destructor
//-------------------------------------------------------------------

CCapture::~CCapture() {
  assert(m_pReader == NULL);
  assert(m_pWriter == NULL);
  DeleteCriticalSection(&m_critsec);
}

/////////////// IUnknown methods ///////////////

//-------------------------------------------------------------------
//  AddRef
//-------------------------------------------------------------------

ULONG CCapture::AddRef() {
  return InterlockedIncrement(&m_nRefCount);
}

//-------------------------------------------------------------------
//  Release
//-------------------------------------------------------------------

ULONG CCapture::Release() {
  ULONG uCount = InterlockedDecrement(&m_nRefCount);
  if (uCount == 0) {
    delete this;
  }
  return uCount;
}

//-------------------------------------------------------------------
//  QueryInterface
//-------------------------------------------------------------------

HRESULT CCapture::QueryInterface(REFIID riid, void** ppv) {
  static const QITAB qit[] =
      {
          QITABENT(CCapture, IMFSourceReaderCallback),
          {0},
      };
  return QISearch(this, qit, riid, ppv);
}

/////////////// IMFSourceReaderCallback methods ///////////////

//-------------------------------------------------------------------
// OnReadSample
//
// Called when the IMFMediaSource::ReadSample method completes.
//-------------------------------------------------------------------

HRESULT CCapture::OnReadSample(
    HRESULT hrStatus,
    DWORD /*dwStreamIndex*/,
    DWORD /*dwStreamFlags*/,
    LONGLONG llTimeStamp,
    IMFSample* pSample  // Can be NULL
) {
  EnterCriticalSection(&m_critsec);

  if (!IsCapturing()) {
    LeaveCriticalSection(&m_critsec);
    return S_OK;
  }

  HRESULT hr = S_OK;

  if (FAILED(hrStatus)) {
    hr = hrStatus;
    goto done;
  }

  if (pSample) {
    if (m_bFirstSample) {
      m_llBaseTime = llTimeStamp;
      m_bFirstSample = FALSE;
    }

    // rebase the time stamp
    llTimeStamp -= m_llBaseTime;

    hr = pSample->SetSampleTime(llTimeStamp);

    if (FAILED(hr)) {
      goto done;
    }
    if (m_pWriter) {
      hr = m_pWriter->WriteSample(0, pSample);
      if (FAILED(hr)) {
        goto done;
      }
    } else if (m_frameCallback) {
      // Deliver sample to the registered frame callback with format conversion
      IMFMediaType* pType = NULL;
      GUID subtype = {0};
      UINT32 width = 0, height = 0;

      if (SUCCEEDED(m_pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pType)) && pType) {
        pType->GetGUID(MF_MT_SUBTYPE, &subtype);
        MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);
      }

      IMFMediaBuffer* pBuffer = NULL;
      HRESULT hrBuf = pSample->ConvertToContiguousBuffer(&pBuffer);
      if (SUCCEEDED(hrBuf) && pBuffer) {
        BYTE* pData = NULL;
        DWORD maxLen = 0, curLen = 0;
        hrBuf = pBuffer->Lock(&pData, &maxLen, &curLen);
        if (SUCCEEDED(hrBuf) && pData && curLen > 0) {
          try {
            std::vector<uint8_t> out;

            if (subtype == MFVideoFormat_NV12) {
              // NV12 -> RGBA
              size_t expected = static_cast<size_t>(width) * height * 3 / 2;
              if (curLen >= expected) {
                out.resize(static_cast<size_t>(width) * height * 4);
                const uint8_t* yPlane = pData;
                const uint8_t* uvPlane = pData + (width * height);

                for (UINT32 y = 0; y < height; ++y) {
                  for (UINT32 x = 0; x < width; ++x) {
                    size_t yIndex = (size_t)y * width + x;
                    size_t uvIndex = (size_t)(y / 2) * width + (x & ~1);
                    int Y = yPlane[yIndex];
                    int U = uvPlane[uvIndex];
                    int V = uvPlane[uvIndex + 1];

                    int C = Y - 16;
                    int D = U - 128;
                    int E = V - 128;

                    int R = (298 * C + 409 * E + 128) >> 8;
                    int G = (298 * C - 100 * D - 208 * E + 128) >> 8;
                    int B = (298 * C + 516 * D + 128) >> 8;

                    if (R < 0)
                      R = 0;
                    else if (R > 255)
                      R = 255;
                    if (G < 0)
                      G = 0;
                    else if (G > 255)
                      G = 255;
                    if (B < 0)
                      B = 0;
                    else if (B > 255)
                      B = 255;

                    size_t outIndex = (size_t)y * width * 4 + x * 4;
                    out[outIndex + 0] = static_cast<uint8_t>(R);
                    out[outIndex + 1] = static_cast<uint8_t>(G);
                    out[outIndex + 2] = static_cast<uint8_t>(B);
                    out[outIndex + 3] = 255;
                  }
                }
              }
            } else if (subtype == MFVideoFormat_YUY2) {
              // YUY2 packed (4 bytes per 2 pixels)
              size_t expected = static_cast<size_t>(width) * height * 2;
              if (curLen >= expected) {
                out.reserve(static_cast<size_t>(width) * height * 4);
                const uint8_t* p = pData;
                size_t end = expected;
                for (size_t i = 0; i + 3 < end; i += 4) {
                  int y0 = p[i + 0];
                  int u = p[i + 1];
                  int y1 = p[i + 2];
                  int v = p[i + 3];

                  int c0 = y0 - 16;
                  int c1 = y1 - 16;
                  int d = u - 128;
                  int e = v - 128;

                  int r0 = (298 * c0 + 409 * e + 128) >> 8;
                  int g0 = (298 * c0 - 100 * d - 208 * e + 128) >> 8;
                  int b0 = (298 * c0 + 516 * d + 128) >> 8;

                  int r1 = (298 * c1 + 409 * e + 128) >> 8;
                  int g1 = (298 * c1 - 100 * d - 208 * e + 128) >> 8;
                  int b1 = (298 * c1 + 516 * d + 128) >> 8;

                  // clamp and push
                  out.push_back(static_cast<uint8_t>(r0 < 0 ? 0 : (r0 > 255 ? 255 : r0)));
                  out.push_back(static_cast<uint8_t>(g0 < 0 ? 0 : (g0 > 255 ? 255 : g0)));
                  out.push_back(static_cast<uint8_t>(b0 < 0 ? 0 : (b0 > 255 ? 255 : b0)));
                  out.push_back(255);

                  out.push_back(static_cast<uint8_t>(r1 < 0 ? 0 : (r1 > 255 ? 255 : r1)));
                  out.push_back(static_cast<uint8_t>(g1 < 0 ? 0 : (g1 > 255 ? 255 : g1)));
                  out.push_back(static_cast<uint8_t>(b1 < 0 ? 0 : (b1 > 255 ? 255 : b1)));
                  out.push_back(255);
                }
              }
            } else if (subtype == MFVideoFormat_RGB32) {
              // Already RGB32: copy to out
              out.assign(pData, pData + curLen);
            } else if (subtype == MFVideoFormat_RGB24) {
              // RGB24: expand to RGBA
              out.resize(static_cast<size_t>(width) * height * 4);
              const uint8_t* src = pData;
              for (UINT32 y = 0; y < height; ++y) {
                for (UINT32 x = 0; x < width; ++x) {
                  size_t srcIdx = (size_t)(y * width + x) * 3;
                  size_t dstIdx = (size_t)(y * width + x) * 4;
                  out[dstIdx + 0] = src[srcIdx + 0];
                  out[dstIdx + 1] = src[srcIdx + 1];
                  out[dstIdx + 2] = src[srcIdx + 2];
                  out[dstIdx + 3] = 255;
                }
              }
            } else {
              // Unknown subtype: pass raw buffer
              out.assign(pData, pData + curLen);
            }

            if (!out.empty()) {
              m_frameCallback(std::move(out));
            }

          } catch (...) {
            // ignore conversion errors
          }
        }
        pBuffer->Unlock();
      }
      SafeRelease(&pBuffer);
      SafeRelease(&pType);
    }
  }

  // Read another sample.
  hr = m_pReader->ReadSample(
      (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
      0,
      NULL,  // actual
      NULL,  // flags
      NULL,  // timestamp
      NULL   // sample
  );

done:
  if (FAILED(hr)) {
    NotifyError(hr);
  }

  LeaveCriticalSection(&m_critsec);
  return hr;
}

//-------------------------------------------------------------------
// OpenMediaSource
//
// Set up preview for a specified media source.
//-------------------------------------------------------------------

HRESULT CCapture::OpenMediaSource(IMFMediaSource* pSource) {
  HRESULT hr = S_OK;

  IMFAttributes* pAttributes = NULL;

  hr = MFCreateAttributes(&pAttributes, 2);

  if (SUCCEEDED(hr)) {
    hr = pAttributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, this);
  }

  if (SUCCEEDED(hr)) {
    hr = MFCreateSourceReaderFromMediaSource(
        pSource,
        pAttributes,
        &m_pReader);
  }

  SafeRelease(&pAttributes);
  return hr;
}

//-------------------------------------------------------------------
// StartCapture
//
// Start capturing.
//-------------------------------------------------------------------

HRESULT CCapture::StartCapture(
    IMFActivate* pActivate,
    const EncodingParameters& param) {
  HRESULT hr = S_OK;

  IMFMediaSource* pSource = NULL;

  EnterCriticalSection(&m_critsec);

  // If we don't already have a source reader, create the media source
  // and open it. InitFromActivate may have already created m_pReader
  // so avoid re-activating/opening which can cause the media source to
  // report that an event generator already has a listener.
  if (m_pReader == NULL) {
    // Create the media source for the device.
    hr = pActivate->ActivateObject(
        __uuidof(IMFMediaSource),
        (void**)&pSource);

    // Get the symbolic link. This is needed to handle device-
    // loss notifications. (See CheckDeviceLost.)
    if (SUCCEEDED(hr)) {
      hr = pActivate->GetAllocatedString(
          MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
          &m_pwszSymbolicLink,
          NULL);
    }

    if (SUCCEEDED(hr)) {
      hr = OpenMediaSource(pSource);
    }
  } else {
    // We already initialized the source reader in InitFromActivate.
    // Ensure we have a symbolic link recorded for device loss handling.
    if (m_pwszSymbolicLink == NULL) {
      HRESULT hrSym = pActivate->GetAllocatedString(
          MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
          &m_pwszSymbolicLink,
          NULL);
      if (FAILED(hrSym)) {
        // Non-fatal: continue with existing reader if symbolic link cannot be obtained.
      }
    }
  }

  // We don't write to files in this build; operate in callback-only mode.
  if (SUCCEEDED(hr)) {
    m_pWriter = NULL;
  }

  // Set up the encoding parameters. Only configure the sink writer when
  // we actually have a writer. When no writer is present (frame-callback
  // mode), request RGB32 output from the source reader so the sample
  // buffers are uncompressed and usable by the embedding (JS).
  if (SUCCEEDED(hr)) {
    if (m_pWriter) {
      hr = ConfigureCapture(param);
    } else {
      // Prefer RGB32 by asking ConfigureSourceReader to set the reader's
      // output type. ConfigureSourceReader will try RGB32 first and fall
      // back to other supported formats.
      HRESULT hrCfg = ConfigureSourceReader(m_pReader);
      if (FAILED(hrCfg)) {
        // Non-fatal: continue with whatever format the reader provides.
        hr = S_OK;
      }
    }
  }

  if (SUCCEEDED(hr)) {
    m_bFirstSample = TRUE;
    m_llBaseTime = 0;

    // Request the first video frame.

    hr = m_pReader->ReadSample(
        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        0,
        NULL,
        NULL,
        NULL,
        NULL);
  }

  SafeRelease(&pSource);
  LeaveCriticalSection(&m_critsec);
  return hr;
}

// Helper to extract contiguous buffer from an IMFSample and call the frame callback
static HRESULT DeliverSampleToCallback(IMFSample* pSample, std::function<void(std::vector<uint8_t>&&)>& callback) {
  if (!pSample || !callback) return E_POINTER;

  IMFMediaBuffer* pBuffer = NULL;
  HRESULT hr = pSample->ConvertToContiguousBuffer(&pBuffer);
  if (FAILED(hr)) return hr;

  BYTE* pData = NULL;
  DWORD maxLen = 0, curLen = 0;
  hr = pBuffer->Lock(&pData, &maxLen, &curLen);
  if (SUCCEEDED(hr)) {
    std::vector<uint8_t> vec(pData, pData + curLen);
    callback(std::move(vec));
    pBuffer->Unlock();
  }

  SafeRelease(&pBuffer);
  return hr;
}

// Initialize the CCapture instance from an IMFActivate without starting capture.
// This creates the media source and source reader so GetSupportedFormats can
// enumerate native types without beginning an actual capture session.
HRESULT CCapture::InitFromActivate(IMFActivate* pActivate) {
  if (!pActivate) return E_POINTER;
  IMFMediaSource* pSource = NULL;
  HRESULT hr = pActivate->ActivateObject(__uuidof(IMFMediaSource), (void**)&pSource);
  if (SUCCEEDED(hr)) {
    hr = OpenMediaSource(pSource);
  }
  if (pSource) SafeRelease(&pSource);
  return hr;
}

//-------------------------------------------------------------------
// EndCaptureSession
//
// Stop the capture session.
//
// NOTE: This method resets the object's state to State_NotReady.
// To start another capture session, call SetCaptureFile.
//-------------------------------------------------------------------

HRESULT CCapture::EndCaptureSession() {
  EnterCriticalSection(&m_critsec);

  HRESULT hr = S_OK;

  if (m_pWriter) {
    hr = m_pWriter->Finalize();
  }

  SafeRelease(&m_pWriter);
  SafeRelease(&m_pReader);

  LeaveCriticalSection(&m_critsec);

  return hr;
}

BOOL CCapture::IsCapturing() {
  EnterCriticalSection(&m_critsec);

  // Consider us capturing if we have a writer OR a registered frame callback.
  BOOL bIsCapturing = (m_pWriter != NULL) || (m_frameCallback != nullptr);

  LeaveCriticalSection(&m_critsec);

  return bIsCapturing;
}

//-------------------------------------------------------------------
//  CheckDeviceLost
//  Checks whether the video capture device was removed.
//
//  The application calls this method when is receives a
//  WM_DEVICECHANGE message.
//-------------------------------------------------------------------

HRESULT CCapture::CheckDeviceLost(DEV_BROADCAST_HDR* pHdr, BOOL* pbDeviceLost) {
  if (pbDeviceLost == NULL) {
    return E_POINTER;
  }

  EnterCriticalSection(&m_critsec);

  DEV_BROADCAST_DEVICEINTERFACE* pDi = NULL;

  *pbDeviceLost = FALSE;

  if (!IsCapturing()) {
    goto done;
  }
  if (pHdr == NULL) {
    goto done;
  }
  if (pHdr->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE) {
    goto done;
  }

  // Compare the device name with the symbolic link.

  pDi = (DEV_BROADCAST_DEVICEINTERFACE*)pHdr;

  if (m_pwszSymbolicLink) {
#ifdef UNICODE
    if (_wcsicmp(m_pwszSymbolicLink, pDi->dbcc_name) == 0) {
      *pbDeviceLost = TRUE;
    }
#else
    // When UNICODE is not defined, pDi->dbcc_name is ANSI (char[]).
    // Convert to wide string before comparing to m_pwszSymbolicLink.
    WCHAR wszName[MAX_PATH];
    if (MultiByteToWideChar(CP_ACP, 0, pDi->dbcc_name, -1, wszName, MAX_PATH) > 0) {
      if (_wcsicmp(m_pwszSymbolicLink, wszName) == 0) {
        *pbDeviceLost = TRUE;
      }
    }
#endif
  }

done:
  LeaveCriticalSection(&m_critsec);
  return S_OK;
}

/////////////// Private/protected class methods ///////////////

//-------------------------------------------------------------------
//  ConfigureSourceReader
//
//  Sets the media type on the source reader.
//-------------------------------------------------------------------

HRESULT ConfigureSourceReader(IMFSourceReader* pReader) {
  // The list of acceptable types.
  GUID subtypes[] = {
      MFVideoFormat_NV12, MFVideoFormat_YUY2, MFVideoFormat_UYVY, MFVideoFormat_RGB32, MFVideoFormat_RGB24, MFVideoFormat_IYUV};

  HRESULT hr = S_OK;
  BOOL bUseNativeType = FALSE;

  GUID subtype = {0};

  IMFMediaType* pType = NULL;

  // If the source's native format matches any of the formats in
  // the list, prefer the native format.

  // Register the color converter MFT locally so the source reader can
  // use it to convert to RGB32 if available.
  // This is safe to call multiple times.
  MFTRegisterLocalByCLSID(
      __uuidof(CColorConvertDMO),
      MFT_CATEGORY_VIDEO_PROCESSOR,
      L"",
      MFT_ENUM_FLAG_SYNCMFT,
      0,
      NULL,
      0,
      NULL);

  // Note: The camera might support multiple output formats,
  // including a range of frame dimensions. The application could
  // provide a list to the user and have the user select the
  // camera's output format. That is outside the scope of this
  // sample, however.

  hr = pReader->GetNativeMediaType(
      (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
      0,  // Type index
      &pType);

  if (FAILED(hr)) {
    goto done;
  }

  hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);

  if (FAILED(hr)) {
    goto done;
  }

  // First, attempt to set an explicit RGB32 output type copied from the
  // native attributes (frame size/frame rate). This makes RGB32 the
  // preferred output when the source reader can perform the conversion.
  {
    IMFMediaType* pRgb = NULL;
    HRESULT hrRgb = MFCreateMediaType(&pRgb);
    if (SUCCEEDED(hrRgb) && pRgb) {
      hrRgb = pRgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    }
    if (SUCCEEDED(hrRgb)) {
      hrRgb = pRgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    }
    if (SUCCEEDED(hrRgb)) hrRgb = CopyAttribute(pType, pRgb, MF_MT_FRAME_SIZE);
    if (SUCCEEDED(hrRgb)) hrRgb = CopyAttribute(pType, pRgb, MF_MT_FRAME_RATE);
    if (SUCCEEDED(hrRgb)) hrRgb = CopyAttribute(pType, pRgb, MF_MT_PIXEL_ASPECT_RATIO);

    if (SUCCEEDED(hrRgb)) {
      HRESULT hrSet = pReader->SetCurrentMediaType(
          (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
          NULL,
          pRgb);
      if (SUCCEEDED(hrSet)) {
        SafeRelease(&pRgb);
        SafeRelease(&pType);
        return S_OK;
      }
    }
    SafeRelease(&pRgb);
  }

  for (UINT32 i = 0; i < ARRAYSIZE(subtypes); i++) {
    if (subtype == subtypes[i]) {
      hr = pReader->SetCurrentMediaType(
          (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
          NULL,
          pType);

      bUseNativeType = TRUE;
      break;
    }
  }

  if (!bUseNativeType) {
    // None of the native types worked. The camera might offer
    // output a compressed type such as MJPEG or DV.

    // Try adding a decoder.

    for (UINT32 i = 0; i < ARRAYSIZE(subtypes); i++) {
      hr = pType->SetGUID(MF_MT_SUBTYPE, subtypes[i]);

      if (FAILED(hr)) {
        goto done;
      }

      hr = pReader->SetCurrentMediaType(
          (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
          NULL,
          pType);

      if (SUCCEEDED(hr)) {
        break;
      }
    }
  }

done:
  SafeRelease(&pType);
  return hr;
}

HRESULT ConfigureEncoder(
    const EncodingParameters& params,
    IMFMediaType* pType,
    IMFSinkWriter* pWriter,
    DWORD* pdwStreamIndex) {
  HRESULT hr = S_OK;

  IMFMediaType* pType2 = NULL;

  hr = MFCreateMediaType(&pType2);

  if (SUCCEEDED(hr)) {
    hr = pType2->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  }

  if (SUCCEEDED(hr)) {
    hr = pType2->SetGUID(MF_MT_SUBTYPE, params.subtype);
  }

  if (SUCCEEDED(hr)) {
    hr = pType2->SetUINT32(MF_MT_AVG_BITRATE, params.bitrate);
  }

  if (SUCCEEDED(hr)) {
    hr = CopyAttribute(pType, pType2, MF_MT_FRAME_SIZE);
  }

  if (SUCCEEDED(hr)) {
    hr = CopyAttribute(pType, pType2, MF_MT_FRAME_RATE);
  }

  if (SUCCEEDED(hr)) {
    hr = CopyAttribute(pType, pType2, MF_MT_PIXEL_ASPECT_RATIO);
  }

  if (SUCCEEDED(hr)) {
    hr = CopyAttribute(pType, pType2, MF_MT_INTERLACE_MODE);
  }

  if (SUCCEEDED(hr)) {
    hr = pWriter->AddStream(pType2, pdwStreamIndex);
  }

  SafeRelease(&pType2);
  return hr;
}

//-------------------------------------------------------------------
// ConfigureCapture
//
// Configures the capture session.
//
//-------------------------------------------------------------------

HRESULT CCapture::ConfigureCapture(const EncodingParameters& param) {
  HRESULT hr = S_OK;
  DWORD sink_stream = 0;

  IMFMediaType* pType = NULL;

  hr = ConfigureSourceReader(m_pReader);

  if (SUCCEEDED(hr)) {
    hr = m_pReader->GetCurrentMediaType(
        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        &pType);
  }

  if (SUCCEEDED(hr)) {
    hr = ConfigureEncoder(param, pType, m_pWriter, &sink_stream);
  }

  if (SUCCEEDED(hr)) {
    // Register the color converter DSP for this process, in the video
    // processor category. This will enable the sink writer to enumerate
    // the color converter when the sink writer attempts to match the
    // media types.

    hr = MFTRegisterLocalByCLSID(
        __uuidof(CColorConvertDMO),
        MFT_CATEGORY_VIDEO_PROCESSOR,
        L"",
        MFT_ENUM_FLAG_SYNCMFT,
        0,
        NULL,
        0,
        NULL);
  }

  if (SUCCEEDED(hr)) {
    hr = m_pWriter->SetInputMediaType(sink_stream, pType, NULL);
  }

  if (SUCCEEDED(hr)) {
    hr = m_pWriter->BeginWriting();
  }

  SafeRelease(&pType);
  return hr;
}

//-------------------------------------------------------------------
// EndCaptureInternal
//
// Stops capture.
//-------------------------------------------------------------------

HRESULT CCapture::EndCaptureInternal() {
  HRESULT hr = S_OK;

  if (m_pWriter) {
    hr = m_pWriter->Finalize();
  }

  SafeRelease(&m_pWriter);
  SafeRelease(&m_pReader);

  CoTaskMemFree(m_pwszSymbolicLink);
  m_pwszSymbolicLink = NULL;

  return hr;
}

// Enumerate supported native media types from the active source reader.
HRESULT CCapture::GetSupportedFormats(std::vector<std::tuple<UINT32, UINT32, double>>& outFormats) {
  outFormats.clear();

  if (m_pReader == NULL) return E_FAIL;

  std::vector<std::tuple<UINT32, UINT32, double>> temp;

  DWORD index = 0;
  while (true) {
    IMFMediaType* pType = NULL;
    HRESULT hr = m_pReader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &pType);
    if (FAILED(hr)) break;

    UINT32 width = 0, height = 0;
    UINT32 num = 0, denom = 0;

    MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);
    MFGetAttributeRatio(pType, MF_MT_FRAME_RATE, &num, &denom);
    double frameRate = 0.0;
    if (denom != 0) frameRate = static_cast<double>(num) / static_cast<double>(denom);

    temp.emplace_back(width, height, frameRate);

    SafeRelease(&pType);
    ++index;
  }

  // Sort and deduplicate (use small epsilon for floating-point FPS)
  std::sort(temp.begin(), temp.end(), [](const auto& a, const auto& b) {
    if (std::get<0>(a) != std::get<0>(b)) return std::get<0>(a) < std::get<0>(b);
    if (std::get<1>(a) != std::get<1>(b)) return std::get<1>(a) < std::get<1>(b);
    return std::get<2>(a) < std::get<2>(b);
  });

  const double eps = 1e-6;
  auto last = std::unique(temp.begin(), temp.end(), [eps](const auto& a, const auto& b) {
    return std::get<0>(a) == std::get<0>(b) && std::get<1>(a) == std::get<1>(b) && std::fabs(std::get<2>(a) - std::get<2>(b)) < eps;
  });
  temp.erase(last, temp.end());

  outFormats = std::move(temp);
  // store in internal cache as well
  m_lastSupportedFormats = outFormats;
  return S_OK;
}

HRESULT CCapture::SetDesiredFormat(UINT32 width, UINT32 height, double frameRate) {
  if (m_pReader == NULL) return E_FAIL;

  DWORD index = 0;
  HRESULT hr = E_FAIL;
  while (true) {
    IMFMediaType* pType = NULL;
    HRESULT hrType = m_pReader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &pType);
    if (FAILED(hrType)) break;

    UINT32 w = 0, h = 0;
    UINT32 num = 0, denom = 0;
    MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &w, &h);
    MFGetAttributeRatio(pType, MF_MT_FRAME_RATE, &num, &denom);
    double fr = 0.0;
    if (denom != 0) fr = static_cast<double>(num) / static_cast<double>(denom);

    if (w == width && h == height && std::abs(fr - frameRate) < 1e-6) {
      hr = m_pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pType);
      SafeRelease(&pType);
      break;
    }

    SafeRelease(&pType);
    ++index;
  }

  return hr;
}

HRESULT CCapture::GetCurrentDimensions(UINT32* pWidth, UINT32* pHeight, double* pFrameRate) {
  if (pWidth) *pWidth = 0;
  if (pHeight) *pHeight = 0;
  if (pFrameRate) *pFrameRate = 0.0;

  if (m_pReader == NULL) return E_FAIL;

  IMFMediaType* pType = NULL;
  HRESULT hr = m_pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pType);
  if (FAILED(hr) || pType == NULL) {
    SafeRelease(&pType);
    return hr;
  }

  UINT32 w = 0, h = 0;
  UINT32 num = 0, denom = 0;
  MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &w, &h);
  MFGetAttributeRatio(pType, MF_MT_FRAME_RATE, &num, &denom);
  double fr = 0.0;
  if (denom != 0) fr = static_cast<double>(num) / static_cast<double>(denom);

  if (pWidth) *pWidth = w;
  if (pHeight) *pHeight = h;
  if (pFrameRate) *pFrameRate = fr;

  SafeRelease(&pType);
  return S_OK;
}

// ...existing code...
// Add after last CCapture method (e.g., after GetCurrentDimensions)
HRESULT CCapture::ReleaseDevice() {
  EndCaptureSession();
  if (m_pReader) {
    m_pReader->Release();
    m_pReader = nullptr;
  }
  if (m_pWriter) {
    m_pWriter->Release();
    m_pWriter = nullptr;
  }
  if (m_pwszSymbolicLink) {
    CoTaskMemFree(m_pwszSymbolicLink);
    m_pwszSymbolicLink = nullptr;
  }
  m_lastSupportedFormats.clear();
  m_rgbaBuffer.clear();
  m_frameCallback = nullptr;
  m_bFirstSample = TRUE;
  m_llBaseTime = 0;
  return S_OK;
}

// static
// EnumerateFormatsFromActivate removed; CCapture now exposes InitFromActivate + GetSupportedFormats

//-------------------------------------------------------------------
// CopyAttribute
//
// Copy an attribute value from one attribute store to another.
//-------------------------------------------------------------------

HRESULT CopyAttribute(IMFAttributes* pSrc, IMFAttributes* pDest, const GUID& key) {
  PROPVARIANT var;
  PropVariantInit(&var);

  HRESULT hr = S_OK;

  hr = pSrc->GetItem(key, &var);
  if (SUCCEEDED(hr)) {
    hr = pDest->SetItem(key, var);
  }

  PropVariantClear(&var);
  return hr;
}
