#!/usr/bin/env bash
#
# TraceSmith Cloud Agent install script.
#
# Idempotent: safe to run repeatedly. Installs system build dependencies,
# builds the C++ library / CLI / examples / tests, and installs the Python
# package (with pybind11 bindings) into a project-local virtualenv.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

echo "==> Installing system build dependencies"
export DEBIAN_FRONTEND=noninteractive
sudo apt-get update -qq
sudo apt-get install -y --no-install-recommends \
  build-essential \
  cmake \
  ninja-build \
  git \
  pkg-config \
  libunwind-dev \
  python3 \
  python3-dev \
  python3-venv \
  python3-pip

# On some base images the default cc/c++ resolve to a clang toolchain that
# cannot find libstdc++, breaking the C++17 build. Pin gcc/g++ as the default
# compilers so every build (CMake and pip) works out of the box.
echo "==> Pinning gcc/g++ as the default compiler"
sudo update-alternatives --install /usr/bin/cc  cc  /usr/bin/gcc 100
sudo update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++ 100
sudo update-alternatives --set cc  /usr/bin/gcc
sudo update-alternatives --set c++ /usr/bin/g++

# Build the C++ library, CLI, examples, and unit tests (Release).
echo "==> Configuring and building C++ (Release, Ninja)"
CC=gcc CXX=g++ cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DTRACESMITH_BUILD_TESTS=ON \
  -DTRACESMITH_BUILD_EXAMPLES=ON \
  -DTRACESMITH_BUILD_CLI=ON
cmake --build build --parallel "$(nproc)"

# Build and install the Python package (editable) into a local virtualenv.
echo "==> Creating virtualenv and installing tracesmith (editable)"
if [ ! -d .venv ]; then
  python3 -m venv .venv
fi
# shellcheck disable=SC1091
source .venv/bin/activate
pip install --quiet --upgrade pip
pip install --quiet pytest numpy
CC=gcc CXX=g++ pip install -e .

echo "==> Install complete"
echo "    C++ binaries : build/bin/ (e.g. build/bin/tracesmith --version)"
echo "    Python venv  : source .venv/bin/activate  ->  import tracesmith / tracesmith-cli"
