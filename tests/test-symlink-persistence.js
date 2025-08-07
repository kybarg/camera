const Camera = require('../addon.js');
const fs = require('fs');

// File to store the symbolic link
const SYMLINK_FILE = '../last-camera-symlink.txt';

async function demonstrateSymbolicLinkPersistence() {
  const camera = new Camera();
  
  try {
    console.log('🔍 Enumerating devices...');
    const devices = await camera.enumerateDevices();
    
    if (devices.length === 0) {
      console.log('❌ No camera devices found');
      return;
    }
    
    const firstDevice = devices[0];
    console.log(`✅ Found device: ${firstDevice.friendlyName}`);
    console.log(`🔗 Symbolic Link: ${firstDevice.symbolicLink}`);
    
    // Save the symbolic link to a file
    fs.writeFileSync(SYMLINK_FILE, firstDevice.symbolicLink, 'utf8');
    console.log(`💾 Saved symbolic link to ${SYMLINK_FILE}`);
    
    // Try to claim the device using the symbolic link
    console.log('\n📹 Claiming device by symbolic link...');
    const result = await camera.claimDevice(firstDevice.symbolicLink);
    console.log('✅ Device claimed successfully:', result);
    
    // Demonstrate reading the symbolic link from file
    console.log('\n📖 Demonstrating persistence...');
    const savedSymlink = fs.readFileSync(SYMLINK_FILE, 'utf8');
    console.log(`📂 Read symbolic link from file: ${savedSymlink}`);
    
    if (savedSymlink === firstDevice.symbolicLink) {
      console.log('✅ Symbolic link matches! This link can be used to claim the same device in future sessions.');
    }
    
    console.log('\n💡 Benefits of using symbolic link:');
    console.log('  • Persistent across Windows sessions and reboots');
    console.log('  • Unique identifier for each camera device');
    console.log('  • Works even if device index changes');
    console.log('  • More reliable than device enumeration order');
    
  } catch (error) {
    console.error('❌ Error:', error.message);
  }
}

// Cleanup function
function cleanup() {
  if (fs.existsSync(SYMLINK_FILE)) {
    fs.unlinkSync(SYMLINK_FILE);
    console.log(`🧹 Cleaned up ${SYMLINK_FILE}`);
  }
}

// Run the demonstration
demonstrateSymbolicLinkPersistence().finally(() => {
  // Uncomment the next line if you want to clean up the file
  // cleanup();
});
