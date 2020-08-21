#!/bin/bash
name=mklittlefs-$(git rev-parse --short HEAD)
rel=${rel:=-2.5.0}
subrel=${subrel:-2}

rm -f *.json *.gz *.zip

build ()
{(
    TARGET_OS=${tgt} CC=${pfx}-gcc CXX=${pfx}-g++ STRIP=${pfx}-strip make clean mklittlefs${exe} BUILD_CONFIG_NAME="-arduino-esp8266" CPPFLAGS="-DSPIFFS_USE_MAGIC_LENGTH=0 -DSPIFFS_ALIGNED_OBJECT_INDEX_TABLES=1"
    rm -rf tmp
    mkdir -p tmp/mklittlefs
    mv mklittlefs${exe} tmp/mklittlefs/.
    cd tmp
    if [ "${exe}" == "" ]; then
        tarball=${pfx}-$name.tar.gz
        tar zcvf ../${tarball} mklittlefs
    else
        tarball=${pfx}-$name.zip
        zip -rq ../${tarball} mklittlefs
    fi
    cd ..
    rm -rf tmp
    tarballsize=$(stat -c%s ${tarball})
    tarballsha256=$(sha256sum ${tarball} | cut -f1 -d" ")
    ( echo '            {' &&
      echo '              "host": "'$AHOST'",' &&
      echo '              "url": "https://github.com/earlephilhower/mklittlefs/releases/download/'${rel}-${subrel}'/'${tarball}'",' &&
      echo '              "archiveFileName": "'${tarball}'",' &&
      echo '              "checksum": "SHA-256:'${tarballsha256}'",' &&
      echo '              "size": "'${tarballsize}'"' &&
      echo '            }') > ${tarball}.json
)}

tgt=osx pfx=x86_64-apple-darwin14 exe="" AHOST="x86_64-apple-darwin" build
tgt=windows pfx=x86_64-w64-mingw32 exe=".exe" AHOST="x86_64-mingw32" build
tgt=windows pfx=i686-w64-mingw32 exe=".exe" AHOST="i686-mingw32" build
tgt=linux pfx=arm-linux-gnueabihf exe="" AHOST="arm-linux-gnueabihf" build
tgt=linux pfx=aarch64-linux-gnu exe="" AHOST="aarch64-linux-gnu" build
tgt=linux pfx=x86_64-linux-gnu exe="" AHOST="x86_64-pc-linux-gnu" build
