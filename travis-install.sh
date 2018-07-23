#!/bin/sh -ex

LIBCHECK_TAG=0.12.0

if [ -z "$TEST_EXT_DIR" ]; then
    if [ -n "$1" ]; then
        TEST_EXT_DIR=$1
    else
        >&2 echo "The directory for required external libraries for testing is not given."
        exit 1
    fi
fi

case $TEST_EXT_DIR in
    /*) : ;; # Do nothing
    *)  TEST_EXT_DIR=`pwd`/$TEST_EXT_DIR ;;
esac

# Install build tools
sudo apt-get update
sudo apt-get install -y autoconf texinfo

# Clone libraries needed for test if they are not cached
TEST_EXT_DIR_CREATED=0
mkdir "$TEST_EXT_DIR" && TEST_EXT_DIR_CREATED=1
if [ $TEST_EXT_DIR_CREATED -eq 1 ]; then
    cd "$TEST_EXT_DIR"
    git clone --branch "$LIBCHECK_TAG" https://github.com/libcheck/check
    git clone https://github.com/ozars/pcg-c
fi

# Install check
cd "$TEST_EXT_DIR/check"
autoreconf -vfi
./configure
make
sudo make install

# Install psuedo-random lib
cd "$TEST_EXT_DIR/pcg-c"
make
sudo make install

# Refresh linker cache
sudo ldconfig

# Go back to build dir
cd "$TRAVIS_BUILD_DIR"
