FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    ninja-build \
    g++ \
    git \
    ca-certificates \
    python3 \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

RUN pip3 install --break-system-packages conan

WORKDIR /src

# Copy Conan files first for dependency caching.
COPY conanfile.py ./
COPY conan/ conan/
RUN conan install . \
    --output-folder=build \
    --profile=conan/profiles/default \
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
    -DProjectCharybdis_BUILD_TESTING=ON \
    -DProjectCharybdis_ENABLE_CLANG_TIDY=OFF \
    -DProjectCharybdis_ENABLE_CPPCHECK=OFF \
    && cmake --build build

# Run tests as part of the build to validate.
RUN ctest --test-dir build --output-on-failure

RUN cmake --install build --prefix /install

FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /install/bin/ProjectCharybdis /usr/local/bin/ProjectCharybdis

ENTRYPOINT ["/usr/local/bin/ProjectCharybdis"]
