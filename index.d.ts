/// <reference types="node" />

import { EventEmitter } from 'events';

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
  /** Width of the camera format in pixels */
  width: number;
  /** Height of the camera format in pixels */
  height: number;
  /** Frame rate of the camera format in frames per second */
  frameRate: number;
}

/**
 * Camera dimensions
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
 * Camera events interface
 */
export interface CameraEvents {
  /** Emitted when a new frame is captured. frameData is a Buffer containing RGBA pixel data */
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
   * Set the desired camera format (resolution and frame rate)
   * The camera will select the closest matching format if exact match is not available
   * @param width - Desired width in pixels
   * @param height - Desired height in pixels
   * @param frameRate - Desired frame rate in fps
   * @returns Promise that resolves to the actual format that was set
   * @throws Error if format cannot be set
   */
  setDesiredFormat(width: number, height: number, frameRate: number): Promise<SetFormatResult>;

  /**
   * Get the current camera dimensions
   * @returns Current camera dimensions
   */
  getDimensions(): CameraDimensions;

  /**
   * Start capturing frames from the camera
   * Frames are emitted as 'frame' events with RGBA buffer data
   * @returns Promise that resolves when capture starts successfully
   * @throws Error if capture cannot be started or if already capturing
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

  emit<K extends keyof CameraEvents>(event: K, ...args: Parameters<CameraEvents[K]>): boolean;
  emit(event: string | symbol, ...args: any[]): boolean;

  off<K extends keyof CameraEvents>(event: K, listener: CameraEvents[K]): this;
  off(event: string | symbol, listener: (...args: any[]) => void): this;

  removeListener<K extends keyof CameraEvents>(event: K, listener: CameraEvents[K]): this;
  removeListener(event: string | symbol, listener: (...args: any[]) => void): this;
}

// For CommonJS usage
declare const Camera: {
  new (): Camera;
};

export = Camera;
