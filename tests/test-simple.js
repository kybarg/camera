// Simple test to check if module loads
try {
  const Camera = require('./addon.js');
  console.log('✅ Module loaded successfully');
  
  const camera = new Camera();
  console.log('✅ Camera instance created');
  
  // Test async methods that should work
  camera.enumerateDevices().then(devices => {
    console.log('✅ enumerateDevices works:', devices.length);
  }).catch(err => {
    console.error('❌ enumerateDevices failed:', err.message);
  });
  
} catch (error) {
  console.error('❌ Module loading failed:', error.message);
}
