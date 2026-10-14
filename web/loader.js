// Engine host thread ("main runtime thread", like the GTA port's loader.js).
// It spawns the IO worker (streaming) and the GPU worker (WebGPU), loads the
// Emscripten glue (game.js) — which runs the C++ engine's main() here. The engine
// then drives rendering: it uploads the scene once and writes a per-frame MVP
// matrix into a SharedArrayBuffer that the GPU worker reads (zero-copy, the same
// SharedArrayBuffer + Atomics sync the GTA port uses for its cross-thread input).
'use strict';

let started = false;
self.onmessage = (ev) => {
  if (started) return;
  started = true;
  const m = ev.data;
  const base = m.base || '';
  const bc = new BroadcastChannel('progress');

  // Shared per-frame state: a 4x4 MVP matrix (64 bytes) + a frame counter (4 bytes).
  const frameState = new SharedArrayBuffer(64 + 4);

  // 1. Spawn the helper workers and hand the OffscreenCanvas to the GPU worker.
  const io = new Worker(base + '/io_worker.js');
  const gpu = new Worker(base + '/wgpu_worker.js');
  gpu.postMessage({ type: 'init', canvas: m.canvas, frameState }, [m.canvas]);

  // 2. Module object read by the Emscripten glue.
  self.Module = {
    ioWorker: io,
    gpuWorker: gpu,
    frameState,
    inputState: m.inputState,
    locateFile: (p) => base + '/' + p,
    print: (t) => { console.log('[engine]', t); bc.postMessage({ log: '[engine] ' + t }); },
    printErr: (t) => { console.error('[engine]', t); bc.postMessage({ log: '[engine!] ' + t }); },
    onAbort: (what) => bc.postMessage({ error: 'abort: ' + what }),
  };

  // 3. Run the engine (blocks this worker while it streams + drives frames).
  importScripts(base + '/game.js');
};
