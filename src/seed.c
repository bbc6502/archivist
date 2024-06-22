#include <sys/random.h>
#include "logs.h"
#include "sha1.h"
#include "blocks.h"
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "../include/seed.h"

int initialise_seed(unsigned char seed[]) {
    ssize_t size;

    size = getrandom(seed, AA_SEED_SIZE, 0);
    if (size==AA_SEED_SIZE) {
        return 0;
    }
    return -1;
}