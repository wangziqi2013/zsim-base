//#include <iostream>
//#include <vector>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
//void test (void) {
//     std::cout << "test" << std::endl;

//}

#include "compression.h"

#define BDI_BASE_COUNT      2


static unsigned long long my_llabs(long long x) {
  unsigned long long t = x >> 63;
  return (x ^ t) - t;
}


static unsigned my_abs(int x) {
  unsigned t = x >> 31;
  return (x ^ t) - t;
}

// This function unpacks an array of values of size "step" into a unsigned long long array
// Size is the byte size of the input stream, step is the number of bytes in each element
void convertBuffer2Array(char *in_buf, unsigned long long *out_buf,unsigned size, unsigned step) {
  //long long unsigned* values = (long long unsigned*)malloc(sizeof(long long unsigned) * size / step);
  //      std::cout << std::dec << "ConvertBuffer = " << size/step << std::endl;
  //init
  unsigned int i, j;
  for (i = 0; i < size / step; i++) {
    out_buf[i] = 0; // Initialize all elements to zero.
  }
  for (i = 0; i < size; i += step) {
    for (j = 0; j < step; j++) {
      out_buf[i / step] += ((long long unsigned)(unsigned char)in_buf[i + j] << (8 * j));
      //printf("0x%X ", (unsigned int)(unsigned char)in_buf[i + j]);
    }
    // Copy sign bit
    if(in_buf[i + step - 1] & 0x80) {
      for (j = step; j < sizeof(unsigned long long); j++) {
        out_buf[i / step] += (((unsigned long long)0xFFU) << (8 * j));
        //printf("%llX ", out_buf[i / step]);
      }
    }
    //printf("Index %d value 0x%llX\n", i / step, out_buf[i / step]);
  }
  return;
}

///
/// Check if the cache line consists of only zero values
///
int isZeroPackable(long long unsigned* values, unsigned size) {
  int nonZero = 0;
  unsigned int i;
  for (i = 0; i < size; i++) {
    if (values[i] != 0) {
      nonZero = 1;
      break;
    }
  }
  return !nonZero;
}

///
/// Check if the cache line consists of only same values
///
int isSameValuePackable(long long unsigned* values, unsigned size) {
  int notSame = 0;
  unsigned int i;
  for (i = 0; i < size; i++) {
    if (values[0] != values[i]) {
      notSame = 1;
      break;
    }
  }
  return !notSame;
}

///
/// Check if the cache line values can be compressed with multiple base + 1,2,or 4-byte offset
/// Returns size after compression
///
unsigned doubleExponentCompression(long long unsigned* values, unsigned size,
                                   unsigned blimit, unsigned bsize) {
  unsigned long long limit = 0;
  //define the appropriate size for the mask
  switch (blimit) {
  case 1:
    limit = 56;
    break;
  case 2:
    limit = 48;
    break;
  default:
    // std::cout << "Wrong blimit value = " <<  blimit << std::endl;
    exit(1);
  }
  // finding bases: # BASES
  // find how many elements can be compressed with mbases
  unsigned compCount = 0;
  unsigned int i;
  for (i = 0; i < size; i++) {
    if ((values[0] >> limit) == (values[i] >> limit)) {
      compCount++;
    }
  }
  //return compressed size
  if (compCount != size)
    return size * bsize;
  return size * bsize - (compCount - 1) * blimit;
}

///
/// Check if the cache line values can be compressed with multiple base + 1,2,or 4-byte offset
/// Returns size after compression
///
// size: Number of input words
// blimit: Byte size of target word
// bsize: Byte size of input word
// Return value is the number of bytes of the compressed line
unsigned multBaseCompression(long long unsigned *values, unsigned size, unsigned blimit, unsigned bsize) {
  unsigned long long limit = 0;
  //define the appropriate size for the mask
  switch (blimit) {
    case 1:
      limit = 0xFF;
      break;
    case 2:
      limit = 0xFFFF;
      break;
    case 4:
      limit = 0xFFFFFFFF;
      break;
    default:
      //std::cout << "Wrong blimit value = " <<  blimit << std::endl;
      exit(1);
  }
  // finding bases: # BASES
  //std::vector<unsigned long long> mbases;
  //mbases.push_back(values[0]); //add the first base
  unsigned long long mbases[64];
  unsigned baseCount = 1;
  mbases[0] = 0ULL; // Implicit base zero for compressing immediate values
  unsigned int i, j;
  for (i = 0; i < size; i++) {
    // Values cannot be compressed by existing bases will be used as the new base
    for (j = 0; j < baseCount; j++) {
      if (my_llabs((long long int)(mbases[j] - values[i])) > limit) {
        mbases[baseCount++] = values[i];
      }
    }
    if (baseCount >= BDI_BASE_COUNT) { //we don't have more bases
      break;
    }
  }
  // find how many elements can be compressed with mbases
  // Some elements may not be compressed
  unsigned compCount = 0;
  for (i = 0; i < size; i++) {
    //ol covered = 0;
    unsigned old = compCount;
    for (j = 0; j < baseCount; j++) {
      //printf("index %d values 0x%llX base 0x%llX delta 0x%llX delta abs 0x%llX\n", 
      //  i, values[i], mbases[j], mbases[j] - values[i],
      //  my_llabs((long long int)(mbases[j] - values[i])));
      if (my_llabs((long long int)(mbases[j] - values[i])) <= limit) {
        compCount++;
        break;
      }
    }
    if(compCount == old) {
      //printf("Index %d not compressed\n", i);
    }
  }
  //printf("compCount = %d\n", compCount);
  // If not all of them are compressed, just return the uncompressed size
  if (compCount < size) {
    return size * bsize;
  }
  assert(compCount == size);
  //return compressed size
  unsigned mCompSize = blimit * compCount + bsize * BDI_BASE_COUNT + (size - compCount) * bsize;
  //printf("%d-bases bsize = %d osize = %d CompCount = %d CompSize = %d\n", BASES, bsize, blimit, compCount, mCompSize);
  return mCompSize;
}

unsigned BDICompress(char *buffer, unsigned _blockSize, int *metadata) {
  //char * dst = new char [_blockSize];
  //  print_value(buffer, _blockSize);
  unsigned long long values[32]; // In the worst case there will be 32 elements
  convertBuffer2Array(buffer, values, _blockSize, 8);
  unsigned bestCSize = _blockSize;
  unsigned currCSize = _blockSize;
  *metadata = COMPRESSION_FAIL;
  if(isZeroPackable(values, _blockSize / 8)) {
    bestCSize = 1;
    *metadata = COMPRESSION_ZERO;
  } 
  if(isSameValuePackable(values, _blockSize / 8)) {
    currCSize = 8;
  }
  bestCSize = bestCSize > currCSize ? ((*metadata = COMPRESSION_DUP), currCSize) : bestCSize;
  currCSize = multBaseCompression(values, _blockSize / 8, 1, 8);
  bestCSize = bestCSize > currCSize ? ((*metadata = COMPRESSION_8_1), currCSize) : bestCSize;
  currCSize = multBaseCompression(values, _blockSize / 8, 2, 8);
  bestCSize = bestCSize > currCSize ? ((*metadata = COMPRESSION_8_2), currCSize) : bestCSize;
  currCSize = multBaseCompression(values, _blockSize / 8, 4, 8);
  bestCSize = bestCSize > currCSize ? ((*metadata = COMPRESSION_8_4), currCSize) : bestCSize;
  convertBuffer2Array(buffer, values, _blockSize, 4);
  if(isSameValuePackable(values, _blockSize / 4)) {
    currCSize = 4;
  }
  bestCSize = bestCSize > currCSize ? ((*metadata = COMPRESSION_DUP), currCSize) : bestCSize;
  currCSize = multBaseCompression(values, _blockSize / 4, 1, 4);
  bestCSize = bestCSize > currCSize ? ((*metadata = COMPRESSION_4_1), currCSize) : bestCSize;
  currCSize = multBaseCompression(values, _blockSize / 4, 2, 4);
  bestCSize = bestCSize > currCSize ? ((*metadata = COMPRESSION_4_2), currCSize) : bestCSize;
  convertBuffer2Array(buffer, values, _blockSize, 2);
  currCSize = multBaseCompression(values, _blockSize / 2, 1, 2);
  bestCSize = bestCSize > currCSize ? ((*metadata = COMPRESSION_2_1), currCSize) : bestCSize;
  buffer = NULL;
  return bestCSize;
}

// Return value is byte size of compressed line
// If uncompressable then just return block size
unsigned FPCCompress(char* buffer, unsigned size) {
  unsigned long long values[16];
  convertBuffer2Array(buffer, values, size * 4, 4);
  // Number of payload bytes
  unsigned compressable = 0;
  unsigned int i;
  for (i = 0; i < size; i++) {
    // 000
    if (values[i] == 0) {
      compressable += 1; //printf("000 (1)\n");
      continue;
    }
    // 001 010
    if (my_abs((int)(values[i])) <= 0xFF) {
      compressable += 1; //printf("001/010 (1)\n");
      continue;
    }
    // 011
    if (my_abs((int)(values[i])) <= 0xFFFF) {
      compressable += 2; //printf("011 (2)\n");
      continue;
    }
    //100
    if (((values[i]) & 0xFFFF) == 0) {
      compressable += 2; //printf("100 (2)\n");
      continue;
    }
    //101
    if (my_abs((int)((values[i]) & 0xFFFF)) <= 0xFF &&
        my_abs((int)((values[i] >> 16) & 0xFFFF)) <= 0xFF) {
      compressable += 2; //printf("101 (2)\n");
      continue;
    }
    //110
    unsigned byte0 = (values[i]) & 0xFF;
    unsigned byte1 = (values[i] >> 8) & 0xFF;
    unsigned byte2 = (values[i] >> 16) & 0xFF;
    unsigned byte3 = (values[i] >> 24) & 0xFF;
    if (byte0 == byte1 && byte0 == byte2 && byte0 == byte3) {
      compressable += 1; //printf("110 (1)\n");
      continue;
    }
    //111
    compressable += 4;
    //printf("111 (4)\n");
  }
  // This was in the original: "6 bytes for 3 bit per every 4-byte word in a 64 byte cache line"
  // Each input word incurs a constant 3-bit type field
  // Payload is always byte-aligned, and is tracked by "compressable"
  unsigned compSize = compressable + size * 3 / 8;
  //printf("compressable %u size %u compSize %u\n", compressable, size, compSize);
  if (compSize < size * 4)
    return compSize;
  else
    return size * 4;
}

// compress is the actual compression algorithm
unsigned _GeneralCompress(char* buffer, unsigned _blockSize, unsigned compress, int *metadata) { 
  switch (compress) {
    case COMPRESSION_NONE:
      return _blockSize;
      break;
    case COMPRESSION_BDI:
      return BDICompress(buffer, _blockSize, metadata);
      break;
    case COMPRESSION_FPC:
      //std::cout << "block-size: " << _blockSize << "\n";
      return FPCCompress(buffer, _blockSize / 4);
      break;
    case COMPRESSION_BEST: {
      unsigned BDISize = BDICompress(buffer, _blockSize, metadata);
      unsigned FPCSize = FPCCompress(buffer, _blockSize / 4);
      if (BDISize <= FPCSize)
        return BDISize;
      else
        return FPCSize;
      break;
    }
    default:
      printf("Unknown compression type: %d\n", compress);
      exit(1);
  }
  return 0;
}

unsigned GeneralCompress(void *buffer, unsigned compress, int *metadata) { 
  return _GeneralCompress((char *)buffer, 64, compress, metadata);
}
