#!/bin/bash

set -xveo pipefail

arg1=$1
build="./build"
if [ "$arg1" != "linux" ]; then
	# source /opt/devkitpro/switchvars.sh
	# toolchain="${DEVKITPRO}/switch.cmake"
	toolchain="cmake/switch.cmake"
	export PORTLIBS_PREFIX="$(${DEVKITPRO}/portlibs_prefix.sh switch)"
	build="./build_switch"
fi

SCRIPTDIR=$(dirname "$0")
BASEDIR=$(realpath "${SCRIPTDIR}/../../")

build_chiaki (){
	pushd "${BASEDIR}"
		#rm -rf ./build

		cmake -B "${build}" \
			-GNinja \
			-DCMAKE_TOOLCHAIN_FILE=${toolchain} \
			-DCHIAKI_ENABLE_TESTS=OFF \
			-DCHIAKI_ENABLE_CLI=OFF \
			-DCHIAKI_ENABLE_GUI=OFF \
			-DCHIAKI_ENABLE_ANDROID=OFF \
			-DCHIAKI_ENABLE_BOREALIS=ON \
			-DCHIAKI_LIB_ENABLE_MBEDTLS=ON \
			# -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON \
			# -DCMAKE_FIND_DEBUG_MODE=ON

		ninja -C "${build}"
	popd
}

build_chiaki

