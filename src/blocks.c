/*
  Block reading and writing logic
*/

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include "blocks.h"
#include "sha1.h"
#include "logs.h"
#include "seed.h"
#include <sys/random.h>

void clear_list(int list[]) {
    memset(list, 0, AA_NUM_COPIES * sizeof(int));
}

int count_eof(const int eof[]) {
    int idx;
    int total;
    total = 0;
    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        if (eof[idx]!=0) {
            total++;
        }
    }
    return total;
}

int first_error(const int err_no[]) {
    int idx;
    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        if (err_no[idx]!=0) {
            return err_no[idx];
        }
    }
    return 0;
}

void repair_corrupt_blocks(struct file_entry *file_entry, off_t file_block_ofs, int err_no[]) {
    int idx;
    int idx2;
    uint32_t block_length;
    ssize_t bytes_written;

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        if (file_entry->file[idx].corrupt==1) {
            for(idx2=0; idx2<AA_NUM_COPIES; idx2++) {
                if ((err_no[idx2]==0) && (file_entry->file[idx2].corrupt==0)) {
                    log_info("repair", "Repair idx=%d using idx=%d", idx, idx2);
                    block_length = NTOH(file_entry->file[idx2].block.header.length) + AA_HEAD_SIZE;
                    bytes_written = pwrite(file_entry->file[idx].fd, file_entry->file[idx2].block.data, block_length, file_block_ofs);
                    if (bytes_written==block_length) {
                        memcpy(file_entry->file[idx].block.data, file_entry->file[idx2].block.data, AA_BLOCK_SIZE);
                        file_entry->file[idx].corrupt = 0;
                        err_no[idx] = 0;
                    }
                }
            }
        }
    }

}

void repair_mismatched_blocks(struct file_entry *file_entry, off_t file_block_ofs, const int err_no[], const int eof[]) {
    int idx;
    uint32_t block_length;
    ssize_t bytes_written;

    if ((err_no[0] == 0) && (eof[0] == 0)) {
        for(idx=1; idx<AA_NUM_COPIES; idx++) {
            if ((err_no[idx] == 0) && (eof[idx] == 0)) {
                if (memcmp(file_entry->file[0].block.header.sha1, file_entry->file[idx].block.header.sha1, AA_HASH_SIZE)!=0) {
                    log_info("repair", "Repair mismatch idx=%d using idx=%d", idx, 0);
                    block_length = NTOH(file_entry->file[0].block.header.length) + AA_HEAD_SIZE;
                    bytes_written = pwrite(file_entry->file[idx].fd, file_entry->file[0].block.data, block_length, file_block_ofs);
                    if (bytes_written==block_length) {
                        memcpy(file_entry->file[idx].block.data, file_entry->file[0].block.data, AA_BLOCK_SIZE);
                    }
                }
            }
        }
    }

}

void repair_missing_blocks(struct file_entry *file_entry, off_t file_block_ofs, const int err_no[], int eof[]) {
    int idx;
    uint32_t block_length;
    ssize_t bytes_written;

    if ((err_no[0] == 0) && (eof[0] == 0)) {
        for(idx=1; idx<AA_NUM_COPIES; idx++) {
            if ((err_no[idx] == 0) && (eof[idx] == 1)) {
                log_info("repair", "Repair missing block idx=%d using idx=%d", idx, 0);
                block_length = NTOH(file_entry->file[0].block.header.length) + AA_HEAD_SIZE;
                bytes_written = pwrite(file_entry->file[idx].fd, file_entry->file[0].block.data, block_length, file_block_ofs);
                if (bytes_written==block_length) {
                    memcpy(file_entry->file[idx].block.data, file_entry->file[0].block.data, AA_BLOCK_SIZE);
                    eof[idx] = 0;
                }
            }
        }
    }
}

void initialise_new_block(struct file_entry *file_entry, int err_no[], int eof[]) {
    int idx;
    int idx2;
    unsigned char seed[AA_SEED_SIZE];

    if (count_eof(eof)==AA_NUM_COPIES) {
        if (initialise_seed(seed)==0) {
            for(idx=0; idx<AA_NUM_COPIES; idx++) {
                log_info("initialise", "Initialise idx=%d", idx);
                file_entry->file[idx].block.header.version = HTON(1);
                for(idx2=0; idx2<AA_SEED_SIZE; idx2++) {
                    file_entry->file[idx].block.header.seed[idx2] = seed[idx2];
                }
            }
        } else {
            log_error("initialise", EAGAIN, "Failed to initialise seed");
            for(idx=0; idx<AA_NUM_COPIES; idx++) {
                err_no[idx] = EAGAIN;
            }
        }
    } else if (count_eof(eof)>0) {
        log_error("initialise", EIO, "Cannot initialise because some are EOF and others are not");
        for(idx=0; idx<AA_NUM_COPIES; idx++) {
            err_no[idx] = EIO;
        }
    }
}

void verify_block(struct file_entry *file_entry, const int idx, int err_no[]) {
    SHA1Context cx;
    unsigned char sha1[AA_HASH_SIZE];
    hash_init(&cx);
    hash_step(&cx, file_entry->file[idx].block.header.seed, AA_SEED_SIZE);
    hash_step(&cx, file_entry->file[idx].block.data, AA_DATA_SIZE);
    hash_finish(&cx, sha1);
    if (memcmp(file_entry->file[idx].block.header.sha1, sha1, AA_HASH_SIZE) != 0) {
        err_no[idx] = EIO;
        file_entry->file[idx].corrupt = 1;
        log_error("verify", EIO, "Hash verification mismatch");
    }
}

void attempt_block_read(struct file_entry *file_entry, off_t file_block_ofs, const int idx, int err_no[], int eof[]) {
    ssize_t bytes_read;
    int fd;
    int block_length;

    fd = file_entry->file[idx].fd;
    file_entry->file[idx].corrupt = 0;
    memset(&file_entry->file[idx].block, 0, AA_BLOCK_SIZE);
    bytes_read = pread(fd, &file_entry->file[idx].block, AA_BLOCK_SIZE, file_block_ofs);
    if (bytes_read<0) {
        log_info("readblock", "idx=%d fd=%d offset = %lu , bytes read = %ld , error (%d) %s", idx, fd, file_block_ofs, bytes_read, errno, strerror(errno));
    } else {
        log_info("readblock", "idx=%d fd=%d offset = %lu , bytes read = %ld", idx, fd, file_block_ofs, bytes_read);
    }
    if (bytes_read<0) {
        err_no[idx] = errno;
        log_error("readblock", errno, "idx=%d fd=%d", idx, fd);
    } else if (bytes_read==0) {
        eof[idx] = 1;
        log_info("readblock", "idx=%d fd=%d EOF encountered", idx, fd);
    } else if (bytes_read != (AA_HEAD_SIZE + NTOH(file_entry->file[idx].block.header.length))) {
        block_length = NTOH(file_entry->file[idx].block.header.length);
        err_no[idx] = EIO;
        log_error("readblock", EIO, "idx=%d fd=%d bytes_read=%ld block_length=%d", idx, fd, bytes_read, block_length);
    }
}

void read_and_verify_blocks(struct file_entry *file_entry, off_t file_block_ofs, int err_no[], int eof[]) {
    int idx;

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        attempt_block_read(file_entry, file_block_ofs, idx, err_no, eof);
        if ((err_no[idx] == 0) && (eof[idx] == 0)) {
            verify_block(file_entry, idx, err_no);
        }
    }
}

int read_block(struct file_entry *file_entry, off_t file_block_ofs) {
    int err_no[AA_NUM_COPIES];
    int eof[AA_NUM_COPIES];

    clear_list(err_no);
    clear_list(eof);

    read_and_verify_blocks(file_entry, file_block_ofs, err_no, eof);
    repair_corrupt_blocks(file_entry, file_block_ofs, err_no);
    repair_mismatched_blocks(file_entry, file_block_ofs, err_no, eof);
    repair_missing_blocks(file_entry, file_block_ofs, err_no, eof);
    initialise_new_block(file_entry, err_no, eof);

    return first_error(err_no);
}

int write_block(struct file_entry *file_entry, off_t file_block_ofs) {
    int idx;
    uint32_t block_length;
    ssize_t bytes_written;
    SHA1Context cx;
    int err_no[AA_NUM_COPIES];

    clear_list(err_no);
    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        block_length = NTOH(file_entry->file[idx].block.header.length) + AA_HEAD_SIZE;
        hash_init(&cx);
        hash_step(&cx, file_entry->file[idx].block.header.seed, AA_SEED_SIZE);
        hash_step(&cx, file_entry->file[idx].block.data, AA_DATA_SIZE);
        hash_finish(&cx, file_entry->file[idx].block.header.sha1);
        bytes_written = pwrite(file_entry->file[idx].fd, &file_entry->file[idx].block, block_length, file_block_ofs);
        if (bytes_written!=block_length) {
            log_info("write", "Wrote %d bytes", bytes_written);
            return errno;
        }
    }

    return 0;
}
