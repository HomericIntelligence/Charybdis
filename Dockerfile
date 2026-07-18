# uv binary source — pulled as a named stage so `COPY --from=uv` resolves
# identically under both podman/buildah and docker. A bare
# `COPY --from=ghcr.io/astral-sh/uv:<tag>@<digest>` (tag AND digest together) is
# rejected by buildah with "no stage or image found with that name", so we alias
# the digest-pinned image to a stage name here and COPY from the alias.
# ADR-018: uv manages the CMake/Ninja/Conan build toolchain as locked wheels.
FROM ghcr.io/astral-sh/uv:0.11.21@sha256:ff07b86af50d4d9391d9daf4ff89ce427bc544f9aae87057e69a1cc0aa369946 AS uv

# Pinned to the multi-arch index digest of `ubuntu:24.04` resolved on 2026-05-11.
# Renovate/Dependabot bumps must update both this builder stage and the runtime
# stage below in lockstep so the audit trail stays reproducible. See #131 / #152.
FROM ubuntu:24.04@sha256:c4a8d5503dfb2a3eb8ab5f807da5bc69a85730fb49b5cfca2330194ebcc41c7b AS builder

# System toolchain from apt: the C++ compiler (gcc-14/g++-14), make, git, CA
# certs, and the ASan/UBSan runtimes. CMake/Ninja/Conan are NOT installed here —
# they come from uv (ADR-018) as locked PyPI wheels, provisioned below.
RUN apt-get update && apt-get upgrade -y && apt-get install -y --no-install-recommends \
    make \
    gcc-14 \
    g++-14 \
    git \
    ca-certificates \
    libasan8 \
    libubsan1 \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100 \
    && update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-14 100 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-14 100 \
    && rm -rf /var/lib/apt/lists/*

# Install the uv binary from the named stage above.
COPY --from=uv /uv /uvx /usr/local/bin/

ARG USER_ID=10001
ARG GROUP_ID=10001
ARG USER_NAME=builder

RUN groupadd -g ${GROUP_ID} ${USER_NAME} \
    && useradd -m -u ${USER_ID} -g ${GROUP_ID} ${USER_NAME} \
    && chmod 755 /home/${USER_NAME} \
    && mkdir -p /src && chown ${USER_ID}:${GROUP_ID} /src \
    && mkdir -p /install && chown ${USER_ID}:${GROUP_ID} /install

# Provision the uv-managed build toolchain (CMake/Ninja/Conan/gcovr) into a
# fixed, world-readable project environment. Only the manifest + lockfile are
# needed to resolve the toolchain, so this layer caches independently of source.
# UV_LINK_MODE=copy keeps the venv self-contained; UV_PROJECT_ENVIRONMENT pins
# its path so the non-root builder user can use it after the chown below.
#
# This image ships no system Python (apt python3/python3-venv were dropped per
# ADR-018), so `uv sync` downloads a uv-managed CPython to satisfy the venv. By
# default that interpreter lands under root's private home (~/.local/share/uv,
# mode 0700), which the venv's bin/python symlink then points at — unreachable
# once we drop to the non-root builder user (every conan/cmake/ninja call would
# fail with "Permission denied", exit 126). UV_PYTHON_INSTALL_DIR relocates the
# managed interpreter to a shared path under /opt so it can be chowned to the
# builder user alongside the venv and stays traversable after `USER` switches.
ENV UV_PROJECT_ENVIRONMENT=/opt/charybdis-venv \
    UV_PYTHON_INSTALL_DIR=/opt/uv-python \
    UV_LINK_MODE=copy \
    UV_COMPILE_BYTECODE=1 \
    PATH="/opt/charybdis-venv/bin:$PATH"
WORKDIR /src
COPY pyproject.toml uv.lock ./
RUN uv sync --locked --no-install-project \
    && chown -R ${USER_ID}:${GROUP_ID} /opt/charybdis-venv /opt/uv-python

USER ${USER_NAME}

# Copy Conan files first for dependency caching. Conan/CMake/Ninja are on PATH
# from the uv venv above.
COPY conanfile.py ./
COPY conan/ conan/
RUN conan install . \
    --output-folder=build \
    --profile:all=conan/profiles/default \
    --build=missing

# Copy CMake configuration.
COPY CMakeLists.txt CMakePresets.json ./
COPY cmake/ cmake/

# Copy source tree.
COPY include/ include/
COPY src/ src/
COPY test/ test/

RUN cmake -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCharybdis_BUILD_TESTING=ON \
    -DCharybdis_ENABLE_CLANG_TIDY=OFF \
    -DCharybdis_ENABLE_CPPCHECK=OFF \
    && cmake --build build

# Run tests as part of the build to validate.
RUN ctest --test-dir build --output-on-failure

# Install to /install (pre-created and owned by ${USER_NAME} above so the
# non-root builder user can write to it). The CLI target is defined in
# CMakeLists.txt as `${PROJECT_NAME}_cli` with `OUTPUT_NAME ${PROJECT_NAME}`
# and installed via GNUInstallDirs RUNTIME DESTINATION → `bin`, so the binary
# ends up at `/install/bin/Charybdis`.
RUN cmake --install build --prefix /install

# ---------------------------------------------------------------------------
# Runtime stage — minimal image containing only the compiled binary.
# ---------------------------------------------------------------------------
FROM ubuntu:24.04@sha256:c4a8d5503dfb2a3eb8ab5f807da5bc69a85730fb49b5cfca2330194ebcc41c7b AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -r -s /bin/false charybdis

# Source path mirrors the install rule above; renaming to `charybdis` keeps
# the runtime invocation short and decoupled from the CMake project name.
COPY --from=builder /install/bin/Charybdis /usr/local/bin/charybdis

USER charybdis

ENTRYPOINT ["/usr/local/bin/charybdis"]
