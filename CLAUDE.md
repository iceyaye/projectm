# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Development Notes

- Use `rg` (ripgrep) instead of `grep` for all searches

## Project Overview

projectM is an open-source music visualizer library that reimplements Winamp MilkDrop. It's a cross-platform C++ library (C++14) providing audio visualization through OpenGL rendering.

## Build Commands

```bash
# Clean, configure, build, and install (macOS universal binary)
rm -rf build
cmake -B build \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DBUILD_SHARED_LIBS=ON \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
sudo cmake --install build

# Run tests (must enable BUILD_TESTING first)
cmake -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --verbose
```

### Key CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_TESTING` | OFF | Build unit tests |
| `BUILD_SHARED_LIBS` | ON | Build shared vs static libraries |
| `ENABLE_SDL_UI` | OFF | Build SDL2 test application |
| `ENABLE_GLES` | OFF | Use OpenGL ES 3 (forced ON for Emscripten/Android) |
| `ENABLE_PLAYLIST` | ON | Build playlist library |

### Platform-Specific Notes

- **Windows**: Requires GLEW, use vcpkg for dependencies
- **Raspberry Pi/Embedded**: Use `-DENABLE_GLES=ON`
- **Emscripten**: See EMSCRIPTEN.md

## Architecture

```
src/
├── libprojectM/           # Core visualization engine
│   ├── Audio/             # PCM processing, FFT, beat detection
│   ├── MilkdropPreset/    # Preset parsing and evaluation
│   │   ├── Shaders/       # GLSL shader implementation
│   │   └── Waveforms/     # Waveform rendering (11+ types)
│   ├── Renderer/          # OpenGL rendering pipeline
│   └── UserSprites/       # User sprite system
├── api/                   # Public C API (header-only interface)
│   └── include/projectM-4/
└── playlist/              # Playlist management library
vendor/
├── glm/                   # OpenGL Math (bundled)
├── SOIL2/                 # Image loading
├── hlslparser/            # HLSL to GLSL transpiler
└── projectm-eval/         # Milkdrop expression evaluator (submodule)
```

### Key Design Points

- **C API**: Stable public API in `src/api/include/projectM-4/`. C++ interface exists but is unsupported.
- **Preset Compatibility**: Must maintain MilkDrop preset format compatibility
- **Performance**: Embedded systems support - mesh resolution caps at 300x300 per-pixel
- **OpenGL Requirements**: Core 3.3 or ES 3.2

## Code Style

Uses clang-format (LLVM-based) and clang-tidy. Run before committing.

### Naming Conventions

- **Variables/Parameters**: `camelBack`
- **Classes/Structs**: `CamelCase`
- **Private/Protected Members**: `m_camelBack` prefix
- **Functions/Methods**: `CamelCase`
- **Macros**: `UPPER_CASE`
- **C API functions**: `projectm_*` (exception to CamelCase)

### Formatting

- 4-space indentation, no tabs
- Braces on new line after classes, functions, enums
- No column limit

## Testing

Tests use Google Test framework. Test files in `tests/libprojectM/`.

```bash
# Build with tests
cmake -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .
ctest --verbose
```

## Dependencies

- **Required**: OpenGL 3.3+ (or ES 3.2), CMake 3.21+
- **Windows**: GLEW
- **Optional**: SDL2 (test UI), GTest (tests)
- **Bundled**: GLM, SOIL2, hlslparser

## Submodules

Initialize after cloning:
```bash
git submodule init
git submodule update
```
