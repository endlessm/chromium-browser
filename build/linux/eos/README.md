# Helper build scripts for EOS

A series of scripts to aid when building (incl. cross building) chromium for EOS.

# Requirements
- Supported host architectures: `amd64`
- Supported target architectures: `amd64`, `armhf`

# Usage

## Setting up host

The build scripts assume that the host can run binaries for the target architecture. To do that we recommended using the [multiarch/qemu-user-static](https://github.com/multiarch/qemu-user-static) Docker image to enable the execution of binaries from multiple architectures via [QEMU](https://www.qemu.org) and [binfmt_misc](https://www.kernel.org/doc/html/latest/admin-guide/binfmt-misc.html), as follows:
  ```
  $ sudo podman run --rm --privileged multiarch/qemu-user-static --reset -p yes
  ```

## Building

The build scripts require some packages to be available in the host and for that we recommend using an `eos-toolbox` container to avoid having to modify your host system.

Once the `eos-toolbox` container is setup, proceed with configuring the build:
- Install host build deps:
  ```
  $ build/linux/eos/install-build-deps.sh -a $target_arch
  ```
- Install sysroot for target architecture (where `$ostree_staging_pwd` is the password for the staging ostree repo):
  ```
  $ build/linux/eos/sysroot_scripts/install-sysroot.sh -a $target_arch $ostree_staging_pwd
  ```
- Bootstrap:
  ```
  $ build/linux/eos/bootstrap.sh -a $target_arch
  ```

Now you should be ready to build, to do that run:
  ```
  $ ninja -C out/Release-$target_arch chrome
  ```

# Updating scripts

The following should be kept in sync with the packaging where applicable (e.g. new build deps or updated configure flags):
- `build/linux/eos/gn-args-common`
- `build/linux/eos/sysroot_scripts/install-build-deps.sh`

# Limitations
- Currently requires a target sysroot even for native builds
- Generated sysroots are quite big, could be made smaller if that becomes an issue
- Only supports release builds
