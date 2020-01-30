#!/bin/bash -e
# -*- mode: Shell-script; sh-basic-offset: 2; indent-tabs-mode: nil -*-
#

exec 3>/dev/null

usage()
{
  echo "Bootstrap chromium"
  echo
  echo "Usage:"
  echo "  $0 [options]"
  echo
  echo "Options:"
  echo "  -a ARCH           Target architecture (default: host arch, supported: amd64, armhf)"
  echo "  -v                Enable verbose mode"
  echo "  -h                Display this help and exit"
  echo
}

verbose=false
host_arch=$(dpkg --print-architecture)
target_arch=${host_arch}

while getopts "a:vh" flag; do
  case "${flag}" in
    a) target_arch=${OPTARG}
       ;;
    v) verbose=true
       exec 3>&2
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
  echo "ERROR: Invalid target architecture." >&2
  echo
  usage
  exit 1
fi

srcdir="$(realpath $(dirname $0))"
top_srcdir="${srcdir}"/../../..
builddir="${top_srcdir}"/out/Release-${target_arch}

. "${srcdir}"/common-functions

bootstrap()
{
  CC=clang-6.0 CXX=clang++-6.0 AR=llvm-ar-6.0 \
  "${top_srcdir}"/tools/gn/bootstrap/bootstrap.py \
      --build-path="${builddir}" \
      --skip-generate-buildfiles >&3 2>&1 || fail "Unable to bootstrap build at ${builddir}"
}

configure()
{
  local gn_args="$1"
  local gn_extra_args=
  if ${verbose}; then
    gn_extra_args="${bootstrap_params} -v"
  fi

  "${builddir}"/gn gen --fail-on-unused-args \
      --args="${gn_args}" \
      ${gn_extra_args} \
      "${builddir}" >&3 2>&1 || fail "Unable to configure build at ${builddir}"
}

create_v8_wrapper()
{
  local target_arch="$1"
  local libdir="$2"
  local qemu_arch=$(get_qemu_arch ${target_arch})

  cat > "${builddir}"/v8-wrapper.sh << EOF
#!/bin/bash -e
# This file has been generated automatically.

LD_LIBRARY_PATH="${libdir}" "\$@"
EOF

  chmod 755 "${builddir}"/v8-wrapper.sh
}

chromium_host_arch=$(get_chromium_arch ${host_arch})
chromium_target_arch=$(get_chromium_arch ${target_arch})

# TODO:
# - support different EOS branches (e.g. for stable releases)
# - support building without target sysroot when
#   host_arch=target_arch (i.e. with build deps installed on host)
sysroot="eos_master_${target_arch}-sysroot"
sysrootdir="${top_srcdir}/build/linux/eos/${sysroot}"

. "${srcdir}"/gn-args-common

GN_ARGS="
${GN_ARGS_COMMON}
target_cpu=\"${chromium_target_arch}\"
target_sysroot=\"//build/linux/eos/${sysroot}\"
use_sysroot=false
custom_toolchain=\"//build/toolchain/linux/eos:clang_${chromium_target_arch}\"
host_toolchain=\"//build/toolchain/linux/eos:clang_${chromium_host_arch}\"
v8_snapshot_toolchain=\"//build/toolchain/linux/eos:clang_${chromium_target_arch}\"
v8_use_wrapper=true
"

if [ "${target_arch}" = "amd64" ]; then
  system_libdir="lib/x86_64-linux-gnu"
  GN_ARGS+=" system_libdir=\"${system_libdir}\""
  GN_ARGS+=" use_vaapi=true"
elif [ "${target_arch}" = "armhf" ]; then
  system_libdir="lib/arm-linux-gnueabihf"
  GN_ARGS+=" system_libdir=\"${system_libdir}\""
  GN_ARGS+=" arm_use_neon=true"
  GN_ARGS+=" arm_float_abi=\"hard\""
  GN_ARGS+=" arm_use_thumb=true"
  GN_ARGS+=" use_v4l2_codec=true"
  GN_ARGS+=" symbol_level=0"
fi

mkdir -p "${builddir}"
rel_builddir="$(realpath --relative-to="$(pwd)" "${builddir}")"
echo "Configuring chromium build at ${rel_builddir} (target arch: ${target_arch})..."

echo "Running bootstrap..."
bootstrap

echo "Running configure..."
configure "${GN_ARGS}"

echo "Creating v8 wrapper..."
create_v8_wrapper ${target_arch} "${sysrootdir}/usr/${system_libdir}"

echo
echo "Done! Run \`ninja -C ${rel_builddir} chrome\`."
