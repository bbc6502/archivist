#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <fcntl.h>
#include "blocks.h"
#include "sha1.h"
#include "seed.h"
#include <stdio.h>

int main(int argc, char* argv[]) {
    int fd_in;
    int fd_out;
    ssize_t len;
    ssize_t ofs;
    ssize_t size;
    ssize_t block_length;
    char fpath_in[PATH_MAX];
    char fpath_out[PATH_MAX];
    struct data_block block;
    SHA1Context cx;

    if (argc != 3) {
        fprintf(stderr, "Error %d (%s) , Invalid arguments\n", EINVAL, strerror(EINVAL));
        exit(1);
    }

    strcpy(fpath_in, argv[1]);
    if (!strcmp(fpath_in, "-")) {
        fd_in = STDIN_FILENO;
    } else {
        fd_in = open(fpath_in, O_RDONLY);
    }
    if (fd_in == -1) {
        fprintf(stderr, "Error %d (%s) , Failed to open %s\n", errno, strerror(errno), fpath_in);
        exit(1);
    }

    strcpy(fpath_out, argv[2]);
    if (!strcmp(fpath_out, "-")) {
        fd_out = STDOUT_FILENO;
    } else {
        fd_out = open(fpath_in, O_WRONLY);
    }
    if (fd_out == -1) {
        fprintf(stderr, "Error %d (%s) , Failed to open %s\n", errno, strerror(errno), fpath_out);
        exit(1);
    }

    ofs = 0;
    size = AA_DATA_SIZE;
    memset(&block, 0, AA_BLOCK_SIZE);
    len = read(fd_in, &block.data[ofs], size);
    while (len>0) {

        ofs += len;
        size -= len;
        while (len > 0 && size > 0) {
            len = read(fd_in, &block.data[ofs], size);
            if (len > 0) {
                ofs += len;
                size -= len;
            }
        }
        if (len<0) {
            fprintf(stderr, "Error %d (%s) , Failed to read from %s\n", errno, strerror(errno), fpath_in);
            exit(1);
        }

        block.header.version = HTON(1);
        block.header.length = HTON(ofs);
        initialise_seed(block.header.seed);
        hash_init(&cx);
        hash_step(&cx, block.header.seed, AA_SEED_SIZE);
        hash_step(&cx, block.data, AA_DATA_SIZE);
        hash_finish(&cx, block.header.sha1);

        block_length = ofs + AA_HEAD_SIZE;
        len = write(fd_out, &block, block_length);
        if (len != block_length) {
            fprintf(stderr, "Error %d (%s) , Failed to write to %s\n", EIO, strerror(EIO), fpath_out);
            exit(1);
        }

        ofs = 0;
        size = AA_DATA_SIZE;
        memset(&block, 0, AA_BLOCK_SIZE);
        len = read(fd_in, &block.data[ofs], size);
    }
    if (len<0) {
        fprintf(stderr, "Error %d (%s) , Failed to read from %s\n", errno, strerror(errno), fpath_in);
        exit(1);
    }

    close(fd_out);
    close(fd_in);
    return 0;
}
