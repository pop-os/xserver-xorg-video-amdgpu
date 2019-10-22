#!/bin/bash

set -e
set -o xtrace

echo 'deb-src https://deb.debian.org/debian testing main' >/etc/apt/sources.list.d/deb-src.list
apt-get update
apt-get install -y git ca-certificates build-essential automake autoconf libtool pkg-config

echo 'APT::Get::Build-Dep-Automatic "true";' >>/etc/apt/apt.conf
apt-get build-dep -y xorg-server

git clone https://gitlab.freedesktop.org/xorg/lib/libXfont.git
cd libXfont
git checkout libXfont-1.5-branch
./autogen.sh
make install-pkgconfigDATA
cd .. && rm -rf libXfont

git clone https://gitlab.freedesktop.org/xorg/xserver.git
cd xserver

git checkout server-1.13-branch
./autogen.sh --prefix=/usr/local/xserver-1.13 --enable-dri2
make -C include install-nodist_sdkHEADERS
make install-headers install-aclocalDATA install-pkgconfigDATA clean

git checkout server-1.14-branch
./autogen.sh --prefix=/usr/local/xserver-1.14 --enable-dri2
make -C include install-nodist_sdkHEADERS
make install-headers install-aclocalDATA install-pkgconfigDATA clean

git checkout server-1.15-branch
./autogen.sh --prefix=/usr/local/xserver-1.15 --enable-dri2
make -C include install-nodist_sdkHEADERS
make install-headers install-aclocalDATA install-pkgconfigDATA clean

git checkout server-1.16-branch
./autogen.sh --prefix=/usr/local/xserver-1.16 --enable-dri2 --enable-dri3 --enable-glamor
make -C include install-nodist_sdkHEADERS
make install-headers install-aclocalDATA install-pkgconfigDATA clean

git checkout server-1.17-branch
./autogen.sh --prefix=/usr/local/xserver-1.17 --enable-dri2 --enable-dri3 --enable-glamor
make -C include install-nodist_sdkHEADERS
make install-headers install-aclocalDATA install-pkgconfigDATA clean

git checkout server-1.18-branch
./autogen.sh --prefix=/usr/local/xserver-1.18 --enable-dri2 --enable-dri3 --enable-glamor
make -C include install-nodist_sdkHEADERS
make install-headers install-aclocalDATA install-pkgconfigDATA clean

git checkout server-1.19-branch
./autogen.sh --prefix=/usr/local/xserver-1.19 --enable-dri2 --enable-dri3 --enable-glamor
make -C include install-nodist_sdkHEADERS
make install-headers install-aclocalDATA install-pkgconfigDATA clean

git checkout server-1.20-branch
./autogen.sh --prefix=/usr/local/xserver-1.20 --enable-dri2 --enable-dri3 --enable-glamor
make -C include install-nodist_sdkHEADERS
make install-headers install-aclocalDATA install-pkgconfigDATA clean
cd .. && rm -rf xserver

apt-get install -y clang xutils-dev libdrm-dev libgl1-mesa-dev libgbm-dev libudev-dev \
        x11proto-dev libpixman-1-dev libpciaccess-dev
apt-get purge -y git ca-certificates
apt-get autoremove -y --purge
