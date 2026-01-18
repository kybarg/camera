/// <reference types="node" />

import { EventEmitter } from "events";

/**
 * Device information returned by camera enumeration
 */
export interface DeviceInfo {
  /** Friendly name of the camera device */
  friendlyName: string;
  /** Symbolic link to the camera device */
  symbolicLink: string;
}

/**
 * Camera format information including resolution and frame rate
 */
export interface CameraFormat {
  /** Native subtype GUID string (round-trippable) */
  /** Friendly short subtype name (e.g., 'MJPEG', 'NV12', 'YUY2', 'RGB32') */
  subtype: string;
  /** Native subtype GUID string (round-trippable); present in getSupportedFormats entries */
  guid?: string;
  /** Width of the camera format in pixels */
  width: number;
  /** Height of the camera format in pixels */
  height: number;
  /** Frame rate of the camera format in frames per second (frameRate) */
  frameRate: number;
}

/**
 * Camera dimensions (current capture output)
 */
export interface CameraDimensions {
  /** Width of the current camera format in pixels */
  width: number;
  /** Height of the current camera format in pixels */
  height: number;
}

/**
 * Result of a successful operation
 */
export interface OperationResult {
  /** Whether the operation was successful */
  success: boolean;
  /** Descriptive message about the operation */
  message: string;
}

/**
 * Result of claiming a device
 */
export interface ClaimDeviceResult extends OperationResult {
  /** Symbolic link of the claimed device */
  symbolicLink: string;
}

/**
 * Result of setting a desired format
 */
export interface SetFormatResult extends OperationResult {
  /** Actual width that was set */
  actualWidth: number;
  /** Actual height that was set */
  actualHeight: number;
}

/**
 * Camera information returned by getCameraInfo()
 * This is a flexible shape; implementations may include grouped `formats` keyed by
 * subtype and optional `encoders` information. Use `any` fields for extensibility.
 */
export interface CameraInfo {
  friendlyName?: string;
  symbolicLink?: string;
  /** Optional encoders supported by the camera or driver */
  encoders?: string[];
  /** Optional grouped formats map: subtype -> { subtype, resolutions: CameraFormat[] } */
  formats?: CameraFormat[];
  // legacy fields may also be present (supportedResolutions, supportedResolutionsBySubtype)
  [k: string]: any;
}

/**
 * Camera events interface
 */
export interface CameraEvents {
  /** Emitted when a new frame/sample is captured.
   *
   * The payload is a Node.js Buffer containing the raw contiguous sample bytes
   * returned by the OS/driver for the negotiated media subtype. For example:
   * - MJPEG/MJPEG frames: JPEG bitstream bytes
   * - NV12/YUY2: raw YUV planar/interleaved bytes
   * - RGB32/RGB24: raw pixel bytes
   *
   * The exact layout depends on the chosen CameraFormat (check the `subtype`/`guid`).
   */
  frame: (frameData: Buffer) => void;
}

/**
 * Main Camera class for interacting with camera devices
 * Extends EventEmitter to provide frame events
 */
export declare class Camera extends EventEmitter {
  constructor();

  /**
   * Enumerate all available camera devices
   * @returns Promise that resolves to an array of device information
   * @throws Error if enumeration fails
   */
  enumerateDevices(): Promise<DeviceInfo[]>;

  /**
   * Claim a specific camera device for exclusive use
   * @param symbolicLink - The symbolic link of the device to claim
   * @returns Promise that resolves to claim result
   * @throws Error if device cannot be claimed
   */
  claimDevice(symbolicLink: string): Promise<ClaimDeviceResult>;

  /**
   * Release the currently claimed camera device
   * @returns Promise that resolves to operation result
   * @throws Error if device cannot be released
   */
  releaseDevice(): Promise<OperationResult>;

  /**
   * Get all supported camera formats for the claimed device
   * @returns Promise that resolves to an array of supported formats
   * @throws Error if no device is claimed or if formats cannot be retrieved
   */
  getSupportedFormats(): Promise<CameraFormat[]>;

  /**
   * Get rich camera information for the claimed device.
   * Returns a flexible object that includes at least `friendlyName` and `symbolicLink`,
   * and may include grouped `formats` or legacy fields.
   */
  getCameraInfo(): Promise<CameraInfo>;

  /**
   * Set the desired camera format (resolution and frame rate)
   * The camera will select the closest matching format if exact match is not available
   * @param width - Desired width in pixels
   * @param height - Desired height in pixels
   * @param frameRate - Desired frame rate (frameRate)
   * @returns Promise that resolves to the actual format that was set
   * @throws Error if format cannot be set
   */
  /**
   * Set desired format using an explicit native subtype identifier (string) plus resolution and frameRate.
   * The subtype may be a common name like 'NV12', 'RGB24', 'RGB32', 'MJPG' or a GUID string.
   */
  /**
   * Set desired format using an explicit native subtype identifier (string) plus resolution and frameRate.
   * The subtype may be a common name like 'NV12', 'RGB24', 'RGB32', 'MJPG' or a GUID string.
   */
  // Accept a single CameraFormat object: { subtype, width, height, frameRate }
  setFormat(format: CameraFormat): Promise<SetFormatResult>;

  /**
   * Set the output format for frame conversion using Windows Media Foundation.
   * When set, captured frames will be converted from the native camera format
   * to the specified output format before being delivered to the 'frame' event.
   *
   * Supported output formats: 'RGB32', 'RGB24', 'NV12', 'YUY2', 'UYVY', 'IYUV', or a GUID string.
   *
   * @param format - Output format string (e.g., 'RGB32', 'NV12') or null/undefined to disable conversion
   * @returns Promise that resolves when the output format is set
   * @throws Error if the format is not supported or conversion cannot be configured
   *
   * @example
   * // Enable conversion to RGB32 (BGRA)
   * await camera.setOutputFormat('RGB32');
   *
   * // Disable conversion (return raw native frames)
   * await camera.setOutputFormat(null);
   */
  setOutputFormat(format?: string | null): Promise<OperationResult>;

  /**
   * Get the current camera dimensions
   * @returns Current camera dimensions
   */
  getDimensions(): CameraDimensions;

  /**
   * Start capturing frames from the camera.
   * Frames are emitted via the 'frame' event with Buffer payloads.
   * The JS wrapper passes an internal frame emitter callback into native code,
   * so no callback parameter is required here.
   * @returns Promise that resolves when capture starts successfully
   */
  startCapture(): Promise<OperationResult>;

  /**
   * Stop capturing frames from the camera
   * @returns Promise that resolves when capture stops successfully
   * @throws Error if capture cannot be stopped
   */
  stopCapture(): Promise<OperationResult>;

  /**
   * Check if the camera is currently capturing
   * @returns true if capturing, false otherwise
   */
  isCapturing(): boolean;

  // EventEmitter overrides for better TypeScript support
  on<K extends keyof CameraEvents>(event: K, listener: CameraEvents[K]): this;
  on(event: string | symbol, listener: (...args: any[]) => void): this;

  once<K extends keyof CameraEvents>(event: K, listener: CameraEvents[K]): this;
  once(event: string | symbol, listener: (...args: any[]) => void): this;

  emit<K extends keyof CameraEvents>(
    event: K,
    ...args: Parameters<CameraEvents[K]>
  ): boolean;
  emit(event: string | symbol, ...args: any[]): boolean;

  off<K extends keyof CameraEvents>(event: K, listener: CameraEvents[K]): this;
  off(event: string | symbol, listener: (...args: any[]) => void): this;

  removeListener<K extends keyof CameraEvents>(
    event: K,
    listener: CameraEvents[K],
  ): this;
  removeListener(
    event: string | symbol,
    listener: (...args: any[]) => void,
  ): this;
}

// For CommonJS usage
declare const Camera: {
  new (): Camera;
};

export = Camera;
