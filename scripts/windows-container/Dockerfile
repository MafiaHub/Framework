FROM docker.io/library/ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    wine64 winbind xvfb xauth python3 msitools ca-certificates git curl \
    p7zip-full unzip cabextract && rm -rf /var/lib/apt/lists/*

# Use the VS 2022 toolset, matching the native build. Keep the download manifest
# in the image so the exact Microsoft package versions remain inspectable.
ARG MSVC_WINE_REVISION=514f8ea34842cd6d831804d0e9658d3a32870ae1
RUN git init /opt/msvc-wine && cd /opt/msvc-wine && \
    git fetch --depth=1 https://github.com/mstorsjo/msvc-wine.git ${MSVC_WINE_REVISION} && \
    git checkout --detach FETCH_HEAD && \
    python3 vsdownload.py --accept-license --major 17 --architecture x64 \
        --with-msbuild no --save-manifest --dest /opt/msvc

ARG VCPKG_REVISION=5f96cd15fd745122cf27e0524606d6c1efc5fd07
RUN git init /opt/vcpkg && cd /opt/vcpkg && \
    git fetch --depth=1 https://github.com/microsoft/vcpkg.git ${VCPKG_REVISION} && \
    git checkout --detach FETCH_HEAD
COPY install-tools.py /opt/install-tools.py
RUN python3 /opt/install-tools.py

# Ubuntu's Wine 9 misparses the numeric FOR variables in VSDevCmd.bat. Use
# WineHQ's newer cmd implementation, including the 32-bit vcpkg helper runtime.
ARG WINE_VERSION=11.0.0.0~noble-1
RUN dpkg --add-architecture i386 && mkdir -p /etc/apt/keyrings && \
    curl -fsSL https://dl.winehq.org/wine-builds/winehq.key -o /etc/apt/keyrings/winehq.asc && \
    echo 'deb [signed-by=/etc/apt/keyrings/winehq.asc] https://dl.winehq.org/wine-builds/ubuntu/ noble main' > /etc/apt/sources.list.d/winehq.list && \
    apt-get update && apt-get install -y --no-install-recommends \
        winehq-stable=${WINE_VERSION} wine-stable-i386:i386=${WINE_VERSION} && \
    rm -rf /var/lib/apt/lists/*

COPY environment.cmd entrypoint.sh /opt/
# The downloader retains VS shell extensions for IDE components it prunes.
# Use our pinned CMake/vcpkg; the IDE's SSH Connection Manager isn't installed.
RUN rm /opt/msvc/Common7/Tools/vsdevcmd/ext/vcpkg.bat \
       /opt/msvc/Common7/Tools/vsdevcmd/ext/cmake.bat \
       /opt/msvc/Common7/Tools/vsdevcmd/ext/ConnectionManagerExe.bat && \
    chmod +x /opt/entrypoint.sh && chmod -R a+rX /opt
ENV WINEDEBUG=-all WINEDLLOVERRIDES="mshtml=" WINEARCH=win64
WORKDIR /workspace
ENTRYPOINT ["/opt/entrypoint.sh"]
