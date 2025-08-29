const Camera = require('../addon.js');

// Usage: node examples/measure_fps.js [durationMs] [formatIndex]
// durationMs: milliseconds to measure (default 5000)
// formatIndex: index into getSupportedFormats() to set before capture (optional)

const durationMs = parseInt(process.argv[2], 10) || 5000;
const formatIndex = typeof process.argv[3] !== 'undefined' ? parseInt(process.argv[3], 10) : null;

function hrNowMs() {
  return Number(process.hrtime.bigint()) / 1e6;
}

async function measure() {
  const cam = new Camera();

  try {
    const devices = await cam.enumerateDevices();
    if (!devices || devices.length === 0) {
      console.error('No devices found');
      return;
    }

    console.log('Claiming device:', devices[0].friendlyName || devices[0].symbolicLink);
    await cam.claimDevice(devices[0].symbolicLink);

    const formats = await cam.getSupportedFormats();
    console.log('Supported formats:', formats.length);
    if (formats.length === 0) {
      console.error('No supported formats');
    } else {
      if (formatIndex !== null && formats[formatIndex]) {
        const f = formats[formatIndex];
        console.log('Setting format to', f);
        await cam.setFormat(f);
      } else {
        console.log('No format index given; using current/default format');
      }
    }

    const dims = await cam.getDimensions();
    console.log('getDimensions:', dims);

    let timestamps = [];

    const onFrame = (buffer) => {
      timestamps.push(hrNowMs());
    };

    cam.on('frame', onFrame);

    console.log(`Starting capture for ${durationMs}ms...`);
    await cam.startCapture();

    await new Promise((res) => setTimeout(res, durationMs));

    console.log('Stopping capture...');
    await cam.stopCapture();
    cam.removeListener('frame', onFrame);

    // compute FPS and stats
    const frameCount = timestamps.length;
    if (frameCount < 2) {
      console.log('Frames captured:', frameCount);
    } else {
      const intervals = [];
      for (let i = 1; i < timestamps.length; ++i) {
        intervals.push(timestamps[i] - timestamps[i - 1]);
      }
      const sum = intervals.reduce((a, b) => a + b, 0);
      const avgMs = sum / intervals.length;
      const fps = 1000 / avgMs;
      // stddev
      const variance = intervals.reduce((a, b) => a + Math.pow(b - avgMs, 2), 0) / intervals.length;
      const stddev = Math.sqrt(variance);

      console.log('Frames captured:', frameCount);
      console.log('Average inter-frame (ms):', avgMs.toFixed(2));
      console.log('Estimated FPS:', fps.toFixed(2));
      console.log('Inter-frame stddev (ms):', stddev.toFixed(2));
    }

    await cam.releaseDevice();
    console.log('Device released.');
  } catch (err) {
    console.error('Error during measurement:', err);
    try { await cam.releaseDevice(); } catch (e) {}
  }
}

measure();
