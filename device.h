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

#ifndef CaptureDevice_H
#define CaptureDevice_H

// Structure to hold device information
struct DeviceInfo {
  std::wstring friendlyName;
  std::wstring symbolicLink;

  DeviceInfo(const std::wstring& name, const std::wstring& link)
    : friendlyName(name), symbolicLink(link) {}
};

template <class T>
void SafeRelease(T** ppT) {
  if (*ppT) {
    (*ppT)->Release();
    *ppT = NULL;
  }
}

class CaptureDevice {
  UINT32 m_cDevices;
  IMFActivate** m_ppDevices;

  bool isCapturing = false;

  // Pre-allocated buffers for better performance
  IMFSample* m_pReusableOutSample = NULL;
  IMFMediaBuffer* m_pReusableBuffer = NULL;
  MFT_OUTPUT_STREAM_INFO m_StreamInfo = {0};
  bool m_bStreamInfoInitialized = false;

 public:
  IMFTransform* m_pTransform = NULL;
  IMFMediaSource* m_pSource = NULL;
  IMFSourceReader* m_pReader = NULL;
  UINT32 width = 0;
  UINT32 height = 0;

  CaptureDevice()
      : m_ppDevices(NULL), m_cDevices(0) {
  }
  ~CaptureDevice() {
    Clear();
    SafeRelease(&m_pReusableOutSample);
    SafeRelease(&m_pReusableBuffer);
  }

  UINT32 Count() const { return m_cDevices; }

  void Clear();
  HRESULT EnumerateDevices();
  std::vector<DeviceInfo> GetDevicesList();
  HRESULT SelectDeviceBySymbolicLink(const std::wstring& symbolicLink);
  HRESULT ReleaseDevice();
  HRESULT CreateStream();
  HRESULT StartCapture(std::function<HRESULT(IMFMediaBuffer*)> callback);
  HRESULT StopCapture();

  // New methods for resolution and framerate management
  std::vector<std::tuple<UINT32, UINT32, UINT32>> GetSupportedFormats();
  HRESULT SetDesiredFormat(UINT32 desiredWidth, UINT32 desiredHeight, UINT32 desiredFrameRate);
  
  // Device validation method
  bool IsDeviceValid();
};

#endif  // CaptureDevice_H
