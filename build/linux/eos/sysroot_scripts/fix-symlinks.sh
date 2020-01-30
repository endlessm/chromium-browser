#!/bin/bash -e
# -*- mode: Shell-script; sh-basic-offset: 2; indent-tabs-mode: nil -*-
#

export DEBIAN_FRONTEND=noninteractive
apt-get -y update && \
    apt-get -y install symlinks

pushd /
symlinks -r -c .
popd
