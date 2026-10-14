# A Linux build environment, for building and testing the Linux port from
# another system (for example an Apple Silicon Mac, which gives linux-arm64).
# See profiles/sarusaru/README.md. The packages match .github/workflows/build.yml.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build python3 pkg-config git ca-certificates xz-utils file \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev \
    libwayland-dev libxkbcommon-dev wayland-protocols libdecor-0-dev \
    libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev libdrm-dev libgbm-dev libegl-dev \
    && rm -rf /var/lib/apt/lists/*
