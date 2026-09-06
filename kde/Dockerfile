FROM invent-registry.kde.org/neon/docker-images/plasma@sha256:de4b6e2d98b30cacead1dc5923d875cd055a1758bea74abfa627d39c5b1b613f

USER root

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        cmake \
        extra-cmake-modules \
        g++ \
        libkf6plasma-dev \
        ninja-build \
        qt6-base-dev \
        qt6-declarative-dev \
    && rm -rf /var/lib/apt/lists/*
