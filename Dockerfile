# Build and test forma on a chosen Ubuntu.
#
#   docker build --build-arg UBUNTU=22.04 -t forma:22.04 .
#   docker build --build-arg UBUNTU=24.04 -t forma:24.04 .
#   docker run --rm forma:24.04
#
# 22.04 is the oldest image this file supports: the kernel is C++20, and SFML 3.1
# wants CMake 3.28, which 22.04 does not ship. The file installs a newer CMake when
# the distro one is older. Distro packages of SFML are 2.x, so SFML 3.1 is built here.
ARG UBUNTU=24.04
FROM ubuntu:${UBUNTU}

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        curl \
        git \
        ninja-build \
        pkg-config \
        libx11-dev \
        libxrandr-dev \
        libxcursor-dev \
        libxi-dev \
        libudev-dev \
        libfreetype6-dev \
        libflac-dev \
        libvorbis-dev \
        libgl1-mesa-dev \
        libegl1-mesa-dev \
        libharfbuzz-dev \
        libmbedtls-dev \
        fonts-liberation \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 22.04's CMake is 3.22. SFML 3.1 refuses anything older than 3.28.
RUN ver=$(cmake --version | awk 'NR==1 { print $3 }') \
    && if dpkg --compare-versions "$ver" lt 3.28; then \
         arch=$(uname -m); \
         case "$arch" in \
           x86_64) plat=linux-x86_64 ;; \
           aarch64) plat=linux-aarch64 ;; \
           *) echo "no CMake binary for $arch" >&2; exit 1 ;; \
         esac; \
         curl -fsSL -o /tmp/cmake.tgz \
           "https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-${plat}.tar.gz"; \
         tar -C /opt -xzf /tmp/cmake.tgz; \
         rm /tmp/cmake.tgz; \
         ln -sfn "/opt/cmake-3.31.6-${plat}/bin/cmake" /usr/local/bin/cmake; \
         ln -sfn "/opt/cmake-3.31.6-${plat}/bin/ctest" /usr/local/bin/ctest; \
         ln -sfn "/opt/cmake-3.31.6-${plat}/bin/cpack" /usr/local/bin/cpack; \
       fi \
    && cmake --version

ARG SFML_VERSION=3.1.0
RUN git clone --depth 1 --branch ${SFML_VERSION} https://github.com/SFML/SFML.git /tmp/sfml \
    && cmake -S /tmp/sfml -B /tmp/sfml/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_INSTALL_PREFIX=/usr/local \
         -DSFML_BUILD_EXAMPLES=OFF \
         -DSFML_BUILD_TEST_SUITE=OFF \
         -DSFML_BUILD_DOC=OFF \
    && cmake --build /tmp/sfml/build \
    && cmake --install /tmp/sfml/build \
    && ldconfig \
    && rm -rf /tmp/sfml

WORKDIR /src
COPY . /src

RUN cmake -S /src -B /src/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DSFML_DIR=/usr/local/lib/cmake/SFML \
    && cmake --build /src/build \
    && test -x /src/build/forma_tests \
    && test -x /src/build/forma_playground \
    && test -x /src/build/forma_editor \
    && /src/build/forma_tests

CMD ["/src/build/forma_tests"]
