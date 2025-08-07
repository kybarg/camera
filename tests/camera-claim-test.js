const Camera = require("../addon.js");

async function testCameraClaimStatus() {
  try {
    const camera = new Camera();

    console.log("Testing camera claim status with actual capture attempt...\n");

    // Get devices
    const devices = await camera.enumerateDevices();

    console.log(`Found ${devices.length} camera device(s):`);

    for (let i = 0; i < devices.length; i++) {
      const device = devices[i];
      console.log(`\nDevice ${i}: ${device.friendlyName}`);
      console.log(`  Initial Status: ✅ AVAILABLE`);

      // Try to actually use the device
      try {
        await camera.claimDevice(device.symbolicLink);
        console.log(`  ✅ Device selection: SUCCESS`);

        // Try to get formats
        const formats = camera.getSupportedFormats();
        console.log(`  ✅ Format enumeration: SUCCESS (${formats.length} formats)`);

        // Try to actually start capture (this will reveal if device is truly available)
        console.log(`  🔄 Testing actual capture...`);

        let captureStarted = false;
        let captureError = null;

        const capturePromise = camera.startCapture(
          (error, result) => {
            if (error) {
              captureError = error;
            } else {
              captureStarted = true;
            }
          }
        );

        // Wait a bit to see if capture starts
        await new Promise(resolve => setTimeout(resolve, 1000));

        if (captureError) {
          console.log(`  ❌ Capture test: FAILED - ${captureError.message}`);
          console.log(`  🔍 Real Status: CLAIMED (device is actually in use)`);
        } else if (captureStarted) {
          console.log(`  ✅ Capture test: SUCCESS`);
          console.log(`  🔍 Real Status: AVAILABLE (device is truly free)`);

          // Stop capture
          await camera.stopCapture();
        } else {
          console.log(`  ⏳ Capture test: TIMEOUT (unclear status)`);
        }

      } catch (error) {
        console.log(`  ❌ Device test: FAILED - ${error.message}`);
        console.log(`  🔍 Real Status: CLAIMED or ERROR`);
      }
    }

  } catch (error) {
    console.error("Error:", error.message);
  }
}

// Run the test
testCameraClaimStatus().catch(console.error);
