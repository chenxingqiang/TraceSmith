# TraceSmith Docker Image
#
# Build:
#   docker build -t tracesmith .
#
# Run:
#   docker run -it tracesmith ./bin/tracesmith-cli --help

FROM ubuntu:22.04

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    python3 \
    python3-dev \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /tracesmith

# Copy source code
COPY . .

# Build TraceSmith
RUN mkdir -p build && cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DTRACESMITH_BUILD_TESTS=ON \
        -DTRACESMITH_BUILD_EXAMPLES=ON \
        -DTRACESMITH_BUILD_CLI=ON && \
    cmake --build . -j$(nproc)

# Set PATH to include built binaries
ENV PATH="/tracesmith/build/bin:${PATH}"

# Default command
CMD ["tracesmith-cli", "--help"]
