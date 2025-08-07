const Camera = require('../addon.js');

async function testClaimDevice() {
  const camera = new Camera();
  
  try {
    console.log('🔍 Enumerating devices...');
    const devices = await camera.enumerateDevices();
    console.log(`✅ Found ${devices.length} device(s):`);
    
    devices.forEach((device, index) => {
      console.log(`  ${index}: ${device.friendlyName}`);
    });
    
    if (devices.length > 0) {
      const firstDevice = devices[0];
      console.log(`\n📹 Claiming device by symbolic link...`);
      console.log(`🔗 Symbolic Link: ${firstDevice.symbolicLink}`);
      
      const result = await camera.claimDevice(firstDevice.symbolicLink);
      console.log('✅ Device claimed successfully:', result);
      
      console.log('\n📏 Getting dimensions...');
      const dimensions = camera.getDimensions();
      console.log(`📐 Camera dimensions: ${dimensions.width}x${dimensions.height}`);
      
      console.log('\n🎯 Getting supported formats...');
      const formats = camera.getSupportedFormats();
      console.log(`📋 Found ${formats.length} supported formats:`);
      formats.slice(0, 5).forEach((format, i) => {
        console.log(`  ${i + 1}. ${format.width}x${format.height} @ ${format.frameRate}fps`);
      });
      if (formats.length > 5) {
        console.log(`  ... and ${formats.length - 5} more formats`);
      }
      
      console.log('\n✅ Device is now claimed and ready for use!');
      console.log('💡 The device should remain claimed even if system goes to sleep.');
      console.log('🔗 Symbolic link is persistent across Windows sessions and reboots.');
      
    } else {
      console.log('❌ No camera devices found');
    }
    
  } catch (error) {
    console.error('❌ Error:', error.message);
  }
}

testClaimDevice();
