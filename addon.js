const addon = require("bindings")("addon.node");
const fs = require("fs");
// const sharp = require("sharp");

const camera1 = new addon.Camera();
console.log("camera1", camera1.enumerateDevices());
console.log("camera1", camera1.selectDevice(0));
camera1.startCapture((error, response) => {
  console.error("camera1", error);
  console.log("camera1", response);
});

// const camera2 = new addon.Camera();

// console.log("camera2", camera2.enumerateDevices());

// console.log("camera2", camera2.selectDevice(0));

// console.log(addon);
// const devices = addon.enumerateDevices();
// console.log(devices);

// // console.log(devices[0].symbolicLink)

// let i = 0;
// addon.startCapture(devices[0].symbolicLink, (result) => {
//   // console.log("startCapture", result);

//   // console.log(typeof result.data)

//   if (i % 20 == 0) {
//     sharp(Buffer.from(result.data), {
//       raw: {
//         channels: 3,
//         width: result.width,
//         height: result.height,
//       },
//     }).toFile("output.png", (err, info) => {
//       if (err) {
//         console.error("Error saving the image:", err);
//       } else {
//         console.log("Image saved successfully:", info);
//       }
//     });
//   }

//   i++;
// });

// console.log("rest");

// function callback(data) {
//   console.log(data);
// }

// let some = addon.start()(callback, 2);

// let some2 = addon.start(callback, 3);

// setTimeout(() => {
//   some = null;
// }, 5000);

// Code here will continue to execute while the C++ addon runs in a loop
console.log("Code after starting the loop...");
