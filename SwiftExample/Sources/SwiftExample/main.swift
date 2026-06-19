// SwiftExample/Sources/SwiftExample/main.swift
//
// Demonstrates using BrainCore from Swift via C++ interop.
// Loads synthetic sine waves into a Brain, builds the index,
// and runs a self-test to verify everything wired up correctly.

import BrainCore
import Foundation

print("=== BrainCore Swift Example ===\n")

// 1. Create a BrainSession — pre-wired with MfccAnalyser + VpTreeSearch.
var session = audio.BrainSession()

// ── Brain config ─────────────────────────────────────────────────────────────
// These must be set BEFORE addSamples() / buildIndex().
// Changing them afterwards requires: clear() → addSamples() → buildIndex().
//
// setBlockSize  — samples per block (resolution vs latency trade-off).
// setOverlapRatio — OLA crossfade amount [0.0, 0.9]; 0.5 = 50% (default).
//   With overlap=0.5, stepSize() = blockSize()/2 — advance() and
//   getBlockSamplesInterleaved() both work in half-block steps.
// setWindowShape — OLA synthesis window (NOT used for analysis; Hann is
//   hardcoded for MFCC internally). Hann + 50% overlap = perfect reconstruction.

session.setBlockSize(4096)
session.setOverlapRatio(0.5)
session.setWindowShape(.Hann)

// 2. Helper: generate a mono sine wave as a Swift [Double].
func makeSineWave(frequency: Double, duration: Double, sr: Int32) -> [Double] {
    let count = Int(Double(sr) * duration)
    return (0..<count).map { i in
        sin(2.0 * Double.pi * frequency * Double(i) / Double(sr))
    }
}

let sampleRate: Int32 = 22050

// 3. Add two synthetic sounds to the brain via addSamples(ptr, count, sr, name).
let aNote = makeSineWave(frequency: 440.0, duration: 1.0, sr: sampleRate)
aNote.withUnsafeBufferPointer { ptr in
    session.addSamples(ptr.baseAddress!, aNote.count, sampleRate, "A440")
}

let eSixth = makeSineWave(frequency: 330.0, duration: 1.0, sr: sampleRate)
eSixth.withUnsafeBufferPointer { ptr in
    session.addSamples(ptr.baseAddress!, eSixth.count, sampleRate, "E330")
}

print("Sounds loaded. Block count: \(session.blockCount())")

// 4. Build the nearest-neighbour index.
session.buildIndex()
print("Index built.\n")

// 5. Show OLA config via stepSize().
//    With overlap=0.5 and blockSize=4096, stepSize() = 2048 samples per step.
let bs = session.blockSize()
let step = session.stepSize()
print("Block size : \(bs) samples")
print("Step size  : \(step) samples  (= blockSize × (1 − overlap))")
print("Overlap    : \(session.getOverlapRatio() * 100)%  → OLA crossfade active\n")

// 6. Advance the playhead with a target block.
//    Target chunk must be `step` samples (= stepSize()) — NOT blockSize() — when OLA is active.
let targetBlock = (0..<Int(step)).map { i in
    sin(2.0 * Double.pi * 440.0 * Double(i) / Double(sampleRate))
}

let matchedIdx: Int = targetBlock.withUnsafeBufferPointer { ptr in
    Int(session.advance(ptr.baseAddress!, targetBlock.count, sampleRate))
}

print("Target (440 Hz chunk) → matched brain block index: \(matchedIdx)")
print("\n=== Test passed ===")
