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
    ssize_t len;
    char fpath_in[PATH_MAX];
    struct data_block block;
    SHA1Context cx;
    unsigned char sha1[AA_HASH_SIZE];
    size_t count_blocks;
    size_t file_bytes;
    size_t data_bytes;

    if (argc != 2) {
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

    count_blocks = 0;
    file_bytes = 0;
    data_bytes = 0;
    memset(&block, 0, AA_BLOCK_SIZE);
    len = read(fd_in, &block, AA_BLOCK_SIZE);
    while (len>0) {
        if (len != (NTOH(block.header.length) + AA_HEAD_SIZE)) {
            fprintf(stderr, "Error %d (%s) , Invalid block length (%zd) when read block (%zu) from %s\n", EIO, strerror(EIO), len, count_blocks, fpath_in);
            exit(1);
        }
        hash_init(&cx);
        hash_step(&cx, block.header.seed, AA_SEED_SIZE);
        hash_step(&cx, block.data, AA_DATA_SIZE);
        hash_finish(&cx, sha1);
        if (memcmp(sha1, block.header.sha1, AA_HASH_SIZE)!=0) {
            fprintf(stderr, "Error %d (%s) , Invalid block hash when read block (%zu) from %s\n", EIO, strerror(EIO), count_blocks, fpath_in);
            exit(1);
        }
        count_blocks++;
        file_bytes += len;
        data_bytes += NTOH(block.header.length);
        memset(&block, 0, AA_BLOCK_SIZE);
        len = read(fd_in, &block, AA_BLOCK_SIZE);
    }
    if (len<0) {
        fprintf(stderr, "Error %d (%s) , Failed to read from %s\n", errno, strerror(errno), fpath_in);
        exit(1);
    }

    close(fd_in);
    fprintf(stderr, "Verification of %ld blocks containing %ld data bytes in a file of %ld bytes successful for %s\n", count_blocks, data_bytes, file_bytes, fpath_in);
    return 0;

}
