FROM kalilinux/kali-rolling AS afl-dyninst-base
MAINTAINER mh@mh-sec.de

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get upgrade && apt-get install -y \
        build-essential \
        gcc \
        g++ \
        make \
        cmake \
        git \
        ca-certificates \
        tar \
        gzip \
        vim \
        curl \
        apt-utils \
        libelf-dev \
        libelf1 \
        libiberty-dev \
        libboost-all-dev \
        libtbb2 \
        libtbb-dev \
    && apt-get -y autoremove && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/dyninst/dyninst \
        && cd dyninst && mkdir build && cd build \
        && cmake .. \
        && make \
        && make install \
        && cd .. ; rm -rf build ; cd ..

RUN git clone https://github.com/vanhauser-thc/AFLplusplus \
        && cd AFLplusplus \
        && make source-only \
        && make install \
        && make clean \
        && cd ..

RUN git clone https://github.com/vanhauser-thc/afl-dyninst \
        && cd afl-dyninst \
        && ln -s ../AFLplusplus afl \
        && make \
        && make install \
        && make clean \
        && cd .. \
        && echo "/usr/local/lib" > /etc/ld.so.conf.d/dyninst.conf && ldconfig \
        && echo "export DYNINSTAPI_RT_LIB=/usr/local/lib/libdyninstAPI_RT.so" >> .bashrc

ENV DYNINSTAPI_RT_LIB /usr/local/lib/libdyninstAPI_RT.so

# the afl-dyninst image:
FROM afl-dyninst-base AS afl-dyninst
CMD afl-dyninst.sh

# the afl-fuzz-dyninst image (with afl++):
FROM afl-dyninst-base AS afl-fuzz-dyninst
CMD afl-fuzz-dyninst.sh
