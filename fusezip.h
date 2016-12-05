#ifndef FUSEZIP_H
#define FUSEZIP_H

enum file_t
{
    ZIP_INVALID = -1,
    ZIP_FOLDER,
    ZIP_FILE,
};

struct element
{
    const char* name;
    enum file_t type;
    const char* parent;
    int size;
};

#endif
