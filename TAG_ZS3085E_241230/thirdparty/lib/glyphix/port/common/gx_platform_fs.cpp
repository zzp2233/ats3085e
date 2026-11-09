/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#include "gx_file.h"
#include "platform/gx_fs.h"
#include "sys/printk.h"
#include <cstddef>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define handle_cast(file) int(intptr_t(file) - 1)
namespace gx {
namespace os {
static char root[256] = "/NAND:";

static bool test(int flags, int mask) { return (flags & mask) == mask; }

static const char *cat(char *buf, const char *path) {
    if (!root[0])
        return path;
    std::strcpy(buf, root);
    if (path[0] != '/')
        std::strcat(buf, "/");

    path = std::strcat(buf, path);
    return path;
}

static int parseMode(int mode) {
    int posixMode = 0;
    if (test(mode, File::ReadWrite))
        posixMode |= O_RDWR;
    else if (mode & File::ReadOnly)
        posixMode |= O_RDONLY;
    else if (mode & File::WriteOnly)
        posixMode |= O_WRONLY;
    if (mode & File::WriteOnly && !(mode & File::ExistingOnly))
        posixMode |= O_CREAT;
#if defined(O_TEXT) && defined(O_BINARY)
    posixMode |= test(mode, File::Text) ? O_TEXT : O_BINARY;
#endif
    if (test(mode, File::Append))
        posixMode |= O_APPEND;
    if (test(mode, File::Truncate))
        posixMode |= O_TRUNC;
    return posixMode;
}

void chroot(const char *path) { std::strcpy(root, path); }

file_t open(const char *path, int mode) {
    if (mode < File::WriteOnly && !isfile(path))
        return file_t();
    char buf[512];
    return file_t(intptr_t(::open(cat(buf, path), parseMode(mode), 0755)) + 1);
}

bool close(file_t file) { return ::close(handle_cast(file)) == 0; }

int write(file_t file, const void *buffer, std::size_t count) {
    return (int)::write(handle_cast(file), buffer, count);
}

int read(file_t file, void *buffer, std::size_t count) {
    return (int)::read(handle_cast(file), buffer, count);
}

off_t lseek(file_t file, off_t offset, int whence) {
    return ::lseek(handle_cast(file), offset, whence);
}

void fsync(file_t file) {
    //TODO Zephyr NOT have fsync, need ATS to porting. fs_fsync?
    ::fsync(handle_cast(file));
}

void *rawptr(file_t) { return nullptr; }

bool exists(const char *path) {
    char buf[512];
    struct fs_dirent dirent;
    return fs_stat(cat(buf, path), &dirent) == 0;
}

char *getcwd(char *buffer, int length) {
    if (root[0])
        return std::strcpy(buffer, "/");
    char *res = NULL;//::getcwd(buffer, length);
    return res;
}

bool isfile(const char *path) {
    char buf[512];
    struct fs_dirent dirent;
    return fs_stat(cat(buf, path), &dirent) == 0 && dirent.type == FS_DIR_ENTRY_FILE;
}

bool isdir(const char *path) {
    char buf[512];
    struct fs_dirent dirent;
    return fs_stat(cat(buf, path), &dirent) == 0 && dirent.type == FS_DIR_ENTRY_DIR;
}

bool mkdir(const char *path) {
    char buf[512];
    return ::mkdir(cat(buf, path), 0755) == 0;
}

bool rename(const char *from, const char *to) {
    char buf[512];
    return ::rename(cat(buf, from), cat(buf, to)) == 0;
}

bool remove(const char *path) {
    char buf[512];
    struct ::stat st;
    if (::stat(cat(buf, path), &st) != 0)
        return false;

    path = cat(buf, path);

    return ::unlink(path) == 0;
}

bool dirfirst(dirinfo_t *info, const char *path) {
    char buf[512];
    info->dir = ::opendir(cat(buf, path));
    if (info->dir)
        return dirnext(info);
    return false;
}

bool dirnext(dirinfo_t *info) {
    DIR *dir = (DIR *)info->dir;
    struct ::dirent *item = ::readdir(dir);
    info->item = (item->d_name[0] == '\0') ? NULL : item;
    if (info->item) {
        info->name = item->d_name;
        info->type = (item->is_dir ? FT_DIR : FT_REG);
    }
    return info->item;
}

bool dirclose(dirinfo_t *info) {
    if (info->dir)
        return ::closedir((DIR *)info->dir) != 0;
    return false;
}
} // namespace os
} // namespace gx
