#!/bin/bash
set -e

echo "Starting build inside Ubuntu container..."

# Compile dependencies
echo "Compiling dependencies..."
cd depends
chmod +x config.sub config.guess
make -j24 HOST=x86_64-pc-linux-gnu
cd ..

# Compile DSW
echo "Compiling DSW..."
chmod +x share/genbuild.sh autogen.sh
./autogen.sh
./configure --enable-glibc-back-compat \
            --prefix=$(pwd)/depends/x86_64-pc-linux-gnu \
            LDFLAGS="-static-libstdc++" \
            --enable-cxx \
            --enable-static \
            --disable-shared \
            --disable-debug \
            --disable-tests \
            --disable-bench \
            --with-pic \
            CPPFLAGS="-fPIC -O3 --param ggc-min-expand=1 --param ggc-min-heapsize=32768" \
            CXXFLAGS="-fPIC -O3 --param ggc-min-expand=1 --param ggc-min-heapsize=32768"

make clean
make -j24 HOST=x86_64-pc-linux-gnu

echo "Build completed successfully!"

