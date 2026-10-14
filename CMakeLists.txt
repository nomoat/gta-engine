cmake_minimum_required(VERSION 3.16)
project(gta_engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# ---- Engine core (platform-agnostic; builds natively and with Emscripten) ----
add_library(engine
  src/json.cpp
  src/stream.cpp
  src/manifest.cpp
  src/bundle.cpp
  src/raster.cpp
  src/engine.cpp
)
target_include_directories(engine PUBLIC src)

# ---- Demo executable (native / wasm) ----
add_executable(demo src/main.cpp)
target_link_libraries(demo engine)

# ---- Bundle packing tool (host only) ----
add_executable(make_bundle tools/make_bundle.cpp src/bundle.cpp)
target_include_directories(make_bundle PRIVATE src)

# ---- Emscripten specifics ----
# Build with:
#   emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
#   emmake cmake --build build-web -j
# then serve web/ together with build-web/game.js + game.wasm.
if(EMSCRIPTEN)
  set(CMAKE_EXECUTABLE_SUFFIX ".js")
  # WebAssembly stream store (SharedArrayBuffer + Atomics via io_worker.js).
  target_sources(engine PRIVATE src/stream_wasm.cpp)
  target_link_options(demo PRIVATE
    -sENVIRONMENT=worker
    -sALLOW_MEMORY_GROWTH=1
  )
  set_target_properties(demo PROPERTIES OUTPUT_NAME "game")
endif()
