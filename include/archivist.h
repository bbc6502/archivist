#ifndef __ARCHIVIST__
#define __ARCHIVIST__

#include <fuse.h>
#include <limits.h>
#include <stdio.h>
#include "blocks.h"

#define MAX_FUSE_OPEN_FILES 128

struct archivist_state {
    char root_dir[AA_NUM_COPIES][PATH_MAX];
    uint64_t used_entries;
    struct file_entry entry[MAX_FUSE_OPEN_FILES];
};

#define AA_DATA ((struct archivist_state *) fuse_get_context()->private_data)

extern int open_file_entry(const char* path, struct file_entry *file_entry, int flags);

#endif
