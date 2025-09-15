# BRAINIO

## Run

Go to the .devcontainer folder and select the option to "Create container and mount sources"

Once its built, open the container and run

```bash
conan install . --output-folder=build --build=missing
```

Add the following to your cmake opts

```bash
-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="conan_provider.cmake"
```