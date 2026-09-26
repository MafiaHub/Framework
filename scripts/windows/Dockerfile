FROM node:22-bookworm AS node
FROM ubuntu:24.04

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    wine wine64 winbind python3 msitools ca-certificates git curl zip unzip \
    cmake ninja-build build-essential pkg-config autoconf automake libtool \
    perl nasm file bison && rm -rf /var/lib/apt/lists/*
COPY --from=node /usr/local/ /usr/local/

ARG MSVC_WINE_REV=514f8ea34842cd6d831804d0e9658d3a32870ae1
RUN git init /opt/msvc-wine && cd /opt/msvc-wine && \
    git remote add origin https://github.com/mstorsjo/msvc-wine.git && \
    git fetch --depth 1 origin "$MSVC_WINE_REV" && git checkout --detach FETCH_HEAD
RUN cd /opt/msvc-wine && python3 vsdownload.py --accept-license --major 17 --architecture x64 x86 --dest /opt/msvc && \
    ./install.sh /opt/msvc

ENV PATH="/opt/msvc/bin/x64:${PATH}" WINEDEBUG=-all
WORKDIR /workspace

# Run the dependency build tools as Windows programs too: OpenSSL's vcpkg
# port uses nmake and cmd, and rejects a Linux CMake host.
RUN mkdir -p /opt/windows && cd /opt/windows && \
    curl -fL --retry 3 -o cmake.zip https://github.com/Kitware/CMake/releases/download/v3.31.8/cmake-3.31.8-windows-x86_64.zip && \
    unzip -q cmake.zip && mv cmake-3.31.8-windows-x86_64 cmake && \
    curl -fL --retry 3 -o ninja.zip https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip && \
    unzip -q ninja.zip -d ninja && \
    curl -fL --retry 3 -o git.zip https://github.com/git-for-windows/git/releases/download/v2.55.0.windows.5/MinGit-2.55.0.5-64-bit.zip && \
    unzip -q git.zip -d git && \
    curl -fL --retry 3 -o node.zip https://nodejs.org/dist/v22.22.0/node-v22.22.0-win-x64.zip && \
    unzip -q node.zip && mv node-v22.22.0-win-x64 node && \
    curl -fL --retry 3 -o vcpkg.exe https://github.com/microsoft/vcpkg-tool/releases/download/2026-07-27/vcpkg.exe && \
    rm *.zip

# vcpkg bootstraps its x64 tools with a 32-bit 7zr executable.
RUN dpkg --add-architecture i386 && apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends wine32:i386 && \
    rm -rf /var/lib/apt/lists/*

# Use current Wine for the Windows build tools and PowerShell runtime.
RUN mkdir -p /etc/apt/keyrings && \
    curl -fsSL https://dl.winehq.org/wine-builds/winehq.key -o /etc/apt/keyrings/winehq-archive.key && \
    curl -fsSL https://dl.winehq.org/wine-builds/ubuntu/dists/noble/winehq-noble.sources -o /etc/apt/sources.list.d/winehq.sources && \
    apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends wine-stable=11.0.0.0~noble-1 && \
    rm -rf /var/lib/apt/lists/*
ENV PATH="/opt/wine-stable/bin:${PATH}"

RUN curl -fL --retry 3 -o /opt/windows/python.zip https://www.python.org/ftp/python/3.13.2/python-3.13.2-embed-amd64.zip && \
    unzip -q /opt/windows/python.zip -d /opt/windows/python && rm /opt/windows/python.zip

# libunwind's Linux vcpkg port needs the macro archive at configure time.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    autoconf-archive && rm -rf /var/lib/apt/lists/*
