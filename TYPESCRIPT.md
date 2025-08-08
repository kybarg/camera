# TypeScript Support

This package now includes comprehensive TypeScript type definitions for enhanced development experience.

## Installation

```bash
npm install @kybarg/camera
# For TypeScript development
npm install --save-dev typescript @types/node ts-node
```

## TypeScript Usage

### Basic Example

```typescript
import Camera = require('@kybarg/camera');

async function main() {
  const camera = new Camera();

  try {
    // Enumerate devices with type safety
    const devices = await camera.enumerateDevices();
    console.log(`Found ${devices.length} camera device(s)`);

    if (devices.length > 0) {
      // Claim first device
      const result = await camera.claimDevice(devices[0].symbolicLink);
      console.log(`Device claimed: ${result.message}`);

      // Get supported formats
      const formats = await camera.getSupportedFormats();
      console.log(`Supported formats: ${formats.length}`);

      // Set format
      const formatResult = await camera.setDesiredFormat(1280, 720, 30);
      console.log(`Format set: ${formatResult.actualWidth}x${formatResult.actualHeight}`);

      // Start capture with typed event handlers
      camera.on('frame', (frameData: Buffer) => {
        console.log(`Frame received: ${frameData.length} bytes`);
      });

      await camera.startCapture();

      // Stop after 2 seconds
      setTimeout(async () => {
        await camera.stopCapture();
        await camera.releaseDevice();
      }, 2000);
    }
  } catch (error) {
    console.error('Error:', error);
  }
}

main();
```

### Type Definitions

The package exports the following TypeScript interfaces:

#### `DeviceInfo`
```typescript
interface DeviceInfo {
  friendlyName: string;
  symbolicLink: string;
}
```

#### `CameraFormat`
```typescript
interface CameraFormat {
  width: number;
  height: number;
  frameRate: number;
}
```

#### `OperationResult`
```typescript
interface OperationResult {
  success: boolean;
  message: string;
}
```

#### `ClaimDeviceResult`
```typescript
interface ClaimDeviceResult extends OperationResult {
  symbolicLink: string;
}
```

#### `SetFormatResult`
```typescript
interface SetFormatResult extends OperationResult {
  actualWidth: number;
  actualHeight: number;
}
```

#### `CameraDimensions`
```typescript
interface CameraDimensions {
  width: number;
  height: number;
}
```

### Camera Class Methods

All methods are fully typed and documented:

- `enumerateDevices(): Promise<DeviceInfo[]>`
- `claimDevice(symbolicLink: string): Promise<ClaimDeviceResult>`
- `releaseDevice(): Promise<OperationResult>`
- `getSupportedFormats(): Promise<CameraFormat[]>`
- `setDesiredFormat(width: number, height: number, frameRate: number): Promise<SetFormatResult>`
- `getDimensions(): CameraDimensions`
- `startCapture(): Promise<OperationResult>`
- `stopCapture(): Promise<OperationResult>`
- `isCapturing(): boolean`

### Event Handling

The Camera class extends EventEmitter with typed events:

```typescript
camera.on('frame', (frameData: Buffer) => {
  // Type-safe frame handling
  console.log(`Frame size: ${frameData.length} bytes`);
});
```

## Scripts

Run the TypeScript example:
```bash
npm run example-ts
```

Type-check your code:
```bash
npm run type-check
```

## Configuration

The package includes a `tsconfig.json` for TypeScript compilation and `index.d.ts` for type definitions.
