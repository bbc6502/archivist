/*
 * Inspiration for this came from a fuse tutorial by Joseph J. Pfeiffer, Jr., Ph.D.
 * https://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/
 * Thank you for your tutorial which helped me put together a working fuse implementation for this project.
 */

#define FUSE_USE_VERSION 30

#include <stdio.h>
#include <stdlib.h>
#include <fuse.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <dirent.h>
#include <arpa/inet.h>
#include "logs.h"
#include "archivist.h"

void usage() {
    fprintf(stderr, "Usage: archivist [FUSE options] mount-point root-dir-1 root-dir-2\n");
}

static void data_file_path(char fpath[PATH_MAX], const char* path, int idx) {
    char *pos;
    size_t len;

    strcpy(fpath, AA_DATA->root_dir[idx]);
    pos = (char *)path;
    len = strcspn(pos, "/");
    while (pos[0] != 0) {
        if (len > 0) {
            strcat(fpath, "/");
            strncat(fpath, pos, len);
            pos += len;
            strcat(fpath, "@");
        }
        while (pos[0] == '/') {
            pos++;
        }
        len = strcspn(pos, "/");
    }
    log_info("", "%s -> %s", path, fpath);
}

int getattr_call(const char *path, struct stat *statbuf)
{
    int rc;
    char file_path[PATH_MAX];

    log_info("getattr","%s", path);

    data_file_path(file_path, path, 0);
    rc = lstat(file_path, statbuf);
    if (rc<0) {
        return log_error("getattr", errno, "%s", path);
    }
    if ((statbuf->st_mode & S_IFMT) == S_IFREG) {
        if (statbuf->st_size>0) {
            statbuf->st_size = (off_t)((statbuf->st_size / AA_BLOCK_SIZE) * AA_DATA_SIZE + (statbuf->st_size % AA_BLOCK_SIZE) - ((statbuf->st_size % AA_BLOCK_SIZE)!=0?AA_HEAD_SIZE:0));
        }
    }

    return log_status("getattr", rc, "%s", path);
}

int fgetattr_call(const char *path, struct stat *statbuf, struct fuse_file_info *fi) {
    int rc;

    log_info("fgetattr", "%s", path);

    if (!strcmp(path, "/")) {
        return getattr_call(path, statbuf);
    }

    rc = fstat(AA_DATA->entry[fi->fh].file[0].fd, statbuf);
    if (rc < 0) {
        return log_error("fgetattr", errno, "fstat failed");
    }

    return log_status("fgetattr", 0, "");
}

uint64_t find_next_entry() {
    uint64_t fd;
    int idx;
    for(fd=AA_DATA->used_entries; fd<MAX_FUSE_OPEN_FILES; fd++) {
        for(idx=0; idx<AA_NUM_COPIES; idx++) {
            if (AA_DATA->entry[fd].file[idx].fd >= 0) {
                break;
            }
        }
        if (idx==AA_NUM_COPIES) {
            break;
        }
    }
    return fd;
}

void close_all(struct file_entry *file_entry) {
    int idx;
    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        if (file_entry->file[idx].fd >= 0) {
            close(file_entry->file[idx].fd);
            log_info("close", "idx=%d fd=%d", idx, file_entry->file[idx].fd);
            file_entry->file[idx].fd = -1;
        }
    }
}

int open_file_entry(const char* path, struct file_entry *file_entry, int flags) {
    int idx;
    char fpath[AA_NUM_COPIES][PATH_MAX];
    int err_no[AA_NUM_COPIES];

    clear_list(err_no);

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        data_file_path(fpath[idx], path, idx);
    }

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        file_entry->file[idx].fd = open(fpath[idx], flags);
        if (file_entry->file[idx].fd<0) {
            err_no[idx] = errno;
            log_error("open", errno, "idx=%d", idx);
        } else {
            log_info("open", "idx=%d fd=%d", idx, file_entry->file[idx].fd);
        }
    }

    if (first_error(err_no)!=0) {
        close_all(file_entry);
        return first_error(err_no);
    }

    return 0;
}

int open_call(const char* path, struct fuse_file_info *fi) {
    uint64_t fd;
    int flags;
    int err_no;
    struct file_entry *file_entry;

    flags = fi->flags;
    if ((flags & O_ACCMODE) == O_WRONLY) {
        flags ^= O_WRONLY;
        flags |= O_RDWR;
    }
    if ((flags & O_ACCMODE) == O_RDONLY) {
        flags ^= O_RDONLY;
        flags |= O_RDWR;
    }

    log_info("open", "%s : flags = %u", path, flags);

    fd = find_next_entry();
    if (fd==MAX_FUSE_OPEN_FILES) {
        return log_error("open", ENFILE, "Too many open files");
    }
    file_entry = &AA_DATA->entry[fd];

    err_no = open_file_entry(path, file_entry, flags);
    if (err_no!=0) {
        return log_error("open", err_no, "%s", path);
    }

    if (fd>=AA_DATA->used_entries) {
        AA_DATA->used_entries = fd + 1;
    }

    fi->fh = fd;

    return log_status("open", 0, "");
}

int release_call(const char* path, struct fuse_file_info *fi) {
    struct file_entry *file_entry;

    log_info("release", "%s", path);

    file_entry = &AA_DATA->entry[fi->fh];

    close_all(file_entry);

    if (fi->fh<AA_DATA->used_entries) {
        AA_DATA->used_entries = fi->fh;
    }

    return log_status("release", 0, "");
}

int read_call(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    size_t total_size;
    off_t file_block_ofs;
    int block_ofs;
    struct file_entry *file_entry;
    int err_no;
    int block_size;
    char *ptr;

    log_info("read", "%s , size = %lu , offset = %lu", path, size, offset);

    total_size = 0;
    ptr = (char *) buf;

    while (size > 0) {

        file_block_ofs = (offset / AA_DATA_SIZE) * AA_BLOCK_SIZE;
        block_ofs = (int)(offset % AA_DATA_SIZE);

        file_entry = &AA_DATA->entry[fi->fh];

        err_no = read_block(file_entry, file_block_ofs);
        if (err_no != 0) {
            return log_error("read", err_no, "%s", path);
        }

        block_size = NTOH(file_entry->file[0].block.header.length) - block_ofs;
        if (block_size<=0) {
            break;
        }
        if (block_size<size) {
            memcpy(ptr, &file_entry->file[0].block.data[block_ofs], block_size);
            total_size += block_size;
            ptr += block_size;
            size -= block_size;
            offset += block_size;
            log_info("read", "Read %d bytes", block_size);
        } else {
            memcpy(ptr, &file_entry->file[0].block.data[block_ofs], size);
            total_size += size;
            log_info("read", "Read %d bytes", size);
            break;
        }
    }
    return log_status("read", (int)total_size, "Composite read");
}

int write_call(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    off_t file_block_ofs;
    int block_ofs;
    int idx;
    struct file_entry *file_entry;
    int err_no;
    int block_size;
    int write_bytes;
    int written_bytes;
    char* ptr;

    log_info("write", "%s , size = %lu , offset = %lu", path, size, offset);

    if (size<1) {
        return log_status("write", 0, "");
    }

    written_bytes = 0;
    ptr = (char*)buf;

    while (size > 0) {

        file_block_ofs = (offset / AA_DATA_SIZE) * AA_BLOCK_SIZE;
        block_ofs = (int)(offset % AA_DATA_SIZE);

        file_entry = &AA_DATA->entry[fi->fh];

        err_no = read_block(file_entry, file_block_ofs);
        if (err_no != 0) {
            return log_error("write", err_no, "Error reading block");
        }

        block_size = AA_DATA_SIZE - block_ofs;
        write_bytes = (block_size<=size) ? block_size : (int)size;

        for(idx=0; idx<AA_NUM_COPIES; idx++) {
            file_entry->file[idx].block.header.version = HTON(1);
            memcpy(&file_entry->file[idx].block.data[block_ofs], ptr, write_bytes);
            if ((block_ofs+write_bytes)>NTOH(file_entry->file[idx].block.header.length)) {
                file_entry->file[idx].block.header.length = HTON(block_ofs + write_bytes);
            }
        }

        err_no = write_block(file_entry, file_block_ofs);
        if (err_no!=0) {
            return log_error("write", err_no, "");
        }

        written_bytes += write_bytes;
        size -= write_bytes;
        offset += write_bytes;
        ptr += write_bytes;

    }

    return log_status("write", (int)written_bytes, "");

}

int mknod_call(const char *path, mode_t mode, dev_t dev)
{ 
    int retstat;
    char fpath[AA_NUM_COPIES][PATH_MAX];
    int err_no[AA_NUM_COPIES];
    int idx;

    log_info("mknod", "%s", path);

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        err_no[idx] = 0;
        data_file_path(fpath[idx], path, idx);
        if (S_ISREG(mode)) {
            retstat = open(fpath[idx], O_CREAT | O_EXCL | O_WRONLY, mode);
            if (retstat < 0) {
                err_no[idx] = errno;
            } else {
                retstat = close(retstat);
                if (retstat < 0) {
                    err_no[idx] = errno;
                }
            }
        } else if (S_ISFIFO(mode)) {
            retstat = mkfifo(fpath[idx], mode);
            if (retstat < 0) {
                err_no[idx] = errno;
            }
        } else {
            retstat = mknod(fpath[idx], mode, dev);
            if (retstat < 0) {
                err_no[idx] = errno;
            }
        }
    }

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        if (err_no[idx] != 0) {
            return log_error("mknod", err_no[idx], "%s -> %s", path, fpath[idx]);
        }
    }

    return log_status("mknod", 0, "%s", path);

}

int mkdir_call(const char *path, mode_t mode) {
    char fpath[AA_NUM_COPIES][PATH_MAX];
    int err_no[AA_NUM_COPIES];
    int idx;
    int rc;

    log_info("mkdir", "%s", path);

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        err_no[idx] = 0;
        data_file_path(fpath[idx], path, idx);
        rc = mkdir(fpath[idx], mode);
        if (rc!=0) {
            err_no[idx] = errno;
        }
    }

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        if (err_no[idx] != 0) {
            return log_error("mkdir", err_no[idx], "%s -> %s", path, fpath[idx]);
        }
    }

    return log_status("mkdir", 0, "%s", path);
}

int opendir_call(const char *path, struct fuse_file_info *fi)
{
    DIR *dp;
    char fpath[PATH_MAX];

    log_info("opendir", "%s", path);

    data_file_path(fpath, path, 0);

    dp = opendir(fpath);
    if (dp == NULL) {
        return log_error("opendir", errno, "");
    }

    fi->fh = (intptr_t) dp;

    return log_status("opendir", 0, "");
}

int readdir_call(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
               struct fuse_file_info *fi)
{
    char name[257];
    DIR *dp;
    struct dirent *de;

    log_info("readdir", "%s", path);

    dp = (DIR *) (uintptr_t) fi->fh;

    de = readdir(dp);
    if (de == 0) {
        return log_error("readdir", errno, "%s", path);
    }

    do {
        memset(name, 0, sizeof(name));
        if (de->d_name[strlen(de->d_name)-1] == '@') {
            strncpy(name, de->d_name, strlen(de->d_name)-1);
        } else {
            strncpy(name, de->d_name, strlen(de->d_name));
        }
        log_info("readdir", "%s", name);
        if (filler(buf, name, NULL, 0) != 0) {
            return log_error("readdir", ENOMEM, "%s", path);
        }
    } while ((de = readdir(dp)) != NULL);

    return log_status("readdir", 0, "%s", path);
}

int releasedir_call(const char *path, struct fuse_file_info *fi) {
  log_info("releasedir", "%s", path);
  closedir((DIR *) (uintptr_t) fi->fh);
  return log_status("releasedir", 0, "");
}

int unlink_call(const char* path) {
    int rc;
    char fpath[AA_NUM_COPIES][PATH_MAX];
    int err_no[AA_NUM_COPIES];
    int idx;

    log_info("unlink", "%s", path);

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        data_file_path(fpath[idx], path, idx);
        err_no[idx] = 0;
        rc = unlink(fpath[idx]);
        if (rc < 0) {
            err_no[idx] = errno;
        }
    }

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        if (err_no[idx] != 0) {
            return log_error("unlink", err_no[idx], "%s", path);
        }
    }

    return log_status("unlink", 0, "%s", path);

}

int rmdir_call(const char* path) {
    int rc;
    char fpath[AA_NUM_COPIES][PATH_MAX];
    int err_no[AA_NUM_COPIES];
    int idx;

    log_info("rmdir", "%s", path);

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        data_file_path(fpath[idx], path, idx);
        err_no[idx] = 0;
        rc = rmdir(fpath[idx]);
        if (rc < 0) {
            err_no[idx] = errno;
        }
    }

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        if (err_no[idx] != 0) {
            return log_error("rmdir", err_no[idx], "%s -> %s", path, fpath[idx]);
        }
    }

    return log_status("rmdir", 0, "%s", path);

}

int chmod_call(const char* path, mode_t mode) {
    int rc;
    char fpath[AA_NUM_COPIES][PATH_MAX];
    int err_no[AA_NUM_COPIES];
    int idx;

    log_info("chmod", "%s", path);

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        data_file_path(fpath[idx], path, idx);
        err_no[idx] = 0;
        rc = chmod(fpath[idx], mode);
        if (rc < 0) {
            err_no[idx] = errno;
        }
    }

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        if (err_no[idx] != 0) {
            return log_error("chmod", err_no[idx], "%s -> %s", path, fpath[idx]);
        }
    }

    return log_status("chmod", 0, "%s", path);

}

int chown_call(const char* path, uid_t uid, gid_t gid) {
    int rc;
    char fpath[AA_NUM_COPIES][PATH_MAX];
    int err_no[AA_NUM_COPIES];
    int idx;

    log_info("chown", "%s", path);

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        data_file_path(fpath[idx], path, idx);
        err_no[idx] = 0;
        rc = chown(fpath[idx], uid, gid);
        if (rc < 0) {
            err_no[idx] = errno;
        }
    }

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        if (err_no[idx] != 0) {
            return log_error("chown", err_no[idx], "%s -> %s", path, fpath[idx]);
        }
    }

    return log_status("chown", 0, "%s", path);

}

int utime_call(const char* path, struct utimbuf *ubuf) {
    int rc;
    char fpath[AA_NUM_COPIES][PATH_MAX];
    int err_no[AA_NUM_COPIES];
    int idx;

    log_info("utime", "%s", path);

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        data_file_path(fpath[idx], path, idx);
        err_no[idx] = 0;
        rc = utime(fpath[idx], ubuf);
        if (rc < 0) {
            err_no[idx] = errno;
        }
    }

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        if (err_no[idx] != 0) {
            return log_error("utime", err_no[idx], "%s -> %s", path, fpath[idx]);
        }
    }

    return log_status("utime", 0, "%s", path);

}

int truncate_call(const char* path, off_t new_size) {
    int rc;
    char fpath[AA_NUM_COPIES][PATH_MAX];
    int err_no[AA_NUM_COPIES];
    int idx;
    struct file_entry file_entry;
    off_t file_block_ofs;
    int block_length;
    size_t new_file_size;

    log_info("truncate", "%s", path);

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        data_file_path(fpath[idx], path, idx);
        err_no[idx] = 0;
    }

    if (new_size==0) {
        for(idx=0; idx<AA_NUM_COPIES; idx++) {
            rc = truncate(fpath[idx], 0);
            if (rc<0) {
                err_no[idx] = errno;
            }
        }
        for(idx=0; idx<AA_NUM_COPIES; idx++) {
            if (err_no[idx]!=0) {
                return log_error("truncate", err_no[idx], "%s", path);
            }
        }
        return log_status("truncate", 0, "%s", path);
    }

    file_block_ofs = (new_size / AA_DATA_SIZE) * AA_BLOCK_SIZE;
    block_length = (int)(new_size % AA_DATA_SIZE);

    rc = open_file_entry(path, &file_entry, O_RDWR);
    if (rc!=0) {
        return log_error("truncate", rc, "%s", path);
    }
    rc = read_block(&file_entry, file_block_ofs);
    if (rc!=0) {
        close_all(&file_entry);
        return log_error("truncate", rc, "%s", path);
    }
    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        file_entry.file[idx].block.header.length = block_length;
    }
    rc = write_block(&file_entry, file_block_ofs);
    if (rc!=0) {
        close_all(&file_entry);
        return log_error("truncate", rc, "%s", path);
    }
    close_all(&file_entry);

    new_file_size = file_block_ofs + block_length + AA_HEAD_SIZE;

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        rc = truncate(fpath[idx], (off_t)new_file_size);
        if (rc<0) {
            err_no[idx] = errno;
        }
    }

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        if (err_no[idx]!=0) {
            return log_error("truncate", err_no[idx], "%s", path);
        }
    }

    return log_status("truncate", 0, "%s", path);

}

int rename_call(const char* old_path, const char* new_path) {
    int rc;
    char old_fpath[AA_NUM_COPIES][PATH_MAX];
    char new_fpath[AA_NUM_COPIES][PATH_MAX];
    int err_no[AA_NUM_COPIES];
    int idx;

    log_info("rename", "%s -> %s", old_path, new_path);

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        data_file_path(old_fpath[idx], old_path, idx);
        data_file_path(new_fpath[idx], new_path, idx);
        err_no[idx] = 0;
        rc = rename(old_fpath[idx], new_fpath[idx]);
        if (rc<0) {
            err_no[idx] = errno;
        }
    }

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        if (err_no[idx]!=0) {
            return log_error("rename", err_no[idx], "%s -> %s", old_path[idx], new_path[idx]);
        }
    }

    return log_status("rename", 0, "%s -> %s", old_path, new_path);
}

static struct fuse_operations operations = {
    .getattr = getattr_call,
    .open = open_call,
    .release = release_call,
    .read = read_call,
    .write = write_call,
    .fgetattr = fgetattr_call,
    .mknod = mknod_call,
    .mkdir = mkdir_call,
    .chmod = chmod_call,
    .chown = chown_call,
    .utime = utime_call,
    .opendir = opendir_call,
    .readdir = readdir_call,
    .releasedir = releasedir_call,
    .unlink = unlink_call,
    .rmdir = rmdir_call,
    .truncate = truncate_call,
    .rename = rename_call,
};

int main(int argc, char* argv[]) {
    int fuse_stat;
    struct archivist_state *aa_state;
    int idx;
    int index;
    char mount_point[PATH_MAX];

    if ((getuid()==0)||(getgid()==0)) {
        fprintf(stderr, "Running archivist as root has security issues\n");
        exit(1);
    }

    fprintf(stderr, "Fuse library version %d.%d\n", FUSE_MAJOR_VERSION, FUSE_MINOR_VERSION);

    if (argc<(AA_NUM_COPIES+2)) {
        usage();
        exit(1);
    }
    for(idx=argc-AA_NUM_COPIES-1; idx<argc; idx++) {
        if (argv[idx][0]=='-') {
            usage();
            exit(1);
        }
    }

    aa_state = calloc(sizeof(struct archivist_state),1);
    if (aa_state==NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }

    for(index=0; index<MAX_FUSE_OPEN_FILES; index++) {
        for(idx=0; idx<AA_NUM_COPIES; idx++) {
            aa_state->entry[index].file[idx].fd = -1;
        }
    }

    init_logging();

    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        realpath(argv[argc-1], aa_state->root_dir[AA_NUM_COPIES-1-idx]);
        argv[argc-1] = NULL;
        argc -= 1;
    }
    for(idx=0; idx<AA_NUM_COPIES; idx++) {
        if (idx==0) {
            fprintf(stderr, "Primary archive at %s\n", aa_state->root_dir[idx]);
        } else {
            fprintf(stderr, "Secondary archive at %s\n", aa_state->root_dir[idx]);
        }
    }

    realpath(argv[argc-1], mount_point);
    fprintf(stderr, "Starting Fuse on %s\n", mount_point);
    fuse_stat = fuse_main(argc, argv, &operations, aa_state);
    fprintf(stderr, "Fuse returned %d\n", fuse_stat);
    return fuse_stat;

}
