

#define COMPRESSION_NONE    0
#define COMPRESSION_BDI     1
#define COMPRESSION_FPC     2
#define COMPRESSION_BEST    3

#define COMPRESSION_FAIL    -1
#define COMPRESSION_ZERO    0
#define COMPRESSION_DUP     1
#define COMPRESSION_8_1     2
#define COMPRESSION_8_2     3
#define COMPRESSION_8_4     4
#define COMPRESSION_4_1     5
#define COMPRESSION_4_2     6
#define COMPRESSION_2_1     7

void convertBuffer2Array(char *in_buf, unsigned long long *out_buf,unsigned size, unsigned step);
unsigned multBaseCompression(long long unsigned* values, unsigned size, unsigned blimit, unsigned bsize);
unsigned GeneralCompress(void *buffer, unsigned compress, int *metadata);
