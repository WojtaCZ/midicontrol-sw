# syntax=docker/dockerfile:1
FROM debian:trixie AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake pkg-config librtmidi-dev libasound2-dev \
    && rm -rf /var/lib/apt/lists/*
ARG GIT_SHA=unknown
WORKDIR /src
COPY CMakeLists.txt ./
COPY include include
COPY src src
COPY tests tests
COPY songs songs
COPY packaging packaging
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMC_GIT_HASH="${GIT_SHA}" \
    && cmake --build build -j"$(nproc)" \
    && ctest --test-dir build --output-on-failure

FROM debian:trixie-slim
LABEL org.opencontainers.image.source="https://github.com/WojtaCZ/midicontrol-sw"
# librtmidi7 is the runtime package on Debian 13 (trixie); bump the base images and this name together.
RUN apt-get update && apt-get install -y --no-install-recommends \
        librtmidi7 alsa-utils curl jq docker-cli docker-compose ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/midicontrol /usr/local/bin/midicontrol
COPY docker/config.container.conf /usr/local/share/midicontrol/config.conf
COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
COPY docker/updater/lib.sh /usr/local/lib/midicontrol/lib.sh
COPY docker/updater/updater.sh /usr/local/bin/midicontrol-updater
RUN chmod +x /usr/local/bin/entrypoint.sh /usr/local/bin/midicontrol-updater
VOLUME /data
EXPOSE 50118
HEALTHCHECK --interval=30s --timeout=5s --start-period=30s --retries=3 \
    CMD curl -fs http://127.0.0.1:50118/api/status || exit 1
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
