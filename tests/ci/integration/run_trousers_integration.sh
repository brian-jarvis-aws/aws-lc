#!/bin/bash -exu
#
# Copyright Amazon.com Inc. or its affiliates.  All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0 OR ISC
#

source tests/ci/common_posix_setup.sh

# Set up environment.

# SYS_ROOT
#  |
#  - SRC_ROOT(aws-lc)
#  |
#  - SCRATCH_FOLDER
#    |
#    - trousers
#    - trousers-install
#    - AWS_LC_BUILD_FOLDER
#    - AWS_LC_INSTALL_FOLDER

# Assumes script is executed from the root of aws-lc directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
SCRATCH_FOLDER=${SYS_ROOT}/"TROUSERS_SCRATCH"
TROUSERS_SRC_FOLDER="${SCRATCH_FOLDER}/trousers"
TROUSERS_INSTALL_FOLDER="${SCRATCH_FOLDER}/trousers-install"
AWS_LC_BUILD_FOLDER="${SCRATCH_FOLDER}/aws-lc-build"
AWS_LC_INSTALL_FOLDER="${SCRATCH_FOLDER}/aws-lc-install"

mkdir -p "${SCRATCH_FOLDER}"
rm -rf "${SCRATCH_FOLDER:?}"/*

pushd "${SCRATCH_FOLDER}"

function trousers_build() {
  sh ./bootstrap.sh
  ./configure --with-gui=none --prefix="${TROUSERS_INSTALL_FOLDER}" --with-openssl="${AWS_LC_INSTALL_FOLDER}"
  make -j "${NUM_CPU_THREADS}"
  make install
  ldd "${TROUSERS_INSTALL_FOLDER}/sbin/tcsd" | grep "${AWS_LC_INSTALL_FOLDER}/lib/libcrypto.so" || exit 1
}


# Get latest trousers version.
git clone https://git.code.sf.net/p/trousers/trousers "${TROUSERS_SRC_FOLDER}"
mkdir -p "${AWS_LC_BUILD_FOLDER}" "${AWS_LC_INSTALL_FOLDER}" "${TROUSERS_INSTALL_FOLDER}"
ls

aws_lc_build "${SRC_ROOT}" "${AWS_LC_BUILD_FOLDER}" "${AWS_LC_INSTALL_FOLDER}" -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=1 -DCMAKE_BUILD_TYPE=RelWithDebInfo
export PKG_CONFIG_PATH="${AWS_LC_INSTALL_FOLDER}"/lib/pkgconfig
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}:${AWS_LC_INSTALL_FOLDER}/lib/"

pushd "${TROUSERS_SRC_FOLDER}"
trousers_build
popd

popd


