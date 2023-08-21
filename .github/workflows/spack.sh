#!/bin/bash -e

bstage=$1
device=$2

set +x
trap 'echo "# $BASH_COMMAND"' DEBUG

source /etc/profile

git submodule update --init

# Setting up Spack
git clone -b gragghia/spack_sycl https://github.com/G-Ragghianti/spack
source spack/share/spack/setup-env.sh
export HOME=$(pwd)
# End Spack setup

module load gcc@10.4.0
if [ "${device}" = "gpu_nvidia" ]; then
  ARCH=$(nvidia-smi --query-gpu=compute_cap --format=csv,noheader | head -1 | sed -e 's/\.//')
  SPEC="+cuda cuda_arch=$ARCH %gcc"
elif [ "${device}" = "gpu_nvidia" ]; then
  TARGET=$(rocminfo | grep Name | grep gfx | head -1 | awk '{print $2}')
  SPEC="+rocm amdgpu_target=$TARGET %gcc"
else
  SPEC="+sycl %oneapi"
  module load intel-oneapi-compilers
fi

if [ "${bstage}" = "test" ]; then
  TEST="--test=root"
fi

spack compiler find
spack test remove slate
spack spec slate@master $SPEC
#spack dev-build --fresh $TEST slate@master $SPEC

if [ "${bstage}" = "smoke" ]; then
  spack test run slate
fi
