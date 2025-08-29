//////////////////////////////////////////////////////////////////////////
//
// capture.h: Manages video capture.
//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
//////////////////////////////////////////////////////////////////////////

#pragma once

#include <windows.h>
#include <Dbt.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <string>
#include <functional>
#include <utility>
#include <vector>
#include <tuple>

template <class T>
inline void SafeRelease(T** ppT) {
  if (ppT && *ppT) {
    (*ppT)->Release();
    *ppT = NULL;
  }
}

const UINT WM_APP_PREVIEW_ERROR = WM_APP + 1;  // wparam = HRESULT

class DeviceList {
  UINT32 m_cDevices;
  IMFActivate** m_ppDevices;

 public:
  DeviceList() : m_ppDevices(NULL), m_cDevices(0) {
  }
  ~DeviceList() {
    Clear();
  }

  UINT32 Count() const { return m_cDevices; }

  void Clear();
  // Find and return the IMFActivate whose friendly name or symbolic link matches
  // the provided identifier (case-insensitive). The returned IMFActivate is AddRef'd.
  HRESULT GetDevice(const WCHAR* identifier, IMFActivate** ppActivate);
  // Returns a vector of (friendlyName, symbolicLink) copied into std::wstring
  HRESULT GetAllDevices(std::vector<std::pair<std::wstring, std::wstring>>& outDevices);
};

struct EncodingParameters {
  GUID subtype;
  UINT32 bitrate;
};

class CCapture : public IMFSourceReaderCallback {
 public:
  static HRESULT CreateInstance(
      HWND hwnd,
      CCapture** ppPlayer);

  // IUnknown methods
  STDMETHODIMP QueryInterface(REFIID iid, void** ppv);
  STDMETHODIMP_(ULONG)
  AddRef();
  STDMETHODIMP_(ULONG)
  Release();

  // IMFSourceReaderCallback methods
  STDMETHODIMP OnReadSample(
      HRESULT hrStatus,
      DWORD dwStreamIndex,
      DWORD dwStreamFlags,
      LONGLONG llTimestamp,
      IMFSample* pSample);

  STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*) {
    return S_OK;
  }

  STDMETHODIMP OnFlush(DWORD) {
    return S_OK;
  }

  HRESULT StartCapture(IMFActivate* pActivate, const EncodingParameters& param);
  HRESULT EndCaptureSession();
  BOOL IsCapturing();
  HRESULT CheckDeviceLost(DEV_BROADCAST_HDR* pHdr, BOOL* pbDeviceLost);
  // Initialize internal reader from an IMFActivate without starting capture
  HRESULT InitFromActivate(IMFActivate* pActivate);
  // Enumerate supported native media types from the active source reader (if any).
  HRESULT GetSupportedFormats(std::vector<std::tuple<UINT32, UINT32, double>>& outFormats);
  // Enumerate native media types including subtype GUID for richer info
  HRESULT GetSupportedNativeTypes(std::vector<std::tuple<GUID, UINT32, UINT32, double>>& outTypes);
  // Set the desired native media type on the source reader by explicit native subtype GUID
  HRESULT SetFormat(const GUID& subtype, UINT32 width, UINT32 height, double frameRate);
  // Get current dimensions from the source reader (width, height, frameRate)
  HRESULT GetCurrentDimensions(UINT32* pWidth, UINT32* pHeight, double* pFrameRate);
  // Provide a callback to receive raw frame buffers (moved into the callback)
  void SetFrameCallback(std::function<void(std::vector<uint8_t>&&)> cb) { m_frameCallback = std::move(cb); }
  // Return last enumerated supported formats (stored internally)
  // Deprecated: removed internal cached supported formats
  // (EnumerateFormatsFromActivate removed; CCapture now supports InitFromActivate and GetSupportedFormats)
  // Release all claimed device resources and reset state
  HRESULT ReleaseDevice();
  HRESULT EndCaptureInternal();

 protected:
  enum State {
    State_NotReady = 0,
    State_Ready,
    State_Capturing,
  };

  // Constructor is private. Use static CreateInstance method to instantiate.
  CCapture(HWND hwnd);

  // Destructor is private. Caller should call Release.
  virtual ~CCapture();

  void NotifyError(HRESULT hr) { PostMessage(m_hwndEvent, WM_APP_PREVIEW_ERROR, (WPARAM)hr, 0L); }

  HRESULT OpenMediaSource(IMFMediaSource* pSource);
  HRESULT ConfigureCapture(const EncodingParameters& param);

  long m_nRefCount;  // Reference count.
  CRITICAL_SECTION m_critsec;

  HWND m_hwndEvent;  // Application window to receive events.

  IMFSourceReader* m_pReader;

  BOOL m_bFirstSample;
  LONGLONG m_llBaseTime;

  WCHAR* m_pwszSymbolicLink;
  // (removed) cache of last enumerated formats
  // Frame callback used when delivering frames to the embedding (JS)
  std::function<void(std::vector<uint8_t>&&)> m_frameCallback;
  // Reusable RGBA buffer for frame conversion
  std::vector<uint8_t> m_rgbaBuffer;
};
