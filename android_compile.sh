#!/bin/bash
echo "Configuring for Android aarch64"
NDK_PATH="/home/mans/Documents/programmering/MEX/android-ndk-r27d-linux/android-ndk-r27d"
TOOLCHAIN=$NDK_PATH/toolchains/llvm/prebuilt/linux-x86_64

# Not used by linker, does it still work?
PAGE_ALIGN_FLAGS="-Wl,-z,max-page-size=16384"
CC="$TOOLCHAIN/bin/aarch64-linux-android35-clang $PAGE_ALIGN_FLAGS"

# A little bit hacky, but works?
AS="$CC -nostdlib"
OBJCOPY=$TOOLCHAIN/bin/llvm-objcopy
OBJDUMP=$TOOLCHAIN/bin/llvm-objdump

echo "using ndk: $NDK_PATH"
echo "using CC: $CC"

ARCH=aarch64 CC=$CC AS=$AS OBJCOPY=$OBJCOPY OBJDUMP=$OBJDUMP ./configure

make clean
make


# Push files to android device over adb
adb shell mkdir -p /data/local/tmp/risu
adb push risu /data/local/tmp/risu
adb push test_aarch64.bin /data/local/tmp/risu