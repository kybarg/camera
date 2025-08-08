import * as fs from "node:fs";
import * as path from "node:path";
import sharp from "sharp";
import { exec } from "node:child_process";
const Camera = require("../addon.js");
import type { DeviceInfo, CameraFormat, CameraDimensions, OperationResult } from "../index.d.ts";

async function takeCameraSnapshot(): Promise<void> {
  let cameraWidth: number = 640; // Default fallback
  let cameraHeight: number = 480; // Default fallback
  let snapshotSaved: boolean = false; // Flag to track if snapshot has been saved

  const camera = new Camera();

  try {
    console.log("🚀 Starting camera snapshot example...\n");

    // Step 1: Enumerate available camera devices
    console.log("1️⃣ 📷 Enumerating camera devices...");
    const devices: DeviceInfo[] = await camera.enumerateDevices();
    console.log(`✅ Found ${devices.length} camera device(s):`);
    devices.forEach((device: DeviceInfo, index: number) => {
      console.log(`   📹 Device ${index}: ${device.friendlyName}`);
    });
    console.log("");

    if (devices.length === 0) {
      throw new Error("❌ No camera devices found!");
    }

    // Step 2: Claim the first camera device
    console.log("2️⃣ 🔒 Claiming camera device...");
    console.log(`   📝 Using: ${devices[0].friendlyName}`);
    await camera.claimDevice(devices[0].symbolicLink);
    console.log("✅ Camera device claimed successfully");
    console.log("");

    // Step 3: Get supported formats and set highest resolution
    console.log("3️⃣ 📋 Getting supported camera formats...");
    const formats: CameraFormat[] = await camera.getSupportedFormats();
    console.log(`✅ Found ${formats.length} supported format(s):`);

    // Display first few formats for reference
    const displayCount: number = Math.min(5, formats.length);
    for (let i = 0; i < displayCount; i++) {
      const format: CameraFormat = formats[i];
      console.log(
        `   📐 Format ${i + 1}: ${format.width}x${format.height} @ ${
          format.frameRate
        }fps`
      );
    }
    if (formats.length > displayCount) {
      console.log(`   ... and ${formats.length - displayCount} more formats`);
    }
    console.log("");

    // Select the highest resolution format
    if (formats.length > 0) {
      console.log("4️⃣ ⚙️ Selecting highest resolution format...");

      // Find the format with the highest resolution (width * height)
      let bestFormat: CameraFormat = formats[0];
      let maxPixels: number = bestFormat.width * bestFormat.height;

      for (const format of formats) {
        const pixels: number = format.width * format.height;
        if (pixels > maxPixels) {
          maxPixels = pixels;
          bestFormat = format;
        }
      }

      console.log(
        `   🎯 Selected: ${bestFormat.width}x${bestFormat.height} @ ${
          bestFormat.frameRate
        }fps (${maxPixels.toLocaleString()} pixels)`
      );

      try {
        await camera.setDesiredFormat(
          bestFormat.width,
          bestFormat.height,
          bestFormat.frameRate
        );
        console.log("✅ Format set successfully");
      } catch (error) {
        console.log(
          `⚠️ Could not set desired format, using default: ${(error as Error).message}`
        );
      }
    } else {
      console.log("⚠️ No formats available, using default camera settings");
    }
    console.log("");

    // Step 5: Get final camera dimensions
    console.log("5️⃣ 📐 Getting final camera dimensions...");
    const dimensions: CameraDimensions = camera.getDimensions();
    cameraWidth = dimensions.width;
    cameraHeight = dimensions.height;
    console.log(
      `✅ Final camera resolution: ${cameraWidth} x ${cameraHeight} pixels`
    );
    console.log("");

    // Step 6: Set up frame event listener
    console.log("6️⃣ 📡 Setting up frame event listener...");
    camera.on("frame", async (frameBuffer: Buffer) => {
      // Only process the first frame
      if (!snapshotSaved && frameBuffer && frameBuffer.length > 0) {
        snapshotSaved = true; // Prevent multiple snapshots

        console.log(
          `📹 Frame received: ${frameBuffer.length.toLocaleString()} bytes`
        );
        console.log("📸 Taking snapshot...");

        try {
          // Ensure snapshots directory exists
          const snapshotsDir: string = path.join(__dirname, "../snapshots");
          if (!fs.existsSync(snapshotsDir)) {
            console.log("📁 Creating snapshots directory...");
            fs.mkdirSync(snapshotsDir, { recursive: true });
          }

          // Convert RGBA buffer to JPG using Sharp
          const filename: string = `snapshot_${Date.now()}.jpg`;
          const filepath: string = path.join(snapshotsDir, filename);

          console.log("🎨 Converting RGBA buffer to JPG...");
          await sharp(frameBuffer, {
            raw: {
              width: cameraWidth,
              height: cameraHeight,
              channels: 4, // RGBA = 4 channels
            },
          })
            .jpeg({ quality: 90 }) // Set JPG quality (0-100)
            .toFile(filepath);

          console.log(`✅ Snapshot saved: ${filename}`);
          console.log(`📍 File location: ${filepath}`);
          console.log("");

          // Stop capture after taking the snapshot
          console.log("8️⃣ ⏹️ Stopping capture...");
          await camera.stopCapture();
          console.log("✅ Capture stopped successfully");
          console.log("");

          // Open the saved image with default system app
          console.log("9️⃣ 🖼️  Opening image with default app...");
          const command: string =
            process.platform === "win32"
              ? `start "" "${filepath}"`
              : process.platform === "darwin"
              ? `open "${filepath}"`
              : `xdg-open "${filepath}"`;

          exec(command, async (error: any, stdout: any, stderr: any) => {
            if (error) {
              console.error(`❌ Error opening image: ${error.message}`);
            } else {
              console.log("✅ Image opened successfully");
            }

            console.log("");

            // Release the camera device after image is opened
            console.log("🔟 🔓 Releasing camera device...");
            try {
              await camera.releaseDevice();
              console.log("✅ Camera device released successfully");
            } catch (error) {
              console.error("❌ Error releasing camera device:", error);
            }
            console.log("");

            console.log("✅ Camera snapshot example completed!");
            console.log("🏁 Exiting in 2 seconds...");

            // Exit the process after the image is opened
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

    // Step 7: Start camera capture
    console.log("7️⃣ 🎬 Starting camera capture...");
    try {
      const result: OperationResult = await camera.startCapture();
      console.log("✅ Camera capture started successfully");
      console.log("⏳ Waiting for first frame...");
      console.log("");
    } catch (error) {
      console.error("❌ Error starting camera capture:", error);
      throw error;
    }
  } catch (error) {
    console.error("❌ Error in camera snapshot:", error);
    process.exit(1);
  }
}

// Run the async function
takeCameraSnapshot();
