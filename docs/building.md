## Building

- Platform(s) supported: Linux x86/64, ARM64
- Dependencies: Qt6, Python >= 3.13, rapidjson
- Build dependencies: CMake, C++17

### Requirements

cIRCa runs exclusively on Linux. On an Ubuntu based system, the system dependencies 
you can install are:

```bash
sudo apt install -y \
  qt6-base-dev libqt6network6 libqt6gui6 libqt6sql6 libqt6httpserver6 libqt6concurrent6 \
  build-essential \
  pkg-config \
  cmake \
  ccache \
  rapidjson-dev \
  libsodium-dev
```

`libqt6httpserver6` may fail to install, as some older Ubuntu 
versions do not have it. In this case it is best you install Qt6 manually 
via the [Qt online installer](https://www.qt.io/download-open-source).

### libminisign

This dependency you need to install manually:

```bash
git clone https://github.com/kroketio/libminisign.git
cd libminisign
cmake -Bbuild .
sudo make -Cbuild -j4 install
```

### Compilation

Using CMake we'll do an out-of-tree build. Move to the cIRCa root directory and:

```bash
cmake -Bbuild .
```

In the case you have Qt6 installed in a custom 
places (e.g. from the online installer), the CMake command is:

```bash
cmake -DCMAKE_PREFIX_PATH="/path/to/qt/6.9.0/gcc_64/" -B build .
```

If the CMake configure succeeds, you can continue to compile using `make`.

```bash
make -Cbuild -j6
```

### Running

The `cIRCa` executable will be placed in `build/bin/`