#!/usr/bin/env bash

set -exo pipefail

source_dir=${1}
build_dir=${2}

export LB=${source_dir}
export LB_BUILD=${build_dir}/LB
pushd "$LB_BUILD"

ctest --output-on-failure | tee cmake-output.log

popd
