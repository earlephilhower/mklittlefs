#!/bin/bash
name=mkspiffs-$(git rev-parse --short HEAD)
rel=${rel:=-2.5.0}
subrel=${subrel:-1}

build ()
{(
    TARGET_OS=${tgt} CC=${pfx}-gcc CXX=${pfx}-g++ STRIP=${pfx}-strip make clean mkspiffs${exe} BUILD_CONFIG_NAME="-arduino-esp8266" CPPFLAGS="-DSPIFFS_USE_MAGIC_LENGTH=0 -DSPIFFS_ALIGNED_OBJECT_INDEX_TABLES=1"
    rm -rf tmp
    mkdir -p tmp/mkspiffs
    mv mkspiffs${exe} tmp/mkspiffs/.
    cd tmp
    if [ "${exe}" == "" ]; then
        tarball=${pfx}-$name.tar.gz
        tar zcvf ../${tarball} mkspiffs
    else
        tarball=${pfx}-$name.zip
        zip -rq ../${tarball} mkspiffs
    fi
    cd ..
    rm -rf tmp
    tarballsize=$(stat -c%s ${tarball})
    tarballsha256=$(sha256sum ${tarball} | cut -f1 -d" ")
    ( echo '            {' &&
      echo '              "host": "'$AHOST'",' &&
      echo '              "url": "https://github.com/earlephilhower/mkspiffs/releases/download/'${rel}-${subrel}'/'${tarball}'",' &&
      echo '              "archiveFileName": "'${tarball}'",' &&
      echo '              "checksum": "SHA-256:'${tarballsha256}'",' &&
      echo '              "size": "'${tarballsize}'"' &&
      echo '            }') > ${tarball}.json
)}

tgt=osx pfx=x86_64-apple-darwin14 exe="" build
tgt=windows pfx=x86_64-w64-mingw32 exe=".exe" build
tgt=windows pfx=i686-w64-mingw32 exe=".exe" build
tgt=linux pfx=arm-linux-gnueabihf exe="" build
tgt=linux pfx=aarch64-linux-gnu exe="" build
tgt=linux pfx=x86_64-linux-gnu exe="" build
