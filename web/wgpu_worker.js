// GPU worker: WebGPU rendering backend (mirrors the GTA port's wgpu_worker.js).
//
// Protocol (postMessage):
//   in:  { type:'init', canvas }         -- OffscreenCanvas (transferred)
//   in:  { type:'scene', positions, uvs, indices, texW, texH, texData }
//   out: { type:'ready' } / { type:'error', error }
//
// Once the scene arrives it renders a rotating textured quad with its own
// requestAnimationFrame loop. (Per-frame draw commands from the engine are the
// next step; this first version proves the stream -> GPU upload -> present path.)
'use strict';

let device, ctx, format;
let pipeline, bindGroup, uniformBuffer, vertexBuffer, indexBuffer;
let indexCount = 0;
let canvasW = 640, canvasH = 480;
let pendingScene = null;
let frameMat = null;      // Float32Array(16) over the shared frame state
let frameCounter = null;  // Int32Array(1) over the shared frame state

// ---- column-major 4x4 helpers (m[col*4 + row], matching the C++ side) ----
function m4Identity() {
  const m = new Float32Array(16);
  m[0] = m[5] = m[10] = m[15] = 1;
  return m;
}
function m4Perspective(fovy, aspect, near, far) {
  const m = new Float32Array(16);
  const t = 1 / Math.tan(fovy / 2);
  m[0] = t / aspect;
  m[5] = t;
  m[10] = -(far + near) / (far - near);
  m[11] = -1;
  m[14] = -(2 * far * near) / (far - near);
  m[15] = 0;
  return m;
}
function m4Translate(x, y, z) {
  const m = m4Identity();
  m[12] = x; m[13] = y; m[14] = z;
  return m;
}
function m4RotateY(a) {
  const m = m4Identity();
  const c = Math.cos(a), s = Math.sin(a);
  m[0] = c;  m[2] = s;
  m[8] = -s; m[10] = c;
  return m;
}
function m4RotateX(a) {
  const m = m4Identity();
  const c = Math.cos(a), s = Math.sin(a);
  m[5] = c;  m[6] = -s;
  m[9] = s;  m[10] = c;
  return m;
}
function m4Mul(a, b) {
  const m = new Float32Array(16);
  for (let i = 0; i < 4; i++) {
    for (let j = 0; j < 4; j++) {
      let s = 0;
      for (let k = 0; k < 4; k++) s += a[k * 4 + j] * b[i * 4 + k];
      m[i * 4 + j] = s;
    }
  }
  return m;
}

const VERT = `
struct Uniforms { mvp : mat4x4f };
@group(0) @binding(0) var<uniform> u : Uniforms;

struct VSOut {
  @builtin(position) pos : vec4f,
  @location(0) uv : vec2f,
};

@vertex
fn vs_main(@location(0) pos : vec3f, @location(1) uv : vec2f) -> VSOut {
  var o : VSOut;
  o.pos = u.mvp * vec4f(pos, 1.0);
  o.uv = uv;
  return o;
}
`;

const FRAG = `
@group(0) @binding(1) var tex : texture_2d<f32>;
@group(0) @binding(2) var samp : sampler;

@fragment
fn fs_main(@location(0) uv : vec2f) -> @location(0) vec4f {
  return textureSample(tex, samp, uv);
}
`;

self.onmessage = async (ev) => {
  const m = ev.data;
  if (m.type === 'init') {
    try {
      const adapter = await navigator.gpu.requestAdapter();
      device = await adapter.requestDevice();
      ctx = m.canvas.getContext('webgpu');
      format = navigator.gpu.getPreferredCanvasFormat();
      m.canvas.width = 640; m.canvas.height = 480;
      canvasW = m.canvas.width; canvasH = m.canvas.height;
      ctx.configure({ device, format, alphaMode: 'opaque' });
      if (m.frameState) {
        frameMat = new Float32Array(m.frameState, 0, 16);
        frameCounter = new Int32Array(m.frameState, 64, 1);
      }
      self.postMessage({ type: 'ready' });
      if (pendingScene) { const s = pendingScene; pendingScene = null; setupScene(s); }
    } catch (e) {
      console.error('[wgpu] init failed:', e);
      self.postMessage({ type: 'error', error: String(e) });
    }
  } else if (m.type === 'scene') {
    if (device) setupScene(m); else pendingScene = m;
  }
};

function setupScene(m) {
  if (!device) return;

  const vcount = m.positions.length / 3;
  const interleaved = new Float32Array(vcount * 5);
  for (let i = 0; i < vcount; i++) {
    interleaved[i * 5 + 0] = m.positions[i * 3 + 0];
    interleaved[i * 5 + 1] = m.positions[i * 3 + 1];
    interleaved[i * 5 + 2] = m.positions[i * 3 + 2];
    interleaved[i * 5 + 3] = m.uvs[i * 2 + 0];
    interleaved[i * 5 + 4] = m.uvs[i * 2 + 1];
  }

  vertexBuffer = device.createBuffer({
    size: interleaved.byteLength,
    usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
  });
  device.queue.writeBuffer(vertexBuffer, 0, interleaved);

  indexBuffer = device.createBuffer({
    size: m.indices.byteLength,
    usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST,
  });
  device.queue.writeBuffer(indexBuffer, 0, m.indices);
  indexCount = m.indices.length;

  const texture = device.createTexture({
    size: { width: m.texW, height: m.texH },
    format: 'rgba8unorm',
    usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
  });
  device.queue.writeTexture(
    { texture },
    m.texData,
    { bytesPerRow: m.texW * 4 },
    { width: m.texW, height: m.texH },
  );

  const sampler = device.createSampler({ magFilter: 'linear', minFilter: 'linear' });

  uniformBuffer = device.createBuffer({
    size: 64,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  });

  const vsModule = device.createShaderModule({ code: VERT });
  const fsModule = device.createShaderModule({ code: FRAG });

  pipeline = device.createRenderPipeline({
    layout: 'auto',
    vertex: {
      module: vsModule,
      entryPoint: 'vs_main',
      buffers: [{
        arrayStride: 5 * 4,
        attributes: [
          { shaderLocation: 0, offset: 0, format: 'float32x3' },
          { shaderLocation: 1, offset: 12, format: 'float32x2' },
        ],
      }],
    },
    fragment: {
      module: fsModule,
      entryPoint: 'fs_main',
      targets: [{ format }],
    },
    primitive: { topology: 'triangle-list' },
  });

  bindGroup = device.createBindGroup({
    layout: pipeline.getBindGroupLayout(0),
    entries: [
      { binding: 0, resource: { buffer: uniformBuffer } },
      { binding: 1, resource: texture.createView() },
      { binding: 2, resource: sampler },
    ],
  });

  frame();
}

let lastFrame = 0;
function frame() {
  if (!device || !ctx || !pipeline) return;

  // The engine writes a fresh MVP matrix each frame. Skip duplicate frames if
  // the counter hasn't advanced (the engine may be throttled).
  const n = frameCounter ? Atomics.load(frameCounter, 0) : 0;
  if (frameCounter && n === lastFrame && lastFrame !== 0) { setTimeout(frame, 16); return; }
  lastFrame = n;

  if (frameMat) device.queue.writeBuffer(uniformBuffer, 0, frameMat);

  const encoder = device.createCommandEncoder();
  const pass = encoder.beginRenderPass({
    colorAttachments: [{
      view: ctx.getCurrentTexture().createView(),
      clearValue: { r: 0.094, g: 0.094, b: 0.094, a: 1 },
      loadOp: 'clear',
      storeOp: 'store',
    }],
  });
  pass.setPipeline(pipeline);
  pass.setBindGroup(0, bindGroup);
  pass.setVertexBuffer(0, vertexBuffer);
  pass.setIndexBuffer(indexBuffer, 'uint32');
  pass.drawIndexed(indexCount);
  pass.end();
  device.queue.submit([encoder.finish()]);

  // No requestAnimationFrame in a dedicated worker; schedule the next frame.
  setTimeout(frame, 16);
}
