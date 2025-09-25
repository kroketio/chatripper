## Building

### Requirements

cIRCus runs exclusively on Linux. On an Ubuntu based system, the dependencies 
required are:

```bash
sudo apt install -y \
  build-essential \
  qt6-base-dev libqt6network6 libqt6gui6 libqt6sql6 libqt6httpserver6 libqt6concurrent6 \
  rapidjson-dev \
  libsodium-dev \
  cmake ccache
```

`libqt6httpserver6` may fail to install, as some older Ubuntu 
versions do not have it. In this case it is best you install Qt6 manually 
via the [Qt online installer](https://www.qt.io/download-open-source).

### Compilation

Using CMake we'll do an out-of-tree build.

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

The `circus` executable will be placed in `build/bin/`