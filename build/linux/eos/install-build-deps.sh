#!/bin/bash -e
# -*- mode: Shell-script; sh-basic-offset: 2; indent-tabs-mode: nil -*-
#

usage()
{
  echo "Install chromium build deps"
  echo
  echo "Usage:"
  echo "  $0 [options]"
  echo
  echo "Options:"
  echo "  -a ARCH           Target architecture (default: host arch, supported: amd64, armhf)"
  echo "  -h                Display this help and exit"
  echo
}

host_arch=$(dpkg --print-architecture)
target_arch=${host_arch}

while getopts "a:h" flag; do
  case "${flag}" in
    a) target_arch=${OPTARG}
       ;;
    h) usage
       exit 1
       ;;
  esac
done

if [ "${host_arch}" != "amd64" ]; then
  echo "ERROR: Invalid host architecture (supported: amd64)" >&2
  echo
  usage
  exit 1
fi

if [ "${target_arch}" != "amd64" ] && [ "${target_arch}" != "armhf" ]; then
  echo "ERROR: Invalid target architecture" >&2
  echo
  usage
  exit 1
fi

srcdir="$(realpath $(dirname $0))"
top_srcdir="${srcdir}"/../../..

. "${srcdir}"/common-functions

# libcups is needed for cups-config
pkglist="\
  bison
  clang-6.0
  default-jre-headless
  gperf
  gzip
  lld-6.0
  libcups2-dev
  libexpat1-dev
  libglib2.0-dev
  libkrb5-dev
  libnss3-dev
  nodejs
  ostree
  pkg-config
"

if [ "${target_arch}" = "armhf" ]; then
  pkglist+=" gcc-8-arm-linux-gnueabihf"
fi

echo "Installing packages..."
sudo apt-get -y update && \
    sudo apt-get -y install ${pkglist}

# FIXME: required to run ARM binaries during build (??)
if [ "${target_arch}" = "armhf" ] && [ ! -f /lib/ld-linux-armhf.so.3 ]; then
  echo "Fixing link for /lib/ld-linux-armhf.so.3..."
  sudo ln -s /usr/arm-linux-gnueabihf/lib/ld-linux-armhf.so.3 /lib/ld-linux-armhf.so.3
fi

echo
echo "Done!"
