# Camera Device Management Library

[![npm version](https://img.shields.io/npm/v/@kybarg/camera.svg)](https://www.npmjs.com/package/@kybarg/camera)
[![npm downloads](https://img.shields.io/npm/dm/@kybarg/camera.svg)](https://www.npmjs.com/package/@kybarg/camera)
[![license](https://img.shields.io/github/license/kybarg/camera.svg)](LICENSE)
[![github stars](https://img.shields.io/github/stars/kybarg/camera?style=social)](https://github.com/kybarg/camera)

Node.js native addon for Windows camera capture using the Media Foundation API.

## Highlights

- Format selection: choose native subtypes such as `mjpeg`/`MJPEG`, `nv12`, `yuy2`, or supply a GUID string to target a specific media subtype.
- `setFormat(format: CameraFormat)` is a single API: `format.subtype` (string or GUID), `width`, `height`, and a required `frameRate`.
- Automatic recovery/resume support for device sleep or transient device loss (see `recoverDevice()` / `recoverDeviceAsync()` and `examples/recovery_test.js`).
- Promise-based async API and TypeScript typings included.

## Installation

```powershell
npm install
npm run build
```

Notes: building the native addon requires Visual Studio Build Tools and Python (for `node-gyp`).

## Quick Examples

### JavaScript (minimal)

```javascript
const Camera = require('@kybarg/camera');

async function main() {
  const cam = new Camera();
  const devices = await cam.enumerateDevices();
  if (!devices.length) return console.log('No cameras');

  await cam.claimDevice(devices[0].symbolicLink);

  // Choose a supported format (eg. MJPEG or NV12) from getSupportedFormats()
  const formats = await cam.getSupportedFormats();
  // formats contain { subtype, width, height, frameRate, ... }
  const fmt = formats.find(f => {
    const s = (f.subtype || '').toLowerCase();
    return s.includes('mjpeg') || s.includes('mjpg');
  }) || formats[0];

  await cam.setFormat({ subtype: fmt.subtype, width: fmt.width, height: fmt.height, frameRate: fmt.frameRate });

  // Listen for raw sample buffers (Uint8Array-backed Buffer)
  cam.on('frame', (buf) => {
    console.log('frame bytes:', buf.length);
    // buf is the raw contiguous sample bytes from the camera (MJPEG packet or NV12 plane data)
  });

  await cam.startCapture();

  // stop later
  setTimeout(async () => {
    await cam.stopCapture();
    await cam.releaseDevice();
  }, 5000);
}

main().catch(console.error);
```

Important: the frames you receive are the native sample payloads. For compressed formats (MJPEG) the buffer contains JPEG frames. For planar formats (NV12) you'll get the raw plane data. The library intentionally avoids converting pixel formats automatically so you can control decoding/processing downstream.

## API Overview

All async methods return Promises. See `index.d.ts` for full TypeScript types.

Camera methods of interest:

- `enumerateDevices(): Promise<DeviceInfo[]>` — list attached cameras with persistent `symbolicLink` identifiers.
- `claimDevice(symbolicLink): Promise<OperationResult>` — claim exclusive use of a device (survives process restarts if claimed).
- `releaseDevice(): Promise<OperationResult>` — release claimed device.
- `getSupportedFormats(): Promise<CameraFormat[]>` — returns formats with fields `{ subtype, width, height, frameRate, guid? }`.
- `setFormat(format: CameraFormat): Promise<SetFormatResult>` — set format using `subtype` (string like `nv12` or a GUID string), required `width`, `height`, and required `frameRate`.
- `startCapture(): Promise<OperationResult>` — begin streaming; frames are emitted as `'frame'` events.
- `stopCapture(): Promise<OperationResult>` — stop streaming.
- `recoverDevice(): Promise<OperationResult>` — attempt to recover a previously-claimed device after sleep or transient loss; the native side will try small toggles and a recreate/restart before failing.
- `isCapturing(): boolean` — synchronous check for capture state.

Events:

- `'frame'` — emitted with a single argument: `Buffer` containing the raw sample bytes for that sample.

Example TypeScript types (see `index.d.ts` in the repo):

```ts
interface CameraFormat {
  subtype: string; // human-friendly name (eg. 'nv12', 'mjpe g', 'yuy2') or GUID string
  width: number;
  height: number;
  frameRate: number; // required
}
```

## Recovery and Resume

The addon implements recovery helpers to improve robustness after system sleep or temporary device loss. `recoverDevice()` / `recoverDeviceAsync()` will attempt:

1. a stream toggle (end session, short wait, restart) with a longer wait for the first sample;
2. if that fails, a full recreate of the capture object and a restart attempt;
3. it reports operation-level HRESULTs and diagnostic logs to stderr when enabled in the native build.

See `examples/recovery_test.js` for an automated test harness that triggers recovery on inactivity and validates whether frames resume.

## Examples and Tests

Run the examples:

```powershell
npm run example
# or
node examples/example.js

# recovery test (non-interactive)
node examples/recovery_test.js
```

## Building

```powershell
npm install
npm run build
```

Requirements: Windows 10/11, Visual Studio Build Tools (or VS), Python 3.x for node-gyp.

## Notes

- The library delivers raw sample buffers. If you need decoded RGBA frames, decode MJPEG or convert NV12 -> RGBA using your preferred native or JS library (for example `sharp` for JPEG decoding or a native fast converter for NV12).
- The `setFormat` API now accepts subtype names and GUID strings; for best results use a format object returned by `getSupportedFormats()`.

## License

See [LICENSE](LICENSE).
