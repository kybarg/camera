const Camera = require("./addon.js");

async function demonstrateFormatFeatures() {
  try {
    const camera = new Camera();

    // Enumerate and select device
    const devices = await camera.enumerateDevices();
    console.log("Available devices:", devices);

    await camera.selectDevice(0);
    console.log("Device selected");

    // Get all supported formats (resolution + frame rate combinations)
    const formats = camera.getSupportedFormats();
    console.log("\nSupported Formats:", formats);
    formats.forEach((format, index) => {
      console.log(`  ${index + 1}. ${format.width}x${format.height} @ ${format.frameRate}fps`);
    });

    if (formats.length > 0) {
      // Set a specific format
      console.log("\nTrying to set 1080p at 30 FPS...");
      const formatResult = camera.setDesiredFormat(1920, 1080, 30);

      console.log(formatResult)
      if (formatResult.success) {
        console.log(`✓ Format set successfully: ${formatResult.actualWidth}x${formatResult.actualHeight}`);
      } else {
        console.log(`✗ Failed to set format: ${formatResult.error}`);
      }

      // Try a different format
      console.log("\nTrying to set 720p at 60 FPS...");
      const formatResult2 = camera.setDesiredFormat(1280, 720, 60);
      if (formatResult2.success) {
        console.log(`✓ Format set successfully: ${formatResult2.actualWidth}x${formatResult2.actualHeight}`);
      } else {
        console.log(`✗ Failed to set format: ${formatResult2.error}`);
      }

      // Get current dimensions
      const currentDimensions = camera.getDimensions();
      console.log(`\nCurrent dimensions: ${currentDimensions.width}x${currentDimensions.height}`);

      // Group formats by resolution for easier viewing
      console.log("\nFormats grouped by resolution:");
      const groupedFormats = {};
      formats.forEach(format => {
        const key = `${format.width}x${format.height}`;
        if (!groupedFormats[key]) {
          groupedFormats[key] = [];
        }
        groupedFormats[key].push(format.frameRate);
      });

      Object.entries(groupedFormats).forEach(([resolution, frameRates]) => {
        console.log(`  ${resolution}: ${frameRates.sort((a, b) => b - a).join(', ')} fps`);
      });
    }

  } catch (error) {
    console.error("Error:", error);
  }
}

demonstrateFormatFeatures();
