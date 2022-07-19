FROM debian:bullseye AS builder
LABEL maintainer="mario.bielert@tu-dresden.de"

RUN useradd -m metricq
RUN apt-get update && apt-get install -y \
      cmake \
      git \
      libprotobuf-dev \
      protobuf-compiler \
      build-essential \
      libssl-dev \
      && rm -rf /var/lib/apt/lists/*

USER metricq
COPY --chown=metricq:metricq . /home/metricq/metricq

WORKDIR /home/metricq/metricq
RUN mkdir build

WORKDIR /home/metricq/metricq/build
RUN cmake -DCMAKE_BUILD_TYPE=Release .. && make -j 2
RUN make package


FROM debian:bullseye

RUN apt-get update && apt-get install -y \
      libssl1.1 \
      libprotobuf23 \
      tzdata \
      && rm -rf /var/lib/apt/lists/*

RUN useradd -m metricq
COPY --chown=metricq:metricq --from=builder /home/metricq/metricq/build/metricq-*-Linux.sh /home/metricq/metricq-Linux.sh

USER root
RUN /home/metricq/metricq-Linux.sh --skip-license --prefix=/usr
USER metricq
