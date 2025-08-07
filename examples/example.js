const fs = require("node:fs");
const path = require("node:path");
const sharp = require("sharp");
const { exec } = require("node:child_process");
const Camera = require("../addon.js");

async function takeCameraSnapshot() {
  let cameraWidth = 640;  // Default fallback
  let cameraHeight = 480; // Default fallback
  let snapshotSaved = false; // Flag to track if snapshot has been saved

  const camera = new Camera();

  try {
    console.log("🚀 Starting camera snapshot example...\n");

    // Step 1: Enumerate available camera devices
    console.log("1. 📷 Enumerating camera devices...");
    const devices = await camera.enumerateDevices();
    console.log(`✅ Found ${devices.length} camera device(s):`);
    devices.forEach((device, index) => {
      console.log(`   📹 Device ${index}: ${device.friendlyName}`);
    });
    console.log("");

    if (devices.length === 0) {
      throw new Error("❌ No camera devices found!");
    }

    // Step 2: Claim the first camera device
    console.log("2. 🔒 Claiming camera device...");
    console.log(`   📝 Using: ${devices[0].friendlyName}`);
    await camera.claimDevice(devices[0].symbolicLink);
    console.log("✅ Camera device claimed successfully");
    console.log("");

    // Step 3: Get camera dimensions
    console.log("3. 📐 Getting camera dimensions...");
    const dimensions = camera.getDimensions();
    cameraWidth = dimensions.width;
    cameraHeight = dimensions.height;
    console.log(`✅ Camera resolution: ${cameraWidth} x ${cameraHeight} pixels`);
    console.log("");

    // Step 4: Set up frame event listener
    console.log("4. 📡 Setting up frame event listener...");
    camera.on('frame', async (frameBuffer) => {
      // Only process the first frame
      if (!snapshotSaved && frameBuffer) {
        snapshotSaved = true; // Prevent multiple snapshots

        console.log(`📹 Frame received: ${frameBuffer.length.toLocaleString()} bytes`);
        console.log("📸 Taking snapshot...");

        try {
          // Ensure snapshots directory exists
          const snapshotsDir = path.join(__dirname, '../snapshots');
          if (!fs.existsSync(snapshotsDir)) {
            console.log("📁 Creating snapshots directory...");
            fs.mkdirSync(snapshotsDir, { recursive: true });
          }

          // Convert RGBA buffer to JPG using Sharp
          const filename = `snapshot_${Date.now()}.jpg`;
          const filepath = path.join(snapshotsDir, filename);

          console.log("🎨 Converting RGBA buffer to JPG...");
          await sharp(frameBuffer, {
            raw: {
              width: cameraWidth,
              height: cameraHeight,
              channels: 4 // RGBA = 4 channels
            }
          })
          .jpeg({ quality: 90 }) // Set JPG quality (0-100)
          .toFile(filepath);

          console.log(`✅ Snapshot saved: ${filename}`);
          console.log(`📍 File location: ${filepath}`);
          console.log("");

          // Stop capture after taking the snapshot
          console.log("6. ⏹️ Stopping capture...");
          await camera.stopCapture();
          console.log("✅ Capture stopped successfully");
          console.log("");

          // Open the saved image with default system app
          console.log("7. 🖼️  Opening image with default app...");
          const command = process.platform === 'win32' ? `start "" "${filepath}"` :
                         process.platform === 'darwin' ? `open "${filepath}"` :
                         `xdg-open "${filepath}"`;

          exec(command, (error, stdout, stderr) => {
            if (error) {
              console.error(`❌ Error opening image: ${error.message}`);
            } else {
              console.log("✅ Image opened successfully");
            }

            console.log("");
            console.log("✅ Camera snapshot example completed!");
            console.log("🏁 Exiting in 2 seconds...");

            // Exit the process after opening the image
            setTimeout(() => {
              process.exit(0);
            }, 2000);
          });

        } catch (error) {
          console.error("❌ Error processing snapshot:", error);
          process.exit(1);
        }
      }
    });
    console.log("✅ Frame event listener ready");
    console.log("");

    // Step 5: Start camera capture
    console.log("5. 🎬 Starting camera capture...");
    const result = await camera.startCapture();
    console.log("✅ Camera capture started successfully");
    console.log("⏳ Waiting for first frame...");
    console.log("");

  } catch (error) {
    console.error("❌ Error in camera snapshot:", error);
    process.exit(1);
  }
}

// Run the async function
takeCameraSnapshot();
