#!/bin/bash -e
# -*- mode: Shell-script; sh-basic-offset: 2; indent-tabs-mode: nil -*-
#

usage()
{
  echo "Install Endless OS sysroots for building chromium"
  echo
  echo "Usage:"
  echo "  $0 [options] PASSWORD"
  echo
  echo "Options:"
  echo "  -a ARCH           Target architecture (default: host arch, supported: amd64, armhf)"
  echo "  -h                Display this help and exit"
  echo
  echo "Arguments:"
  echo "  PASSWORD          Development password (for staging ostree branch)"
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

password="${@:$OPTIND:1}"

if [ -z ${password} ]; then
  usage
  exit 1
fi

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
top_srcdir="${srcdir}"/../../../..
outdir="${srcdir}"/..
tmpdir=`mktemp -d ${TMPDIR:-/tmp}/eos-sysroot.XXXXXX`

. "${srcdir}"/../common-functions

# TODO: support different EOS branches (e.g. for stable releases)
eos_branch="master"

if [ "${target_arch}" = "amd64" ] ; then
  eos_platform="amd64"
elif [ "${target_arch}" == "armhf" ] ; then
  eos_platform="ec100"
fi

export_ostree()
{
  local OSTREE_BASE_URL="https://endless:${password}@origin.ostree.endlessm.com/staging/dev"
  local ostree_tarball="$1"
  local ostree_repo="${tmpdir}/repo"
  local ostree_remote='endless'
  local ostree_url="${OSTREE_BASE_URL}/eos-${target_arch}"
  local ostree_keyring="/usr/share/keyrings/eos-ostree-keyring.gpg"
  local ostree_ref="os/eosminbase/${eos_platform}/${eos_branch}"

  echo "Fetching eosminbase ostree from ${ostree_ref}..."
  ostree init --mode="bare-user" --repo="${ostree_repo}" || fail "Initializing repo"
  ostree remote add --repo="${ostree_repo}" "${ostree_remote}" "${ostree_url}"
  ostree remote gpg-import --repo="${ostree_repo}" --keyring="${ostree_keyring}" "${ostree_remote}"
  ostree pull --repo="${ostree_repo}" --depth=0 "${ostree_remote}" "${ostree_ref}" || fail "Pulling ${ostree_ref} from repo ${ostree_url}"

  echo "Exporting eosminbase ostree from ${ostree_ref} to ${ostree_tarball}..."
  ostree export --repo="${ostree_repo}" "${ostree_ref}" | gzip > "${ostree_tarball}"
}

create_sysroot()
{
  local ostree_tarball="$1"
  local sysroot_outdir="$2"

  echo "Building sysroot at ${sysroot_outdir}..."
  tar -xf "${ostree_tarball}" -C "${sysroot_outdir}"

  mv "${sysroot_outdir}"/usr/etc "${sysroot_outdir}"/

  cp /etc/resolv.conf "${sysroot_outdir}"/etc

  # The real homedir is /home and not /sysroot/home, update accordingly
  sed -i "s#/sysroot/home#/home#g" \
      "${sysroot_outdir}"/etc/passwd* \
      "${sysroot_outdir}"/etc/adduser.conf \
      "${sysroot_outdir}"/etc/default/useradd

  # Remove broken links
  rm -rf "${sysroot_outdir}"/ostree "${sysroot_outdir}"/.fscrypt

  # Fix /opt and /root links
  mkdir -p "${sysroot_outdir}"/var/opt "${sysroot_outdir}"/var/roothome

  # Fix /tmp permissions
  chmod ugo+rwXt "${sysroot_outdir}"/tmp

  mkdir "${sysroot_outdir}"/tmp/sysroot_scripts/
  install -m0755 "${srcdir}"/cleanup-cache.sh "${sysroot_outdir}"/tmp/sysroot_scripts/
  install -m0755 "${srcdir}"/fix-symlinks.sh "${sysroot_outdir}"/tmp/sysroot_scripts/
  install -m0755 "${srcdir}"/install-build-deps.sh "${sysroot_outdir}"/tmp/sysroot_scripts/

  echo "Installing build deps on sysroot..."
  sudo chroot "${sysroot_outdir}" \
      /tmp/sysroot_scripts/install-build-deps.sh
  echo "Fixing sysroot symlinks..."
  sudo chroot "${sysroot_outdir}" \
      /tmp/sysroot_scripts/fix-symlinks.sh
  echo "Cleaning up sysroot cache..."
  sudo chroot "${sysroot_outdir}" \
      /tmp/sysroot_scripts/cleanup-cache.sh
  rm -rf "${sysroot_outdir}"/tmp/sysroot_scripts/

  echo > "${sysroot_outdir}"/etc/resolv.conf

  # Fix sysroot permissions
  local user_id=$(id -u)
  local group_id=$(id -g)
  echo "Fixing sysroot permissions..."
  sudo chown -R ${user_id}:${group_id} "${sysroot_outdir}"
}

echo "Setting up target sysroot..."
ostree_tarball_filename="eosminbase-${eos_branch}-${target_arch}.tar.gz"
ostree_tarball="${outdir}/${ostree_tarball_filename}"

if test -f "${ostree_tarball}"; then
  prompt="Found eosminbase ostree tarball for arch ${target_arch} at ${ostree_tarball}. Use it? [Y/n]:"
  if ask_for_confirmation "y" "${prompt}"; then
    do_create_ostree_tarball=false
  else
    do_create_ostree_tarball=true
  fi
else
  do_create_ostree_tarball=true
fi

if ${do_create_ostree_tarball}; then
  export_ostree "${ostree_tarball}"
fi

sysroot_outdir="${outdir}/eos_${eos_branch}_${target_arch}-sysroot"
# We use sudo to remove the dir here in case a previous attempt was stopped
# before completing and the sysroot has wrong permissions
sudo rm -rf "${sysroot_outdir}"
mkdir -p "${sysroot_outdir}"
create_sysroot "${ostree_tarball}" "${sysroot_outdir}"

rm -rf "${tmpdir}"

echo
echo "Done!"
