/*
 *  Simple zip file system implemented using libfuse and libzip
 *  Copyright (C) 2016  William Tan
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.

 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

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
