FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get upgrade -y && apt-get install -y --no-install-recommends \
    cmake \
    ninja-build \
    g++ \
    git \
    ca-certificates \
    python3 \
    python3-pip \
    python3-venv \
    && rm -rf /var/lib/apt/lists/*

ARG USER_ID=1000
ARG GROUP_ID=1000
ARG USER_NAME=builder

RUN groupadd -g ${GROUP_ID} ${USER_NAME} \
    && useradd -m -u ${USER_ID} -g ${GROUP_ID} ${USER_NAME} \
    && chmod 755 /home/${USER_NAME} \
    && mkdir -p /src && chown ${USER_ID}:${GROUP_ID} /src

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
