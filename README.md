# gta_engine — 一个照 GTA 浏览器移植版架构重写的引擎骨架

这是对 `gta.deloskiyt.pl`（GTA V 浏览器移植版）的 **clean-room 架构重写**：
不复制它的代码（它的 C++ 是编译进 `game.wasm`、符号已剥离，也无法"反编译"），
而是**照它的引擎使用方式**从零实现一套等价架构，并改用**自定义资源格式**，
这样你可以自己造 assets 直接接进来。

当前已是一个**可玩的第三人称骨架**：一块地面 + 一个角色方块，WASD 走动、
鼠标拖拽转相机、滚轮缩放，全部跑在真 WebGPU 管线 + 字节区间流式 IO 上。

> 注意：该站点加载的是 Rockstar 的版权游戏数据。本项目不包含、也不下载任何
> GTA 资源；`data/` 里是用脚本生成的自制测试资源。

## 架构对照（我们 vs 那个 GTA 移植版）

| 职责 | GTA 移植版 | 本项目 |
|---|---|---|
| 页面 | `index.html`（canvas + 启动 worker） | `web/index.html` |
| 引擎宿主线程 | `loader.js`（Worker，跑 main()，可阻塞） | `web/loader.js` |
| 流式 IO | `io_worker.js` + `data/bootset.json`（HTTP Range 按字节区间取） | `web/io_worker.js` + `data/bootset.json` |
| 渲染 | `wgpu_worker.js`（WebGPU） | `web/wgpu_worker.js`（WebGPU 骨架） |
| 引擎本体 | `game.wasm`（C++） | `src/*.cpp`（C++，本仓库源码） |
| 资源容器 | `.rpf` | `.bundle`（`src/bundle.cpp`） |

核心思想一致：**启动时只按 manifest 里的字节区间预取归档头部，其余资源用
`[offset, length)` 按需流式读取**，而不是整包下载。

## 目录

```
src/        引擎核心（平台无关，原生 + Emscripten 都能编译）
  linmath.h   最小线性代数（CPU 光栅化用）
  json.*      极简 JSON 解析器（bootset.json 用，无外部依赖）
  stream.*    流式 IO 抽象 IStreamStore + 本地文件实现
  manifest.*  bootset 清单解析
  bundle.*    .bundle 容器（类似 .rpf 的 header+payload 布局）
  raster.*    软件光栅化器（原生调试用；浏览器里换成 WebGPU）
  engine.*    应用框架：manifest→预取→解包→按需流式加载→渲染循环
  main.cpp    原生 demo 入口
tools/
  make_bundle.cpp  把资产文件打成 .bundle
scripts/
  gen_demo_assets.py  生成测试资源 + bootset.json
web/
  index.html / loader.js / io_worker.js / wgpu_worker.js
data/
  game.bundle / bootset.json（由脚本生成）
```

## 自定义资源格式

- **`.bundle`**（`src/bundle.h`）：`BNDL1` 魔数 + 条目表 + 原始资产字节。
  条目表：`u16 nameLen + name + u8 type + u64 offset + u64 length`。
  type：`1 = mesh`、`2 = texture`、`0 = other`。
- **mesh**：`MESH` + `u32 vertexCount + u32 indexCount` + 每顶点 `3f pos + 2f uv`
  + `indexCount × u32`。
- **texture**：`TEX1` + `u32 width + u32 height + u32 channels(4)` + RGBA。
- **`bootset.json`**：`[ [ "path", [ [offset,length], ... ] ], ... ]`。

## 构建

原生（macOS/Linux，无依赖）：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
python3 scripts/gen_demo_assets.py
./build/demo data            # 输出 60 帧 frame_*.ppm
```

浏览器（需要 Emscripten）：

```bash
# 安装 emsdk 后 source ~/emsdk/emsdk_env.sh
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
emmake cmake --build build-web -j
cp build-web/game.js build-web/game.wasm web/
python3 serve.py 8790 web      # 带 COOP/COEP + Range 的静态服务器
# 打开 http://localhost:8790/ —— 会看到旋转的棋盘格贴图四边形
```

浏览器端已打通：Emscripten 编译 → loader.js（worker 里跑 main）→
`EmscriptenStreamStore`（同步 XHR 按字节区间拉取）→ 场景上传给
`wgpu_worker.js` → WebGPU 渲染（WGSL 着色器 + 贴图）。C++ 引擎逐帧把 MVP 矩阵
写进 SharedArrayBuffer（零拷贝），GPU worker 读它来驱动相机；页面把键盘/鼠标
（指针锁）写进另一个 SharedArrayBuffer，引擎读输入来转相机——和 GTA 移植版用
SharedArrayBuffer + Atomics 做跨线程同步是同一套做法。

操作：点击锁定鼠标，拖拽/滚轮/QE 转相机缩放，WASD/方向键让角色在地面上走动。

## 下一步（把骨架做成完整引擎）

1. **异步 IO 桥**：把 `EmscriptenStreamStore` 的同步 XHR 换成
   `io_worker.js` 异步 fetch + SharedArrayBuffer/Atomics 桥（同步 XHR 在
   worker 里可用但已弃用；接口不变，只换传输）。
2. **深度缓冲**：渲染管线加 depthStencil，让多物体按深度正确遮挡。
3. **场景/资源系统**：多个 bundle、依赖加载、LRU 缓存、加载队列、碰撞/重力。
4. **存档**：IndexedDB 读写 `/userdata`（对标它的 `gta5-userdata`）。

## 测试

- `./build/demo data`：原生跑通流式管线（bootset → header 预取 → 按需拉
  mesh/texture → 渲染 60 帧）。
- 浏览器 `http://localhost:8790/`：同样的管线在 wasm 里跑通，HTTP 日志里应看到
  对 `game.bundle` 的多个 `206 Partial Content` 字节区间请求。
