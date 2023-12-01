// const fs = require("node:fs");
const Camera = require("./addon.js");

try {
  let snapshot;

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
        console.log(1);
      });
    })
    .then(() => {
      console.log("4. Start Capture");
    })
    .catch(console.error);

  console.log("1. Console log");

  setInterval(() => {
    try {
      console.log({ snapshot });

      // TO DO - HERE WHEN TRYING TO uSE APP EXITS
    } catch (error) {
      console.error(error);
    }
    // try {
    //   fs.writeFileSync("data.bin", snapshot.buffer);
    // } catch (error) {
    //   console.log(error);
    // }
  }, 2000);
} catch (error) {
  console.error(error);
}
