const addon = require("bindings")("addon.node");
const util = require("util");

class Camera extends addon.Camera {
  constructor() {
    super(); // Call the parent constructor
    this.enumerateDevices = util.promisify(this.enumerateDevicesN.bind(this));
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
  }
}

module.exports = Camera;
