#!/bin/bash

set -xe
cd "`dirname $(readlink -f ${0})`"

docker build -t chiaki-bullseye . -f Dockerfile.bullseye
cd ..
docker run --rm -v "`pwd`:/build" chiaki-bullseye /bin/bash -c "
  cd /build &&
  mkdir build_bullseye &&
  cmake -Bbuild_bullseye -GNinja -DCHIAKI_ENABLE_SETSU=ON -DCHIAKI_USE_SYSTEM_JERASURE=ON -DCHIAKI_USE_SYSTEM_NANOPB=ON &&
  ninja -C build_bullseye &&
  ninja -C build_bullseye test"

