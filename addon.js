const addon = require("bindings")("addon.node");
const EventEmitter = require("events");

class Camera extends EventEmitter {
  constructor() {
    super(); // Call EventEmitter constructor

    // Create native camera instance
    this._nativeCamera = new addon.Camera();

    // Bind async methods from native camera
    this.enumerateDevices = this._nativeCamera.enumerateDevicesAsync.bind(
      this._nativeCamera
    );
    this.claimDevice = this._nativeCamera.claimDeviceAsync.bind(
      this._nativeCamera
    );
    this.releaseDevice = this._nativeCamera.releaseDeviceAsync.bind(
      this._nativeCamera
    );

    // Bind other native methods
    this.getSupportedFormats = this._nativeCamera.getSupportedFormatsAsync.bind(
      this._nativeCamera
    );
    // Provide a normalized getCameraInfo that returns a single `formats` object
    this.getCameraInfo = async () => {
      const info = await this._nativeCamera.getCameraInfoAsync();

      // If native already provides `formats`, return as-is
      if (info && info.formats) return info;

      const out = Object.assign({}, info);

      try {
        // Legacy shapes: supportedResolutionsBySubtype, supportedResolutions, resolutions
        if (info && info.supportedResolutionsBySubtype) {
          // If it's already grouped, map it to `formats` (keys kept)
          out.formats = info.supportedResolutionsBySubtype;
          return out;
        }

        const flat = info && (info.supportedResolutions || info.resolutions);
        if (Array.isArray(flat)) {
          const formats = {};
          for (const r of flat) {
            const subtype = r.subtype || '<unknown>';
            const key = subtype;
            if (!formats[key]) formats[key] = { subtype: subtype, resolutions: [] };
            formats[key].resolutions.push({ width: r.width, height: r.height, frameRate: r.frameRate });
          }
          out.formats = formats;
        }
      } catch (e) {
        // ignore and return original info
      }

      return out;
    };
    // Expose native subtype-aware format setter
    this.setFormat = this._nativeCamera.setFormatAsync.bind(
      this._nativeCamera
    );
    this.getDimensions = this._nativeCamera.getDimensions.bind(
      this._nativeCamera
    );

  this._isCapturing = false;

    // Frame event emitter used by native code
    this._frameEventEmitter = (frameData) => {
      // Keep reference alive until event handlers run
      this.emit("frame", frameData);
    };
  }

  // Modern async startCapture method with events
  async startCapture() {
    if (this._isCapturing) {
      throw new Error("Capture is already in progress");
    }

    this._isCapturing = true;

    try {
      // Pass the frame event emitter to the native method
      const result = await this._nativeCamera.startCaptureAsync(
        this._frameEventEmitter
      );
      return result;
    } catch (error) {
      this._isCapturing = false;
      throw error;
    }
  }

  // Override stopCapture to update internal state
  async stopCapture() {
    this._isCapturing = false;
    try {
      const result = await this._nativeCamera.stopCaptureAsync();
      return result;
    } catch (error) {
      throw error;
    }
  }

  // Helper method to check if capturing
  isCapturing() {
    return this._isCapturing;
  }
}

module.exports = Camera;
