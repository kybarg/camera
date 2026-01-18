import * as fs from "node:fs";
import * as path from "node:path";
import { exec } from "node:child_process";
const Camera = require("../addon.js");
import type { DeviceInfo, CameraFormat, OperationResult } from "../index.d.ts";

async function takeCameraSnapshot(): Promise<void> {
  const camera = new Camera();
  let frameCount = 0;
  let snapshotSaved = false;

  try {
    const devices: DeviceInfo[] = await camera.enumerateDevices();
    if (!devices || devices.length === 0) {
      throw new Error("No camera devices found");
    }

    console.log(
      "Claiming device:",
      devices[0].friendlyName || devices[0].symbolicLink,
    );
    await camera.claimDevice(devices[0].symbolicLink);

    const formats: CameraFormat[] = await camera.getSupportedFormats();
    console.log(`Found ${formats.length} supported formats`);

    // Pick best format: largest pixel count, tie-break by frameRate
    let chosen: CameraFormat | null = null;
    for (const f of formats) {
      if (!chosen) {
        chosen = f;
        continue;
      }
      const p = f.width * f.height;
      const cp = chosen.width * chosen.height;
      if (p > cp || (p === cp && (f.frameRate || 0) > (chosen.frameRate || 0)))
        chosen = f;
    }

    if (chosen) {
      console.log(
        `Setting format to ${chosen.subtype || (chosen as any).guid} ${chosen.width}x${chosen.height}@${chosen.frameRate}`,
      );
      try {
        await camera.setFormat(chosen);
        console.log("Format set");
      } catch (e: any) {
        console.warn(
          "Failed to set format, continuing with default:",
          e && e.message ? e.message : e,
        );
      }
    }

    // Set output format to MJPEG - frames will be converted automatically
    // If native format is already MJPEG, no conversion occurs (pass-through)
    await camera.setOutputFormat("MJPEG");
    console.log("Output format set to MJPEG");

    camera.on("frame", async (frameBuffer: Buffer) => {
      if (snapshotSaved) return;
      if (!frameBuffer || frameBuffer.length === 0) return;

      // Skip first 10 frames (camera warmup)
      frameCount++;
      if (frameCount < 20) return;

      snapshotSaved = true;

      try {
        const snapshotsDir: string = path.join(__dirname, "../snapshots");
        if (!fs.existsSync(snapshotsDir))
          fs.mkdirSync(snapshotsDir, { recursive: true });

        const filename = `snapshot_${Date.now()}.jpg`;
        const filepath = path.join(snapshotsDir, filename);

        // With setOutputFormat('MJPEG'), frames are always JPEG
        fs.writeFileSync(filepath, frameBuffer);
        console.log("Snapshot saved:", filename);

        await camera.stopCapture();
        console.log("Capture stopped");

        const command: string =
          process.platform === "win32"
            ? `start "" "${filepath}"`
            : process.platform === "darwin"
              ? `open "${filepath}"`
              : `xdg-open "${filepath}"`;
        exec(command, async (error: any) => {
          if (error)
            console.error("Error opening image:", error.message || error);
          try {
            await camera.releaseDevice();
          } catch (e) {
            /* ignore */
          }
          setTimeout(() => process.exit(0), 2000);
        });
      } catch (err) {
        console.error("Error processing frame:", err);
        try {
          await camera.stopCapture();
        } catch (e) {}
        try {
          await camera.releaseDevice();
        } catch (e) {}
        process.exit(1);
      }
    });

    await camera.startCapture();
    console.log("Waiting for first frame...");
  } catch (err) {
    console.error("Example error:", err);
    try {
      await camera.releaseDevice();
    } catch (e) {}
    process.exit(1);
  }
}

takeCameraSnapshot();
