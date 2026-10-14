// IO worker: streams asset files by byte range over HTTP, and hands results to the
// C++ engine through a SharedArrayBuffer (the engine blocks with Atomics.wait, the
// same sync technique the GTA port uses for its cross-thread input). This avoids
// synchronous XHR (blocked in modern browsers) while keeping the C++ IO synchronous.
//
// Message protocol (postMessage):
//   in: { type:'read', url, offset, length, sab }
//        sab layout: Int32[0] status (0 pending, 1 ok, -1 error), Int32[1] length,
//                    bytes from offset 8.
//   in: { type:'head', url, sab }
//        sab layout: Int32[0] status, Float64[1] file size (at offset 8).
'use strict';

self.onmessage = async (ev) => {
  const m = ev.data;
  if (m.type === 'read') {
    const i32 = new Int32Array(m.sab, 0, 2);
    const out = new Uint8Array(m.sab, 8);
    try {
      const res = await fetch(m.url, {
        headers: { Range: `bytes=${m.offset}-${m.offset + m.length - 1}` },
      });
      if (res.status !== 206 && res.status !== 200) throw new Error('HTTP ' + res.status);
      const buf = await res.arrayBuffer();
      const data = res.status === 206
        ? new Uint8Array(buf)
        : new Uint8Array(buf, m.offset, m.length); // server ignored Range
      out.set(data.subarray(0, m.length));
      i32[1] = data.length;
      i32[0] = 1;
    } catch (e) {
      i32[1] = 0;
      i32[0] = -1;
    }
    Atomics.notify(i32, 0);
  } else if (m.type === 'head') {
    const i32 = new Int32Array(m.sab, 0, 1);
    const f64 = new Float64Array(m.sab, 8, 1);
    try {
      const res = await fetch(m.url, { method: 'HEAD' });
      f64[0] = Number(res.headers.get('Content-Length')) || 0;
      i32[0] = 1;
    } catch (e) {
      f64[0] = 0;
      i32[0] = -1;
    }
    Atomics.notify(i32, 0);
  }
};
