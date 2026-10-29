# SPDX-License-Identifier: AGPL-3.0-or-later
# Builds the fork's KT_Kick for Linux arm64 in Debian 12 (bookworm), the same distribution as the
# faithful-mode guest, so the binary links against the guest's glibc and libraries.
# Used by modes/vm/artifact.sh --build. Packages: Kick/how-to/Kick_Compilation.txt and the fork's
# .github/workflows/build.yml, minus WebKit (JUCE_WEB_BROWSER=0), plus libxi-dev (JUCE multitouch).
FROM debian:bookworm
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        ca-certificates git gcc g++ make cmake ninja-build pkg-config file python3 \
        libasound2-dev libjack-jackd2-dev libcurl4-openssl-dev libfreetype6-dev \
        libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxi-dev \
        libxrandr-dev libxrender-dev libglu1-mesa-dev mesa-common-dev libserial-dev \
    && rm -rf /var/lib/apt/lists/*
