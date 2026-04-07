FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV PATH="/root/.cargo/bin:${PATH}"

ARG NATS_C_VERSION=3.12.0

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    clang \
    cmake \
    curl \
    git \
    libssl-dev \
    pkg-config \
    python3 \
  && rm -rf /var/lib/apt/lists/*

# Rust toolchain for the wasm-exec-env crate.
RUN curl https://sh.rustup.rs -sSf | sh -s -- -y --profile minimal

# Build and install the nats.c client library used by the worker.
RUN git clone --depth 1 --branch "v${NATS_C_VERSION}" https://github.com/nats-io/nats.c.git /tmp/nats.c \
  && cmake -S /tmp/nats.c -B /tmp/nats.c/build \
       -DCMAKE_BUILD_TYPE=Release \
       -DNATS_BUILD_TESTS=OFF \
       -DNATS_BUILD_EXAMPLES=OFF \
  && cmake --build /tmp/nats.c/build -j "$(nproc)" \
  && cmake --install /tmp/nats.c/build \
  && rm -rf /tmp/nats.c

WORKDIR /src

COPY CMakeLists.txt ./
COPY external ./external
COPY src ./src

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  && cmake --build build -j "$(nproc)" \
  && strip build/detersl-worker

FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libgcc-s1 \
    libssl3 \
    libstdc++6 \
    zlib1g \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /usr/local /usr/local
COPY --from=builder /src/build/detersl-worker /usr/local/bin/detersl-worker
COPY config.json ./config.json
COPY applications ./applications
COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh

RUN chmod +x /usr/local/bin/entrypoint.sh \
  && ldconfig

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
