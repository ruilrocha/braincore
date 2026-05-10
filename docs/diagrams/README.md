# brain-io Architecture Diagrams

Three PlantUML diagrams documenting the project architecture.

## Files

| File | Diagram type | What it shows |
|------|-------------|---------------|
| [`01_layers.puml`](01_layers.puml) | Component | Onion / hexagonal layers and dependency arrows |
| [`02_dataflow.puml`](02_dataflow.puml) | Sequence | End-to-end data flow for all three runtime modes (batch, stream, infinite) |
| [`03_classes.puml`](03_classes.puml) | Class | Key classes, port interfaces, and their relationships |

## Rendering

Any of the following tools will render these files:

```sh
# PlantUML CLI (requires Java + plantuml.jar)
java -jar plantuml.jar docs/diagrams/*.puml

# VS Code: install the "PlantUML" extension (jebbs.plantuml)

# IntelliJ / CLion: built-in PlantUML support (install plugin)

# Online: paste content into https://www.plantuml.com/plantuml/uml/
```

## Quick summary

```
main.cpp  ←  Composition Root
│
├── ADAPTERS  (outermost — depend on external libs)
│     analysis/MfccAnalyser       → IAnalyser
│     effects/FftwSpectralMorph   → IBlockEffect
│     gateway/LibSndFileGateway   → ISoundFileGateway
│     playback/MiniaudioOutput    → IAudioOutput
│     search/{7 strategies}       → ISearchStrategy
│
├── USE-CASES
│     SoundProcessor   — batch reconstruction
│     StreamProcessor  — real-time stream / infinite mode
│     EffectHelpers    — shared grain/stutter/envelope functions
│
└── DOMAIN CORE  (innermost — zero external deps)
      Brain / Block / Sound / Fingerprints
      BlockConfig / SearchParams / WindowFunction / Random.h
      port/{IAnalyser, ISearchStrategy, ISoundFileGateway,
            IAudioOutput, IBlockEffect}
```

