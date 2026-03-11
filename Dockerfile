FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    nasm \
    qemu-system-x86 \
    mtools \
    python3 \
    xxd \
    xorriso \
    grub-pc-bin \
    grub-common \
    gdb \
    gcc-multilib \
    dosfstools \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /os