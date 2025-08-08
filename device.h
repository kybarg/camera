#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <windows.h>
#include <functional>
#include <string>
#include <tuple>
#include <vector>

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

  // Pre-allocated buffers for better performance
  IMFSample* m_pReusableOutSample = NULL;
  IMFMediaBuffer* m_pReusableBuffer = NULL;
  MFT_OUTPUT_STREAM_INFO m_StreamInfo = {0};
  bool m_bStreamInfoInitialized = false;

 public:
  bool isCapturing = false;
  IMFTransform* m_pTransform = NULL;
  IMFMediaSource* m_pSource = NULL;
  IMFSourceReader* m_pReader = NULL;
  UINT32 width = 0;
  UINT32 height = 0;

  CaptureDevice()
      : m_ppDevices(NULL), m_cDevices(0) {
  }
  ~CaptureDevice() {
    ReleaseDevice();  // Properly release all media resources
    Clear();          // Clear device enumeration
  }

  UINT32 Count() const { return m_cDevices; }

  void Clear();
  HRESULT EnumerateDevices();
  std::vector<DeviceInfo> GetDevicesList();
  HRESULT SelectDeviceBySymbolicLink(const std::wstring& symbolicLink);
  HRESULT ReleaseDevice();
  HRESULT CreateStream();
  HRESULT SetupCapture(std::function<HRESULT(IMFMediaBuffer*)> callback);
  HRESULT StartCapture(std::function<HRESULT(IMFMediaBuffer*)> callback);
  HRESULT RunCaptureLoop(std::function<HRESULT(IMFMediaBuffer*)> callback);
  HRESULT StopCapture();

  // New methods for resolution and framerate management
  std::vector<std::tuple<UINT32, UINT32, UINT32>> GetSupportedFormats();
  HRESULT SetDesiredFormat(UINT32 desiredWidth, UINT32 desiredHeight, UINT32 desiredFrameRate);

  // (was: IsDeviceValid) — removed as unused
};

#endif  // CaptureDevice_H
