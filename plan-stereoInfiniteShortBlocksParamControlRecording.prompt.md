# Plan: Stereo Infinite Mode, Short Block Padding, WebSocket Parameter Control, and Output Recording

Four improvements: fix mono-only infinite mode, pad short blocks instead of dropping them, add real-time WebSocket parameter control via ixwebsocket, and record live output from stream/infinite modes.

## Steps

### 1. Fix `streamInfinite` to output stereo ✅

In `StreamProcessor.h` and `StreamProcessor.cpp`, changed `streamInfinite` default `channels` from 1 to 2. In `main.cpp`, probes the actual channel count from the first source file and passes it to `streamInfinite`.

**Files modified:**
- `src/usecase/StreamProcessor.h` — default `channels = 2`
- `src/main.cpp` — probes `num_ch` from first source, passes to `streamInfinite`

### 2. Pad short/trailing blocks with silence ✅

In `Brain::addSound()`, changed loop condition from `i + bs <= ch0.size()` to `i < ch0.size()` and pads extracted samples with `resize(bs, 0.0)` when the remaining audio is shorter than the block size.

**Files modified:**
- `src/domain/Brain.cpp` — new loop condition + zero-padding

### 3. Add real-time parameter control via WebSocket (ixwebsocket) ✅

Since `liblo` is not available on Conan, used `ixwebsocket/11.4.5` instead. Runs a WebSocket server on port 7770; a companion HTML/JS control panel (`web/control-panel.html`) connects and sends JSON messages to update `SearchParams` in real-time.

**Implementation:**
- Domain port `IParamController` in `src/domain/port/IParamController.h` — `start()`, `stop()`, `getParams()`, `setParams()`.
- Adapter `WebSocketParamController` in `src/adapter/control/` — uses `ix::WebSocketServer`, mutex-guarded `SearchParams`.
- `StreamProcessor` accepts optional `shared_ptr<IParamController>`, snapshots params via `activeParams()` each block.
- `main.cpp` creates and starts the controller for stream/infinite modes.
- `web/control-panel.html` — self-contained HTML/JS control panel with sliders for all params.

**Files created:**
- `src/domain/port/IParamController.h`
- `src/adapter/control/WebSocketParamController.h`
- `src/adapter/control/WebSocketParamController.cpp`
- `web/control-panel.html`

**Files modified:**
- `conandata.yml` — added `ixwebsocket/11.4.5`
- `CMakeLists.txt` — `find_package(ixwebsocket)`, link, new source files
- `src/usecase/StreamProcessor.h` — accepts `shared_ptr<IParamController>`
- `src/usecase/StreamProcessor.cpp` — uses `activeParams()` per block
- `src/main.cpp` — wires `WebSocketParamController`

### 4. Add output recording for stream/infinite modes ✅

New domain port `IRecorder` with `open()`, `write()`, `close()`, `isOpen()`. Adapter `LibSndFileRecorder` writes WAV (PCM 24-bit) incrementally via `sf_writef_double`. `StreamProcessor::outputBlock()` tees interleaved data to the recorder. CLI flag `-r <path>` enables recording.

**Files created:**
- `src/domain/port/IRecorder.h`
- `src/adapter/gateway/LibSndFileRecorder.h`
- `src/adapter/gateway/LibSndFileRecorder.cpp`

**Files modified:**
- `src/usecase/StreamProcessor.h` — accepts `shared_ptr<IRecorder>`
- `src/usecase/StreamProcessor.cpp` — tees to recorder in `outputBlock()`
- `src/main.cpp` — `-r <path>` flag, creates recorder, injects into StreamProcessor
- `CMakeLists.txt` — new source files

### 5. Update copilot-instructions.md

Update `.github/copilot-instructions.md` to document the new features.

**Files modified:**
- `.github/copilot-instructions.md`

---

## Architecture Alignment

All new code follows the hexagonal architecture:
- `IRecorder` and `IParamController` are domain ports (no external dependencies)
- `LibSndFileRecorder` is an adapter (depends on libsndfile)
- `WebSocketParamController` is an adapter (depends on ixwebsocket)
- `main.cpp` remains the sole composition root that wires everything together

## Usage

### WebSocket Control Panel
1. Start brain-io in `stream` or `infinite` mode
2. Open `web/control-panel.html` in a browser
3. Enter `ws://localhost:7770` and click Connect
4. Move sliders to control parameters in real-time

### Recording
```sh
./brainio stream -i sounds/a.wav -t sounds/target.wav -r sounds/recording.wav
./brainio infinite -d sounds/SAMPLES/ -r sounds/recording.wav
```
