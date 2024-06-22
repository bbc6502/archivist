
#ifndef ARCHIVIST_SEED_H
#define ARCHIVIST_SEED_H

#include <stdint.h>
#include <limits.h>
#include <unistd.h>

#define AA_SEED_SIZE 8

extern int initialise_seed(unsigned char seed[]);

#endif //ARCHIVIST_SEED_H
