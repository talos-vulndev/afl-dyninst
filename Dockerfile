FROM kalilinux/kali-rolling AS afl-dyninst
MAINTAINER vh@thc.org

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get -y upgrade && apt-get -y install \
        build-essential \
        gcc \
        g++ \
        make \
        cmake \
        git \
        gdb \
        ca-certificates \
        tar \
        gzip \
        vim \
        joe \
        wget \
        curl \
        apt-utils \
        libelf-dev \
        libelf1 \
        libiberty-dev \
        libboost-all-dev \
        libdw-dev \
        libtbb2 \
        libtbb-dev \
    && apt-get -y autoremove && rm -rf /var/lib/apt/lists/*

RUN git clone --depth=1 https://github.com/dyninst/dyninst \
        && cd dyninst && mkdir build && cd build \
        && cmake .. \
        && make -j3 \
        && make install

RUN git clone --depth=1 https://github.com/AFLplusplus/AFLplusplus \
        && cd AFLplusplus \
        && make source-only \
        && make install

RUN git clone --depth=1 https://github.com/vanhauser-thc/afl-dyninst \
        && cd afl-dyninst \
        && ln -s ../AFLplusplus afl \
        && make \
        && make install

RUN echo "/usr/local/lib" > /etc/ld.so.conf.d/dyninst.conf && ldconfig \
        && echo "export DYNINSTAPI_RT_LIB=/usr/local/lib/libdyninstAPI_RT.so" >> .bashrc

RUN rm -rf afl-dyninst AFLplusplus dyninst

ENV DYNINSTAPI_RT_LIB /usr/local/lib/libdyninstAPI_RT.so
