#!/bin/sh

LIBCHECK_TAG = 0.12.0

if [ -z "$1" ]; then
    >&2 echo "The directory for required external libraries for testing is not given."
    exit 1
fi

# Install build tools
sudo apt-get update
sudo apt-get install -y autoconf texinfo

# Clone libraries needed for test if they are not cached
mkdir "$TEST_EXT_DIR"
if [ $? -ne 0 ]; then
    cd "$TEST_EXT_DIR"
    git clone --branch "$LIBCHECK_TAG" https://github.com/libcheck/check
    git clone https://github.com/imneme/pcg-c
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
