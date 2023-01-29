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
#include <utime.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <memory>
#include <cstdlib>
#include "tclap/CmdLine.h"
#include "tclap/UnlabeledValueArg.h"

extern "C" {
#ifndef LFS_NAME_MAX
#define LFS_NAME_MAX 32
#endif
#include "littlefs/lfs.h"
}

#ifndef PATH_MAX
#define PATH_MAX 512
#endif

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

  s_cfg.read_size = 64;
  s_cfg.prog_size = 64;
  s_cfg.block_size =  s_blockSize;
  s_cfg.block_count = s_flashmem.size() / s_blockSize;
  s_cfg.block_cycles = 16; // TODO - need better explanation
  s_cfg.cache_size = 64;
  s_cfg.lookahead_size = 64;
  s_cfg.read_buffer = nullptr;
  s_cfg.prog_buffer = nullptr;
  s_cfg.lookahead_buffer = nullptr;
  s_cfg.name_max = 0;
  s_cfg.file_max = 0;
  s_cfg.attr_max = 0;
}

int littlefsTryMount() {
  setLfsConfig();
  int ret = lfs_mount(&s_fs, &s_cfg);
  if (ret) {
    s_mounted = false;
    return -1;
  }
  s_mounted = true;
  return 0;
}

bool littlefsMount(){
  if (s_mounted)
    return true;
  int res = littlefsTryMount();
  return (res == 0);
}

void littlefsUnmount() {
  if (s_mounted) {
    lfs_unmount(&s_fs);
    s_mounted = false;
  }
}

bool littlefsFormat(){
  littlefsUnmount();
  setLfsConfig();
  int formated = lfs_format(&s_fs, &s_cfg);
  if(formated != 0)
    return false;
  return (littlefsTryMount() == 0);
}

int addFile(char* name, const char* path) {
    FILE* src = fopen(path, "rb");
    if (!src) {
        std::cerr << "error: failed to open " << path << " for reading" << std::endl;
        return 1;
    }

    struct stat sbuf;

    // Make any subdirs required to place this file
    char pathStr[PATH_MAX+1];
    strcpy(pathStr, name); // Already know path length < LFS_NAME_MAX
    // Make dirs up to the final fnamepart
    char *ptr = strchr(pathStr, '/');
    while (ptr) {
       *ptr = 0;
       lfs_mkdir(&s_fs, pathStr); // Ignore error, we'll catch later if it's fatal
       // Add time metadata 't'
       if (!stat(path, &sbuf)) {
            uint32_t ftime = sbuf.st_mtime;
            lfs_setattr(&s_fs, pathStr, 't', (const void *)&ftime, sizeof(ftime));
            // There is no portable way to get creation time via stat, so simply call it identical to the last write in this case
            lfs_setattr(&s_fs, pathStr, 'c', (const void *)&ftime, sizeof(ftime));
       }
       *ptr = '/';
       ptr = strchr(ptr+1, '/');
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

    // Add time metadata 't'
    if (!stat(path, &sbuf)) {
        uint32_t ftime = sbuf.st_mtime;
        lfs_setattr(&s_fs, name, 't', (const void *)&ftime, sizeof(ftime));
        // There is no portable way to get creation time via stat, so simply call it identical to the last write in this case
        lfs_setattr(&s_fs, name, 'c', (const void *)&ftime, sizeof(ftime));
    }
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
#if !defined(_WIN32)
            {
                struct stat path_stat;
                std::string name = dirPath + ent->d_name;
                int loopcount = 10; // where is SYMLOOP_MAX?
                bool skipentry = false;
                std::string target = name;

                // follow a chain of softlinks
                lstat(name.c_str(), &path_stat);
                while (S_ISLNK(path_stat.st_mode) && loopcount > 0) {
                    char buf[PATH_MAX];
                    ssize_t size = readlink(target.c_str(), buf, sizeof buf);

                    if (size < 0) {
                        perror(("readlink " + target).c_str());
                        skipentry = true;
                        break;
                    }
                    if (buf[0] == '/') {
                        target = std::string(buf, size);
                    } else {
                        target = dirPath + std::string(buf, size);
                    }
                    char rpath[PATH_MAX];
                    const char *ptr = realpath(target.c_str(), rpath);
                    if (ptr == NULL) {
                        perror(("realpath " + target).c_str());
                        skipentry = true;
                        continue;
                    }
                    target = rpath;
                    lstat(target.c_str(), &path_stat);
                    // if it points to a directory, skip that entry
                    if (S_ISDIR(path_stat.st_mode)) {
                        std::cerr << "symlink " << name << " points to directory " << target << " - skipping" << std::endl;
                        skipentry = true;
                    }
                }
                // also skip links pointing to themselves
                if (S_ISLNK(path_stat.st_mode) && name.compare(target) == 0) {
                std::cerr << "symlink " << name << " loops back to itself - skipping"
                            << std::endl;
                skipentry = true;
                break;
            }
            name = target;
            loopcount--;
            if (loopcount == 0) {
                std::cerr << "symlink " << name
                    << " - too many redirections, skipping" << std::endl;
                continue;
            }
            if (skipentry)
                continue;
            }
#endif
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

void listFiles(const char *path) {
    int ret;
    lfs_dir_t dir;
    lfs_info it;

    ret = lfs_dir_open(&s_fs, &dir, path);
    if (ret < 0) {
        std::cerr << "unable to open directory '" << path << "'" << std::endl;
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

        uint32_t ftime;
        time_t t;
        if (it.type == LFS_TYPE_DIR) {
            char newpath[PATH_MAX];
            sprintf(newpath, "%s/%s", path, it.name);
            if (lfs_getattr(&s_fs, newpath, 't', (uint8_t *)&ftime, sizeof(ftime)) >= 0) { // and/or check 'c' as well?
                t = (time_t)ftime;
                std::cout << "<dir>" << '\t' << path << "/" << it.name  << '\t' << asctime(gmtime(&t)); 
            } else {
                std::cout << "<dir>" << '\t' << path << "/" << it.name << std::endl;
            }
            listFiles(newpath);
        } else {
            char buff[PATH_MAX];
            snprintf(buff, sizeof(buff), "%s/%s",  path, it.name);
            if (lfs_getattr(&s_fs, buff, 't', (uint8_t *)&ftime, sizeof(ftime)) >= 0) { // and/or check 'c' as well?
                t = (time_t)ftime;
                std::cout << it.size << '\t' << path << "/" << it.name  << '\t' << asctime(gmtime(&t));
            } else {
                std::cout << it.size << '\t' << path << "/" << it.name << std::endl;
            }
        }
    }
    lfs_dir_close(&s_fs, &dir);
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
 * @param littlefsFile SPIFFS dir entry pointer.
 * @param destPath Destination file path path.
 * @return True or false.
 *
 * @author Pascal Gollor (http://www.pgollor.de/cms/)
 */
bool unpackFile(const char *lfsDir, lfs_info *littlefsFile, const char *destPath) {
    uint8_t buffer[littlefsFile->size];
    std::string filename = lfsDir + std::string("/") + littlefsFile->name;

    // Open file from littlefs file system.
    lfs_file_t src;
    int ret = lfs_file_open(&s_fs, &src, (char *)(filename.c_str()), LFS_O_RDONLY);
    if (ret < 0) {
        std::cerr << "unable to open '" << filename.c_str() << "." << std::endl;
        return false;
    }

    // read content into buffer
    lfs_file_read(&s_fs, &src, buffer, littlefsFile->size);

    // Close littlefs file.
    lfs_file_close(&s_fs, &src);

    // Open file.
    FILE* dst = fopen(destPath, "wb");
    if (!dst) return false;

    // Write content into file.
    fwrite(buffer, sizeof(uint8_t), sizeof(buffer), dst);

    // Close file.
    fclose(dst);

    // Adjust time, if present
    uint32_t ftime;
    if (lfs_getattr(&s_fs, (char *)(filename.c_str()), 't', (uint8_t *)&ftime, sizeof(ftime)) >= 0) {
        struct utimbuf ut;
        ut.actime = ftime;
        ut.modtime = ftime;
        utime(destPath, &ut);
    }

    return true;
}

bool unpackLFSDirFiles(std::string sDest, const char *lfsDir) {
    lfs_dir_t dir;
    lfs_info ent;

    // Check if directory exists. If it does not then try to create it with permissions 755.
    if (! dirExists(sDest.c_str())) {
        std::cout << "Directory " << sDest << " does not exists. Try to create it." << std::endl;

        // Try to create directory.
        if (! dirCreate(sDest.c_str())) {
            return false;
        }
    }

    // Open directory.
    lfs_dir_open(&s_fs, &dir, lfsDir);

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

            // Unpack file to destination directory.
            if (! unpackFile(lfsDir, &ent, sDestFilePath.c_str()) ) {
                std::cout << "Can not unpack " << ent.name << "!" << std::endl;
                return false;
            }

            // Output stuff.
            std::cout
                << lfsDir
                << ent.name
                << '\t'
                << " > " << sDestFilePath
                << '\t'
                << "size: " << ent.size << " Bytes"
                << std::endl;
        } else if (ent.type == LFS_TYPE_DIR) {
            char newPath[PATH_MAX];
            if (lfsDir[0]) {
                sprintf(newPath, "%s/%s/", lfsDir, ent.name);
            } else {
                sprintf(newPath, "%s/", ent.name);
            }
            std::string newDest = sDest + ent.name + "/";
            dirCreate(newDest.c_str());
            unpackLFSDirFiles(newDest, newPath);
        }
        // Get next file handle.
    } // end while

    // Close directory.
    lfs_dir_close(&s_fs, &dir);

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
    // Add "./" to path if is not given.
    if (sDest.find("./") == std::string::npos && sDest.find("/") == std::string::npos) {
        sDest = "./" + sDest;
    }
    if (sDest.back() != '/') {
        sDest += "/";
    }

    // Check if directory exists. If it does not then try to create it with permissions 755.
    if (! dirExists(sDest.c_str())) {
        std::cout << "Directory " << sDest << " does not exists. Try to create it." << std::endl;

        // Try to create directory.
        if (! dirCreate(sDest.c_str())) {
            return false;
        }
    }

    return unpackLFSDirFiles(sDest, "");
}

// Actions

int actionPack() {
    if (!s_imageSize) {
        std::cerr << "error: image size not specified, can't create filesystem" << std::endl;
        return 1;
    }

    s_flashmem.resize(s_imageSize, 0xff);

    FILE* fdres = fopen(s_imageName.c_str(), "wb");
    if (!fdres) {
        std::cerr << "error: failed to open image file" << std::endl;
        return 1;
    }

    littlefsFormat();
    int result = addFiles(s_dirName.c_str(), "/");

    // Set creation/modification time of volume on root
    time_t ct = time(NULL);
    lfs_setattr(&s_fs, "/", 't', &ct, sizeof(ct));
    lfs_setattr(&s_fs, "/", 'c', &ct, sizeof(ct));

    littlefsUnmount();

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

    // open littlefs image
    FILE* fdsrc = fopen(s_imageName.c_str(), "rb");
    if (!fdsrc) {
        std::cerr << "error: failed to open image file" << std::endl;
        return 1;
    }

    fseek(fdsrc, 0L, SEEK_END);
    int filesize = s_imageSize ? s_imageSize : ftell(fdsrc);
    fseek(fdsrc, 0L, SEEK_SET);
    s_flashmem.resize(filesize, 0xff);

    // read content into s_flashmem
    if (s_flashmem.size()/4 != fread(&s_flashmem[0], 4, s_flashmem.size()/4, fdsrc)) {
        std::cerr << "error: couldn't read image file" << std::endl;
        return 1;
    }

    // close fiel handle
    fclose(fdsrc);

    // mount file system
    littlefsMount();

    // unpack files
    if (! unpackFiles(s_dirName)) {
        ret = 1;
    }

    // unmount file system
    littlefsUnmount();

    return ret;
}


int actionList() {
    FILE* fdsrc = fopen(s_imageName.c_str(), "rb");
    if (!fdsrc) {
        std::cerr << "error: failed to open image file" << std::endl;
        return 1;
    }

    fseek(fdsrc, 0L, SEEK_END);
    int filesize = s_imageSize ? s_imageSize : ftell(fdsrc);
    fseek(fdsrc, 0L, SEEK_SET);
    s_flashmem.resize(filesize, 0xff);

    if (s_flashmem.size()/4 != fread(&s_flashmem[0], 4, s_flashmem.size()/4, fdsrc)) {
        std::cerr << "error: couldn't read image file" << std::endl;
        return 1;
    }
    fclose(fdsrc);
    littlefsMount();
    listFiles("");

    time_t ct;
    if (lfs_getattr(&s_fs, "/", 't', &ct, sizeof(ct)) >= 0) { // and/or check 'c' as well?
        std::cout << "Creation time:" << '\t' << asctime(gmtime(&ct));
    }

    littlefsUnmount();
    return 0;
}

#define PRINT_INT_MACRO(def_name) \
    std::cout << "  " # def_name ": " << def_name << std::endl;

class CustomOutput : public TCLAP::StdOutput
{
public:
    virtual void version(TCLAP::CmdLineInterface& c)
    {
        std::cout << "mklittlefs ver. " VERSION << std::endl;
        const char* configName = BUILD_CONFIG_NAME;
        if (configName[0] == '-') {
            configName += 1;
        }
        std::cout << "Build configuration name: " << configName << std::endl;
        std::cout << "LittleFS ver. " << LITTLEFS_VERSION << std::endl;
        const char* buildConfig = BUILD_CONFIG;
        std::cout << "Extra build flags: " << (strlen(buildConfig) ? buildConfig : "(none)") << std::endl;
        std::cout << "LittleFS configuration:" << std::endl;
        PRINT_INT_MACRO(LFS_NAME_MAX);
        PRINT_INT_MACRO(LFS_FILE_MAX);
        PRINT_INT_MACRO(LFS_ATTR_MAX);
    }
};

#undef PRINT_INT_MACRO

void processArgs(int argc, const char** argv) {
    TCLAP::CmdLine cmd("", ' ', VERSION);
    CustomOutput output;
    cmd.setOutput(&output);

    TCLAP::ValueArg<std::string> packArg( "c", "create", "create littlefs image from a directory", true, "", "pack_dir");
    TCLAP::ValueArg<std::string> unpackArg( "u", "unpack", "unpack littlefs image to a directory", true, "", "dest_dir");
    TCLAP::SwitchArg listArg( "l", "list", "list files in littlefs image", false);
    TCLAP::UnlabeledValueArg<std::string> outNameArg( "image_file", "littlefs image file", true, "", "image_file"  );
    TCLAP::ValueArg<int> imageSizeArg( "s", "size", "fs image size, in bytes", false, 0, "number" );
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
