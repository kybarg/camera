const Camera = require('./addon.js');

async function testEventBasedCapture() {
  try {
    const camera = new Camera();
    
    console.log('🔍 Testing event-based startCapture...');
    
    // Enumerate and claim device
    const devices = await camera.enumerateDevices();
    console.log(`✅ Found ${devices.length} device(s)`);
    
    if (devices.length > 0) {
      await camera.claimDevice(devices[0].symbolicLink);
      console.log('✅ Device claimed successfully');
      
      let frameCount = 0;
      
      // Set up frame event listener
      camera.on('frame', (frameData) => {
        frameCount++;
        console.log(`📹 Frame ${frameCount}: ${frameData.width}x${frameData.height} (${frameData.buffer?.length || 0} bytes)`);
        
        // Stop after 10 frames
        if (frameCount >= 10) {
          console.log('🛑 Stopping capture...');
          camera.stopCapture();
          process.exit(0);
        }
      });
      
      // Set up error event listener
      camera.on('error', (error) => {
        console.error('❌ Frame error:', error);
      });
      
      // Start async capture
      console.log('🎬 Starting event-based capture...');
      const result = await camera.startCapture();
      console.log('✅ Capture started:', result);
      
    } else {
      console.log('❌ No camera devices found');
    }
    
  } catch (error) {
    console.error('❌ Test failed:', error);
  }
}

testEventBasedCapture();
