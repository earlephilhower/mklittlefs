//
//  main.cpp
//  make_littlefs
//
//  Created by Earle F. Philhower, III on December 15, 2018
//  Derived from mkspiffs:
//  | Created by Ivan Grokhotkov on 13/05/15.
//  | Copyright (c) 2015 Ivan Grokhotkov. All rights reserved.
//
#define TCLAP_SETBASE_ZERO 1

#include <iostream>
#include <vector>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <memory>
#include <cstdlib>
#include "tclap/CmdLine.h"
#include "tclap/UnlabeledValueArg.h"

extern "C" {
#define LFS_NAME_MAX 32
#include "littlefs/lfs.h"
}


static std::vector<uint8_t> s_flashmem;

static std::string s_dirName;
static std::string s_imageName;
static uint32_t s_imageSize;
static uint32_t s_pageSize;
static uint32_t s_blockSize;

enum Action { ACTION_NONE, ACTION_PACK, ACTION_UNPACK, ACTION_LIST };
static Action s_action = ACTION_NONE;

static int s_debugLevel = 0;
static bool s_addAllFiles;

// Unless -a flag is given, these files/directories will not be included into the image
static const char* ignored_file_names[] = {
    ".DS_Store",
    ".git",
    ".gitignore",
    ".gitmodules"
};

int lfs_flash_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
  memcpy(buffer, &s_flashmem[0] + c->block_size * block + off, size);
  return 0;
}

int lfs_flash_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
  memcpy(&s_flashmem[0] + block * c->block_size + off, buffer, size);
  return 0;
}

int lfs_flash_erase(const struct lfs_config *c, lfs_block_t block)
{
  memset(&s_flashmem[0] + block * c->block_size, 0, c->block_size);
  return 0;
}

int lfs_flash_sync(const struct lfs_config *c) {
  (void) c;
  return 0;
}


// Implementation

static lfs_t s_fs;
static lfs_config s_cfg;
bool s_mounted = false;

void setLfsConfig()
{
  memset(&s_fs, 0, sizeof(s_fs));
  memset(&s_cfg, 0, sizeof(s_cfg));
  s_cfg.read  = lfs_flash_read;
  s_cfg.prog  = lfs_flash_prog;
  s_cfg.erase = lfs_flash_erase;
  s_cfg.sync  = lfs_flash_sync;

  s_cfg.read_size = s_blockSize;
  s_cfg.prog_size = s_blockSize;
  s_cfg.block_size = s_blockSize;
  s_cfg.block_count = s_flashmem.size() / s_blockSize;
  s_cfg.lookahead = 128;
}

int spiffsTryMount() {
  setLfsConfig();
  int ret = lfs_mount(&s_fs, &s_cfg);
  if (ret) {
    s_mounted = false;
    return -1;
  }
  s_mounted = true;
  return 0;
}

bool spiffsMount(){
  if (s_mounted)
    return true;
  int res = spiffsTryMount();
  return (res == 0);
}

void spiffsUnmount() {
  if (s_mounted) {
    lfs_unmount(&s_fs);
    s_mounted = false;
  }
}

bool spiffsFormat(){
  spiffsUnmount();
  setLfsConfig();
  int formated = lfs_format(&s_fs, &s_cfg);
  if(formated != 0)
    return false;
  return (spiffsTryMount() == 0);
}

int addFile(char* name, const char* path) {
    FILE* src = fopen(path, "rb");
    if (!src) {
        std::cerr << "error: failed to open " << path << " for reading" << std::endl;
        return 1;
    }

    lfs_file_t dst;
    int ret = lfs_file_open(&s_fs, &dst, name, LFS_O_CREAT | LFS_O_TRUNC | LFS_O_WRONLY);
    if (ret < 0) {
        std::cerr << "unable to open '" << name << "." << std::endl;
        return 1;
    }
    // read file size
    fseek(src, 0, SEEK_END);
    size_t size = ftell(src);
    fseek(src, 0, SEEK_SET);

    if (s_debugLevel > 0) {
        std::cout << "file size: " << size << std::endl;
    }

    size_t left = size;
    uint8_t data_byte;
    while (left > 0){
        if (1 != fread(&data_byte, 1, 1, src)) {
            std::cerr << "fread error!" << std::endl;

            fclose(src);
            lfs_file_close(&s_fs, &dst);
            return 1;
        }
        int res = lfs_file_write(&s_fs, &dst, &data_byte, 1);
        if (res < 0) {
            std::cerr << "lfs_write error(" << res << "): ";

            if (res == LFS_ERR_NOSPC) {
                std::cerr << "File system is full." << std::endl;
            } else {
                std::cerr << "unknown";
            }
            std::cerr << std::endl;

            if (s_debugLevel > 0) {
                std::cout << "data left: " << left << std::endl;
            }

            fclose(src);
            lfs_file_close(&s_fs, &dst);
            return 1;
        }
        left -= 1;
    }

    lfs_file_close(&s_fs, &dst);
    fclose(src);

    return 0;
}

int addFiles(const char* dirname, const char* subPath) {
    DIR *dir;
    struct dirent *ent;
    bool error = false;
    std::string dirPath = dirname;
    dirPath += subPath;

    // Open directory
    if ((dir = opendir (dirPath.c_str())) != NULL) {

        // Read files from directory.
        while ((ent = readdir (dir)) != NULL) {

            // Ignore dir itself.
            if ((strcmp(ent->d_name, ".") == 0) || (strcmp(ent->d_name, "..") == 0)) {
                continue;
            }

            if (!s_addAllFiles) {
                bool skip = false;
                size_t ignored_file_names_count = sizeof(ignored_file_names) / sizeof(ignored_file_names[0]);
                for (size_t i = 0; i < ignored_file_names_count; ++i) {
                    if (strcmp(ent->d_name, ignored_file_names[i]) == 0) {
                        std::cerr << "skipping " << ent->d_name << std::endl;
                        skip = true;
                        break;
                    }
                }
                if (skip) {
                    continue;
                }
            }

            std::string fullpath = dirPath;
            fullpath += ent->d_name;
            struct stat path_stat;
            stat (fullpath.c_str(), &path_stat);

            if (!S_ISREG(path_stat.st_mode)) {
                // Check if path is a directory.
                if (S_ISDIR(path_stat.st_mode)) {
                    // Prepare new sub path.
                    std::string newSubPath = subPath;
                    newSubPath += ent->d_name;
                    newSubPath += "/";

                    if (addFiles(dirname, newSubPath.c_str()) != 0)
                    {
                        std::cerr << "Error for adding content from " << ent->d_name << "!" << std::endl;
                    }

                    continue;
                }
                else
                {
                    std::cerr << "skipping " << ent->d_name << std::endl;
                    continue;
                }
            }

            // Filepath with dirname as root folder.
            std::string filepath = subPath;
            filepath += ent->d_name;
            std::cout << filepath << std::endl;

            // Add File to image.
            if (addFile((char*)filepath.c_str(), fullpath.c_str()) != 0) {
                std::cerr << "error adding file!" << std::endl;
                error = true;
                if (s_debugLevel > 0) {
                    std::cout << std::endl;
                }
                break;
            }
        } // end while
        closedir (dir);
    } else {
        std::cerr << "warning: can't read source directory" << std::endl;
        return 1;
    }

    return (error) ? 1 : 0;
}

void listFiles() {
    int ret;
    lfs_dir_t dir;
    lfs_info it;

    setLfsConfig();
    ret = lfs_mount(&s_fs, &s_cfg);
    ret = lfs_dir_open(&s_fs, &dir, "/");
    if (ret < 0) {
        std::cerr << "unable to open root directory!" << std::endl;
        return;
    }
    while (true) {
        int res = lfs_dir_read(&s_fs, &dir, &it);
        if (res <= 0)
            break;

        // Ignore special dir entries
        if ((strcmp(it.name, ".") == 0) || (strcmp(it.name, "..") == 0)) {
            continue;
        }

        std::cout << it.size << '\t' << it.name << std::endl;
    }
    lfs_dir_close(&s_fs, &dir);
    lfs_unmount(&s_fs);
    s_mounted = false;
}

/**
 * @brief Check if directory exists.
 * @param path Directory path.
 * @return True if exists otherwise false.
 *
 * @author Pascal Gollor (http://www.pgollor.de/cms/)
 */
bool dirExists(const char* path) {
    DIR *d = opendir(path);

    if (d) {
        closedir(d);
        return true;
    }

    return false;
}

/**
 * @brief Create directory if it not exists.
 * @param path Directory path.
 * @return True or false.
 *
 * @author Pascal Gollor (http://www.pgollor.de/cms/)
 */
bool dirCreate(const char* path) {
    // Check if directory also exists.
    if (dirExists(path)) {
	    return false;
    }

    // platform stuff...
#if defined(_WIN32)
    if (mkdir(path) != 0) {
#else
    if (mkdir(path, S_IRWXU | S_IXGRP | S_IRGRP | S_IROTH | S_IXOTH) != 0) {
#endif
	    std::cerr << "Can not create directory!!!" << std::endl;
		return false;
    }

    return true;
}

/**
 * @brief Unpack file from file system.
 * @param spiffsFile SPIFFS dir entry pointer.
 * @param destPath Destination file path path.
 * @return True or false.
 *
 * @author Pascal Gollor (http://www.pgollor.de/cms/)
 */
bool unpackFile(lfs_info *spiffsFile, const char *destPath) {
    uint8_t buffer[spiffsFile->size];
    std::string filename = (const char*)(spiffsFile->name);

    // Open file from spiffs file system.
    lfs_file_t src;
    int ret = lfs_file_open(&s_fs, &src, (char *)(filename.c_str()), LFS_O_RDONLY);
    if (ret < 0) {
        std::cerr << "unable to open '" << filename.c_str() << "." << std::endl;
        return false;
    }

    // read content into buffer
    lfs_file_read(&s_fs, &src, buffer, spiffsFile->size);

    // Close spiffs file.
    lfs_file_close(&s_fs, &src);

    // Open file.
    FILE* dst = fopen(destPath, "wb");

    // Write content into file.
    fwrite(buffer, sizeof(uint8_t), sizeof(buffer), dst);

    // Close file.
    fclose(dst);


    return true;
}

/**
 * @brief Unpack files from file system.
 * @param sDest Directory path as std::string.
 * @return True or false.
 *
 * @author Pascal Gollor (http://www.pgollor.de/cms/)
 *
 * todo: Do unpack stuff for directories.
 */
bool unpackFiles(std::string sDest) {
    lfs_dir_t dir;
    lfs_info ent;

    // Add "./" to path if is not given.
    if (sDest.find("./") == std::string::npos && sDest.find("/") == std::string::npos) {
        sDest = "./" + sDest;
    }

    // Check if directory exists. If it does not then try to create it with permissions 755.
    if (! dirExists(sDest.c_str())) {
        std::cout << "Directory " << sDest << " does not exists. Try to create it." << std::endl;

        // Try to create directory.
        if (! dirCreate(sDest.c_str())) {
            return false;
        }
    }

    // Open directory.
    lfs_dir_open(&s_fs, &dir, "");

    // Read content from directory.
    while (lfs_dir_read(&s_fs, &dir, &ent)==1) {
        // Ignore special dir entries
        if ((strcmp(ent.name, ".") == 0) || (strcmp(ent.name, "..") == 0)) {
            continue;
        }

        // Check if content is a file.
        if ((int)(ent.type) == LFS_TYPE_REG) {
            std::string name = (const char*)(ent.name);
            std::string sDestFilePath = sDest + name;
            size_t pos = name.find_first_of("/", 1);

            // If file is in sub directories?
            while (pos != std::string::npos) {
                // Subdir path.
                std::string path = sDest;
                path += name.substr(0, pos);

                // Create subddir if subdir not exists.
                if (!dirExists(path.c_str())) {
                    if (!dirCreate(path.c_str())) {
                        return false;
                    }
                }

                pos = name.find_first_of("/", pos + 1);
            }

            // Unpack file to destination directory.
            if (! unpackFile(&ent, sDestFilePath.c_str()) ) {
                std::cout << "Can not unpack " << ent.name << "!" << std::endl;
                return false;
            }

            // Output stuff.
            std::cout
                << ent.name
                << '\t'
                << " > " << sDestFilePath
                << '\t'
                << "size: " << ent.size << " Bytes"
                << std::endl;
        }

        // Get next file handle.
    } // end while

    // Close directory.
    lfs_dir_close(&s_fs, &dir);

    return true;
}

// Actions

int actionPack() {
    s_flashmem.resize(s_imageSize, 0xff);

    FILE* fdres = fopen(s_imageName.c_str(), "wb");
    if (!fdres) {
        std::cerr << "error: failed to open image file" << std::endl;
        return 1;
    }

    spiffsFormat();
    int result = addFiles(s_dirName.c_str(), "/");
    spiffsUnmount();

    fwrite(&s_flashmem[0], 4, s_flashmem.size()/4, fdres);
    fclose(fdres);

    return result;
}

/**
 * @brief Unpack action.
 * @return 0 success, 1 error
 *
 * @author Pascal Gollor (http://www.pgollor.de/cms/)
 */
int actionUnpack(void) {
    int ret = 0;
    s_flashmem.resize(s_imageSize, 0xff);

    // open spiffs image
    FILE* fdsrc = fopen(s_imageName.c_str(), "rb");
    if (!fdsrc) {
        std::cerr << "error: failed to open image file" << std::endl;
        return 1;
    }

    // read content into s_flashmem
    if (s_flashmem.size()/4 != fread(&s_flashmem[0], 4, s_flashmem.size()/4, fdsrc)) {
        std::cerr << "error: couldn't read image file" << std::endl;
        return 1;
    }

    // close fiel handle
    fclose(fdsrc);

    // mount file system
    spiffsMount();

    // unpack files
    if (! unpackFiles(s_dirName)) {
        ret = 1;
    }

    // unmount file system
    spiffsUnmount();

    return ret;
}


int actionList() {
    s_flashmem.resize(s_imageSize, 0xff);

    FILE* fdsrc = fopen(s_imageName.c_str(), "rb");
    if (!fdsrc) {
        std::cerr << "error: failed to open image file" << std::endl;
        return 1;
    }
    if (s_flashmem.size()/4 != fread(&s_flashmem[0], 4, s_flashmem.size()/4, fdsrc)) {
        std::cerr << "error: couldn't read image file" << std::endl;
        return 1;
    }
    fclose(fdsrc);
    spiffsMount();
    listFiles();
    spiffsUnmount();
    return 0;
}

void processArgs(int argc, const char** argv) {
    TCLAP::CmdLine cmd("", ' ', VERSION);
    TCLAP::ValueArg<std::string> packArg( "c", "create", "create spiffs image from a directory", true, "", "pack_dir");
    TCLAP::ValueArg<std::string> unpackArg( "u", "unpack", "unpack spiffs image to a directory", true, "", "dest_dir");
    TCLAP::SwitchArg listArg( "l", "list", "list files in spiffs image", false);
    TCLAP::UnlabeledValueArg<std::string> outNameArg( "image_file", "spiffs image file", true, "", "image_file"  );
    TCLAP::ValueArg<int> imageSizeArg( "s", "size", "fs image size, in bytes", false, 0x10000, "number" );
    TCLAP::ValueArg<int> pageSizeArg( "p", "page", "fs page size, in bytes", false, 256, "number" );
    TCLAP::ValueArg<int> blockSizeArg( "b", "block", "fs block size, in bytes", false, 4096, "number" );
    TCLAP::SwitchArg addAllFilesArg( "a", "all-files", "when creating an image, include files which are normally ignored; currently only applies to '.DS_Store' files and '.git' directories", false);
    TCLAP::ValueArg<int> debugArg( "d", "debug", "Debug level. 0 means no debug output.", false, 0, "0-5" );

    cmd.add( imageSizeArg );
    cmd.add( pageSizeArg );
    cmd.add( blockSizeArg );
    cmd.add( addAllFilesArg );
    cmd.add( debugArg );
    std::vector<TCLAP::Arg*> args = {&packArg, &unpackArg, &listArg};
    cmd.xorAdd( args );
    cmd.add( outNameArg );
    cmd.parse( argc, argv );

    if (debugArg.getValue() > 0) {
        std::cout << "Debug output enabled" << std::endl;
        s_debugLevel = debugArg.getValue();
    }

    if (packArg.isSet()) {
        s_dirName = packArg.getValue();
        s_action = ACTION_PACK;
    } else if (unpackArg.isSet()) {
        s_dirName = unpackArg.getValue();
        s_action = ACTION_UNPACK;
    } else if (listArg.isSet()) {
        s_action = ACTION_LIST;
    }

    s_imageName = outNameArg.getValue();
    s_imageSize = imageSizeArg.getValue();
    s_pageSize  = pageSizeArg.getValue();
    s_blockSize = blockSizeArg.getValue();
    s_addAllFiles = addAllFilesArg.isSet();
}

int main(int argc, const char * argv[]) {

    try {
        processArgs(argc, argv);
    } catch(...) {
        std::cerr << "Invalid arguments" << std::endl;
        return 1;
    }

    switch (s_action) {
    case ACTION_PACK:
        return actionPack();
        break;
    case ACTION_UNPACK:
    	return actionUnpack();
        break;
    case ACTION_LIST:
        return actionList();
        break;
    default:
        break;
    }

    return 1;
}
