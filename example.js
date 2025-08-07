const fs = require("node:fs");
const path = require("node:path");
const sharp = require("sharp");
const Camera = require("./addon.js");

try {
  let snapshot;
  let cameraWidth = 640;  // Default fallback
  let cameraHeight = 480; // Default fallback

  const camera = new Camera();

  camera
    .enumerateDevices()
    .then((devices) => {
      console.log("2. Enumerate Devices", devices);
      return camera.selectDevice(0);
    })
    .then(() => {
      console.log("3. Select Device");
      return camera.startCapture((error, result) => {
        if (error) {
          console.error(error);
          return;
        }

        snapshot = result;

        // Update dimensions from the actual capture result
        if (result.width && result.height) {
          cameraWidth = result.width;
          cameraHeight = result.height;
          console.log(`Actual camera resolution: ${cameraWidth}x${cameraHeight}`);
        }

        console.log("Frame captured");
      });
    })
    .then(() => {
      console.log("4. Start Capture");
    })
    .catch(console.error);

  console.log("1. Console log");

  setInterval(async () => {
    try {
      if (snapshot && snapshot.buffer) {
        console.log("Saving snapshot as JPG...");

        // Ensure snapshots directory exists
        const snapshotsDir = path.join(__dirname, 'snapshots');
        if (!fs.existsSync(snapshotsDir)) {
          fs.mkdirSync(snapshotsDir, { recursive: true });
        }

        // Convert RGBA buffer to JPG using Sharp
        const filename = `snapshot_${Date.now()}.jpg`;
        const filepath = path.join(snapshotsDir, filename);

        await sharp(snapshot.buffer, {
          raw: {
            width: cameraWidth,
            height: cameraHeight,
            channels: 4 // RGBA = 4 channels
          }
        })
        .jpeg({ quality: 90 }) // Set JPG quality (0-100)
        .toFile(filepath);

        console.log(`Snapshot saved: ${filename}`);
      } else {
        console.log("No snapshot available yet...");
      }
    } catch (error) {
      console.error("Error saving snapshot:", error);
    }
  }, 2000);
} catch (error) {
  console.error(error);
}
