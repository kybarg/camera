const bindings = require('bindings');
const native = bindings('addon.node');

// Single bench runner that calls both native benchmarks and prints a merged result.
function runAllBench(width, height, testConfigs = [{ iters: 50, repeat: 5 }]) {
  const results = [];
  for (const cfg of testConfigs) {
    const iters = cfg.iters || 50;
    const repeat = cfg.repeat || 5;
    console.log(`Running benchmark ${width}x${height} (${iters} iters x ${repeat} repeats)`);

    if (typeof native.runRgb32Bench !== 'function') {
      console.error('native.runRgb32Bench is not available');
      return;
    }

  const rgbRes = native.runRgb32Bench(width, height, iters, repeat);

  // rgbRes now contains baseline, optimized, simd timings and a `cpu` object.
  const cpu = rgbRes.cpu || {};

  // Build merged object with cpu set first so it appears first in JSON output
  const merged = {};
  merged.cpu = cpu;
  merged.width = rgbRes.width || width;
  merged.height = rgbRes.height || height;
  merged.pixels = rgbRes.pixels || (width * height);
  merged.iters = iters;
  merged.repeat = repeat;
  merged.baseline_ms = rgbRes.baseline_ms;
  merged.optimized_ms = rgbRes.optimized_ms;
  merged.simd_ms = rgbRes.simd_ms;

  results.push(merged);
  }

  console.log(JSON.stringify(results, null, 2));
}

// CLI: node bench.js [width height [iters repeat]] | all
const args = process.argv.slice(2);
const presets = [ [640,480], [1280,720], [1920,1080] ];
const defaultTestConfigs = [{ iters: 50, repeat: 5 }, { iters: 200, repeat: 3 }];

if (args.length === 0) {
  for (const [w,h] of presets) runAllBench(w,h, defaultTestConfigs);
} else if (args[0] === 'all') {
  for (const [w,h] of presets) runAllBench(w,h, defaultTestConfigs);
} else if (args.length >= 2) {
  const w = parseInt(args[0], 10);
  const h = parseInt(args[1], 10);
  if (!Number.isFinite(w) || !Number.isFinite(h)) {
    console.error('Invalid width/height args');
  } else if (args.length >= 4) {
    const iters = parseInt(args[2], 10);
    const repeat = parseInt(args[3], 10);
    runAllBench(w, h, [{ iters, repeat }]);
  } else {
    runAllBench(w, h, defaultTestConfigs);
  }
} else {
  console.error('Usage: node bench.js [width height [iters repeat]] | all');
}
