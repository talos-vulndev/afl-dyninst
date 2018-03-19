FROM ubuntu:trusty
MAINTAINER rjohnson@moflow.org/vh@thc.org

# dyninst ubuntu 14.04/x64
RUN apt-get update && apt-get install -y \
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
        libelf-dev \
        libelf1 \
        libiberty-dev \
        libboost-all-dev \
    && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/dyninst/dyninst \
        && cd dyninst && mkdir build && cd build \
        && cmake .. \
        && make \
        && make install \
        && cd ../..

RUN curl http://lcamtuf.coredump.cx/afl/releases/afl-latest.tgz | tar -zxvf - \
        && cd afl-* \
        && make \
        && make install \
        && cd ..

RUN git clone https://github.com/vanhauser-thc/afl-dyninst \
        && cd afl-dyninst \
        && ln -s `ls -d1 ../afl-2* | tail -1` afl \
        && make \
        && make install \
        && cd .. \
        && echo "/usr/local/lib" > /etc/ld.so.conf.d/dyninst.conf && ldconfig \
        && echo "export DYNINSTAPI_RT_LIB=/usr/local/lib/libdyninstAPI_RT.so" >> .bashrc

# output usage and give a shell 
CMD afl-dyninst ; /bin/bash -i
