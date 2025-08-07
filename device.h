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

  IMFMediaSource* m_pSource = NULL;
  IMFSourceReader* m_pReader = NULL;
  bool isCapturing = false;

  // Pre-allocated buffers for better performance
  IMFSample* m_pReusableOutSample = NULL;
  IMFMediaBuffer* m_pReusableBuffer = NULL;
  MFT_OUTPUT_STREAM_INFO m_StreamInfo = {0};
  bool m_bStreamInfoInitialized = false;

 public:
  IMFTransform* m_pTransform = NULL;
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
  std::vector<std::pair<std::wstring, std::wstring>> GetDevicesList();
  HRESULT SelectDevice(int index);
  HRESULT CreateStream();
  HRESULT StartCapture(std::function<HRESULT(IMFMediaBuffer*)> callback);
  HRESULT StopCapture();
};

#endif  // CaptureDevice_H
