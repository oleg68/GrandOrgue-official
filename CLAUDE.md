# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

GrandOrgue is a sample-based pipe organ simulator written in C++20 with wxWidgets GUI. It supports cross-platform audio (Linux/Windows/macOS) via multiple backends (PortAudio, RtAudio, JACK) and provides realistic polyphonic organ sound synthesis with multi-threaded audio processing.

## Building the Project

### Prerequisites Installation

**Linux (Debian/Ubuntu-based):**
```bash
./build-scripts/for-linux/prepare-debian-based.sh
```

**Linux (Fedora):**
```bash
./build-scripts/for-linux/prepare-fedora.sh
```

**Linux (OpenSuse):**
```bash
./build-scripts/for-linux/prepare-opensuse.sh
```

**macOS:**
```bash
./build-scripts/for-osx/prepare-osx.sh
```

### Building

**Quick build (Linux):**
```bash
./build-scripts/for-linux/build-on-linux.sh
```
Output: `build/linux/bin/` (executables), `build/linux/` (packages)

**Manual build:**
```bash
mkdir -p build/linux
cd build/linux
cmake -DCMAKE_BUILD_TYPE=Release -G "Unix Makefiles" ../..
make -j$(nproc)
```

**Debug build:**
```bash
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-g -DCMAKE_C_FLAGS=-g -G "Unix Makefiles" ../..
make -j$(nproc)
```

**Create packages:**
```bash
make package
```

### Testing

**Enable testing:**
```bash
cmake -DGO_BUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug -G "Unix Makefiles" ../..
make -j$(nproc)
```

**Run all tests:**
```bash
make test
# or
ctest
```

**Run test executable directly:**
```bash
./bin/GOTestExe
```

**Code coverage (requires GO_BUILD_TESTING=ON):**
The build system adds `--coverage` flag automatically when testing is enabled.

## Architecture Overview

### High-Level System Structure

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
│  (src/grandorgue/)                                           │
│  GOApp, GODocument, GOOrganController, wxWidgets GUI        │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────┴──────────────────────────────────────┐
│                    Core Engine Layer                         │
│  (src/core/)                                                 │
│  GOOrganModel, GOSoundEngine, GOSoundScheduler              │
│  GOSoundingPipe, GOWindchest, Memory/File Utilities         │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────┴──────────────────────────────────────┐
│                   Audio Backend Layer                        │
│  (src/portaudio/, src/rt/)                                   │
│  GOSoundPort → GOSoundPortaudioPort, GOSoundRtPort,         │
│                GOSoundJackPort                               │
└─────────────────────────────────────────────────────────────┘
```

### Audio Processing Pipeline

The real-time audio flow:

```
MIDI/GUI Input
    ↓
GOOrganController → GOEventDistributor
    ↓
GOManual/GOStop → GOSoundingPipe (model objects)
    ↓
GOSoundEngine (master orchestrator)
    ├→ GOSoundScheduler (lock-free task dispatch)
    │   ├→ GOSoundTremulantTask
    │   ├→ GOSoundWindchestTask (per windchest DSP)
    │   ├→ GOSoundGroupTask
    │   ├→ GOSoundOutputTask
    │   └→ GOSoundReleaseTask
    │
    ├→ GOSoundSamplerPool (manages active samplers)
    │   └→ GOSoundSampler (per-voice state)
    │       └→ GOSoundStream (decoding, resampling)
    │
    └→ GOSound (facade, thread coordination)
        └→ GOSoundPort (abstract backend interface)
            ├→ GOSoundPortaudioPort
            ├→ GOSoundRtPort
            └→ GOSoundJackPort
                ↓
            Audio Hardware
```

### Key Abstractions

**Sound Buffer Classes** (`src/grandorgue/sound/buffer/`):
- `GOSoundBuffer`: Read-only wrapper (non-owning, interleaved float data)
- `GOSoundBufferMutable`: Mutable buffer interface
- `GOSoundBufferManaged`: RAII-based owned buffer with automatic cleanup

**Audio Callback Signature:**
```cpp
bool AudioCallback(GOSoundBufferMutable &outputBuffer)
```
This pattern flows through: `GOSoundPort` → `GOSound` → `GOSoundEngine`

**Provider Pattern:**
- `GOSoundProvider`: Abstract base for audio sources
- `GOSoundProviderWave`: Loads wave files (attack/release segments)
- `GOSoundProviderSynthedTrem`: Synthesized tremulant oscillations

**Task-Based Scheduling:**
- `GOSoundScheduler`: Lock-free multi-threaded task dispatch
- Task priorities: `TREMULANT(10) < WINDCHEST(20) < AUDIOGROUP(50) < AUDIOOUTPUT(100) < RELEASE(160) < TOUCH(700)`
- Thread pool: `GOSoundThread` instances execute tasks from scheduler queue

### Organ Model Architecture

**GOOrganModel**: Central organ structure
- Contains all organ components (manuals, stops, couplers, tremulants, enclosures)
- Manages windchests (1-indexed, each groups related pipes)
- Stores ranks and pipes, handles combination system

**GOSoundingPipe**: Individual pipe model
- Bridges organ model and sound engine
- Contains `GOSoundProviderWave` (audio data)
- Manages pipe configuration (audio group, windchest, tuning, volume)
- Handles tremulant switching callbacks

**GOWindchest**: Logical sound grouping
- Groups pipes for synchronized processing
- Applies enclosure attenuation
- Associates tremulants with pipes

### Threading Model

- **Main thread**: UI updates, configuration changes
- **Audio thread**: Real-time callback from audio backend (high priority)
- **Worker threads**: `GOSoundThread` pool for parallel DSP processing
- **Lock-free scheduler**: Minimizes latency in audio path

### Configuration & Caching

- **ODF files**: Organ Definition Format (organ structure)
- **CMB files**: Combination files (user settings/state)
- **Cache files**: Precomputed pipe audio (WavPack compressed)
- `GOCache`, `GOCacheWriter`: Handle serialization

### MIDI Integration

- `GOMidi`: Central MIDI interface
- `GOMidiListener`: Event listener interface
- `GOMidiRecorder`/`GOMidiPlayer`: Record/playback functionality
- Events dispatched via `GOEventDistributor`

## Code Organization

### Main Source Directories

- `src/grandorgue/`: Main application (wxWidgets GUI, controllers, config)
  - `config/`: Configuration management
  - `sound/`: Audio engine and sound processing
  - `model/`: Organ model (pipes, stops, windchests)
  - `gui/`: wxWidgets UI panels
  - `midi/`: MIDI handling
  - `loader/`: ODF parsing

- `src/core/`: Core utilities (file I/O, memory management, abstractions)

- `src/portaudio/`: PortAudio wrapper and integration

- `src/rt/`: RtAudio and RtMidi library integration

- `src/tests/`: Test framework and test suites
  - `common/`: Test infrastructure (`GOTest`, `GOTestCollection`)
  - `testing/`: Actual test implementations

- `perftests/`: Performance test data (WAV files for benchmarking)

- `src/tools/`: Command-line tools (GrandOrgueTool, GrandOrguePerfTest)

- `src/build/`: Build-time code generation tools

### Build Configuration Options

Key CMake options (see `CMakeLists.txt` for full list):
- `RTAUDIO_USE_JACK`: Enable JACK support (default: ON on Linux)
- `RTAUDIO_USE_ALSA`: Enable ALSA support (Linux, default: ON)
- `RTAUDIO_USE_ASIO`: Enable ASIO support (Windows, default: ON)
- `GO_BUILD_TESTING`: Enable test suite (default: OFF, set to ON for testing)
- `INSTALL_DEMO`: Install demo sampleset (default: ON)
- `USE_INTERNAL_PORTAUDIO`: Use bundled PortAudio (default: ON)
- `CMAKE_BUILD_TYPE`: `Release` or `Debug`

## Coding Conventions

### Naming Patterns

**Variables:**
- Local variables/parameters: `camelCase` (e.g., `nSamples`, `devIndex`)
- Loop index from 0: suffix `I` (e.g., `sampleI`, `channelI`)
- Count from 1: suffix `N` (e.g., `windchestN`, `deviceN`)
- Member variables: `m_` prefix + PascalCase for compound names (e.g., `m_NChannels`, `m_AudioOutput`)

**Pointers:**
- Local pointers: `pCamelCase` prefix (e.g., `pData`, `pBuffer`)
- Non-owning member pointers: `p_` prefix (e.g., `p_buffer`, `p_AudioEngine`)
- Owning member pointers: `mp_` prefix (e.g., `mp_buffer`, `mp_AudioOutput`)

**Output Parameters:**
- Place at end of parameter lists
- Prefix with `out` (e.g., `GOSoundBufferMutable &outOutputBuffer`)

**Types:**
- Classes/structs: `PascalCase` (e.g., `GOSoundEngine`, `GOSoundBuffer`)

### Code Style

**Formatting:**
- Empty line between variable declarations and code
- Empty line after pointer declaration before using it
- Use ternary operator for simple conditional returns

**Type Preferences:**
- Prefer references over pointers where appropriate
- Use RAII and managed objects over manual memory management
- Pass buffers by reference when possible

**Assertions:**
- Use `assert()` to check invariants, especially in real-time audio code

**Comments:**
- Update comments when refactoring to match new implementation
- Use clear, descriptive comments for non-obvious operations

**Memory Management:**
- Prefer `GOSoundBufferManaged` over manual `new`/`delete`
- Use managed buffers for owned memory

## Important Patterns

### Buffer Handling
Always use buffer wrapper classes (`GOSoundBuffer`, `GOSoundBufferMutable`, `GOSoundBufferManaged`). Pass buffers by reference when possible. Use managed buffers for owned memory.

### Real-Time Audio Code
- Avoid allocations in audio callback path
- Use pool-based allocation for samplers
- Assert invariants (e.g., buffer size validation)
- Minimize locks (use lock-free scheduler)

### Callback Chain
When implementing audio callbacks, follow the signature:
```cpp
bool AudioCallback(GOSoundBufferMutable &outputBuffer)
```
Return `true` to continue, `false` to stop audio.

### Task-Based Processing
For multi-threaded audio processing, create tasks that inherit from appropriate base classes and register with `GOSoundScheduler`. Tasks execute on worker threads from the thread pool.

## Common Development Workflows

### Adding a New Audio Backend
1. Create new class inheriting from `GOSoundPort`
2. Implement `AudioCallback()` method
3. Handle latency reporting
4. Integrate with `GOSound` for lifecycle management
5. Add CMake option to enable/disable backend

### Modifying Audio Processing
1. Locate relevant task class in `GOSoundScheduler`
2. Modify task's `Run()` method for DSP changes
3. Ensure thread-safety (tasks run in parallel)
4. Add assertions for buffer size validation
5. Test with various buffer sizes and sample rates

### Adding New Organ Components
1. Create model class in `src/grandorgue/model/`
2. Implement serialization (ODF loading, cache support)
3. Add to `GOOrganModel` ownership/lifecycle
4. Create controller logic if needed
5. Add GUI representation in `src/grandorgue/gui/`

## Repository Information

- **License**: GPL-2.0-or-later
- **Main branch**: `master`
- **C++ Standard**: C++20
- **Build system**: CMake 3.10+
- **GUI Framework**: wxWidgets
- **Primary platforms**: Linux, Windows, macOS
