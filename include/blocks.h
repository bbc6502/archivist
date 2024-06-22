#ifndef __BLOCKS__
#define __BLOCKS__

#include <unistd.h>
#include <limits.h>
#include <stdint.h>
#include "seed.h"

#define AA_NUM_COPIES 2

#define AA_HASH_SIZE 20
#define AA_HEAD_SIZE 32
#define AA_DATA_SIZE 480
#define AA_BLOCK_SIZE 512

#define NTOH ntohs
#define HTON htons

struct data_header {
    uint16_t version;
    uint16_t length;
    unsigned char sha1[AA_HASH_SIZE];
    unsigned char seed[AA_SEED_SIZE];
};

struct data_block {
    struct data_header header;
    unsigned char data[AA_DATA_SIZE];
};

struct data_entry {
    int fd;
    int corrupt;
    struct data_block block;
};

struct file_entry {
    struct data_entry file[AA_NUM_COPIES];
};

extern void clear_list(int list[]);
extern int first_error(const int err_no[]);
extern int read_block(struct file_entry *file_entry, off_t file_block_ofs);
extern int write_block(struct file_entry *file_entry, off_t file_block_ofs);

#endif
