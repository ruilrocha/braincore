// SwiftExample/Sources/SwiftExample/main.swift
//
// Demonstrates using BrainCore from Swift via C++ interop.
// Loads synthetic sine waves into a Brain, builds the index,
// and runs a self-test to verify everything wired up correctly.

import BrainCore
import Foundation

print("=== BrainCore Swift Example ===\n")

// 1. Create a BrainSession — pre-wired with MfccAnalyser + ClosestSearch.
var session = audio.BrainSession()

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

// 5. Run the built-in self-test.
let report = String(session.selfTest())
print(report)

// 6. Advance the playhead with a target block (one block of 440 Hz sine).
let blockSize = 4096
let targetBlock = (0..<blockSize).map { i in
    sin(2.0 * Double.pi * 440.0 * Double(i) / Double(sampleRate))
}

let matchedIdx: Int = targetBlock.withUnsafeBufferPointer { ptr in
    Int(session.advance(ptr.baseAddress!, targetBlock.count, sampleRate))
}

print("Target (440 Hz block) → matched brain block index: \(matchedIdx)")
print("\n=== Test passed ===")
