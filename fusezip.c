/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall fusexmp.c `pkg-config fuse --cflags --libs` -o fusexmp
*/

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <zip.h>

#include "fusezip.h"

static zip_t* ziparchive;
struct element* contents;

/*
 * Get file type
 */
static enum file_t fzip_file_type(const char *path)
{
    if (strrchr(path, '/') == path+strlen(path)-1)
        return ZIP_FOLDER; // ends with /. then it is a folder
    else
        return ZIP_FILE;
}

static void split_path(const char* path, struct element* e)
{
    char* p = strdup(path);
    const char* parent = "";
    const char* file = "";
    char* token = strtok(p, "/");

    while ( token != NULL )
    {
        parent = file;
        file = token;
        token = strtok(NULL, "/");
    }

    e->name = strdup(file);
    e->parent = strdup(parent);

    free(p);
}

/*
 * Initialize elements with contents of zip archive
 */
static int init_contents()
{
    int entries = zip_get_num_entries(ziparchive, 0);
    contents = malloc(entries * sizeof(struct element));
    // TODO Free this somewhere
    // TODO realloc when adding new stuff

    for(int i = 0; i < entries; i++)
    {
        struct zip_stat sb;
        zip_stat_index(ziparchive, i, 0, &sb);
        contents[i].type = fzip_file_type(sb.name);
        contents[i].size = sb.size;
        split_path(sb.name, &contents[i]);
    }

    return entries;
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

    struct element e;
    int entries = zip_get_num_entries(ziparchive, 0);

    split_path(path, &e);

    for (int i = 0; i < entries; i++)
    {
        if (strcmp(contents[i].name, e.name) == 0)
        {
            switch (contents[i].type)
            {
                case ZIP_FILE:
                    stbuf->st_mode = S_IFREG | 0666;
                    stbuf->st_nlink = 1;
                    stbuf->st_size = contents[i].size;
                    break;
                case ZIP_FOLDER:
                    stbuf->st_mode = S_IFDIR | 0755;
                    stbuf->st_nlink = 2;
                    stbuf->st_size = 1;
                    break;
                default:
                    return -ENOENT;
            }
        }
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
        fzip_getattr(path, &st);
        if (strcmp(path + 1, contents[i].parent) == 0) 
            if (filler(buf, contents[i].name, &st, 0))
                break;
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
 * Contains the set of valid fuse operations for this file system
 */
static struct fuse_operations fzip_oper = {
    .getattr        = fzip_getattr,
    .readdir        = fzip_readdir,
    .open           = fzip_open,
    .read           = fzip_read,
    .write          = fzip_write,
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
    init_contents();
    return fuse_main(argc-1, fuseargv, &fzip_oper, NULL);
}