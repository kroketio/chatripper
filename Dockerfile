# Base image
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PATH="/opt/venv/bin:$PATH"

# Install dependencies and add deadsnakes PPA for Python 3.13
RUN apt-get update && apt-get install -y --no-install-recommends \
        software-properties-common \
        ca-certificates \
        libgl1-mesa-dev \
        libpng-dev \
        build-essential \
        pkg-config \
        cmake \
        tini \
        ccache \
        git \
        libdbus-1-3 \
        libpulse-mainloop-glib0 \
        rapidjson-dev \
        libsodium-dev \
        curl \
    && add-apt-repository -y ppa:deadsnakes/ppa \
    && apt-get update && apt-get install -y --no-install-recommends \
        python3.13 \
        python3.13-venv \
        python3.13-dev \
    && rm -rf /var/lib/apt/lists/*

# venv
RUN python3.13 -m venv /opt/venv

# pip
RUN /opt/venv/bin/python -m ensurepip --upgrade
RUN /opt/venv/bin/pip install --upgrade pip

# install aqtinstall inside venv
RUN /opt/venv/bin/pip install aqtinstall==3.3.0

# Qt to /opt/qt/
RUN /opt/venv/bin/aqt install-qt linux desktop 6.9.1 linux_gcc_64 \
    --outputdir /opt/qt/ -m qthttpserver qtimageformats qtwebsockets

# install libminisign
RUN git clone https://github.com/kroketio/libminisign.git /tmp/libminisign \
    && cd /tmp/libminisign \
    && cmake -Bbuild . \
    && make -Cbuild -j$(nproc) install \
    && rm -rf /tmp/libminisign

# Set working directory
WORKDIR /app

# Copy project files
COPY . .

# Configure and build project
RUN cmake -Bbuild -DCMAKE_PREFIX_PATH=/opt/qt/6.9.1/gcc_64/ . \
    && make -Cbuild -j$(nproc)

# Use tini as PID 1
ENTRYPOINT ["/usr/bin/tini", "--"]

# Default command
CMD ["./build/bin/circus"]