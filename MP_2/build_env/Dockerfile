# csce438/mp2:build_env
# --------------------------- NOTES --------------------------------
# Using an amazonlinux image, we'll provision with dependancies then
# build Cmake and gRPC. Some notes are included for faster image
# building in case you already have some files locally.
# 
# Toggle Cmake and gRPC sections then, from this directory, run:
# 
# `docker build -t csce438/mp2:build_env .`
# 
# ------------------------------------------------------------------
FROM amazonlinux:latest

# Install path environment variable
ENV MY_INSTALL_DIR=$HOME/.grpc
RUN mkdir -p $MY_INSTALL_DIR

# Install deps
RUN yum install -y autoconf \
                   libtool \
                   nano \
                   git \
                   wget \
                   gcc-c++ \
                   make \
                   which \
                   pkgconfig \
                   openssl-devel \
                   procps \
    yum clean all

# ------------------ Adding Cmake build file -----------------------
# (EITHER) Use this if you want to fetch Cmake build file
# >>>
# RUN wget -q -O cmake-linux.sh \
#      https://github.com/Kitware/CMake/releases/download/v3.19.6/cmake-3.19.6-Linux-x86_64.sh
# <<<

# (OR) Use this if you have the Cmake build script in this directory
# >>>
ADD ./cmake-linux.sh .
# <<<
# ------------------------------------------------------------------

# Install Cmake
RUN sh cmake-linux.sh -- \
                      --skip-license \
                      --prefix=$MY_INSTALL_DIR

ENV PATH=$MY_INSTALL_DIR/bin:$PATH
RUN rm cmake-linux.sh

# ------------ Adding gRPC and Protocol Buffers --------------------
# (EITHER) Use this if you want to fetch grpc build files
# >>>
RUN git clone --recurse-submodules -b v1.43.0 https://github.com/grpc/grpc
# <<<

# (OR) Use this if you have grpc build file in this directory
# >>>
# RUN mkdir grpc
# ADD ./grpc /grpc 
# <<<
# ------------------------------------------------------------------

# Installing gRPC and Protocol Buffers
RUN mkdir -p /grpc/cmake/build
RUN pushd /grpc/cmake/build; \
    cmake -DgRPC_INSTALL=ON \
          -DgRPC_BUILD_TESTS=OFF \
          -DCMAKE_INSTALL_PREFIX=$MY_INSTALL_DIR \
          ../..; \
    make; \
    make install

# Sourcing environment variables
ENV PKG_CONFIG_PATH=$MY_INSTALL_DIR/lib/pkgconfig/
ENV PKG_CONFIG_PATH=$PKG_CONFIG_PATH:$MY_INSTALL_DIR/lib64/pkgconfig
# ENV PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/usr/bin/openssl