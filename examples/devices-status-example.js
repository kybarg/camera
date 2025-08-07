const Camera = require("../addon.js");

async function demonstrateDevicesStatus() {
  try {
    const camera = new Camera();

    console.log("Enumerating camera devices...\n");

    // Get devices
    const devices = await camera.enumerateDevices();

    console.log(`Found ${devices.length} camera device(s):`);

    devices.forEach((device, index) => {
      console.log(`✅ Found available device at index ${index}: ${device.friendlyName}`);
      console.log(`  Symbolic Link: ${device.symbolicLink}`);
    });

    if (devices.length > 0) {
      const firstDevice = devices[0];
      console.log(`✅ Device selected successfully`);
      console.log(`  Symbolic Link: ${firstDevice.symbolicLink}`);

      // Try to claim and get formats for the first device
      try {
        await camera.claimDevice(firstDevice.symbolicLink);
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
      console.log("\n❌ No camera devices found");
    }

  } catch (error) {
    console.error("Error:", error.message);
  }
}

// Run the demonstration
demonstrateDevicesStatus().catch(console.error);
