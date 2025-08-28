const bindings = require('bindings');
const native = bindings('addon.node');

// Single bench runner that calls both native benchmarks and prints a merged result.
function runAllBench(width, height, testConfigs = [{ iters: 50, repeat: 5 }]) {
  const results = [];
  // compute total work for progress reporting
  const totalRuns = testConfigs.length;
  let completed = 0;
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
  // RGB24 timings (if provided by native bench)
  merged.rgb24_baseline_ms = rgbRes.rgb24_baseline_ms;
  merged.rgb24_optimized_ms = rgbRes.rgb24_optimized_ms;
  merged.rgb24_simd_ms = rgbRes.rgb24_simd_ms;

    results.push(merged);
    completed += 1;
    const overallIndex = completed;
    console.log(`Completed ${overallIndex}/${totalRuns} for ${width}x${height}`);
  }

  return results;
}

// CLI: node bench.js [width height [iters repeat]] | all
const args = process.argv.slice(2);
const presets = [ [640,480], [1280,720], [1920,1080] ];
const defaultTestConfigs = [{ iters: 50, repeat: 5 }, { iters: 200, repeat: 3 }];

// Run all and aggregate results
async function main() {
  const aggregate = [];

  const runSizes = (args.length === 0 || args[0] === 'all') ? presets : [[parseInt(args[0],10), parseInt(args[1],10)]];
  const runConfigs = (args.length >= 4) ? [{ iters: parseInt(args[2],10), repeat: parseInt(args[3],10) }] : defaultTestConfigs;

  for (const [w,h] of runSizes) {
    if (!Number.isFinite(w) || !Number.isFinite(h)) {
      console.error('Invalid preset size', w, h);
      continue;
    }
    const res = runAllBench(w, h, runConfigs);
    aggregate.push(...res.map(r => Object.assign({ size: `${w}x${h}` }, r)));
  }

  // Print aggregated table (performance-only) and CPU features separately
  if (aggregate.length === 0) {
    console.error('No results to display');
    return;
  }

    // CPU feature summary: take from first result's cpu object
    const cpu = aggregate[0].cpu || {};
    console.log('\nCPU features:');
    const cpuFlags = [ 'avx2', 'ssse3', 'sse2', 'sse3', 'sse4_1', 'avx', 'bmi2' ];
    for (const f of cpuFlags) {
      console.log(`  ${f}: ${cpu[f] ? 'yes' : 'no'}`);
    }

    // --- RGB32 table (baseline | optimized | simd) ---
  const headers32 = ['size', 'iters', 'repeat', 'base_ms', 'opt_ms', 'simd_ms', 'opt%', 'simd%'];
    const rows32 = aggregate.map(r => {
      const base = Number.isFinite(r.baseline_ms) ? r.baseline_ms : NaN;
      const opt = Number.isFinite(r.optimized_ms) ? r.optimized_ms : NaN;
      const simd = Number.isFinite(r.simd_ms) ? r.simd_ms : NaN;
      const optBoost = (Number.isFinite(base) && Number.isFinite(opt) && base > 0) ? ((base - opt) / base) * 100.0 : NaN;
      const simdBoost = (Number.isFinite(base) && Number.isFinite(simd) && base > 0) ? ((base - simd) / base) * 100.0 : NaN;
      return [
        r.size,
        String(r.iters),
        String(r.repeat),
        Number.isFinite(base) ? base.toFixed(4) : '',
        Number.isFinite(opt) ? opt.toFixed(4) : '',
        Number.isFinite(simd) ? simd.toFixed(4) : '',
        Number.isFinite(optBoost) ? optBoost.toFixed(2) + '%' : '',
        Number.isFinite(simdBoost) ? simdBoost.toFixed(2) + '%' : ''
      ];
    });

    // --- RGB24 table (baseline | optimized) ---
  const headers24 = ['size', 'iters', 'repeat', 'base_ms', 'opt_ms', 'simd_ms', 'opt%', 'simd%'];
    const rows24 = aggregate.map(r => {
      const base24 = Number.isFinite(r.rgb24_baseline_ms) ? r.rgb24_baseline_ms : NaN;
      const opt24 = Number.isFinite(r.rgb24_optimized_ms) ? r.rgb24_optimized_ms : NaN;
      const simd24 = Number.isFinite(r.rgb24_simd_ms) ? r.rgb24_simd_ms : NaN;
      const opt24Boost = (Number.isFinite(base24) && Number.isFinite(opt24) && base24 > 0) ? ((base24 - opt24) / base24) * 100.0 : NaN;
      const simd24Boost = (Number.isFinite(base24) && Number.isFinite(simd24) && base24 > 0) ? ((base24 - simd24) / base24) * 100.0 : NaN;
      return [
        r.size,
        String(r.iters),
        String(r.repeat),
        Number.isFinite(base24) ? base24.toFixed(4) : '',
        Number.isFinite(opt24) ? opt24.toFixed(4) : '',
        Number.isFinite(simd24) ? simd24.toFixed(4) : '',
        Number.isFinite(opt24Boost) ? opt24Boost.toFixed(2) + '%' : '',
        Number.isFinite(simd24Boost) ? simd24Boost.toFixed(2) + '%' : ''
      ];
    });

    function pad(s, n) { return String(s).padEnd(n); }

    // print helper
    function printTable(title, headers, rows) {
      const colWidths = headers.map((h, i) => Math.max(h.length, ...rows.map(r => (r[i]||'').length)));
      const headerLine = headers.map((h,i)=>pad(h, colWidths[i])).join(' | ');
      const sepLine = colWidths.map((w)=>'-'.repeat(w)).join('-|-');
      console.log(`\n${title}`);
      console.log(headerLine);
      console.log(sepLine);
      for (const row of rows) {
        console.log(row.map((c,i)=>pad(c, colWidths[i])).join(' | '));
      }
    }

    printTable('Aggregated results (RGB32 performance):', headers32, rows32);
    printTable('Aggregated results (RGB24 performance):', headers24, rows24);
}

main();
