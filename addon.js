const addon = require("bindings")("addon.node");
const util = require("util");

class Camera extends addon.Camera {
  constructor() {
    super(); // Call the parent constructor
    this.enumerateDevices = this.enumerateDevicesAsync; // Use the new async method
    this.selectDevice = util.promisify(this.selectDeviceN.bind(this));

    this.tmp = null;

    this.startCapture = (callback) => {
      return new Promise((resolve, reject) => {
        this.startCaptureN(
          (error, result) => {
            this.tmp = result; // to keep object in scope while capturing
            callback(error, result);
          },
          (error, success) => {
            if (error) {
              reject(error);
              return;
            }
            resolve(success);
            return;
          }
        );
      });
    };
    this.stopCapture = this.stopCaptureN;

    // Note: getSupportedFormats, setDesiredFormat, and getDimensions
    // are native methods exposed directly from C++, no need to bind them
  }
}

module.exports = Camera;
