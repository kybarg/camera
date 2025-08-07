# Camera Device Management Library

Node.js C++ addon for Windows camera capture using Media Foundation API with persistent device management.

## Features

- **Ultra-fast pixel processing** with 15-20x speedup for BGRA to RGBA conversion
- **Persistent device identification** using Windows symbolic links
- **Promise-based async API** with comprehensive error handling
- **Format management** with automatic best-match selection
- **JPG image saving** with Sharp library integration
- **Session-independent device claiming** that survives reboots

## Installation

```bash
npm install
```

## Project Structure

```
├── addon.cc              # Main N-API addon entry point
├── addon.js              # JavaScript API wrapper
├── camera.cc/camera.h    # Camera management and async operations
├── device.cc/device.h    # Device enumeration and capture logic
├── binding.gyp           # Build configuration
├── examples/             # Usage examples
│   ├── example.js        # Basic camera capture with JPG saving
│   ├── format-example.js # Format management demonstration
│   └── devices-status-example.js # Device enumeration and claiming
├── tests/                # Test files
│   ├── test-claim-device.js # Device claiming functionality test
│   ├── camera-claim-test.js # Camera availability testing
│   └── test-symlink-persistence.js # Symbolic link persistence test
└── MIGRATION.md          # Migration guide from old API
```

## Quick Start

### Installation and Build

```bash
npm install
npm run build
```

### Running Examples

```bash
# Basic camera capture
npm run example

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
```

```javascript
const Camera = require('./addon.js');

async function captureCamera() {
  const camera = new Camera();

  // Enumerate available devices
  const devices = await camera.enumerateDevices();
  console.log('Available devices:', devices);

  // Claim device using persistent symbolic link
  await camera.claimDevice(devices[0].symbolicLink);

  // Start capture
  camera.startCapture((frame) => {
    console.log(`Captured frame: ${frame.length} bytes`);
    return 0; // Continue capture
  });
}

captureCamera().catch(console.error);
```

## Examples

Run any example from the `examples/` directory:

```bash
# Basic camera capture
npm run example
# or: node examples/example.js

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

#### `enumerateDevices(): Promise<DeviceInfo[]>`
Returns list of available camera devices with persistent symbolic links.

#### `claimDevice(symbolicLink: string): Promise<void>`
Claims exclusive access to camera device using symbolic link.

#### `startCapture(callback: Function): void`
Starts camera capture with frame callback.

#### `stopCapture(): void`
Stops camera capture.

#### `getSupportedFormats(): Array<{width, height, frameRate}>`
Returns supported camera formats.

#### `setDesiredFormat(width, height, frameRate): Promise<void>`
Sets desired camera format (selects closest match).

## Migration

For migration from the old `selectDevice()` API, see [MIGRATION.md](MIGRATION.md).

## License

See [LICENSE](LICENSE) file for details.
