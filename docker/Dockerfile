# Build and benchmark environment for Mako with the ALEX index.
#
#   docker build -t learned-index-mako docker/
#   docker run --rm -it -v "$PWD:/lim" learned-index-mako
#
# Dependencies come from Mako's own apt_packages.sh at the pinned commit, so the image
# matches what upstream builds with.
FROM ubuntu:22.04

ARG MAKO_COMMIT=0b62cf1a97508a70bcedfd9fbeb62d8b36537b4d
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
      sudo curl wget git ca-certificates build-essential cmake patch rsync time procps psmisc \
    && rm -rf /var/lib/apt/lists/*

ADD https://raw.githubusercontent.com/makodb/mako/${MAKO_COMMIT}/apt_packages.sh /tmp/apt_packages.sh
RUN bash /tmp/apt_packages.sh && rm -rf /tmp/apt_packages.sh /var/lib/apt/lists/*

RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal
ENV PATH="/root/.cargo/bin:${PATH}"

# The checkout is bind-mounted and owned by the host user; let git operate on it.
RUN git config --global --add safe.directory '*'

ENV MODE=perf
WORKDIR /lim
CMD ["/bin/bash"]
