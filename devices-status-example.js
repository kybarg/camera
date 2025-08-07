const Camera = require("./addon.js");

async function demonstrateDevicesStatus() {
  try {
    const camera = new Camera();

    console.log("Enumerating camera devices with status information...\n");

    // Get devices with claim status
    const devices = await camera.enumerateDevices();

    console.log(`Found ${devices.length} camera device(s):\n`);

    devices.forEach((device, index) => {
      console.log(`Device ${index}:`);
      console.log(`  Name: ${device.friendlyName}`);
      console.log(`  Symbolic Link: ${device.symbolicLink}`);
      console.log(`  Status: ${device.isClaimed ? '❌ CLAIMED (in use by another app)' : '✅ AVAILABLE'}`);
      console.log('');
    });

    // Find first available device
    const availableDevice = devices.find(device => !device.isClaimed);

    if (availableDevice) {
      const availableIndex = devices.indexOf(availableDevice);
      console.log(`\n✅ Found available device at index ${availableIndex}: ${availableDevice.friendlyName}`);

      // Try to select and get formats for available device
      try {
        await camera.selectDevice(availableIndex);
        console.log("✅ Device selected successfully");

        const formats = camera.getSupportedFormats();
        console.log(`\n📹 Supported formats for this device:`);
        formats.slice(0, 5).forEach((format, index) => {
          console.log(`  ${index + 1}. ${format.width}x${format.height} @ ${format.frameRate}fps`);
        });

        if (formats.length > 5) {
          console.log(`  ... and ${formats.length - 5} more formats`);
        }

      } catch (error) {
        console.log(`❌ Failed to select device: ${error.message}`);
      }

    } else {
      console.log("\n❌ No available devices found - all cameras are currently in use by other applications");
    }

  } catch (error) {
    console.error("Error:", error.message);
  }
}

// Run the demonstration
demonstrateDevicesStatus().catch(console.error);
