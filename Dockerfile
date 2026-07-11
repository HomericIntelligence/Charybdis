# Pinned to the multi-arch index digest of `ubuntu:24.04` resolved on 2026-05-11.
# Renovate/Dependabot bumps must update both this builder stage and the runtime
# stage below in lockstep so the audit trail stays reproducible. See #131 / #152.
FROM ubuntu:24.04@sha256:c4a8d5503dfb2a3eb8ab5f807da5bc69a85730fb49b5cfca2330194ebcc41c7b AS builder

RUN apt-get update && apt-get upgrade -y && apt-get install -y --no-install-recommends \
    cmake \
    make \
    ninja-build \
    gcc-14 \
    g++-14 \
    git \
    ca-certificates \
    python3 \
    python3-pip \
    python3-venv \
    libasan8 \
    libubsan1 \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100 \
    && update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-14 100 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-14 100 \
    && rm -rf /var/lib/apt/lists/*

ARG USER_ID=10001
ARG GROUP_ID=10001
ARG USER_NAME=builder

RUN groupadd -g ${GROUP_ID} ${USER_NAME} \
    && useradd -m -u ${USER_ID} -g ${GROUP_ID} ${USER_NAME} \
    && chmod 755 /home/${USER_NAME} \
    && mkdir -p /src && chown ${USER_ID}:${GROUP_ID} /src \
    && mkdir -p /install && chown ${USER_ID}:${GROUP_ID} /install

ENV VIRTUAL_ENV=/opt/conan-venv
RUN python3 -m venv $VIRTUAL_ENV \
    && $VIRTUAL_ENV/bin/pip install --upgrade pip \
    && $VIRTUAL_ENV/bin/pip install "conan==2.12.1" \
    && chown -R ${USER_ID}:${GROUP_ID} $VIRTUAL_ENV
ENV PATH="$VIRTUAL_ENV/bin:$PATH"

USER ${USER_NAME}

WORKDIR /src

# Copy Conan files first for dependency caching.
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
