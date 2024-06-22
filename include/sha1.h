#ifndef __SHA1__
#define __SHA1__

typedef struct SHA1Context SHA1Context;
struct SHA1Context {
    unsigned int state[5];
    unsigned int count[2];
    unsigned char buffer[64];
};

extern void hash_init(SHA1Context *p);
extern void hash_step(SHA1Context *p, const unsigned char *data, unsigned int len);
extern void hash_finish(SHA1Context *p, unsigned char *digest);

extern void SHA1(const unsigned char *data, size_t count, unsigned char* md_buf);

#endif
