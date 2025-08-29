import * as fs from "node:fs";
import * as path from "node:path";
import sharp from "sharp";
import { exec } from "node:child_process";
const Camera = require("../addon.js");
import type { DeviceInfo, CameraFormat, CameraDimensions, OperationResult, CameraInfo } from "../index.d.ts";

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

    // Additional: get camera info (name/model/encoders/resolutions)
    try {
      console.log("3.1️⃣ ℹ️ Getting camera info...");
      const info: CameraInfo = await camera.getCameraInfo();
      // Native returns friendlyName, symbolicLink, model, encoders, supportedResolutions
      const name = (info && (info.name || info.friendlyName)) || "<unknown>";
      const encoders = (info && (info.encoders || [])) as string[];
      // Single canonical `formats` map: { friendlyName: { subtype, resolutions: [...] } }
      const formatsMap = (info && (info.formats || {})) as { [k: string]: any };

      console.log("   🔎 Camera info:");
      console.log(`     name: ${name}`);
      if (Array.isArray(encoders) && encoders.length) {
        console.log(`     encoders: ${encoders.join(", ")}`);
      } else {
        console.log("     encoders: <none>");
      }
      // Print canonical formats map: list each subtype and its resolutions
      const formatKeys = Object.keys(formatsMap);
      if (formatKeys.length > 0) {
        console.log("     formats (grouped by subtype):");
        for (const key of formatKeys) {
          const group = formatsMap[key] || {};
          const res = Array.isArray(group.resolutions) ? group.resolutions : [];
          console.log(
            `       ${key}  (subtype=${group.subtype || "<unknown>"})`
          );
          if (res.length === 0) {
            console.log("         <no resolutions>");
            continue;
          }
          // Show up to 6 resolutions per subtype for brevity
          for (const r of res.slice(0, 6)) {
            console.log(
              `         - ${r.width}x${r.height} @ ${r.frameRate}fps`
            );
          }
          if (res.length > 6) console.log(`         ... and ${res.length - 6} more`);
        }
      } else {
        console.log("     formats: <none>");
      }
      console.log("");
    } catch (err) {
      console.log(
        "   ⚠️ getCameraInfo() failed or is not implemented on this build:",
        err && (err as Error).message ? (err as Error).message : err
      );
      console.log("");
    }

    // Select the highest resolution format
    if (formats.length > 0) {
      console.log("4️⃣ ⚙️ Selecting highest resolution format...");

      // Find the format with the highest resolution (width * height).
      // On tie, prefer the one with the higher frameRate.
      let bestFormat: CameraFormat = formats[0];
      let maxPixels: number = bestFormat.width * bestFormat.height;
      let overallMaxFps: number = bestFormat.frameRate || 0;

      for (const format of formats) {
        const pixels: number = format.width * format.height;
        const fr: number = format.frameRate || 0;
        overallMaxFps = Math.max(overallMaxFps, fr);
        if (
          pixels > maxPixels ||
          (pixels === maxPixels && fr > (bestFormat.frameRate || 0))
        ) {
          maxPixels = pixels;
          bestFormat = format;
        }
      }

      // Compute best fps available for the selected resolution
      let bestFpsForResolution = 0;
      for (const format of formats) {
        if (format.width === bestFormat.width && format.height === bestFormat.height) {
          bestFpsForResolution = Math.max(bestFpsForResolution, format.frameRate || 0);
        }
      }

      console.log(
        `   🎯 Selected: ${bestFormat.width}x${bestFormat.height} @ ${bestFpsForResolution}fps (${maxPixels.toLocaleString()} pixels)`
      );
      console.log(`     🔢 Best fps for this resolution: ${bestFpsForResolution}fps`);
      console.log(`     🌐 Best fps overall available: ${overallMaxFps}fps`);

      try {
        await camera.setDesiredFormat(
          bestFormat.width,
          bestFormat.height,
          bestFpsForResolution
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

          const w = cameraWidth;
          const h = cameraHeight;
          const expectedRGBA = w * h * 4;
          const expectedRGB24 = w * h * 3;
          const expectedNV12 = Math.floor(w * h * 1.5);

          const isJPEG =
            frameBuffer.length >= 2 && frameBuffer[0] === 0xff && frameBuffer[1] === 0xd8;

          const filename: string = `snapshot_${Date.now()}.jpg`;
          const filepath: string = path.join(snapshotsDir, filename);

          if (frameBuffer.length === expectedRGBA) {
            console.log("🎨 Interpreting buffer as RGBA (4 channels)");
            console.log("🎨 Converting RGBA buffer to JPG...");
            await sharp(frameBuffer, {
              raw: { width: w, height: h, channels: 4 },
            })
              .jpeg({ quality: 90 })
              .toFile(filepath);
          } else if (frameBuffer.length === expectedRGB24) {
            console.log(
              "🎨 Interpreting buffer as RGB24 (3 channels). Doing JS expansion to RGBA..."
            );
            // Expand RGB24 -> RGBA (simple and safe JS fallback)
            const rgba = Buffer.alloc(expectedRGBA);
            for (let src = 0, dst = 0; src < frameBuffer.length; src += 3, dst += 4) {
              rgba[dst] = frameBuffer[src];
              rgba[dst + 1] = frameBuffer[src + 1];
              rgba[dst + 2] = frameBuffer[src + 2];
              rgba[dst + 3] = 255; // opaque alpha
            }
            console.log("🎨 Converting expanded RGBA buffer to JPG...");
            await sharp(rgba, { raw: { width: w, height: h, channels: 4 } })
              .jpeg({ quality: 90 })
              .toFile(filepath);
          } else if (frameBuffer.length === expectedNV12) {
            console.log(
              "⚠️ Detected NV12 (YUV 4:2:0) frame format. This example does not implement NV12->RGBA conversion in JS."
            );
            console.log(
              "     Options: 1) Select an RGB format on the camera, 2) Add a native NV12->RGBA converter, or 3) handle NV12 decoding in your app."
            );
            throw new Error("NV12 frames are not supported by this example");
          } else if (isJPEG) {
            console.log("🎯 Detected MJPEG/JPEG frame. Saving directly as JPG...");
            fs.writeFileSync(filepath, frameBuffer);
          } else {
            // Unknown format: try to be helpful by reporting expected sizes
            console.log("⚠️ Unknown frame buffer layout. Sizes:");
            console.log(
              `   received: ${frameBuffer.length}, expected RGBA: ${expectedRGBA}, RGB24: ${expectedRGB24}, NV12: ${expectedNV12}`
            );
            throw new Error("Unknown/unhandled frame buffer format");
          }

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
