const Camera = require('../addon.js');

async function delay(ms) { return new Promise(r => setTimeout(r, ms)); }

async function runCycles(cycles = 10, runMs = 500) {
  const cam = new Camera();

  try {
    const devices = await cam.enumerateDevices();
    if (!devices || devices.length === 0) {
      console.error('No devices found');
      return;
    }

    await cam.claimDevice(devices[0].symbolicLink);
    console.log('Device claimed');

    let success = 0;

    for (let i = 0; i < cycles; ++i) {
      console.log(`Cycle ${i+1}/${cycles}: starting capture`);
      try {
        const startRes = await cam.startCapture();
        console.log(' start result:', startRes);
      } catch (e) {
        console.error(' start failed:', e);
        break;
      }

      // wait a short while while capturing
      await delay(runMs);

      console.log(`Cycle ${i+1}: stopping capture`);
      try {
        const stopRes = await cam.stopCapture();
        console.log(' stop result:', stopRes);
        success++;
      } catch (e) {
        console.error(' stop failed:', e);
        break;
      }

      // small pause between cycles
      await delay(200);
    }

    console.log(`Completed ${success}/${cycles} successful start/stop cycles`);

    console.log('Releasing device');
    await cam.releaseDevice();
    console.log('Device released');
  } catch (err) {
    console.error('Test error:', err);
  }
}

runCycles(5, 400).then(() => console.log('done'));
