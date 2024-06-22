#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/limits.h>
#include <unistd.h>
#include <stdio.h>
#include "blocks.h"
#include <arpa/inet.h>
#include "sha1.h"

#define NTOH ntohs

int main(int argc, char* argv[]) {
    int fd_in;
    int fd_out;
    ssize_t len;
    char fpath_in[PATH_MAX];
    char fpath_out[PATH_MAX];
    struct data_block block;
    SHA1Context cx;
    unsigned char sha1[AA_HASH_SIZE];

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

    memset(&block, 0, AA_BLOCK_SIZE);
    len = read(fd_in, &block, AA_BLOCK_SIZE);
    while (len>0) {
        if (len != (NTOH(block.header.length) + AA_HEAD_SIZE)) {
            fprintf(stderr, "Error %d (%s) , Invalid block length (%zd) when read from %s\n", EIO, strerror(EIO), len, fpath_in);
            exit(1);
        }
        hash_init(&cx);
        hash_step(&cx, block.header.seed, AA_SEED_SIZE);
        hash_step(&cx, block.data, AA_DATA_SIZE);
        hash_finish(&cx, sha1);
        if (memcmp(sha1, block.header.sha1, AA_HASH_SIZE)!=0) {
            fprintf(stderr, "Error %d (%s) , Invalid block hash when read from %s\n", EIO, strerror(EIO), fpath_in);
            exit(1);
        }
        len = write(fd_out, block.data, NTOH(block.header.length));
        if (len != NTOH(block.header.length)) {
            fprintf(stderr, "Error %d (%s) , Failed to write to %s\n", EIO, strerror(EIO), fpath_out);
            exit(1);
        }
        memset(&block, 0, AA_BLOCK_SIZE);
        len = read(fd_in, &block, AA_BLOCK_SIZE);
    }
    if (len<0) {
        fprintf(stderr, "Error %d (%s) , Failed to read from %s\n", errno, strerror(errno), fpath_in);
        exit(1);
    }

    close(fd_out);
    close(fd_in);
    return 0;

}
