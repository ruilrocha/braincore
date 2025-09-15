# BRAINIO

Scrambling audio

## Run

You need docker installed to build and run this project.

Go to the .devcontainer folder and select the option to "Create container and mount sources"

Once its built, open the container and run

```bash
conan install . --output-folder=build --build=missing
```

Add the following to your cmake opts

```bash
-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="conan_provider.cmake"
```

## Acknowledgements

This software was inspired by Dave Griffiths' and Aphex Twin's
[Samplebrain](https://thentrythis.org/projects/samplebrain) project.

This software uses the following libraries:
* [FFTW](https://www.fftw.org) - Fast Fourier Transform library
* [Aquila](https://aquila-dsp.org) - Digital signal processing library
* [libsndfile](https://libsndfile.github.io/libsndfile) - Library for reading and writing sound files