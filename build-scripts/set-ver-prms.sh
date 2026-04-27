#!/bin/bash

# $1 - Project version
# $2 - Build version
# $3 - Release flag (ON/OFF, default: OFF)

CMAKE_VERSION_PRMS=

[[ -n "$1" ]] && CMAKE_VERSION_PRMS="$CMAKE_VERSION_PRMS -DVERSION=$1"
[[ -n "$2" ]] && CMAKE_VERSION_PRMS="$CMAKE_VERSION_PRMS -DBUILD_VERSION=$2"

export CMAKE_VERSION_PRMS
export CMAKE_RELEASE_FLAG_PRM="-DRELEASE_FLAG=${3:-OFF}"

