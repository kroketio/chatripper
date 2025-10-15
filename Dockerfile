FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PATH="/opt/venv/bin:$PATH:/usr/lib/ccache"
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8
ENV CC="ccache gcc"
ENV CXX="ccache g++"
ENV CCACHE_DIR=/ccache

RUN apt-get update && apt-get install -y locales software-properties-common \
        ca-certificates libgl1-mesa-dev libpng-dev build-essential pkg-config \
        cmake tini ccache git libdbus-1-3 libpulse-mainloop-glib0 rapidjson-dev \
        libsodium-dev curl \
    && locale-gen C.UTF-8 && update-locale LANG=C.UTF-8 \
    && add-apt-repository -y ppa:deadsnakes/ppa \
    && apt-get update && apt-get install -y python3.13 python3.13-venv python3.13-dev \
    && rm -rf /var/lib/apt/lists/*

RUN python3.13 -m venv /opt/venv
RUN /opt/venv/bin/python -m ensurepip --upgrade
RUN /opt/venv/bin/pip install --upgrade pip aqtinstall==3.3.0 requests beautifulsoup4 pillow pyyaml

RUN /opt/venv/bin/aqt install-qt linux desktop 6.9.1 linux_gcc_64 \
    --outputdir /opt/qt/ -m qthttpserver qtimageformats qtwebsockets

RUN git clone https://github.com/kroketio/libminisign.git /tmp/libminisign \
    && cd /tmp/libminisign \
    && cmake -Bbuild . \
    && make -Cbuild -j$(nproc) install \
    && rm -rf /tmp/libminisign

WORKDIR /app

ENTRYPOINT ["bash", "-c", "cmake -Bbuild -DCMAKE_PREFIX_PATH=/opt/qt/6.9.1/gcc_64/ . && make -Cbuild -j$(nproc) && ./build/bin/chatripper"]
