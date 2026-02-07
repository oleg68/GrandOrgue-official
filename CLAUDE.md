# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

GrandOrgue is a sample-based pipe organ simulator written in C++20 using wxWidgets for the GUI. It supports Linux, Windows, and macOS, with audio I/O via RtAudio/PortAudio and MIDI via RtMidi.

## Build System

### Building for Development

**Linux (native):**
```bash
# Create build directory
mkdir -p build/linux && cd build/linux

# Configure with CMake
cmake -DCMAKE_BUILD_TYPE=Release -G "Unix Makefiles" ../..

# Build
make -j$(nproc)

# Create packages
make package
```

**Linux (automated script):**
```bash
./build-scripts/for-linux/build-on-linux.sh
# Output: build/linux subdirectory of current directory
```

**macOS:**
```bash
mkdir -p build/osx && cd build/osx
# On Apple Silicon:
cmake -G "Unix Makefiles" -DDOCBOOK_DIR=/opt/homebrew/opt/docbook-xsl/docbook-xsl ../..
# On Intel:
cmake -G "Unix Makefiles" -DDOCBOOK_DIR=/usr/local/opt/docbook-xsl/docbook-xsl ../..
make -j$(nproc)
```

Or use the automated script:
```bash
./build-scripts/for-osx/build-on-osx.sh
```

### Testing

**Build with tests enabled:**
```bash
./build-scripts/for-linux/build-for-tests.sh
# This adds -DBUILD_TESTING=ON to cmake
```

**Run tests:**
```bash
cd build/linux  # or your build directory
ctest -T test --verbose

# For coverage:
ctest -T coverage

# For coverage report:
gcovr -e 'submodules/*|usr/*'
```

Or use the convenience script in the build directory:
```bash
../build-scripts/for-linux/do-tests.sh  # Runs all test steps
../build-scripts/for-linux/do-tests.sh test  # Just run tests
../build-scripts/for-linux/do-tests.sh coverage  # Just coverage
```

**Adding new tests:**
- Add test .cpp files to `src/tests/CMakeLists.txt`
- Tests are located in `src/tests/` with subdirectories for different test categories

### Code Formatting

The project uses `clang-format` with a Google-based style (configured in `.clang-format`). A `pre-commit` hook automatically formats code before commits.

**Manual formatting:**
```bash
clang-format -i --style=file <file.cpp>
```

The pre-commit hook also updates copyright years automatically.

## Architecture

### High-Level Structure

GrandOrgue follows a layered architecture:

1. **Core Layer** (`src/core/`): Platform-independent utilities
   - `archive/`: Archive file handling (organ package files)
   - `config/`: Configuration file reading/writing
   - `files/`: File abstraction and path handling
   - `settings/`: Settings management system
   - `temperaments/`: Musical temperament definitions
   - `threading/`: Cross-platform threading primitives (mutex, condition, thread)

2. **Sound Engine** (`src/grandorgue/sound/`): Real-time audio processing
   - `GOSoundEngine`: Main audio engine coordinating all sound generation
   - `GOSoundProvider`: Base class for sound sources (samples, synthesis)
   - `GOSoundProviderWave`: Sample-based sound provider
   - `GOSoundProviderSynthedTrem`: Synthesized tremulant effects
   - `GOSoundResample`: Real-time sample rate conversion
   - `GOSoundAudioSection`: Audio data management and decompression
   - `GOSoundRecorder`: Audio recording functionality

3. **MIDI System** (`src/grandorgue/midi/`): MIDI input/output and event handling
   - `events/`: MIDI event types and handling
   - `elements/`: MIDI element abstractions (buttons, encoders, etc.)
   - `ports/`: MIDI port management (input/output)
   - `objects/`: Configurable MIDI objects
   - `GOMidiPlayer`: MIDI file playback
   - `GOMidiRecorder`: MIDI recording to files
   - `GOMidiInputMerger`/`GOMidiOutputMerger`: Merging MIDI streams

4. **Organ Model** (`src/grandorgue/model/`): Virtual organ representation
   - `GOOrganModel`: Main organ model coordinating all components
   - `GOManual`: Keyboard/manual representation
   - `GODrawstop`/`GOCoupler`: Stop and coupler controls
   - `GOEnclosure`: Swell box/expression control
   - `GOEventHandlerList`: Event distribution system

5. **GUI Layer** (`src/grandorgue/gui/`): User interface
   - `frames/`: Main application windows (`GOFrame`, `GOLogWindow`)
   - `panels/`: Organ console panels and controls
   - `dialogs/`: Settings, preferences, and configuration dialogs
   - Uses wxWidgets for cross-platform GUI

6. **Combinations** (`src/grandorgue/combinations/`): Piston/combination system
   - `model/`: Combination data structures (divisional, general)
   - `control/`: Combination button controls and interaction
   - `GOSetter`: Combination setter logic

7. **Loader** (`src/grandorgue/loader/`): Organ definition loading
   - `cache/`: Organ sample cache management
   - Multithreaded organ loading system

8. **Document-Base** (`src/grandorgue/document-base/`): Document/view framework
   - `GODocumentBase`: Base document class
   - `GOView`: Base view class

### Key Dependencies

- **wxWidgets**: GUI framework (html, net, adv, core, base modules)
- **RtAudio/PortAudio**: Real-time audio I/O (in `src/rt/rtaudio/`, `src/portaudio/`)
- **RtMidi**: MIDI I/O (in `src/rt/rtmidi/`)
- **yaml-cpp**: YAML configuration parsing
- **FFTW**: FFT for convolution reverb
- **WavPack**: Audio compression
- **ZitaConvolver**: Convolution engine for reverb

External dependencies are managed via git submodules in `submodules/`.

### Threading Model

GrandOrgue uses multiple threads:
- **Main/GUI thread**: wxWidgets event loop
- **Audio thread**: Real-time sound generation (callback from RtAudio/PortAudio)
- **MIDI threads**: MIDI event processing
- **Loader threads**: Background organ loading (`GOLoadWorker`)

Thread synchronization uses custom wrappers in `src/core/threading/` (`GOMutex`, `GOCondition`, `GOThread`).

### Main Entry Point

- Application entry: `src/grandorgue/GOApp.cpp` (`GOApp` class)
- Main frame: `src/grandorgue/gui/frames/GOFrame.cpp`

## Development Workflow

### Main Branch

- The default branch is `master` (use for PRs)
- Current branch in this session: `issue-rel`

### Common CMake Options

```bash
# Enable specific audio backends (Linux)
-DRTAUDIO_USE_ALSA=ON/OFF
-DRTAUDIO_USE_JACK=ON/OFF
-DGO_USE_JACK=ON/OFF

# Enable ASIO support (Windows)
-DRTAUDIO_USE_ASIO=ON/OFF

# Build type
-DCMAKE_BUILD_TYPE=Release|Debug

# Enable testing
-DBUILD_TESTING=ON

# Static linking
-DSTATIC=1
```

### File Organization

- Application code: `src/grandorgue/`
- Core utilities: `src/core/`
- Tests: `src/tests/`
- Build tools: `src/build/`
- Resources: `resources/`, `sounds/`, `help/`
- Build scripts: `build-scripts/`
- Localization: `po/`

## Important Notes

- **C++ Standard**: C++20 is required (set in CMakeLists.txt)
- **Code Style**: Use the project's clang-format configuration (Google-based style with custom tweaks)
- **Pre-commit hooks**: Automatically format code and update copyright years
- **Submodules**: Always clone with `--recurse-submodules` to get dependencies
- **Cross-compilation**: Scripts available for building Windows binaries on Linux (`build-scripts/for-win64/`)
- **Platform-specific builds**: AppImage, macOS .app, Windows installer via CPack
