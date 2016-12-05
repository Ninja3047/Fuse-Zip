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
static char* zipname;

/*
 * Append a slash to the given path
 */
static char* append_slash(const char* path)
{
    char* search = malloc(strlen(path) + 2);
    strcpy(search, path);
    search[strlen(path)] ='/';
    search[strlen(path) + 1] = 0;

    return search;
}

/*
 * Get file type
 */
static enum file_t fzip_file_type(const char* path)
{
    if (strcmp(path, "/") == 0)
        return ZIP_FOLDER;

    char* slash = append_slash(path + 1);
    int r1 = zip_name_locate(ziparchive, slash, 0);
    int r2 = zip_name_locate(ziparchive, path + 1, 0);

    free(slash);

    if (r1 != -1)
        return ZIP_FOLDER;
    else if (r2 != -1)
        return ZIP_FILE;
    else
        return ZIP_INVALID;
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
    zip_stat_init(&sb);

    char* slash = append_slash(path + 1);

    switch (fzip_file_type(path))
    {
        case ZIP_FILE:
            zip_stat(ziparchive, path + 1, 0, &sb);
            stbuf->st_mode = S_IFREG | 0666;
            stbuf->st_nlink = 1;
            stbuf->st_size = sb.size;
            stbuf->st_mtime = sb.mtime;
            break;
        case ZIP_FOLDER:
            zip_stat(ziparchive, slash, 0, &sb);
            stbuf->st_mode = S_IFDIR | 0755;
            stbuf->st_nlink = 2;
            stbuf->st_size = 0;
            stbuf->st_mtime = sb.mtime;
            break;
        default:
            free(slash);
            return -ENOENT;
    }

    free(slash);
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
            if (zippath[strlen(zippath) - 1] == '/') zippath[strlen(zippath) - 1] = 0;
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

    if(zip_name_locate(ziparchive, path + 1, 0) < 0)
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

    zip_file_t* file = zip_fopen(ziparchive, path + 1, 0);

    if (file == 0)
        return -1;

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
    (void) mode;

    zip_dir_add(ziparchive, path + 1, 0);

    zip_close(ziparchive); // we have to close and reopen to write the changes
    ziparchive = zip_open(zipname, 0, NULL);

    return 0;
}

/*
 * Rename the given file or directory to the given name
 */
static int fzip_rename(const char *from, const char *to)
{
    zip_int64_t index = zip_name_locate(ziparchive, from + 1, 0);
    if (zip_file_rename(ziparchive, index, to + 1, 0)  == -1)
        return -errno;

    zip_close(ziparchive); // we have to close and reopen to write the changes
    ziparchive = zip_open(zipname, 0, NULL);
    return 0;
}

/*
 * Truncate or extend the given file so that it is the given size
 */
static int fzip_truncate(const char *path, off_t size)
{
    printf("truncate: %s size: %ld\n", path, size);

    char tbuf[size];
    memset(tbuf, 0, size);
    if (zip_name_locate(ziparchive, path + 1, 0) > 0)
    {
        zip_file_t *f = zip_fopen(ziparchive, path+1, 0);
        zip_fread(f, tbuf, size);
        zip_fclose(f);
    }

    zip_source_t *s;

    if ((s = zip_source_buffer(ziparchive, tbuf, size, 0)) == NULL ||
        zip_file_add(ziparchive, path+1, s, ZIP_FL_OVERWRITE) < 0)
    {
        zip_source_free(s);
        printf("Error adding file %s\n", path);
        return 0;
    }

    zip_close(ziparchive); // we have to close and reopen to write the changes
    ziparchive = zip_open(zipname, 0, NULL);

    return 0;
}

/*
 * Writes size bytes from the given buffer to the given file beginning
 * at offset
 * Return the number of bytes written (cannot be 0)
 */
static int fzip_write(const char* path, const char *buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
    printf("write: %s buf: %s, size: %ld offset: %ld\n", path, buf, size, offset);
    (void) fi;

    zip_source_t *s;

    char tbuf[size + offset];
    if (zip_name_locate(ziparchive, path + 1, 0) > 0)
    {
        zip_file_t *f = zip_fopen(ziparchive, path + 1, 0);
        zip_fread(f, tbuf, offset);
        memcpy(tbuf + offset, buf, size);
        zip_fclose(f);
    }

    if ((s = zip_source_buffer(ziparchive, tbuf, size + offset, 0)) == NULL ||
        zip_file_add(ziparchive, path+1, s, ZIP_FL_OVERWRITE) < 0)
    {
        zip_source_free(s);
        printf("Error adding file %s\n", path);
        return 0;
    }

    zip_close(ziparchive); // we have to close and reopen to write the changes
    ziparchive = zip_open(zipname, 0, NULL);

    return size;
}

/*
 * Create a file at the given path
 */
static int fzip_mknod(const char* path, mode_t mode, dev_t rdev)
{
    printf("mknod: %s mode: %u\n", path, mode);
    (void) rdev;

    if (mode & S_IFREG)
    {
        fzip_write(path, NULL, 0, 0, NULL);
    }

    return 0;
}

/*
 * Removes the given file
 */
static int fzip_unlink(const char* path)
{
    printf("unlink: %s\n", path);
    int ret = zip_delete(ziparchive, zip_name_locate(ziparchive, path + 1, 0));
    zip_close(ziparchive); // we have to close and reopen to write the changes
    ziparchive = zip_open(zipname, 0, NULL);
    return ret;
}

/*
 * Remove the given directory
 */
static int fzip_rmdir(const char *path)
{
    printf("rmdir: %s\n", path);
    char* folder = append_slash(path);
    fzip_unlink(folder);
    free(folder);

    return 0;
}

/*
 * Returns 0 if file exists, otherwise it returns -ENOENT
 */
static int fzip_access(const char* path, int mask)
{
    printf("access: %s\n", path);

    (void) mask;

    if (fzip_file_type(path) >= 0)
        return 0;

    return -ENOENT;
}

/*
 * Updates the last access time of the given object from st[0] and
 * the last modifcation time from ts[1]
 */
static int fzip_utimens(const char* path, const struct timespec ts[2])
{
    printf("utimens: %s access: %ld modified: %ld\n", path, ts[0].tv_sec, ts[1].tv_sec);

    (void) path;
    (void) ts;

    return 0;
}

static void fzip_destroy(void* private_data)
{
    (void) private_data;

    zip_close(ziparchive);
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
    .mknod          = fzip_mknod,
    .rename         = fzip_rename,
    .truncate       = fzip_truncate,
    .write          = fzip_write,
    .unlink         = fzip_unlink,
    .rmdir          = fzip_rmdir,
    .utimens        = fzip_utimens,
    .destroy        = fzip_destroy,
};

int main(int argc, char *argv[])
{
    zipname = argv[1];
    ziparchive = zip_open(zipname, 0, NULL); // open zip file
    char* fuseargv[argc - 1];
    fuseargv[0] = argv[0];
    for (int i = 1; i < argc - 1; i++)
        fuseargv[i] = argv[i + 1];

    return fuse_main(argc - 1, fuseargv, &fzip_oper, NULL);
}
