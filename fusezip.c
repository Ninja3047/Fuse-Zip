/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall fusexmp.c `pkg-config fuse --cflags --libs` -o fusexmp
*/

#define FUSE_USE_VERSION 26

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>

#include <zip.h>
#include <fuse.h>

#include "fusezip.h"

static zip_t* ziparchive;

/*
 * Get file type
 */
static enum file_t fzip_file_type(const char* path)
{
    if (strcmp(path, "/") == 0)
        return ZIP_FOLDER;

    char search[strlen(path)];
    strcpy(search, path+1);
    search[strlen(path)] = 0;

    search[strlen(path) - 1] = '/';
    int r1 = zip_name_locate(ziparchive, search, 0);
    search[strlen(path) - 1] = 0;
    int r2 = zip_name_locate(ziparchive, search, 0);

    if (r1 != -1)
        return ZIP_FOLDER;
    else if (r2 != -1)
        return ZIP_FILE;
    else
        return -1;
}

/*
 * Get file attributes
 */
static int fzip_getattr(const char *path, struct stat *stbuf)
{
    printf("getting attr: %s\n", path);

    if (strcmp(path, "/") == 0)
    {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        stbuf->st_size = 1;
        return 0;
    }

    zip_stat_t sb;

    zip_stat_index(ziparchive, zip_name_locate(ziparchive, path+1, 0), 0, &sb);

    switch (fzip_file_type(path))
    {
        case ZIP_FILE:
            stbuf->st_mode = S_IFREG | 0666;
            stbuf->st_nlink = 1;
            stbuf->st_size = sb.size;
            break;
        case ZIP_FOLDER:
            stbuf->st_mode = S_IFDIR | 0755;
            stbuf->st_nlink = 2;
            stbuf->st_size = 0;
            break;
        default:
            return -ENOENT;
    }

    return 0;
}

/*
 * Read files in given directory
 */
static int fzip_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info* fi)
{
    printf("readdir: %s, offset = %lu\n", path, offset);

    (void) offset;
    (void) fi;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    for (int i = 0; i < zip_get_num_entries(ziparchive, 0); i++)
    {
        struct stat st;
        memset(&st, 0, sizeof(st));
        zip_stat_t sb;
        zip_stat_index(ziparchive, i, 0, &sb);

        char* zippath = malloc(strlen(sb.name) + 2);
        *zippath = '/';
        strcpy(zippath + 1, sb.name);

        char* dpath = strdup(zippath);
        char* bpath = strdup(zippath);

        if (strcmp(path, dirname(dpath)) == 0)
        {
            fzip_getattr(zippath, &st);
            char* name = basename(bpath);
            if (filler(buf, name, &st, 0))
                break;
        }

        free(zippath);
        free(dpath);
        free(bpath);
    }

    return 0;
}

/*
 * Open a file
 */
static int fzip_open(const char *path, struct fuse_file_info *fi)
{
    printf("open: %s\n", path);

    (void) fi;

    if(zip_name_locate(ziparchive, path+1, 0) < 0)
        return -ENOENT; // some error that says the file does not exist

    return 0;
}

/*
 * Read a file
 */
static int fzip_read(const char *path, char *buf, size_t size,
        off_t offset, struct fuse_file_info* fi)
{
    printf("read: %s offset: %lu\n", path, offset);
    int res;
    (void) fi;

    zip_file_t* file = zip_fopen(ziparchive, path+1, ZIP_FL_ENC_GUESS);

    res = zip_fread(file, buf, size);

    if (res == -1)
        res = -ENOENT;

    zip_fclose(file);

    return size;
}

/*
 * Create a directory with the given name
 */
static int fzip_mkdir(const char *path, mode_t mode)
{
    // zip_dir_add
    return 0;
}

/*
 * Remove the given directory
 */
static int fzip_rmdir(const char *path)
{
    // zip_delete
    return 0;
}

/*
 * Rename the given file or directory to the given name
 */
static int fzip_rename(const char *from, const char *to)
{
    zip_int64_t index = zip_name_locate(ziparchive, from + 1, 0);
    if (zip_file_rename(ziparchive, index, to, 0)  == -1)
        return -errno;

    //contents[index].name = to;
    // TODO parents aren't renamed

    return 0;
}

/*
 * Truncate or extend the given file so that it is the given size
 */
static int fzip_truncate(const char *path, off_t size)
{
    // zip_source_buffer
    // zip_name_locate
    // zip_file_replace
    // zip_source_free
    return 0;
}

/*
 * Writes size byets from the given buffer to the given file beginning
 * at offset
 * Return the number of bytes written (cannot be 0)
 */
static int fzip_write(const char* path, const char *buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
    // zip_source_buffer
    // zip_name_locate
    // zip_file_add (replace)
    // zip_source_free
    return size;
}

/*
 * Removes the given file
 */
static int fzip_unlink(const char* path)
{
    // TODO also remove it from contents and realloc to match size
    // zip_delete
    return 0;
}

/*
 * Returns 0 if file exists, otherwise it returns -ENOENT
 */
static int fzip_access(const char* path, int mask)
{
    // TODO implementation
    return 0;
}

static int fzip_statfs(const char* path, struct statvf* stbuf)
{
    // TODO implementation
    return 0;
}

static int fzip_utimens(const char* path, const struct timespec ts[2])
{
    // TODO implementation
    return 0;
}

/*
 * Contains the set of valid fuse operations for this file system
 */
static struct fuse_operations fzip_oper = {
    .access         = fzip_access,
    .getattr        = fzip_getattr,
    .readdir        = fzip_readdir,
    .open           = fzip_open,
    .read           = fzip_read,
    .mkdir          = fzip_mkdir,
    .rmdir          = fzip_rmdir,
    .rename         = fzip_rename,
    .truncate       = fzip_truncate,
    .write          = fzip_write,
    .unlink         = fzip_unlink,
    .statfs         = fzip_statfs,
    .utimens        = fzip_utimens,
};

int main(int argc, char *argv[])
{
    umask(0);
    ziparchive = zip_open(argv[1], ZIP_RDONLY, NULL); // open zip file
    char* fuseargv[argc-1];
    fuseargv[0] = argv[0];
    for (int i = 1; i < argc-1; i++)
    {
        fuseargv[i] = argv[i+1];
    }
    return fuse_main(argc-1, fuseargv, &fzip_oper, NULL);
}
