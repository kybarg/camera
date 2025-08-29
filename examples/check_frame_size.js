const Camera = require('../addon.js');

function expectedSizes(width, height, subtype) {
  // For compressed subtypes (MJPEG, etc.) the frame buffer length is variable
  // and cannot be predicted from width/height. Return null to indicate
  // variable/compressed frames.
  if (subtype && /(MJP|MJPEG|MJPG|JPEG|H264|AVC1|HEVC|H265|X264)/i.test(subtype)) {
    return null;
  }

  const rgba = width * height * 4; // RGB32 / RGBA
  const rgb24 = width * height * 3; // RGB24
  const nv12 = (width * height * 3) / 2; // NV12 (Y plane + interleaved UV)
  return { rgba, rgb24, nv12 };
}

function formatToString(f) {
  if (!f) return '<none>';
  const subtype = f.subtype || f.guid || '<unknown>';
  const w = f.width || '?';
  const h = f.height || '?';
  const fr = (typeof f.frameRate !== 'undefined') ? f.frameRate : '?';
  return `${subtype} ${w}x${h}@${fr}`;
}

async function runTest() {
  const cam = new Camera();
  try {
    const devs = await cam.enumerateDevices();
    if (!devs || devs.length === 0) {
      console.error('No devices found');
      return;
    }

    console.log('Claiming device:', devs[0].friendlyName || devs[0].symbolicLink);
    await cam.claimDevice(devs[0].symbolicLink);

    const formats = await cam.getSupportedFormats();
    console.log('Supported formats count:', formats.length);
    if (formats.length === 0) {
      console.error('No supported formats reported');
      await cam.releaseDevice();
      return;
    }

  // Pick first supported format (you can change index to test different formats)
  const chosen = formats[0];
  console.log('Setting format to', formatToString(chosen));

  await cam.setFormat(chosen);

    const dims = await cam.getDimensions();
    console.log('getDimensions reports:', dims);

  // Use the actual dimensions returned by the device and the chosen subtype/guid
  const subtypeForCheck = (chosen && (chosen.subtype || chosen.guid)) || null;
  const exp = expectedSizes(dims.width, dims.height, subtypeForCheck);
    console.log('Expected sizes (bytes):', exp);

    let frameCount = 0;
    let mismatches = 0;

    const onFrame = (buffer) => {
      frameCount++;
      const len = buffer.length;
      const matches = [];
      // If exp is null the chosen format is likely compressed (e.g., MJPEG)
      if (exp === null) {
        // Classify as compressed; report length but do not count as mismatch.
        const sub = subtypeForCheck || '<compressed>';
        console.log(`Frame ${frameCount}: buffer.length=${len} (COMPRESSED, subtype=${sub})`);
      } else {
        if (len === exp.rgba) matches.push('RGBA');
        if (len === exp.rgb24) matches.push('RGB24');
        if (len === exp.nv12) matches.push('NV12');

        console.log(`Frame ${frameCount}: buffer.length=${len} (${matches.length ? matches.join('|') : 'UNKNOWN'})`);

        if (!matches.length) {
          mismatches++;
        }
      }

      // After 5 frames stop and report
      if (frameCount >= 5) {
        cam.removeListener('frame', onFrame);
        (async () => {
          try {
            console.log('Stopping capture...');
            await cam.stopCapture();
            console.log(`Frames: ${frameCount}, mismatches: ${mismatches}`);
          } catch (e) {
            console.error('Error stopping capture:', e);
          }

          try {
            await cam.releaseDevice();
            console.log('Device released');
          } catch (e) {
            console.error('Error releasing device:', e);
          }
        })();
      }
    };

    cam.on('frame', onFrame);

    console.log('Starting capture...');
    await cam.startCapture();
  } catch (err) {
    console.error('Test error:', err);
    try { await cam.releaseDevice(); } catch {};
  }
}

runTest();
