#!/bin/sh

set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

cd ${DIR}/..

export PARALLEL_BUILDS=12
./Installation/Jenkins/build.sh \
    standard \
    --rpath \
    --package DEB \
    --builddir build-deb \
    --targetDir /var/tmp/ \
    --jemalloc \

cd ${DIR}/..
