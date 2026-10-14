// GPU worker: WebGPU rendering backend (mirrors the GTA port's wgpu_worker.js).
//
// Protocol (postMessage):
//   in:  { type:'init', canvas, frameState }    -- OffscreenCanvas (transferred)
//   in:  { type:'object', positions, uvs, indices, texW, texH, texData }  (repeated)
//   in:  { type:'done' }                        -- all objects uploaded
//
// frameState layout (SharedArrayBuffer, 8*64 + 4 bytes):
//   Float32Array[0..128) : up to 8 MVP matrices (16 floats each)
//   Int32Array[512..]    : frame counter
'use strict';

const MAX_OBJECTS = 8;

let device, ctx, format;
let pipeline, sampler, uniformBuffer;
let objects = [];
let pending = [];
let frameMat = null, frameCounter = null;
let uniformAlign = 256;

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
      uniformAlign = device.limits.minUniformBufferOffsetAlignment || 256;
      ctx = m.canvas.getContext('webgpu');
      format = navigator.gpu.getPreferredCanvasFormat();
      ctx.configure({ device, format, alphaMode: 'opaque' });
      if (m.frameState) {
        frameMat = new Float32Array(m.frameState, 0, MAX_OBJECTS * 16);
        frameCounter = new Int32Array(m.frameState, MAX_OBJECTS * 64, 1);
      }
      self.postMessage({ type: 'ready' });
      if (pending.length) buildScene(pending);
    } catch (e) {
      console.error('[wgpu] init failed:', e);
    }
  } else if (m.type === 'object') {
    pending.push(m);
  } else if (m.type === 'done') {
    if (device) buildScene(pending);
    // else: init picks up `pending` once the device is ready
  }
};

function buildScene(raw) {
  const vsModule = device.createShaderModule({ code: VERT });
  const fsModule = device.createShaderModule({ code: FRAG });

  sampler = device.createSampler({ magFilter: 'linear', minFilter: 'linear' });
  // One uniform buffer with an aligned 64-byte slot per object (WebGPU requires
  // uniform buffer offsets to be multiples of minUniformBufferOffsetAlignment).
  uniformBuffer = device.createBuffer({
    size: MAX_OBJECTS * uniformAlign,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  });

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
    primitive: { topology: 'triangle-list', cullMode: 'none' },
  });

  objects = raw.slice(0, MAX_OBJECTS).map((m, i) => {
    const vcount = m.positions.length / 3;
    const interleaved = new Float32Array(vcount * 5);
    for (let v = 0; v < vcount; v++) {
      interleaved[v * 5 + 0] = m.positions[v * 3 + 0];
      interleaved[v * 5 + 1] = m.positions[v * 3 + 1];
      interleaved[v * 5 + 2] = m.positions[v * 3 + 2];
      interleaved[v * 5 + 3] = m.uvs[v * 2 + 0];
      interleaved[v * 5 + 4] = m.uvs[v * 2 + 1];
    }

    const vertexBuffer = device.createBuffer({
      size: interleaved.byteLength,
      usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
    });
    device.queue.writeBuffer(vertexBuffer, 0, interleaved);

    const indexBuffer = device.createBuffer({
      size: m.indices.byteLength,
      usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST,
    });
    device.queue.writeBuffer(indexBuffer, 0, m.indices);

    const texture = device.createTexture({
      size: { width: m.texW, height: m.texH },
      format: 'rgba8unorm',
      usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
    });
    device.queue.writeTexture(
      { texture }, m.texData, { bytesPerRow: m.texW * 4 },
      { width: m.texW, height: m.texH },
    );

    const bindGroup = device.createBindGroup({
      layout: pipeline.getBindGroupLayout(0),
      entries: [
        { binding: 0, resource: { buffer: uniformBuffer, offset: i * uniformAlign, size: 64 } },
        { binding: 1, resource: texture.createView() },
        { binding: 2, resource: sampler },
      ],
    });

    return { vertexBuffer, indexBuffer, indexCount: m.indices.length, bindGroup };
  });

  frame();
}

let lastFrame = 0;
function frame() {
  if (!device || !ctx || !pipeline || !objects.length) return;

  const n = frameCounter ? Atomics.load(frameCounter, 0) : 0;
  if (frameCounter && n === lastFrame && lastFrame !== 0) { setTimeout(frame, 16); return; }
  lastFrame = n;

  // Copy each object's MVP into its aligned uniform slot before the pass.
  if (frameMat) {
    for (let i = 0; i < objects.length; i++) {
      const mvp = frameMat.slice(i * 16, i * 16 + 16); // fresh Float32Array(16)
      device.queue.writeBuffer(uniformBuffer, i * uniformAlign, mvp);
    }
  }

  const encoder = device.createCommandEncoder();
  const pass = encoder.beginRenderPass({
    colorAttachments: [{
      view: ctx.getCurrentTexture().createView(),
      clearValue: { r: 0.05, g: 0.07, b: 0.12, a: 1 },
      loadOp: 'clear',
      storeOp: 'store',
    }],
  });
  pass.setPipeline(pipeline);
  for (const obj of objects) {
    pass.setBindGroup(0, obj.bindGroup);
    pass.setVertexBuffer(0, obj.vertexBuffer);
    pass.setIndexBuffer(obj.indexBuffer, 'uint32');
    pass.drawIndexed(obj.indexCount);
  }
  pass.end();
  device.queue.submit([encoder.finish()]);
  setTimeout(frame, 16);
}
