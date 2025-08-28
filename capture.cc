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
#include <cstring>

#include "capture.h"
#include "convert.h"

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
    if (m_frameCallback) {
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
              // Optimized NV12 -> RGBA conversion. Process two pixels at a time
              size_t expected = static_cast<size_t>(width) * height * 3 / 2;
              if (curLen >= expected) {
                out.resize(static_cast<size_t>(width) * height * 4);
                const uint8_t* yPlane = pData;
                const uint8_t* uvPlane = pData + (width * height);
                uint8_t* dst = out.data();

                auto clamp = [](int v) -> uint8_t { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); };

                for (UINT32 y = 0; y < height; ++y) {
                  const uint8_t* yRow = yPlane + (size_t)y * width;
                  const uint8_t* uvRow = uvPlane + (size_t)(y / 2) * width;

                  UINT32 x = 0;
                  for (; x + 1 < width; x += 2) {
                    int Y0 = yRow[x];
                    int Y1 = yRow[x + 1];
                    int uvIndex = (int)(x & ~1);
                    int U = uvRow[uvIndex];
                    int V = uvRow[uvIndex + 1];

                    int C0 = Y0 - 16;
                    int C1 = Y1 - 16;
                    int D = U - 128;
                    int E = V - 128;

                    int R0 = (298 * C0 + 409 * E + 128) >> 8;
                    int G0 = (298 * C0 - 100 * D - 208 * E + 128) >> 8;
                    int B0 = (298 * C0 + 516 * D + 128) >> 8;

                    int R1 = (298 * C1 + 409 * E + 128) >> 8;
                    int G1 = (298 * C1 - 100 * D - 208 * E + 128) >> 8;
                    int B1 = (298 * C1 + 516 * D + 128) >> 8;

                    *dst++ = clamp(R0);
                    *dst++ = clamp(G0);
                    *dst++ = clamp(B0);
                    *dst++ = 255;

                    *dst++ = clamp(R1);
                    *dst++ = clamp(G1);
                    *dst++ = clamp(B1);
                    *dst++ = 255;
                  }

                  // handle odd pixel at end of row
                  if (x < width) {
                    int Y0 = yRow[x];
                    int uvIndex = (int)(x & ~1);
                    int U = uvRow[uvIndex];
                    int V = uvRow[uvIndex + 1];

                    int C0 = Y0 - 16;
                    int D = U - 128;
                    int E = V - 128;

                    int R0 = (298 * C0 + 409 * E + 128) >> 8;
                    int G0 = (298 * C0 - 100 * D - 208 * E + 128) >> 8;
                    int B0 = (298 * C0 + 516 * D + 128) >> 8;

                    *dst++ = clamp(R0);
                    *dst++ = clamp(G0);
                    *dst++ = clamp(B0);
                    *dst++ = 255;
                  }
                }
              }
            } else if (subtype == MFVideoFormat_RGB24) {
              // RGB24 (BGR24) -> RGBA using shared conversion helper (may use SIMD)
              size_t pixels = static_cast<size_t>(width) * height;
              out.resize(pixels * 4);
              simd_rgb24_to_rgba(reinterpret_cast<const uint8_t*>(pData), out.data(), pixels);
            } else if (subtype == MFVideoFormat_RGB32) {
              // Use shared conversion helpers: simdRgb will pick fastest available
              size_t pixels = curLen / 4;
              out.resize(pixels * 4);
              simd_rgb32_to_rgba(reinterpret_cast<const uint8_t*>(pData), out.data(), pixels);
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

  // Entry: start capture

  // If we don't already have a source reader, create the media source
  // and open it. InitFromActivate may have already created m_pReader
  // so avoid re-activating/opening which can cause the media source to
  // report that an event generator already has a listener.
  if (m_pReader == NULL) {
    // Create the media source for the device.
    hr = pActivate->ActivateObject(
        __uuidof(IMFMediaSource),
        (void**)&pSource);
    // ActivateObject result in hr

    // Get the symbolic link. This is needed to handle device-
    // loss notifications. (See CheckDeviceLost.)
    if (SUCCEEDED(hr)) {
      hr = pActivate->GetAllocatedString(
          MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
          &m_pwszSymbolicLink,
          NULL);
      // GetAllocatedString result in hr
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
    // operate in callback-only mode; no sink writer
  }

  // Set up the encoding parameters. Only configure the sink writer when
  // we actually have a writer. When no writer is present (frame-callback
  // mode), request RGB32 output from the source reader so the sample
  // buffers are uncompressed and usable by the embedding (JS).
  if (SUCCEEDED(hr)) {
    // Operate in callback-only mode; prefer RGB32 output from source reader
    HRESULT hrCfg = ConfigureSourceReader(m_pReader);
    if (FAILED(hrCfg)) {
      // Non-fatal: continue with whatever format the reader provides.
      hr = S_OK;
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

HRESULT CCapture::EndCaptureSession()
{
    EnterCriticalSection(&m_critsec);

    HRESULT hr = S_OK;

    SafeRelease(&m_pReader);

    LeaveCriticalSection(&m_critsec);

    return hr;
}


BOOL CCapture::IsCapturing() {
  EnterCriticalSection(&m_critsec);

  // Consider us capturing if we have a writer OR a registered frame callback.
  BOOL bIsCapturing = (m_frameCallback != nullptr);

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

  // Prefer an already-set current media type (e.g. set via SetDesiredFormat).
  // This ensures SetDesiredFormat takes effect instead of being overridden
  // by ConfigureSourceReader enumerating native types.
  HRESULT hrCurr = pReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pType);
  if (SUCCEEDED(hrCurr) && pType) {
    hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
    if (FAILED(hr)) {
      goto done;
    }
  } else {
    // Fall back to querying the native media type list if no current type set.
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

// ConfigureEncoder removed: package operates in callback-only mode (no file writer)

//-------------------------------------------------------------------
// ConfigureCapture
//
// Configures the capture session.
//
//-------------------------------------------------------------------

HRESULT CCapture::ConfigureCapture(const EncodingParameters& param) {
  // In callback-only mode we only need to prefer RGB32 via ConfigureSourceReader
  // and leave the source reader configured appropriately.
  HRESULT hr = ConfigureSourceReader(m_pReader);
  return hr;
}

//-------------------------------------------------------------------
// EndCaptureInternal
//
// Stops capture.
//-------------------------------------------------------------------

HRESULT CCapture::EndCaptureInternal()
{
    HRESULT hr = S_OK;

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

// Enumerate native media types including the subtype GUID so callers can
// inspect whether the device supports RGB32, YUV, MJPEG, etc.
HRESULT CCapture::GetSupportedNativeTypes(std::vector<std::tuple<GUID, UINT32, UINT32, double>>& outTypes) {
  outTypes.clear();
  if (m_pReader == NULL) return E_FAIL;

  DWORD index = 0;
  while (true) {
    IMFMediaType* pType = NULL;
    HRESULT hr = m_pReader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &pType);
    if (FAILED(hr)) break;

    GUID subtype = {0};
    UINT32 width = 0, height = 0;
    UINT32 num = 0, denom = 0;

    pType->GetGUID(MF_MT_SUBTYPE, &subtype);
    MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);
    MFGetAttributeRatio(pType, MF_MT_FRAME_RATE, &num, &denom);
    double frameRate = 0.0;
    if (denom != 0) frameRate = static_cast<double>(num) / static_cast<double>(denom);

    outTypes.emplace_back(subtype, width, height, frameRate);

    SafeRelease(&pType);
    ++index;
  }

  // Sort + unique similar to GetSupportedFormats
  std::sort(outTypes.begin(), outTypes.end(), [](const auto& a, const auto& b) {
    if (std::get<1>(a) != std::get<1>(b)) return std::get<1>(a) < std::get<1>(b);
    if (std::get<2>(a) != std::get<2>(b)) return std::get<2>(a) < std::get<2>(b);
    if (std::get<3>(a) != std::get<3>(b)) return std::get<3>(a) < std::get<3>(b);
    // fallback: compare GUID bytes
    const GUID& ga = std::get<0>(a);
    const GUID& gb = std::get<0>(b);
    return memcmp(&ga, &gb, sizeof(GUID)) < 0;
  });

  const double eps = 1e-6;
  auto last = std::unique(outTypes.begin(), outTypes.end(), [eps](const auto& a, const auto& b) {
    return std::get<1>(a) == std::get<1>(b) && std::get<2>(a) == std::get<2>(b) && std::fabs(std::get<3>(a) - std::get<3>(b)) < eps && memcmp(&std::get<0>(a), &std::get<0>(b), sizeof(GUID)) == 0;
  });
  outTypes.erase(last, outTypes.end());

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
  // No writer member present
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
