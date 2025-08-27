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
#include <utility>
#include <vector>
#include <tuple>

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

  HRESULT StartCapture(IMFActivate* pActivate, const WCHAR* pwszFileName, const EncodingParameters& param);
  HRESULT EndCaptureSession();
  BOOL IsCapturing();
  HRESULT CheckDeviceLost(DEV_BROADCAST_HDR* pHdr, BOOL* pbDeviceLost);
  // Enumerate supported native media types from the active source reader (if any).
  HRESULT GetSupportedFormats(std::vector<std::tuple<UINT32, UINT32, UINT32, std::string>>& outFormats);
  // Enumerate supported formats by activating an IMFActivate and creating a temporary reader.
  static HRESULT EnumerateFormatsFromActivate(IMFActivate* pActivate, std::vector<std::tuple<UINT32, UINT32, UINT32, std::string>>& outFormats);

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
  HRESULT EndCaptureInternal();

  long m_nRefCount;  // Reference count.
  CRITICAL_SECTION m_critsec;

  HWND m_hwndEvent;  // Application window to receive events.

  IMFSourceReader* m_pReader;
  IMFSinkWriter* m_pWriter;

  BOOL m_bFirstSample;
  LONGLONG m_llBaseTime;

  WCHAR* m_pwszSymbolicLink;
};
