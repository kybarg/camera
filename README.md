# Camera Device Management Library

Node.js C++ addon for Windows camera capture using Media Foundation API with persistent device management and full TypeScript support.

## Features

- **Ultra-fast pixel processing** with 15-20x speedup for BGRA to RGBA conversion
- **Persistent device identification** using Windows symbolic links
- **Promise-based async API** with comprehensive error handling and typed responses
- **Format management** with automatic best-match selection
- **JPG image saving** with Sharp library integration
- **Session-independent device claiming** that survives reboots
- **Full TypeScript support** with comprehensive type definitions
- **Event-driven frame capture** using EventEmitter pattern

## Installation

```bash
npm install @kybarg/camera
```

For TypeScript development:
```bash
npm install --save-dev typescript @types/node ts-node
```

## Quick Start

### Installation and Build

```bash
npm install
npm run build
```

### Running Examples

```bash
# Basic camera capture (JavaScript)
npm run example

# TypeScript camera capture example
npm run example-ts

# Format management
npm run example:format

# Device status demonstration
npm run example:devices
```

### Running Tests

```bash
# Run main test
npm test

# Run all tests
npm run test:all

# TypeScript type checking
npm run type-check
```

## Quick Examples

### JavaScript Usage

```javascript
const Camera = require('@kybarg/camera');

async function captureCamera() {
  const camera = new Camera();

  try {
    // Enumerate available devices
    const devices = await camera.enumerateDevices();
    console.log('Available devices:', devices);

    if (devices.length > 0) {
      // Claim device using persistent symbolic link
      const claimResult = await camera.claimDevice(devices[0].symbolicLink);
      console.log('Device claimed:', claimResult.message);

      // Set up event listener for frames
      camera.on('frame', (frameBuffer) => {
        console.log(`Captured frame: ${frameBuffer.length} bytes`);
      });

      // Start capture
      const startResult = await camera.startCapture();
      console.log('Capture started:', startResult.message);

      // Stop after 5 seconds
      setTimeout(async () => {
        await camera.stopCapture();
        await camera.releaseDevice();
      }, 5000);
    }
  } catch (error) {
    console.error('Error:', error);
  }
}

captureCamera().catch(console.error);
```

### TypeScript Usage

```typescript
import Camera = require('@kybarg/camera');
import type { DeviceInfo, CameraFormat, OperationResult } from '@kybarg/camera';

async function captureCamera(): Promise<void> {
  const camera = new Camera();

  try {
    // Enumerate available devices with type safety
    const devices: DeviceInfo[] = await camera.enumerateDevices();
    console.log('Available devices:', devices);

    if (devices.length > 0) {
      // Claim device with typed response
      const claimResult: OperationResult = await camera.claimDevice(devices[0].symbolicLink);
      console.log('Device claimed:', claimResult.message);

      // Get supported formats with typing
      const formats: CameraFormat[] = await camera.getSupportedFormats();
      console.log('Supported formats:', formats);

      // Type-safe event handling
      camera.on('frame', (frameBuffer: Buffer) => {
        console.log(`Captured frame: ${frameBuffer.length} bytes`);
      });

      // Start capture with typed result
      const startResult: OperationResult = await camera.startCapture();
      console.log('Capture started:', startResult.message);

      // Stop after 5 seconds
      setTimeout(async () => {
        await camera.stopCapture();
        await camera.releaseDevice();
      }, 5000);
    }
  } catch (error) {
    console.error('Error:', error);
  }
}

captureCamera().catch(console.error);
```

## Examples

Run any example from the `examples/` directory:

```bash
# Basic camera capture (JavaScript)
npm run example
# or: node examples/example.js

# TypeScript camera capture with full typing
npm run example-ts
# or: npx ts-node examples/typescript-example.ts

# Format management
npm run example:format
# or: node examples/format-example.js

# Device status demonstration
npm run example:devices
# or: node examples/devices-status-example.js
```

## Tests

Run tests from the `tests/` directory:

```bash
# Test device claiming
npm test
# or: node tests/test-claim-device.js

# Test camera availability
node tests/camera-claim-test.js

# Test symbolic link persistence
node tests/test-symlink-persistence.js

# Run all tests
npm run test:all
```

## API Reference

### Camera Class

The Camera class extends EventEmitter and provides async methods for camera operations.

#### `enumerateDevices(): Promise<DeviceInfo[]>`
Returns list of available camera devices with persistent symbolic links.

**Returns:**
```typescript
interface DeviceInfo {
  friendlyName: string;    // Human-readable device name
  symbolicLink: string;    // Persistent device identifier
}
```

#### `claimDevice(symbolicLink: string): Promise<ClaimDeviceResult>`
Claims exclusive access to camera device using symbolic link.

**Returns:**
```typescript
interface ClaimDeviceResult {
  success: boolean;        // Operation success status
  message: string;         // Descriptive message
  symbolicLink: string;    // Claimed device symbolic link
}
```

#### `releaseDevice(): Promise<OperationResult>`
Releases the currently claimed camera device.

#### `getSupportedFormats(): Promise<CameraFormat[]>`
Returns supported camera formats for the claimed device.

**Returns:**
```typescript
interface CameraFormat {
  width: number;           // Format width in pixels
  height: number;          // Format height in pixels
  frameRate: number;       // Frame rate in fps
}
```

#### `setDesiredFormat(width: number, height: number, frameRate: number): Promise<SetFormatResult>`
Sets desired camera format (selects closest match).

**Returns:**
```typescript
interface SetFormatResult {
  success: boolean;        // Operation success status
  message: string;         // Descriptive message
  actualWidth: number;     // Actual width that was set
  actualHeight: number;    // Actual height that was set
}
```

#### `getDimensions(): CameraDimensions`
Gets the current camera dimensions (synchronous).

**Returns:**
```typescript
interface CameraDimensions {
  width: number;           // Current width in pixels
  height: number;          // Current height in pixels
}
```

#### `startCapture(): Promise<OperationResult>`
Starts camera capture. Frames are emitted as 'frame' events.

#### `stopCapture(): Promise<OperationResult>`
Stops camera capture.

#### `isCapturing(): boolean`
Returns true if camera is currently capturing frames.

### Events

#### `'frame'` Event
Emitted when a new frame is captured.

```typescript
camera.on('frame', (frameData: Buffer) => {
  // frameData contains RGBA pixel data
  console.log(`Frame size: ${frameData.length} bytes`);
});
```

### TypeScript Support

For full TypeScript documentation and examples, see [TYPESCRIPT.md](TYPESCRIPT.md).

## Available Scripts

```bash
npm run build         # Build the native addon
npm run example       # Run JavaScript example
npm run example-ts    # Run TypeScript example
npm run type-check    # Check TypeScript types
npm test             # Run main test
npm run test:all     # Run all tests
```

## Requirements

- **Windows 10/11** (Media Foundation API)
- **Node.js 16+**
- **Visual Studio Build Tools** or **Visual Studio 2019/2022**
- **Python 3.x** (for node-gyp)

## Migration

For migration from the old `selectDevice()` API, see [MIGRATION.md](MIGRATION.md).

## License

See [LICENSE](LICENSE) file for details.
