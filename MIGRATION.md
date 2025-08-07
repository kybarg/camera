# Camera Device Management - Migration to claimDevice

## Overview

All examples and test files have been updated to use the new `claimDevice()` function instead of the deprecated `selectDevice()` function. The new function provides better device management with persistent symbolic link identification.

## Key Changes

### Old API (Deprecated):
```javascript
// Select device by index (not persistent)
await camera.selectDevice(0);
```

### New API (Recommended):
```javascript
// Enumerate devices first
const devices = await camera.enumerateDevices();

// Claim device by symbolic link (persistent across sessions)
await camera.claimDevice(devices[0].symbolicLink);
```

## Benefits of claimDevice

1. **Persistent Identification**: Uses symbolic links that survive system reboots
2. **Reliable Device Targeting**: Always claims the same physical device
3. **Session Independence**: Works across Windows sessions
4. **Order Independence**: Doesn't rely on device enumeration order

## Updated Files

### Example Files:
- ✅ `examples/example.js` - Basic camera capture with JPG saving
- ✅ `examples/format-example.js` - Camera format management demonstration
- ✅ `examples/devices-status-example.js` - Device enumeration and claiming

### Test Files:
- ✅ `tests/test-claim-device.js` - Device claiming functionality test
- ✅ `tests/camera-claim-test.js` - Camera availability testing
- ✅ `tests/test-symlink-persistence.js` - Symbolic link persistence demonstration

### API Changes:
- ✅ `claimDevice(symbolicLink)` - New async function using symbolic links
- ❌ `selectDevice(index)` - Deprecated (still available for compatibility)

## Usage Examples

### Basic Device Claiming:
```javascript
const Camera = require('./addon.js');

async function claimCamera() {
  const camera = new Camera();
  
  // Get available devices
  const devices = await camera.enumerateDevices();
  console.log('Available cameras:', devices);
  
  // Claim the first device by symbolic link
  const result = await camera.claimDevice(devices[0].symbolicLink);
  console.log('Device claimed:', result);
  
  // Get camera capabilities
  const formats = camera.getSupportedFormats();
  console.log('Supported formats:', formats);
}
```

### Persistent Device Access:
```javascript
const fs = require('fs');

// Save symbolic link for future use
const devices = await camera.enumerateDevices();
const myCamera = devices[0].symbolicLink;
fs.writeFileSync('my-camera.txt', myCamera);

// Later, in another session:
const savedCamera = fs.readFileSync('my-camera.txt', 'utf8');
await camera.claimDevice(savedCamera); // Works even after reboot!
```

### Error Handling:
```javascript
try {
  await camera.claimDevice(symbolicLink);
  console.log('Camera claimed successfully');
} catch (error) {
  console.error('Failed to claim camera:', error.message);
  // Handle device not available, unplugged, etc.
}
```

## Symbolic Link Format

Windows camera symbolic links follow this format:
```
\\?\usb#vid_3277&pid_0036&mi_00#6&190cf7f4&0&0000#{e5323777-f976-4f5b-9b55-b94699c46e44}\global
```

Components:
- **VID/PID**: Vendor and Product IDs
- **Serial**: Unique device serial number
- **GUID**: Device class identifier (camera)

## Migration Guide

If you're using the old `selectDevice(index)` function:

1. **Update device selection**:
   ```javascript
   // OLD:
   await camera.selectDevice(0);
   
   // NEW:
   const devices = await camera.enumerateDevices();
   await camera.claimDevice(devices[0].symbolicLink);
   ```

2. **Store device references**:
   ```javascript
   // Save the symbolic link for persistent access
   const myPreferredCamera = devices[0].symbolicLink;
   localStorage.setItem('cameraId', myPreferredCamera);
   ```

3. **Handle device persistence**:
   ```javascript
   // Use saved symbolic link
   const savedCamera = localStorage.getItem('cameraId');
   if (savedCamera) {
     try {
       await camera.claimDevice(savedCamera);
     } catch (error) {
       // Device may be unplugged, fallback to enumeration
       const devices = await camera.enumerateDevices();
       await camera.claimDevice(devices[0].symbolicLink);
     }
   }
   ```

## Testing

All example files have been tested and work correctly with the new API:

```bash
# Test basic functionality
node test-claim-device.js

# Test format management
node format-example.js

# Test device enumeration
node devices-status-example.js

# Test persistence
node test-symlink-persistence.js

# Test capture functionality
node example.js
```

The new `claimDevice` function provides a more robust and persistent way to manage camera devices across Windows sessions.
