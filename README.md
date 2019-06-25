# mklittlefs
Tool to build and unpack [littlefs](https://github.com/ARMmbed/littlefs) images.
Based off of [mkspiffs](https://github.com/igrr/mkspiffs) by Ivan Grokhotkov.

## Usage

```

   mklittlefs  {-c <pack_dir>|-u <dest_dir>|-l|-i} [-d <0-5>] [-b <number>]
               [-p <number>] [-s <number>] [--] [--version] [-h]
               <image_file>


Where: 

   -c <pack_dir>,  --create <pack_dir>
     (OR required)  create littlefs image from a directory
         -- OR --
   -u <dest_dir>,  --unpack <dest_dir>
     (OR required)  unpack littlefs image to a directory
         -- OR --
   -l,  --list
     (OR required)  list files in littlefs image
         -- OR --


   -d <0-5>,  --debug <0-5>
     Debug level. 0 means no debug output.

   -b <number>,  --block <number>
     fs block size, in bytes

   -p <number>,  --page <number>
     fs page size, in bytes

   -s <number>,  --size <number>
     fs image size, in bytes

   --,  --ignore_rest
     Ignores the rest of the labeled arguments following this flag.

   --version
     Displays version information and exits.

   -h,  --help
     Displays usage information and exits.

   <image_file>
     (required)  LittleFS image file


```
## Build

You need gcc (≥4.8) or clang(≥600.0.57), and make. On Windows, use MinGW.

Run:
```bash
$ git submodule update --init
$ make dist
```

## LittleFS configuration

Some LittleFS options which are set at mklittlefs build time affect the format of the generated filesystem image. Make sure such options are set to the same values when builing mklittlefs and when building the application which uses LittleFS.

These options include:

  - LFS_NAME_MAX
  - possibly others

To see the default values of these options, check `MAkefile` file in this repository.

To override some options at build time, pass extra `CPPFLAGS` to `make`. You can also set `BUILD_CONFIG_NAME` variable to distinguish the built binary:

```bash
$ make clean
$ make dist CPPFLAGS="-DLFS_NAME_MAX=128" BUILD_CONFIG_NAME=-custom
```

To check which options were set when building mklittlefs, use `--version` command:

```
$ mklittlefs --version

./mklittlefs  version: 0.2.3-6-g9a0e072
```

## Cross-compiling

To build for all cross compiled targets, run
```
docker run --user $(id -u):$(id -g) --rm -v $(pwd):/workdir earlephilhower/gcc-cross bash -c "cd /workdir; bash build-cross.sh"
```


## License

MIT

## To do

- [ ] Add more debug output and print LittleFS debug output
- [ ] Error handling
- [ ] Code cleanup
