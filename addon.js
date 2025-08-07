const addon = require("bindings")("addon.node");
const EventEmitter = require("events");

class Camera extends EventEmitter {
  constructor() {
    super(); // Call EventEmitter constructor
    
    // Create native camera instance
    this._nativeCamera = new addon.Camera();
    
    // Bind async methods from native camera
    this.enumerateDevices = this._nativeCamera.enumerateDevicesAsync.bind(this._nativeCamera);
    this.claimDevice = this._nativeCamera.claimDeviceAsync.bind(this._nativeCamera);
    
    // Bind other native methods
    this.getSupportedFormats = this._nativeCamera.getSupportedFormats.bind(this._nativeCamera);
    this.setDesiredFormat = this._nativeCamera.setDesiredFormat.bind(this._nativeCamera);
    this.getDimensions = this._nativeCamera.getDimensions.bind(this._nativeCamera);
    this.stopCapture = this._nativeCamera.stopCaptureN.bind(this._nativeCamera);

    this.tmp = null;
    this._isCapturing = false;

    // Set up frame event emitter function for native code
    this._setupFrameEventEmitter();
  }

  _setupFrameEventEmitter() {
    // This function will be called by native code for each frame
    this._frameEventEmitter = (frameData) => {
      this.tmp = frameData; // Keep frame in scope
      this.emit('frame', frameData);
    };
  }

  // Modern async startCapture method with events
  async startCapture() {
    if (this._isCapturing) {
      throw new Error('Capture is already in progress');
    }

    this._isCapturing = true;
    
    try {
      // Pass the frame event emitter to the native method
      const result = await this._nativeCamera.startCaptureAsync(this._frameEventEmitter);
      return result;
    } catch (error) {
      this._isCapturing = false;
      throw error;
    }
  }

  // Override stopCapture to update internal state
  stopCapture() {
    this._isCapturing = false;
    return this._nativeCamera.stopCaptureN();
  }

  // Helper method to check if capturing
  isCapturing() {
    return this._isCapturing;
  }
}

module.exports = Camera;
