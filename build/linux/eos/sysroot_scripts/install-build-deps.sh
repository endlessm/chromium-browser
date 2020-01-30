#!/bin/bash -e
# -*- mode: Shell-script; sh-basic-offset: 2; indent-tabs-mode: nil -*-
#
# Install chromium build deps

packages="\
  pkg-config
  lsb-release
  libpulse-dev
  libcups2-dev
  libasound2-dev
  libnss3-dev
  mesa-common-dev
  libnoopenh264-dev
  libfdk-aac-dev
  libgles2-mesa-dev
  libpci-dev
  libxtst-dev
  libxss-dev
  libgtk-3-dev
  libglib2.0-dev
  libudev-dev
  libdrm-dev
  libcap-dev
  libgcrypt-dev
  libkrb5-dev
  libxkbcommon-dev
  libpam0g-dev
  libffi-dev
  libva-dev
  uuid-dev
  chrpath
"

export DEBIAN_FRONTEND=noninteractive
apt-get -y update && \
    apt-get -y install $packages
