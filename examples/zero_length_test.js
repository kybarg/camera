const Camera = require("../addon.js");

async function zeroLengthTest(durationMs = 5000) {
  const camera = new Camera();
  let totalFrames = 0;
  let zeroLengthFrames = 0;
  let receivedFirstNonEmpty = false;

  try {
    console.log("Zero-length frame test starting...");

    const devices = await camera.enumerateDevices();
    if (devices.length === 0) {
      console.error("No camera devices found");
      process.exit(1);
    }

    await camera.claimDevice(devices[0].symbolicLink);

    // Choose best (highest resolution) format
    const formats = await camera.getSupportedFormats();
    if (formats.length > 0) {
      let best = formats[0];
      let maxPix = best.width * best.height;
      let maxFps = best.frameRate;
      for (const f of formats) {
        const p = f.width * f.height;
        if (p > maxPix || (p === maxPix && f.frameRate > maxFps)) {
          best = f;
          maxPix = p;
          maxFps = f.frameRate;
        }
      }
      console.log(
        `Selecting format ${best.width}x${best.height} @ ${best.frameRate}`
      );
      try {
        // Use the CameraFormat object directly with the new setFormat API
        await camera.setFormat(best);
      } catch (e) {
        console.warn("Failed to set format:", e.message || e);
      }
    }

    const dims = camera.getDimensions();
    console.log(`Final dimensions: ${dims.width}x${dims.height}`);

    camera.on("frame", (buf) => {
      totalFrames++;
      if (!buf || buf.length === 0) {
        zeroLengthFrames++;
      } else {
        receivedFirstNonEmpty = true;
      }
    });

    await camera.startCapture();
    console.log(`Capturing for ${durationMs}ms...`);

    await new Promise((res) => setTimeout(res, durationMs));

    await camera.stopCapture();
    await camera.releaseDevice();

    console.log("Test complete:");
    console.log(`  Total frames: ${totalFrames}`);
    console.log(`  Zero-length frames: ${zeroLengthFrames}`);
    console.log(`  Non-empty frames: ${totalFrames - zeroLengthFrames}`);
    console.log(`  Received any non-empty frame: ${receivedFirstNonEmpty}`);
    const resultFps = totalFrames / (durationMs / 1000);
    console.log(`  Result FPS: ${resultFps.toFixed(2)}`);
  } catch (err) {
    console.error("Error during test:", err);
    process.exit(1);
  }
}

// Run test for 10 seconds
zeroLengthTest(10000).then(() => process.exit(0));
