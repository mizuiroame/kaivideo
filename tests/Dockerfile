# What the host tests need:  docker build -t kaivideo-host tests
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
      gcc make libc6-dev util-linux python3 \
      libavformat-dev libavcodec-dev libswresample-dev libavutil-dev \
      libass-dev libjansson-dev libcurl4-openssl-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /w
