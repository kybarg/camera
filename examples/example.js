const fs = require('node:fs');
const path = require('node:path');
const sharp = require('sharp');
const { exec } = require('node:child_process');
const Camera = require('../addon.js');

async function takeCameraSnapshot() {
  const camera = new Camera();
  let cameraWidth = 640;
  let cameraHeight = 480;
  let snapshotSaved = false;

  try {
    const devices = await camera.enumerateDevices();
    if (!devices || devices.length === 0) {
      throw new Error('No camera devices found');
    }

    console.log('Claiming device:', devices[0].friendlyName || devices[0].symbolicLink);
    await camera.claimDevice(devices[0].symbolicLink);

    const formats = await camera.getSupportedFormats();
    console.log(`Found ${formats.length} supported formats`);

    // Pick best format: largest pixel count, tie-break by frameRate
    let chosen = null;
    for (const f of formats) {
      if (!chosen) { chosen = f; continue; }
      const p = f.width * f.height;
      const cp = chosen.width * chosen.height;
      if (p > cp || (p === cp && (f.frameRate || 0) > (chosen.frameRate || 0))) chosen = f;
    }

    if (chosen) {
      console.log(`Setting format to ${chosen.subtype || chosen.guid} ${chosen.width}x${chosen.height}@${chosen.frameRate}`);
      try {
        await camera.setFormat(chosen);
        console.log('Format set');
      } catch (e) {
        console.warn('Failed to set format, continuing with default:', e && e.message ? e.message : e);
      }
    }

    const dims = camera.getDimensions();
    if (dims && dims.width) {
      cameraWidth = dims.width;
      cameraHeight = dims.height;
    }

    camera.on('frame', async (frameBuffer) => {
      if (snapshotSaved) return;
      if (!frameBuffer || frameBuffer.length === 0) return;
      snapshotSaved = true;

      try {
        const snapshotsDir = path.join(__dirname, '../snapshots');
        if (!fs.existsSync(snapshotsDir)) fs.mkdirSync(snapshotsDir, { recursive: true });

        const w = cameraWidth;
        const h = cameraHeight;
        const expectedRGBA = w * h * 4;
        const expectedRGB24 = w * h * 3;
        const expectedNV12 = Math.floor(w * h * 1.5);

        const isJPEG = frameBuffer.length >= 2 && frameBuffer[0] === 0xff && frameBuffer[1] === 0xd8;
        const filename = `snapshot_${Date.now()}.jpg`;
        const filepath = path.join(snapshotsDir, filename);

        if (frameBuffer.length === expectedRGBA) {
          await sharp(frameBuffer, { raw: { width: w, height: h, channels: 4 } }).jpeg({ quality: 90 }).toFile(filepath);
        } else if (frameBuffer.length === expectedRGB24) {
          const rgba = Buffer.alloc(expectedRGBA);
          for (let src = 0, dst = 0; src < frameBuffer.length; src += 3, dst += 4) {
            rgba[dst] = frameBuffer[src];
            rgba[dst + 1] = frameBuffer[src + 1];
            rgba[dst + 2] = frameBuffer[src + 2];
            rgba[dst + 3] = 255;
          }
          await sharp(rgba, { raw: { width: w, height: h, channels: 4 } }).jpeg({ quality: 90 }).toFile(filepath);
        } else if (frameBuffer.length === expectedNV12) {
          throw new Error('NV12 frames are not supported by this example');
        } else if (isJPEG) {
          fs.writeFileSync(filepath, frameBuffer);
        } else {
          throw new Error('Unknown frame format');
        }

        console.log('Snapshot saved:', filename);

        await camera.stopCapture();
        console.log('Capture stopped');

        const command = process.platform === 'win32' ? `start "" "${filepath}"` : process.platform === 'darwin' ? `open "${filepath}"` : `xdg-open "${filepath}"`;
        exec(command, async (error) => {
          if (error) console.error('Error opening image:', error.message || error);
          try { await camera.releaseDevice(); } catch (e) { /* ignore */ }
          setTimeout(() => process.exit(0), 2000);
        });
      } catch (err) {
        console.error('Error processing frame:', err);
        try { await camera.stopCapture(); } catch (e) {}
        try { await camera.releaseDevice(); } catch (e) {}
        process.exit(1);
      }
    });

    await camera.startCapture();
    console.log('Waiting for first frame...');
  } catch (err) {
    console.error('Example error:', err);
    try { await camera.releaseDevice(); } catch (e) {}
    process.exit(1);
  }
}

takeCameraSnapshot();
