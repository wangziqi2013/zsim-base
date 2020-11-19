
#include "2DOC.h"

//* BDI

// Indexed using BDI macro
BDI_param_t BDI_params[6] = {                         // Name    Size Order Type
  {BDI_8_4, 8, 4, 41, 1, 4, 9, 0xFFFFFFFF80000000UL}, // BDI_8_4 41   6     4
  {BDI_8_2, 8, 2, 25, 1, 3, 9, 0xFFFFFFFFFFFF8000UL}, // BDI_8_2 25   3     3
  {BDI_8_1, 8, 1, 17, 1, 2, 9, 0xFFFFFFFFFFFFFF80UL}, // BDI_8_1 17   1     2
  {BDI_4_2, 4, 2, 38, 2, 6, 6, 0x00000000FFFF8000UL}, // BDI_4_2 38   4     6
  {BDI_4_1, 4, 1, 22, 2, 5, 6, 0x00000000FFFFFF80UL}, // BDI_4_1 22   2     5
  {BDI_2_1, 2, 1, 38, 4, 7, 6, 0x000000000000FF80UL}, // BDI_2_1 38   5     7
};

// Indexed using BDI types
BDI_param_t *BDI_types[8] = {
  NULL, NULL,                           // 0 and 1 are not BDI
  &BDI_params[2],                       // 8 - 1
  &BDI_params[1],                       // 8 - 2
  &BDI_params[0],                       // 8 - 4
  &BDI_params[4],                       // 4 - 1
  &BDI_params[3],                       // 4 - 2
  &BDI_params[5],                       // 2 - 1
};

int BDI_comp_order[6] = {
  2, 5, 3, 6, 7, 4,
};

// Write the base value to the given pointer, and return the pointer after the write
void *BDI_comp_write_base(void *out_buf, uint64_t base, int word_size) {
  switch(word_size) {
    case 2: return ptr_append_16(out_buf, (uint16_t)base); 
    case 4: return ptr_append_32(out_buf, (uint32_t)base); 
    case 8: return ptr_append_64(out_buf, (uint64_t)base);
    default: error_exit("Unknown word_size %d\n", word_size);
  }
  return NULL;
}

// Reads the base of size word_size, and changes the input buffer pointer as well
uint64_t BDI_decomp_read_base(void **in_buf, int word_size) {
  switch(word_size) {
    case 2: return ptr_pop_16(in_buf);
    case 4: return ptr_pop_32(in_buf);
    case 8: return ptr_pop_64(in_buf);
    default: error_exit("Unknown word_size %d\n", word_size);
  }
  return -1UL;
}

// Write small value bitmap to the given pointer, and return the pointer after the write
void *BDI_comp_write_bitmap(void *out_buf, uint64_t bitmap, int word_size) {
  switch(word_size) {
    case 2: return ptr_append_32(out_buf, (uint32_t)bitmap);
    case 4: return ptr_append_16(out_buf, (uint16_t)bitmap);
    case 8: return ptr_append_8(out_buf, (uint8_t)bitmap);
    default: error_exit("Unknown word_size %d\n", word_size);
  }
  return NULL;
}

// Read bitmap and move the input buffer pointer
uint64_t BDI_decomp_read_bitmap(void **in_buf, int word_size) {
  switch(word_size) {
    case 2: return ptr_pop_32(in_buf);
    case 4: return ptr_pop_16(in_buf);
    case 8: return ptr_pop_8(in_buf);
    default: error_exit("Unknown word_size %d\n", word_size);
  }
  return -1UL;
}

// Packing uint64_t integers into compact representation with size word_size
void BDI_pack(void *out_buf, uint64_t *in_buf, int word_size, int iter) {
  switch(word_size) {
    case 1: for(int i = 0;i < iter;i++) { out_buf = ptr_append_8(out_buf, in_buf[i]); } break;
    case 2: for(int i = 0;i < iter;i++) { out_buf = ptr_append_16(out_buf, in_buf[i]); } break;
    case 4: for(int i = 0;i < iter;i++) { out_buf = ptr_append_32(out_buf, in_buf[i]); } break;
    case 8: for(int i = 0;i < iter;i++) { out_buf = ptr_append_64(out_buf, in_buf[i]); } break;
    default: error_exit("Unsupported pack word size %d\n", word_size);
  }
  return;
}

// This function unpacks words in the input buffer of word_size to out_buf as uniform 64-bit integers
void BDI_unpack(uint64_t *out_buf, void *in_buf, int word_size, int iter) {
  switch(word_size) {
    case 1: for(int i = 0;i < iter;i++) { out_buf[i] = (uint64_t)((uint8_t *)in_buf)[i]; } break;
    case 2: for(int i = 0;i < iter;i++) { out_buf[i] = (uint64_t)((uint16_t *)in_buf)[i]; } break;
    case 4: for(int i = 0;i < iter;i++) { out_buf[i] = (uint64_t)((uint32_t *)in_buf)[i]; } break;
    case 8: for(int i = 0;i < iter;i++) { out_buf[i] = (uint64_t)((uint64_t *)in_buf)[i]; } break;
    default: error_exit("Unsupported unpack word size %d\n", word_size);
  }
  return;
}

// Returns type (positive number) if succeeds; BDI_TYPE_INVALID (-1) if fails
// in_buf points to the 64 byte cache line whose word size is given by param->word_size
// out_buf points to output buffer whose word size is given by param->target_size
int BDI_comp_scalar(void *out_buf, void *in_buf, BDI_param_t *param, int dry_run) {
  int word_size = param->word_size;
  int target_size = param->target_size;
  void *const original_out_buf = out_buf; // For debugging
  assert(dry_run == 0 || dry_run == 1);
  uint64_t temp[32]; // Expand them to 64-bit integers
  int iter = UTIL_CACHE_LINE_SIZE / word_size; // Number of elements
  BDI_unpack(temp, in_buf, word_size, iter); // Unpack the input to uint64_t integers
  // Whether BDI explicit basd has been found (should be the first value not compressed with implicit base)
  int base_assigned = 0; 
  uint64_t base = -1UL;  // Explicit base value
  uint64_t small_value_bitmap = 0x0UL; // 1 means the value is compressed with implicit zero base
  uint64_t curr_mask = 0x1UL;
  for(int i = 0;i < iter;i++) {
    // First check if the value itself is small (sign extended)
    uint64_t masked_value = temp[i] & param->high_mask;
    if(masked_value == 0UL || masked_value == param->high_mask) {
      // We found a small value
      small_value_bitmap |= curr_mask;
      curr_mask <<= 1;
      continue;
    } else {
      if(base_assigned == 0) {
        base_assigned = 1;
        base = temp[i];
      }
    }
    curr_mask <<= 1;
    assert(base_assigned == 1);
    // Then check delta
    temp[i] -= base; // Subtract temp from the base
    uint64_t masked_delta = temp[i] & param->high_mask;
    if(masked_delta != 0 && masked_delta != param->high_mask) {
      //printf("base %ld, i = %d delta %ld\n", base, i, temp[i]);
      return BDI_TYPE_INVALID; // Could not encode the given cache line with the parameter
    }
  }
  //printf("Bitmap 0x%lX\n\n", small_value_bitmap);
  // If not dry run, we copy the result back to the output
  if(dry_run == 0) {
    // Write metadata first
    out_buf = BDI_comp_write_bitmap(out_buf, small_value_bitmap, word_size);
    out_buf = BDI_comp_write_base(out_buf, base, word_size);
    BDI_pack(out_buf, temp, target_size, iter);
    // Move past the last byte read
    out_buf = (void *)((uint64_t)out_buf + target_size * iter);
    // Make sure actual compressed size always matches the compressed size
    assert(((uint64_t)out_buf - (uint64_t)original_out_buf) == (uint64_t)param->compressed_size);
    (void)original_out_buf; // Avoid warning when assert() is turned off
  }
  return param->type;
}

void BDI_decomp_scalar(void *out_buf, void *in_buf, BDI_param_t *param) {
  assert(param != NULL);
  int word_size = param->word_size;
  int target_size = param->target_size;
  // Read bitmap and base
  uint64_t small_value_bitmap = BDI_decomp_read_bitmap(&in_buf, word_size);
  uint64_t base = BDI_decomp_read_base(&in_buf, word_size);
  int iter = UTIL_CACHE_LINE_SIZE / word_size;
  // Unpack target word into uint64_t
  uint64_t temp[32];
  BDI_unpack(temp, in_buf, target_size, iter);
  for(int i = 0;i < iter;i++) {
    // Sign extension using the mask (note the mask contains sign bit)
    if((temp[i] & param->high_mask) != 0) {
      temp[i] |= param->high_mask;
    }
    int is_small = small_value_bitmap & 0x1UL;
    if(!is_small) {
      temp[i] += base;
    }
    small_value_bitmap >>= 1;
  }
  // Pack them as actual words - this will truncate if word_size is smaller than 8
  BDI_pack(out_buf, temp, word_size, iter);
  return;
}

// Return value is BDI type, not the macro
int BDI_comp(void *out_buf, void *in_buf) {
  // 6 different BDIs to try
  int iter = (int)(sizeof(BDI_comp_order) / sizeof(int));
  assert(iter == 6); 
  int ret;
  for(int i = 0;i < iter;i++) {
    int type = BDI_comp_order[i];
    // Maps type to param
    BDI_param_t *param = BDI_types[type];
    assert(param != NULL);
    // This function returns the run time type of the compression scheme
    ret = BDI_comp_AVX2(out_buf, in_buf, param, 0);
    //printf("i %d type %d ret %d\n", i, type, ret);
    // Use the first one that can actually compress the input
    if(ret != BDI_TYPE_INVALID) {
      assert(ret == type);
      break;
    }
  }
  return ret;
}

void BDI_decomp(void *out_buf, void *in_buf, int type) {
  assert(type >= BDI_TYPE_BEGIN && type < BDI_TYPE_END);
  BDI_param_t *param = BDI_types[type];
  assert(param != NULL);
  BDI_decomp_AVX2(out_buf, in_buf, param);
  return;
}

void BDI_print_compressed(void *in_buf, BDI_param_t *param) {
  int word_size = param->word_size;
  int target_size = param->target_size;
  int iter = UTIL_CACHE_LINE_SIZE / word_size;
  uint64_t small_value_bitmap = BDI_decomp_read_bitmap(&in_buf, word_size);
  uint64_t base = BDI_decomp_read_base(&in_buf, word_size);
  // Save value
  uint64_t small_value_bitmap_original = small_value_bitmap;
  printf("Small value bitmap: 0x%lX\n", small_value_bitmap);
  printf("  Decoded (LSB to MSB): ");
  for(int i = 0;i < iter;i++) {
    if(small_value_bitmap & 0x1UL) printf("1 ");
    else printf("0 ");
    small_value_bitmap >>= 1;
  }
  putchar('\n');
  printf("Base: %ld (lu %lu lX 0x%lX)\n", base, base, base);
  printf("Compressed code words\n");
  printf("---------------------\n");
  uint64_t temp[32];
  BDI_unpack(temp, in_buf, target_size, iter);
  for(int i = 0;i < iter;i++) {
    if((temp[i] & param->high_mask) != 0) {
      // All-1 high mask with low target_size being zero
      // Sign bit is already 1, so we need not set it
      printf("%ld ", temp[i] | (-1UL << (target_size * 8)));
    } else {
      printf("%ld ", temp[i]);
    }
    
  }
  printf("\n---------------------\n");
  printf("Decoded values\n");
  printf("--------------\n");
  small_value_bitmap = small_value_bitmap_original;
  for(int i = 0;i < iter;i++) {
    if((temp[i] & param->high_mask) != 0) {
      temp[i] |= param->high_mask;
    }
    int is_small = small_value_bitmap & 0x1UL;
    if(!is_small) {
      temp[i] += base;
    }
    // Sign extension to 64 bits
    temp[i] = (uint64_t)(((int64_t)(temp[i] << (64 - word_size * 8))) >> (64 - word_size * 8));
    printf("%ld ", temp[i]);
    small_value_bitmap >>= 1;
  }
  printf("\n--------------\n");
  //_mm512_sub_epi16();
  //_mm512_load_epi32();
  return;
}

// Returns compressed size (in bytes) using delta compression (note that delta compression is always doable)
// This function assumes that both are compressed with the same type
// No compressed data is generated, since we only need uncompressed size
int BDI_vertical_comp(void *_base_buf, void *_in_buf, int type) {
  assert(type >= BDI_TYPE_BEGIN && type < BDI_TYPE_END);
  BDI_param_t *param = BDI_types[type];
  //printf("Param word %d target %d\n", param->word_size, param->target_size);
  assert(param != NULL);
  // Move to compressed body
  _base_buf = ptr_add(_base_buf, param->body_offset);
  _in_buf = ptr_add(_in_buf, param->body_offset);
  // Number of compressed words in the body
  int iter = UTIL_CACHE_LINE_SIZE / param->word_size;
  assert(iter % 8 == 0);
  // first half is word size (which we reduce from when a match is found); second half is the extra bitmap
  int size = param->compressed_size + iter / 8;
  //printf("Param %d-%d\n", param->word_size, param->target_size);
  // Compare compressed words, remove the word if it is identical, and add the extra bitmap
  switch(param->target_size) {
    case 1: {
      uint8_t *base_buf = (uint8_t *)_base_buf;
      uint8_t *in_buf = (uint8_t *)_in_buf;
      for(int i = 0;i < iter;i++) {
        if(*base_buf == *in_buf) {
          //printf("Match found size %d\n", size);
          size -= 1; // Drop the compressed word
        }
        base_buf++;
        in_buf++;
      }
    } break;
    case 2: {
      uint16_t *base_buf = (uint16_t *)_base_buf;
      uint16_t *in_buf = (uint16_t *)_in_buf;
      for(int i = 0;i < iter;i++) {
        if(*base_buf == *in_buf) {
          //printf("Match found size %d\n", size);
          size -= 2;
        }
        base_buf++;
        in_buf++;
      }
    } break;
    case 4: {
      uint32_t *base_buf = (uint32_t *)_base_buf;
      uint32_t *in_buf = (uint32_t *)_in_buf;
      for(int i = 0;i < iter;i++) {
        //printf("%X\n", in_buf[i]);
        if(*base_buf == *in_buf) {
          size -= 4;
        }
        base_buf++;
        in_buf++;
      }
    } break;
    default: {
      error_exit("Invalid word size: %d\n", param->word_size);
    } break;
  }
  return size;
} 

// Returns one if all bits are zero
int zero_comp(void *in_buf) {
  for(int i = 0;i < (int)UTIL_CACHE_LINE_SIZE / (int)sizeof(uint64_t);i++) {
    if(((uint64_t *)in_buf)[i] != 0UL) return 0;
  }
  return 1;
}

//* FPC

// Indexed by types
int FPC_data_bits[8] = {
  3, 4, 8, 16, 16, 16, 8, 32,
};

// This function is limited, as it only handles byte aligned source word (but not necessarily 8-multiple bits)
// Also bits must be less than or equal to 64 bits; src must also be 64 bit aligned
// Returns the new dest_offset
// Note: Higher bits of the last word are undefined. Do not rely on them being cleared
int FPC_pack_bits(void *dest, int dest_offset, uint64_t *src, int bits) {
  assert(FPC_BIT_COPY_GRANULARITY == 64);
  assert(bits > 0 && bits <= FPC_BIT_COPY_GRANULARITY);
  int dest_word_index = dest_offset / FPC_BIT_COPY_GRANULARITY;
  int dest_word_offset = dest_offset % FPC_BIT_COPY_GRANULARITY;
  int dest_remains = FPC_BIT_COPY_GRANULARITY - dest_word_offset;
  // First clear the dest word
  ((uint64_t *)dest)[dest_word_index] &= ~(-1UL << dest_word_offset);
  // Then write lower dest_remain bits
  ((uint64_t *)dest)[dest_word_index] |= (*src << dest_word_offset);
  if(bits > dest_remains) {
    // Do not need to clear this word since there is no previous data on this word; direct assign
    // Extract the highest (bits - dest_remains) bits of the input and append to the input
    ((uint64_t *)dest)[dest_word_index + 1] = (*src >> dest_remains);
    // This is for easier debugging, clear higher bits to zero, but is unnecessary
#ifndef NDEBUG
    ((uint64_t *)dest)[dest_word_index + 1] &= ~(-1UL << (bits - dest_remains));
#endif
  } else {
  // Clear higher bits if bits cannot fill the current word
#ifndef NDEBUG
    ((uint64_t *)dest)[dest_word_index] &= (-1UL >> (dest_remains - bits));
#endif
  }
  return dest_offset + bits;
}

// Using BMI bit extraction interface
int FPC_pack_bits_bmi(void *_dest, int dest_offset, uint64_t *src, int bits) {
  assert(FPC_BIT_COPY_GRANULARITY == 64);
  assert(bits > 0 && bits <= FPC_BIT_COPY_GRANULARITY);
  int dest_word_index = dest_offset / FPC_BIT_COPY_GRANULARITY;
  int dest_word_offset = dest_offset % FPC_BIT_COPY_GRANULARITY;
  int dest_remains = FPC_BIT_COPY_GRANULARITY - dest_word_offset;
  uint64_t *dest = (uint64_t *)_dest;
  dest[dest_word_index] = (
    bit_extract64(dest[dest_word_index], 0, dest_word_offset) | // This clears higher bits
    (bit_extract64(*src, 0, bits) << dest_word_offset)  // bits can be larger than actual
  );
  // Move to the next word. This will also set high bits to zero
  int delta = bits - dest_remains;
  if(delta > 0) {
    dest[dest_word_index + 1] = bit_extract64(*src, dest_remains, delta);
  }
  return dest_offset + bits;
}

// Use immediate number (numeric literals) instead of the source
inline static int FPC_pack_bits_imm(void *dest, int dest_offset, uint64_t src, int bits) {
  return FPC_pack_bits_bmi(dest, dest_offset, &src, bits);
}

// The reverse of the pack function. This function only handles byte-aligned dest buffer, and only supports
// less than or equal to 64 bit reads
// Note: dest should be pre-initialized to zero, higher bits are not guaranteed to be cleared
int FPC_unpack_bits(uint64_t *dest, void *src, int src_offset, int bits) {
  assert(FPC_BIT_COPY_GRANULARITY == 64);
  assert(bits > 0 && bits <= FPC_BIT_COPY_GRANULARITY);
  int src_word_index = src_offset / FPC_BIT_COPY_GRANULARITY;
  int src_word_offset = src_offset % FPC_BIT_COPY_GRANULARITY;
  int src_remains = FPC_BIT_COPY_GRANULARITY - src_word_offset;
  if(bits > src_remains) {
    *dest = (((uint64_t *)src)[src_word_index]) >> src_word_offset;
    // Then use the next src word to complete the rest of the bits
    // Lest shift by src_remains bit since that is how many we copied in the previous step
    *dest |= (((uint64_t *)src)[src_word_index + 1] & ~(-1UL << (bits - src_remains))) << src_remains;
  } else {
    // Left shift first to clear the rest of the higher bits not written by this operation to zero
    // and then right shift to make them align to bit offset 0 in the dest word
    *dest = (((uint64_t *)src)[src_word_index] << (src_remains - bits)) >> (FPC_BIT_COPY_GRANULARITY - bits);
  }
  // Clear higher bits for debugging mode
#ifndef NDEBUG
  // This does not work if bits == 64 since ALU will mask off shift amount to zero
  if(bits != 64) {
    *dest &= ~(-1UL << bits);
  }
#endif
  return src_offset + bits;
}

int FPC_unpack_bits_bmi(uint64_t *dest, void *_src, int src_offset, int bits) {
  assert(FPC_BIT_COPY_GRANULARITY == 64);
  assert(bits > 0 && bits <= FPC_BIT_COPY_GRANULARITY);
  int src_word_index = src_offset / FPC_BIT_COPY_GRANULARITY;
  int src_word_offset = src_offset % FPC_BIT_COPY_GRANULARITY;
  int src_remains = FPC_BIT_COPY_GRANULARITY - src_word_offset;
  uint64_t *src = (uint64_t *)_src;
  // Extract min(bits, src_remains); if bits < src_remains this is the only step, otherwise
  // it also does not hurt we read "bits" bits; non-existing bits will be zero
  *dest = bit_extract64(src[src_word_index], src_word_offset, bits);
  int delta = bits - src_remains;
  // Still have bits to copy if delta > 0
  if(delta > 0) {
    *dest |= (bit_extract64(src[src_word_index + 1], 0, delta) << src_remains);
  }
  return src_offset + bits;
}

int FPC_comp(void *out_buf, void *_in_buf) {
  uint32_t *in_buf = (uint32_t *)_in_buf;
  int offset = 0;
  int zero_count = 0; // Track zero runs
  int state = FPC_STATE_NORMAL;
  // These two are universally used for writing the three-bit type and the data after it
  uint64_t type;
  uint64_t data;
  int iter = (int)UTIL_CACHE_LINE_SIZE / (int)sizeof(uint32_t);
  for(int i = 0;i < iter;i++) {
    uint32_t curr = in_buf[i];
    if(curr == 0) {
      if(state == FPC_STATE_ZERO_RUN) {
        zero_count++;
      } else {
        state = FPC_STATE_ZERO_RUN;
        zero_count = 1;
      }
      assert(state == FPC_STATE_ZERO_RUN);
      // Exit zero run mode when we reach a maximum of 8, or when the next word is not zero
      if(zero_count == 7 || (i == iter - 1) || (in_buf[i + 1] != 0)) {
        type = 0UL;
        data = (uint64_t)zero_count;
        offset = FPC_pack_bits_bmi(out_buf, offset, &type, 3);
        offset = FPC_pack_bits_bmi(out_buf, offset, &data, 3);
        zero_count = 0; // Not necessary but let it be for readability
        state = FPC_STATE_NORMAL;
      }
      continue;
    }
    // curr must be non-zero, test for other possibilities
    // This is common to many branches below, just assign it here; Do not need to truncate bits
    // since we know the exact number of bits to append to the output stream
    data = curr; 
    // Type 111: Repeated bytes
    uint8_t *byte_p = (uint8_t *)&curr;
    if(byte_p[0] == byte_p[1] && byte_p[1] == byte_p[2] && byte_p[2] == byte_p[3]) {
      type = 6UL;
    } else if((curr & 0xFFFFFFF8) == 0 || (curr & 0xFFFFFFF8) == 0xFFFFFFF8) {
      type = 1UL; // Sign extended 4 bits
    } else if((curr & 0xFFFFFF80) == 0 || (curr & 0xFFFFFF80) == 0xFFFFFF80) {
      type = 2UL; // Sign extended 8 bits
    } else if((curr & 0xFFFF8000) == 0 || (curr & 0xFFFF8000) == 0xFFFF8000) {
      type = 3UL; // Sign extended 16 bits 
    } else if((curr & 0x0000FFFF) == 0) {
      type = 4UL; // Lower bits are zero
      data = curr >> 16; // Data stores higher 16 bits, so we shift it by 16 bits
    } else if((curr & 0xFF80FF80) == 0 || (curr & 0xFF80FF80) == 0xFF80FF80 || 
              (curr & 0xFF80FF80) == 0xFF800000 || (curr & 0xFF80FF80) == 0x0000FF80) {
      type = 5UL; // Two 16-bit integer compressed to 8 bytes independently
      data = (curr & 0x000000FF) | ((curr & 0x00FF0000) >> 8); // ata[0:7] stores bit 0 - 7 and data[8:15] bit 16 - 23
    } else {
      type = 7UL; // Uncompressable 32 bits
    }
    // Always write 3 bit type and some data according to the lookup table
    assert(type < 8);
    offset = FPC_pack_bits_bmi(out_buf, offset, &type, 3);
    offset = FPC_pack_bits_bmi(out_buf, offset, &data, FPC_data_bits[type]);
  }
  return offset;
}

void FPC_decomp(void *_out_buf, void *in_buf) {
  uint32_t *out_buf = (uint32_t *)_out_buf;
  int iter = (int)UTIL_CACHE_LINE_SIZE / (int)sizeof(uint32_t);
  int count = 0;
  int offset = 0;
  while(count < iter) {
    uint64_t type, _data;
    uint32_t data;
    offset = FPC_unpack_bits_bmi(&type, in_buf, offset, 3);
    assert(type < 8UL);
    offset = FPC_unpack_bits_bmi(&_data, in_buf, offset, FPC_data_bits[type]);
    data = (uint32_t)_data; // Avoid type cast below
    switch(type) {
      case 0: for(int i = 0;i < (int)data;i++) out_buf[count++] = 0; break;
      // 4-bit, 8-bit and 16-bit sign extension respectively
      case 1: out_buf[count++] = (data & 0x8) ? (data | 0xFFFFFFF0) : data; break;
      case 2: out_buf[count++] = (data & 0x80) ? (data | 0xFFFFFF00) : data; break;
      case 3: out_buf[count++] = (data & 0x8000) ? (data | 0xFFFF0000) : data; break;
      case 4: out_buf[count++] = (data << 16); break;
      case 5: {
        // Two 16-bit integers compressed to 8 bytes independently
        uint32_t lo = data & 0xFF;
        uint32_t hi = (data & 0xFF00) << 8;
        if(lo & 0x80) lo |= 0xFF00;
        if(hi & 0x800000) hi |= 0xFF000000;
        out_buf[count++] = lo | hi;
      } break;
      case 6: {
        // Repeated bytes
        data &= 0xFF;
        out_buf[count++] = data | (data << 8) | (data << 16) | (data << 24);
      } break;
      case 7: out_buf[count++] = data; break; // Not compressed
      default: error_exit("Illegal FPC type: %lu (expecting [0, 7])\n", type);
    }
  }
  // Must have exactly decoded this many words
  assert(count == iter);
  return;
}

void FPC_print_packed_bits(uint64_t *buf, int begin_offset, int bits) {
  assert(bits >= 0);
  int word_index = begin_offset / UTIL_CACHE_LINE_SIZE;
  int word_offset = begin_offset % UTIL_CACHE_LINE_SIZE;
  while(bits != 0) {
    uint64_t word = buf[word_index];
    if((0x1UL << word_offset) & word) {
      printf("1 ");
    } else {
      printf("0 ");
    }
    bits--;
    word_offset++;
    if(word_offset == 64) {
      word_offset = 0;
      word_index++;
    }
  }
  return;
}

void FPC_print_compressed(void *in_buf) {
  int iter = (int)UTIL_CACHE_LINE_SIZE / (int)sizeof(uint32_t);
  int count = 0;
  int offset = 0;
  int data_bits = 0;
  int total_bits = 0;
  while(count < iter) {
    uint64_t type, _data;
    uint32_t data;
    offset = FPC_unpack_bits_bmi(&type, in_buf, offset, 3);
    assert(type < 8UL);
    offset = FPC_unpack_bits_bmi(&_data, in_buf, offset, FPC_data_bits[type]);
    data = (uint32_t)_data; // Avoid type cast below
    data_bits += FPC_data_bits[type];
    total_bits += (FPC_data_bits[type] + 3);
    printf("%d bits type %lu data %u (0x%X) ", FPC_data_bits[type], type, data, data);
    switch(type) {
      case 0: {
        printf("zero runs count %u\n", data); 
        count += data;
        continue; // We do not use count++ below
      } break;
      case 1: {
        data = (data & 0x8) ? (data | 0xFFFFFFF0) : data;
        printf("4-bit sign ext decoded %u (0x%X)\n", data, data);
      } break;
      case 2: {
        data = (data & 0x80) ? (data | 0xFFFFFF00) : data;
        printf("8-bit sign ext decoded %u (0x%X)\n", data, data);
      } break;
      case 3: {
        data = (data & 0x8000) ? (data | 0xFFFF0000) : data;
        printf("16-bit sign ext decoded %u (0x%X)\n", data, data);
      } break;
      case 4: printf("High 16 bits decoded %u (0x%X)", data << 16, data << 16); break;
      case 5: {
        uint32_t lo = data & 0xFF;
        uint32_t hi = (data & 0xFF00) << 8;
        if(lo & 0x80) lo |= 0xFF00;
        if(hi & 0x800000) hi |= 0xFF000000;
        data = hi | lo;
        printf("Two 16 bits as bytes decoded %u (0x%X)\n", data, data);
      } break;
      case 6: {
        // Repeated bytes
        data &= 0xFF;
        data = data | (data << 8) | (data << 16) | (data << 24);
        printf("Repeated bytes decoded %u (0x%X)\n", data, data);
      } break;
      case 7: printf("Uncompressed decoded %u (0x%X)\n", data, data); break;
      default: error_exit("Illegal FPC type: %lu (expecting [0, 7])\n", type);
    }
    count++;
  }
  printf("Data bits %d total bits %d (offset %d)\n", data_bits, total_bits, offset);
  assert(count == iter);
  return;
}

//* dmap_t

// This function will be called frequently, so do not init data
dmap_entry_t *dmap_entry_init() {
  dmap_entry_t *entry = (dmap_entry_t *)malloc(sizeof(dmap_entry_t));
  SYSEXPECT(entry != NULL);
  // Init all fields including data
  memset(entry, 0x00, sizeof(dmap_entry_t));
  return entry;
}

void dmap_entry_free(dmap_entry_t *entry) {
  free(entry);
  return;
}

dmap_t *dmap_init() {
  dmap_t *dmap = (dmap_t *)malloc(sizeof(dmap_t));
  SYSEXPECT(dmap != NULL);
  memset(dmap, 0x00, sizeof(dmap_t));
  // Check argument
  if(popcount_int32(DMAP_INIT_COUNT) != 1) {
    error_exit("DMAP_INIT_COUNT is illegal: %d\n", DMAP_INIT_COUNT);
  }
  dmap->default_shape = OCACHE_SHAPE_NONE;
  return dmap;
}

void dmap_free(dmap_t *dmap) {
  for(int i = 0;i < DMAP_INIT_COUNT;i++) {
    dmap_entry_t *curr = dmap->entries[i];
    while(curr != NULL) {
      dmap_entry_t *next = curr->next;
      dmap_entry_free(curr);
      curr = next;
    }
  }
  free(dmap);
  return;
}

// Note that this function does not update the head
void dmap_entry_unlink(dmap_entry_t *entry) {
  if(entry->next != NULL) {
    entry->next->prev = entry->prev;
  } 
  if(entry->prev != NULL) {
    entry->prev->next = entry->next;
  }
  return;
}

// Either return an existing element, or allocate a new entry, insert, and return the new entry
// This function should never return NULL
// Invariant: Head node of the linked list has prev always being NULL
dmap_entry_t *dmap_insert(dmap_t *dmap, uint64_t oid, uint64_t addr) {
  int index = (int)dmap_gen_index(dmap, oid, addr);
  assert(index >= 0 && index < DMAP_INIT_COUNT);
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0UL);
  int iter = 0;
  // Head of the linked list
  dmap_entry_t *curr = dmap->entries[index];
  // Keep a copy of the head
  dmap_entry_t *head = curr;
  while(curr != NULL) {
    //printf("insert iter %d index %d\n", iter, index);
    iter++;
    // Hit: Just move to front
    if(curr->oid == oid && curr->addr == addr) {
      if(curr != head) {
        // Remove curr from the linked list
        dmap_entry_unlink(curr);
        // Link curr before head at the first slot
        curr->prev = NULL;
        curr->next = head;
        head->prev = curr;
        dmap->entries[index] = curr;
      }
      //break;
      goto found;
    }
    curr = curr->next;
  }
  curr = dmap_entry_init();
  curr->next = head;   // This works for NULL head or non-NULL head
  if(head != NULL) {   // For NULL head (empty slot) should not change head->prev
    head->prev = curr;
  }
  // Initialize the new entry
  curr->oid = oid;
  curr->addr = addr;
  // Link into the head of the list
  dmap->entries[index] = curr;
  dmap->count++;
found:
  // Update query and iter stat
  dmap->iter_count += iter;
  dmap->query_count++;
  return curr;
}

// Return NULL if the combination does not exist
// This function also moves the MRU element to the head
dmap_entry_t *dmap_find(dmap_t *dmap, uint64_t oid, uint64_t addr) {
  int index = (int)dmap_gen_index(dmap, oid, addr);
  assert(index >= 0 && index < DMAP_INIT_COUNT);
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0UL);
  int iter = 0;
  dmap_entry_t *curr = dmap->entries[index];
  dmap_entry_t *head = curr;
  while(curr != NULL) {
    //printf("find   iter %d index %d\n", iter, index);
    iter++;
    if(curr->oid == oid && curr->addr == addr) {
      if(curr != head) {
        dmap_entry_unlink(curr);
        curr->prev = NULL;
        curr->next = head;
        head->prev = curr;
        dmap->entries[index] = curr;
      }
      break;
    }
    curr = curr->next;
  }
  // Update query and iter stat
  dmap->iter_count += iter;
  dmap->query_count++;
  return curr;
}

// Return a compressed line in the given buffer, and the type
// If the line could not be compressed, then we return BDI_TYPE_INVALID, and the buffer is not modified
// If the line could not be found, then we return BDI_TYPE_NOT_FOUND, and the buffer is not modified
int dmap_find_compressed(dmap_t *dmap, uint64_t oid, uint64_t addr, void *out_buf) {
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0UL);
  dmap_entry_t *entry = dmap_find(dmap, oid, addr);
  if(entry == NULL) {
    return BDI_TYPE_NOT_FOUND;
  }
  // Type can be BDI_TYPE_INVALID
  int type = BDI_comp(out_buf, entry->data);
  return type; 
}

// Either read or write; The only difference between these two is the direction of copy
// If the data item does not exist, create it first.
// Write size can be arbitrarily large, and addr may not be aligned to any boundary
// Note: addr may be unaligned
void dmap_data_op(dmap_t *dmap, uint64_t oid, uint64_t _addr, int size, void *buf, int op) {
  assert(size > 0);
  assert(op == DMAP_READ || op == DMAP_WRITE);
  uint64_t addr = _addr & (~(UTIL_CACHE_LINE_SIZE - 1)); // Aligned address
  assert(addr <= _addr);
  // This represents the current number of bytes we copy to the line (OID, addr)
  // "size" represents remianing number of bytes
  int curr_size = (int)(UTIL_CACHE_LINE_SIZE - (_addr - addr));
  assert(curr_size > 0 && curr_size <= (int)UTIL_CACHE_LINE_SIZE);
  do {
    void *data = dmap_insert(dmap, oid, addr)->data;
    // This must be before we adjust curr_size, such that the amount is always zero for later iters
    data = ptr_add(data, (int)UTIL_CACHE_LINE_SIZE - curr_size);
    if(curr_size > size) {
      curr_size = size; // Cap on actual number of bytes we have
    }
    // Copy
    if(op == DMAP_WRITE) {
      memcpy(data, buf, curr_size);
    } else {
      memcpy(buf, data, curr_size);
    }
    buf = ptr_add(buf, curr_size);
    addr += UTIL_CACHE_LINE_SIZE;
    // Already copied that many bytes
    size -= curr_size;
    // Next line
    curr_size = UTIL_CACHE_LINE_SIZE;
  } while(size > 0);
  assert(size == 0);
  return;
}

// Page info is stored in aligned address and -1 OID
// This function will use default shape if the page does not exist
// The page address must be page aligned for simplicity
pmap_entry_t *pmap_insert(pmap_t *pmap, uint64_t addr) {
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0UL);
  // Mask off lower bits
  addr = UTIL_GET_PAGE_ADDR(addr);
  //printf("Insert 0x%lX\n", addr);
  // Most queries should return non-NULL for this
  pmap_entry_t *entry = dmap_find(pmap, PMAP_OID, addr);
  if(entry != NULL) {
    return entry;
  }
  //printf("Insert 0x%lX\n", addr);
  // Initialize an entry by setting its shape
  entry = dmap_insert(pmap, PMAP_OID, addr);
  entry->shape = pmap->default_shape;
  // Increment it here such that we can compute space consumed by data and page info separately
  pmap->pmap_count++;
  return entry;
}

// Insert the range [addr, addr + size) into the pmap
// addr and size need not be page aligned
void pmap_insert_range(pmap_t *pmap, uint64_t addr, int size, int shape) {
  assert(size > 0);
  assert(shape >= OCACHE_SHAPE_BEGIN && shape < OCACHE_SHAPE_END);
  uint64_t start_addr = addr & ~(UTIL_PAGE_SIZE - 1);
  uint64_t end_addr = (addr + size - 1) & ~(UTIL_PAGE_SIZE - 1);
  int count = (end_addr - start_addr) / UTIL_PAGE_SIZE + 1;
  assert(count > 0);
  for(int i = 0;i < count;i++) {
    pmap_entry_t *entry = pmap_insert(pmap, start_addr);
    entry->shape = shape;
    start_addr += UTIL_PAGE_SIZE;
  }
  return;
}

void dmap_print(dmap_t *dmap) {
  for(int i = 0;i < DMAP_INIT_COUNT;i++) {
    dmap_entry_t *entry = dmap->entries[i];
    while(entry != NULL) {
      if(entry->oid != PMAP_OID) {
        printf("OID 0x%lX addr 0x%lX data ", entry->oid, entry->addr);
        for(int i = 0;i < 8;i++) {
          printf("%lX ", ((uint64_t *)entry->data)[i]);
        }
        putchar('\n');
      }
      entry = entry->next;
    }
  }
  return;
}

void pmap_print(pmap_t *pmap) {
  for(int i = 0;i < DMAP_INIT_COUNT;i++) {
    pmap_entry_t *entry = pmap->entries[i];
    while(entry != NULL) {
      if(entry->oid == PMAP_OID) {
        printf("OID 0x%lX addr 0x%lX shape %d\n", entry->oid, entry->addr, entry->shape);
      }
      entry = entry->next;
    }
  }
  return;
}

void dmap_conf_print(dmap_t *dmap) {
  printf("---------- dmap_t conf ----------\n");
  printf("init size %d\n", DMAP_INIT_COUNT);
  (void)dmap;
  return;
}

void dmap_stat_print(dmap_t *dmap) {
  printf("---------- dmap_t stat ----------\n");
  printf("Count %d pmap count %d data size %d\n", dmap->count, dmap->pmap_count, 
    dmap_get_count(dmap) * (int)UTIL_CACHE_LINE_SIZE);
  printf("queries %lu iters %lu (avg. %f)\n", 
    dmap->query_count, dmap->iter_count, (double)dmap->iter_count / dmap->query_count);
  return;
}

//* ocache_t

const char *ocache_shape_names[4] = {
  "None", "4 * 1", "1 * 4", "2 * 2",
};

ocache_t *ocache_init(int size, int way_count) {
  // First allocate the cache control part, and then slots using realloc
  ocache_t *ocache = (ocache_t *)malloc(sizeof(ocache_t));
  SYSEXPECT(ocache != NULL);
  memset(ocache, 0x00, sizeof(ocache_t));
  // Report error if args are invalid; Call this before using arguments
  ocache_init_param(ocache, size, way_count);
  int alloc_size = sizeof(ocache_t) + sizeof(ocache_entry_t) * ocache->line_count;
  // The control part is preserved
  ocache = (ocache_t *)realloc(ocache, alloc_size);
  SYSEXPECT(ocache != NULL);
  // All LRU and state bits will be set to zero
  memset(ocache->data, 0x00, sizeof(ocache_entry_t) * ocache->line_count);
  for(int i = 0;i < ocache->line_count;i++) {
    ocache_entry_inv(&ocache->data[i]);
  }
  ocache->level = OCACHE_LEVEL_NONE;
  // LRU entry has this zero, so normal entry should always have one
  ocache->lru_counter = 1;
  return ocache;
}

void ocache_free(ocache_t *ocache) {
  if(ocache->name != NULL) {
    free(ocache->name);
  }
  free(ocache);
  return;
}

// This is how we specify a cache: giving its total size in bytes, and number of ways
// other params are automatically derived
// Latency is configured elsewhere
// This function sets:
//   size, line_count, way_count, set_count, size_bits, indices[]
void ocache_init_param(ocache_t *ocache, int size, int way_count) {
  if(size <= 0 || way_count < 1) {
    error_exit("Illegal size/way combination: %d/%d\n", size, way_count);
  }
  ocache->size = size;
  ocache->way_count = way_count;
  if(size % UTIL_CACHE_LINE_SIZE != 0) {
    error_exit("Size must be a multiple of %d (see %lu)\n", size, UTIL_CACHE_LINE_SIZE);
  }
  ocache->line_count = size / UTIL_CACHE_LINE_SIZE;
  if(ocache->line_count % ocache->way_count != 0) {
    error_exit("Cache line count must be a multiple of ways (%d %% %d == 0)\n",
      ocache->line_count, ocache->way_count);
  }
  ocache->set_count = ocache->line_count / ocache->way_count;
  if(popcount_int32(ocache->set_count) != 1) {
    error_exit("The number of sets must be a power of two (see %d)\n", ocache->set_count);
  }
  // Set bits is just popcount of (set_count - 1)
  ocache->set_bits = popcount_int32(ocache->set_count - 1);
  // Set masks and shifts for both addr and OID
  // cast to int64_t for sign extension, and then to uint64_t for type compliance
  ocache_index_t *curr_index = &ocache->indices[OCACHE_SHAPE_NONE];
  curr_index->addr_mask = ((uint64_t)(int64_t)(ocache->set_count - 1)) << UTIL_CACHE_LINE_BITS;
  curr_index->addr_shift = UTIL_CACHE_LINE_BITS;
  // OID does not shift, and always mask from lower bits
  curr_index->oid_mask = (uint64_t)(int64_t)(ocache->set_count - 1);
  curr_index->oid_shift = 0;
  // For 4 * 1 super blocks, ignore two lower bits of OID
  curr_index = &ocache->indices[OCACHE_SHAPE_4_1];
  curr_index->addr_mask = ocache->indices[OCACHE_SHAPE_NONE].addr_mask;
  curr_index->addr_shift = ocache->indices[OCACHE_SHAPE_NONE].addr_shift;
  curr_index->oid_mask = (ocache->indices[OCACHE_SHAPE_NONE].oid_mask << 2);
  curr_index->oid_shift = 2;
  // For 1 * 4 super blocks, ignore two lower bits of addr
  curr_index = &ocache->indices[OCACHE_SHAPE_1_4];
  curr_index->addr_mask = (ocache->indices[OCACHE_SHAPE_NONE].addr_mask << 2);
  curr_index->addr_shift = ocache->indices[OCACHE_SHAPE_NONE].addr_shift + 2;
  curr_index->oid_mask = ocache->indices[OCACHE_SHAPE_NONE].oid_mask;
  curr_index->oid_shift = ocache->indices[OCACHE_SHAPE_NONE].oid_shift;
  // For 2 * 2 super blocks, ignore lower one bit of both addr and OID
  curr_index = &ocache->indices[OCACHE_SHAPE_2_2];
  curr_index->addr_mask = (ocache->indices[OCACHE_SHAPE_NONE].addr_mask << 1);
  curr_index->addr_shift = ocache->indices[OCACHE_SHAPE_NONE].addr_shift + 1;
  curr_index->oid_mask = (ocache->indices[OCACHE_SHAPE_NONE].oid_mask << 1);
  curr_index->oid_shift = 1;
  return;
}

void ocache_set_name(ocache_t *ocache, const char *name) {
  int len = strlen(name);
  if(ocache->name != NULL) {
    error_exit("Cache \"%s\" has already been named\n", ocache->name);
  }
  ocache->name = (char *)malloc(len + 1);
  SYSEXPECT(ocache->name != NULL);
  strcpy(ocache->name, name);
  return;
}

void ocache_set_level(ocache_t *ocache, int level) {
  if(ocache->level != OCACHE_LEVEL_NONE) {
    error_exit("Cache level has been set (%d)\n", ocache->level);
  }
  ocache->level = level;
  return;
}

// This must be called before shape can be used
void ocache_set_dmap(ocache_t *ocache, dmap_t *dmap) {
  ocache->use_shape = 1;
  ocache->dmap = dmap;
  return;
}

uint64_t ocache_gen_addr(ocache_t *ocache, uint64_t tag, uint64_t set_id, int shape) {
  set_id &= ((0x1UL << ocache->set_bits) - 1);
  int shift_bits = UTIL_CACHE_LINE_BITS;
  switch(shape) {
    case OCACHE_SHAPE_2_2: shift_bits += 1; break;
    case OCACHE_SHAPE_1_4: shift_bits += 2; break;
  }
  return (tag << (shift_bits + ocache->set_bits)) | (set_id << shift_bits);
}

// This function generates an address that is guaranteed to be mapped to the given set_id, with a given tag
// and shape. This function takes OID into consideration, ensuring that when the OID and generated addr,
// if used together to generate the set index, will give a result that matches set_id.
uint64_t ocache_gen_addr_with_oid(ocache_t *ocache, uint64_t oid, uint64_t tag, uint64_t set_id, int shape) {
  switch(shape) {
    case OCACHE_SHAPE_4_1: oid >>= 2; break;
    case OCACHE_SHAPE_2_2: oid >>= 1; break;
  }
  set_id &= (uint64_t)(ocache->set_count - 1);
  // Mask off higher bits to have OID only affect set bits, but not tag bits
  oid &= (uint64_t)(ocache->set_count - 1);
  uint64_t middle_bits = (oid ^ set_id);
  // Use middle bits as the "set_id"
  return ocache_gen_addr(ocache, tag, middle_bits, shape);
}

// Generates addresses within super blocks given the shape and index
void ocache_gen_addr_in_sb(ocache_t *ocache, uint64_t base_oid, uint64_t base_addr, int index, int shape,
  uint64_t *oid_p, uint64_t *addr_p) {
  assert(index >= 0 && index < 4);
  assert(shape >= OCACHE_SHAPE_BEGIN && shape < OCACHE_SHAPE_END);
  assert((base_addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0);
  (void)ocache;
  // If shape is None then index must only be zero
  assert(shape != OCACHE_SHAPE_NONE || index == 0);
  switch(shape) {
    case OCACHE_SHAPE_4_1: {
      assert(base_oid % 4 == 0);
      *oid_p = base_oid + index;
      *addr_p = base_addr;
    } break;
    case OCACHE_SHAPE_1_4: {
      assert(base_addr % (4 * UTIL_CACHE_LINE_SIZE) == 0);
      *oid_p = base_oid;
      *addr_p = base_addr + UTIL_CACHE_LINE_SIZE * index;
    } break;
    case OCACHE_SHAPE_2_2: {
      assert(base_oid % 2 == 0);
      assert(base_addr % (2 * UTIL_CACHE_LINE_SIZE) == 0);
      *oid_p = base_oid + (index % 2);
      *addr_p = base_addr + UTIL_CACHE_LINE_SIZE * (index / 2);
    } break;
    default: {
      *oid_p = base_oid;
      *addr_p = base_addr;
    } break;
  }
  return;
}

// Generate an address in the format used in the cache (tag + set ID + lower bits as zero)
// If set_id exceeds the set count we just mask off higher bits
uint64_t ocache_gen_addr_no_shape(ocache_t *ocache, uint64_t tag, uint64_t set_id) {
  set_id &= ((0x1UL << ocache->set_bits) - 1); // Mask off higher bits
  return (tag << (UTIL_CACHE_LINE_BITS + ocache->set_bits)) | (set_id << UTIL_CACHE_LINE_BITS);
}

// Generate page address; Page page and line will be truncated if they are too large to be represented
uint64_t ocache_gen_addr_page(ocache_t *ocache, uint64_t page, uint64_t line) {
  (void)ocache;
  line &= (UTIL_LINE_PER_PAGE - 1); // Truncate higher bits
  return (page << UTIL_PAGE_BITS) | (line << UTIL_CACHE_LINE_BITS);
}

// The alignment must be a power of two, and the address will be adjusted to that boundary
// Note that in order to align a cache line, we should ignore lower bits and only start
// from the middle bits
uint64_t ocache_align_addr(uint64_t addr, int alignment) {
  if(popcount_int32(alignment) != 1) {
    error_exit("The alignment must be a power of two (see %d)\n", alignment);
  }
  uint64_t mask = (alignment - 1) << UTIL_CACHE_LINE_BITS;
  return addr & ~mask;
}

uint64_t ocache_align_oid(uint64_t oid, int alignment) {
  if(popcount_int32(alignment) != 1) {
    error_exit("The alignment must be a power of two (see %d)\n", alignment);
  }
  uint64_t mask = (alignment - 1);
  return oid & ~mask;
}

// This function implementes the index generation circuit. Shape is used for determining the 
// function parameters
// Apply mask first, and then shift (lower bits of the mask for bits that will be shift out are zero)
// Called for both use_shape == 0 and == 1
int ocache_get_set_index(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape) {
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0UL);
  assert(ocache->use_shape != 0 || shape == OCACHE_SHAPE_NONE);
  assert(shape >= OCACHE_SHAPE_BEGIN && shape < OCACHE_SHAPE_COUNT);
  ocache_index_t *index = ocache->indices + shape;
  //printf("Addr input 0x%lX\n", addr);
  addr = (addr & index->addr_mask) >> index->addr_shift;
  oid = (oid & index->oid_mask) >> index->oid_shift;
  //printf("Addr after mask shift 0x%lX\n", addr);
  // XOR them together to make sure all bits are used
  return (int)(addr ^ oid);
}

// Returns a pointer to the first entry in the current set
// This function is used for both use_shape == 0 or == 1
ocache_entry_t *ocache_get_set_begin(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape) {
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0UL);
  int index = ocache_get_set_index(ocache, oid, addr, shape);
  assert(index >= 0 && index < ocache->set_count);
  ocache_entry_t *entry = ocache->data + ocache->way_count * index;
  assert(entry >= ocache->data && entry < ocache->data + ocache->line_count);
  return entry;
}

// Computes the index of the given OID/Addr pair in the current entry
// Returns -1 if the combination is not in the super block
// This function requires that the entry must be a sb
int ocache_get_sb_index(ocache_t *ocache, ocache_entry_t *entry, uint64_t oid, uint64_t addr) {
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0UL);
  assert(entry->shape != OCACHE_SHAPE_NONE);
  int hit_index = -1;
  // Expand shapes based on shape type and valid bits
  switch(entry->shape) {
    case OCACHE_SHAPE_4_1: {
      assert(entry->oid % 4 == 0);
      if(addr == entry->addr && oid >= entry->oid && oid < entry->oid + 4) {
        hit_index = oid - entry->oid;
      }
    } break;
    case OCACHE_SHAPE_1_4: {
      assert((entry->addr >> UTIL_CACHE_LINE_BITS) % 4 == 0);
      if(oid == entry->oid && addr >= entry->addr && addr < entry->addr + (4 * UTIL_CACHE_LINE_SIZE)) {
        hit_index = (addr - entry->addr) / UTIL_CACHE_LINE_SIZE;
      }
    } break;
    case OCACHE_SHAPE_2_2: {
      assert(entry->oid % 2 == 0);
      assert((entry->addr >> UTIL_CACHE_LINE_BITS) % 2 == 0);
      if(addr >= entry->addr && addr < entry->addr + (2 * UTIL_CACHE_LINE_SIZE) && 
          oid >= entry->oid && oid < entry->oid + 2) {
        // OID first, then line address, so let OID offset be the lowest bit
        hit_index = (((int)(addr - entry->addr) / (int)UTIL_CACHE_LINE_SIZE) << 1) | (oid - entry->oid);
      }
    } break;
    default: {
      printf("entry ptr %p offset (index) %d OID 0x%lX addr 0x%lX set %d\n", 
        entry, (int)(entry - ocache->data), oid, addr, 
        (int)(entry - ocache->data) / ocache->way_count);
      assert(0);
      error_exit("Invalid super block shape: %d\n", entry->shape);
    } break;
  }
  return hit_index;
}

// Given oid/addr and the shape, return the base OID and addr that should be used as
// sb tags and also the index of the block
// Note: addr is cache aligned byte address
void ocache_get_sb_tag(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape,
  uint64_t *oid_base, uint64_t *addr_base, int *index) {
  (void)ocache;
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0UL);
  assert(shape != OCACHE_SHAPE_NONE);
  addr >>= UTIL_CACHE_LINE_BITS; // Shift out offset bits to make it cache line index
  switch(shape) {
    case OCACHE_SHAPE_4_1: {
      *addr_base = addr;
      *oid_base = oid & ~0x3UL; // Align to the lowest multiple of four
      *index = (int)(oid & 0x3UL); // Lower 2 bits represent the offset
    } break;
    case OCACHE_SHAPE_1_4: {
      *oid_base = oid;
      *addr_base = addr & ~0x3UL;
      *index = (int)(addr & 0x3UL);
    } break;
    case OCACHE_SHAPE_2_2: {
      // Round down to multiple of 2
      *oid_base = oid & ~0x1UL;
      *addr_base = addr & ~0x1UL;
      *index = (int)((oid & 0x1UL) + ((addr & 0x1UL) << 1));
    } break;
  }
  assert(*index >= 0 && *index < 4);
  // Restore offset bits from the cache line index
  *addr_base <<= UTIL_CACHE_LINE_BITS;
  return;
}

// Returns the index of the vertical shape given an ocache entry and an entry
// The vertical is the lowest OID on the same address
// Return value:
//   0  if 4 * 1
//   -1 if 1 * 4
//   0, 1 -> 0; 2, 3 -> 2
int ocache_get_vertical_base_index(ocache_t *ocache, ocache_entry_t *entry, int index) {
  assert(entry->shape != OCACHE_SHAPE_NONE);
  assert(index >= 0 && index < 4);
  (void)ocache;
  switch(entry->shape) {
    case OCACHE_SHAPE_4_1: return 0;
    case OCACHE_SHAPE_1_4: return -1;
    case OCACHE_SHAPE_2_2: return index & (~0x1); // Mask off lowest bit
  }
  return -1;
}

// Given oid, return the oid of the base cache line
// If base oid equals input OID, the line itself is a base cache line
uint64_t ocache_get_vertical_base_oid(ocache_t *ocache, uint64_t oid, int shape) {
  assert(shape != OCACHE_SHAPE_NONE);
  assert(shape >= OCACHE_SHAPE_BEGIN && shape < OCACHE_SHAPE_END);
  (void)ocache;
  uint64_t ret = 0UL;
  switch(shape) {
    case OCACHE_SHAPE_4_1: ret = oid & (~0x3UL); break;
    case OCACHE_SHAPE_1_4: ret = oid; break;
    case OCACHE_SHAPE_2_2: ret = oid & (~0x1UL); break;
  }
  return ret;
}

// The read version is simpler than write in the following aspects:
//   1. SB candidates are not computed
//   2. LRU is not computed, but still updated
//   3. Once a hit is found, return immediately
// Only state, hit entry and hit index in the result are updated
void ocache_lookup_read(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, ocache_lookup_result_t *result) {
  assert(shape >= OCACHE_SHAPE_BEGIN && shape < OCACHE_SHAPE_END);
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0);
  assert(ocache->use_shape == 1);
  result->state = OCACHE_LOOKUP_MISS;
  result->hit_entry = NULL;
  ocache_entry_t *entry = ocache_get_set_begin(ocache, oid, addr, shape);
  for(int i = 0;i < ocache->way_count;i++) {
    if(entry->shape == OCACHE_SHAPE_NONE) {
      // Case 1: Normal hit on an uncompressed line
      if(entry->oid == oid && entry->addr == addr && ocache_entry_is_valid(entry, 0)) {
        // At most one hit per lookup, as a design invariant
        assert(result->state == OCACHE_LOOKUP_MISS);
        result->state = OCACHE_LOOKUP_HIT_NORMAL;
        result->hit_entry = entry;
        result->hit_index = 0;
        break;
      }
    } else if(ocache_entry_is_all_invalid(entry) == 0) { // Only for valid tags
      int hit_index = ocache_get_sb_index(ocache, entry, oid, addr);
      if(hit_index != -1) {
        assert(hit_index >= 0 && hit_index < 4);
        if(ocache_entry_is_valid(entry, hit_index)) {
          // Case 2: Hit compressed super block and the line is valid
          // Can only be at most one hit
          assert(result->state == OCACHE_LOOKUP_MISS);
          result->state = OCACHE_LOOKUP_HIT_COMPRESSED;
          result->hit_entry = entry;
          result->hit_index = hit_index;
          break;
        }
      }
    }
    entry++;
  }
  if(result->state != OCACHE_LOOKUP_MISS) {
    ocache_update_entry_lru(ocache, entry);
  }
  return;
}

// This function performs tag lookups for writes. Writes require more information than reads due to
// multiple possible locations a logical line can be stored in. This function always scans the entire 
// tag array and enumarates possible sb candidates. Hits and misses are the same as in read version.
// The "result" parameter stores output value. It does not need to be pre-initialized
// Note
//   1. LRU counter of lines are not updated
//   2. LRU line is always returned for all possible cases
//   3. This function always scans the entire set
void ocache_lookup_write(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, ocache_lookup_result_t *result) {
  assert(shape >= OCACHE_SHAPE_BEGIN && shape < OCACHE_SHAPE_END);
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0); // Address must be aligned
  assert(ocache->use_shape == 1);
  // The candidate array is filled even if the result is hit
  result->count = 0;
  result->state = OCACHE_LOOKUP_MISS;
  result->hit_entry = NULL;
  result->lru_entry = NULL;
  // These two tracks the LRU in the current set
  uint64_t lru_min = -1UL;
  ocache_entry_t *lru_entry = NULL;
  ocache_entry_t *entry = ocache_get_set_begin(ocache, oid, addr, shape);
  for(int i = 0;i < ocache->way_count;i++) {
    if(entry->shape == OCACHE_SHAPE_NONE) {
      // Case 1: Normal hit on an uncompressed line
      if(entry->oid == oid && entry->addr == addr && ocache_entry_is_valid(entry, 0)) {
        // At most one hit per lookup, as a design invariant
        assert(result->state == OCACHE_LOOKUP_MISS);
        result->state = OCACHE_LOOKUP_HIT_NORMAL;
        result->hit_entry = entry;
        result->hit_index = 0;
      }
    } else if(ocache_entry_is_all_invalid(entry) == 0) { // Only do this for valid tags
      // Computes the index of the entry, if it is in the current sb. -1 means not in the sb
      // Valid bits will not be checked
      int hit_index = ocache_get_sb_index(ocache, entry, oid, addr);
      // -1 means the (OID, addr) combination is not in the super block
      if(hit_index != -1) {
        assert(hit_index >= 0 && hit_index < 4);
        if(ocache_entry_is_valid(entry, hit_index)) {
          // Case 2: Hit compressed super block and the line is valid
          // Can only be at most one hit
          assert(result->state == OCACHE_LOOKUP_MISS);
          result->state = OCACHE_LOOKUP_HIT_COMPRESSED;
          result->hit_entry = entry;
          result->hit_index = hit_index;
        }
        // Case 2 and Case 3: In both cases we push the entry and hit index as a potential candidate
        result->candidates[result->count] = entry;
        result->indices[result->count] = hit_index;
        result->count++;
        assert(result->count <= 4);
      }
    }
    // Update LRU information (LRU entry can be a candidate)
    if(entry->lru < lru_min) {
      lru_min = entry->lru;
      lru_entry = entry;
    }
    entry++;
  }
  // Always return valid LRU entry of the set, even on compressed hits, since it can be that none of these
  // compressed SB will fit the block, so we will evict the one, which is the LRU
  result->lru_entry = lru_entry;
  return;
}

// Helper function called by ocache_insert for better readability
static void ocache_insert_helper_uncompressed(
  ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, int dirty, 
  ocache_insert_result_t *insert_result, ocache_lookup_result_t *lookup_result) {
  (void)shape; // Maintain consistency of interface
  int state = lookup_result->state;
  assert(state == OCACHE_LOOKUP_HIT_NORMAL || state == OCACHE_LOOKUP_MISS);
  // This is the entry that needs to be updated after the insert
  ocache_entry_t *entry = NULL;
  if(lookup_result->state == OCACHE_LOOKUP_HIT_NORMAL) {
    // Case 2: Hit uncompressed line
    assert(lookup_result->hit_entry != NULL);
    assert(lookup_result->hit_entry->shape == OCACHE_SHAPE_NONE);
    assert(lookup_result->count == 0);
    entry = lookup_result->hit_entry;
    insert_result->state = OCACHE_INSERT_SUCCESS; // Do not need eviction
  } else {
    // Case 1.1.1 Miss uncompressed line (evicted line may still be compressed)
    ocache_entry_t *lru_entry = lookup_result->lru_entry;
    assert(lru_entry != NULL);
    // Note that we must use this function to determine whether it is an invalid entry
    if(ocache_entry_is_all_invalid(lru_entry) == 1) {
      assert(lru_entry->lru == 0);
      insert_result->state = OCACHE_INSERT_SUCCESS; // Invalid entry miss
    } else {
      assert(lru_entry->lru > 0);
      insert_result->state = OCACHE_INSERT_EVICT;   // Eviction
      // Copy LRU entry to the insert result
      ocache_entry_copy(&insert_result->evict_entry, lru_entry);
    }
    // Must clear all states since the slot might contain a compressed line
    // This will also modify shape, oid and addr
    ocache_entry_inv(lru_entry);
    lru_entry->oid = oid;
    lru_entry->addr = addr;
    lru_entry->shape = OCACHE_SHAPE_NONE;
    ocache_entry_set_valid(lru_entry, 0);
    ocache_entry_set_size(lru_entry, 0, UTIL_CACHE_LINE_SIZE);
    entry = lru_entry;
  }
  assert(entry != NULL);
  // Common to hits and misses, but must use the common entry variable
  ocache_update_entry_lru(ocache, entry);
  if(dirty == 1) {
    ocache_entry_set_dirty(entry, 0);
  }
  return;
}

// This function implements vertical compression policy
// The cache line can be processed with one of the following policies:
//   1. Not compressed at all
//   2. Only compressed with BDI
//   3. Compressed with BDI + vertical
//   4. Compressed with raw vertical
// Not all arguments will be used by this argument
// This function updates five statistics counters
int ocache_insert_helper_get_compressed_size(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape) {
  // Total number of insertions
  ocache->vertical_attempt_count++;
  uint8_t in_buf[UTIL_CACHE_LINE_SIZE];
  // The data must already exist in data map
  int in_type = dmap_find_compressed(ocache->dmap, oid, addr, in_buf);
  assert(in_type != BDI_TYPE_NOT_FOUND);
  // If the line could not be BDI compressed, then we do not do anything
  // This could be changed by doing delta encoding with the base
  if(in_type == BDI_TYPE_INVALID) {
    //printf("Uncompressable\n");
    return UTIL_CACHE_LINE_SIZE;
  }
  //
  // Past this point, the line is either compressed with BDI, or compressed with BDI + vertical
  // So we can increment BDI success count here
  //
  assert(in_type >= BDI_TYPE_BEGIN && in_type < BDI_TYPE_END);
  BDI_param_t *param = BDI_types[in_type];
  ocache->BDI_success_count++;
  // Total inserted size, logical
  ocache->BDI_before_size_sum += UTIL_CACHE_LINE_SIZE;
  // Size after BDI compression
  ocache->BDI_after_size_sum += param->compressed_size;
  ocache->vertical_in_compressed_count++;
  assert(param != NULL);
  // This is the BDI compressed base cache line for vertical compression
  uint8_t base_buf[UTIL_CACHE_LINE_SIZE];
  uint64_t base_oid = ocache_get_vertical_base_oid(ocache, oid, shape);
  // If this happens then the line itself is a base cache line
  if(base_oid == oid) {
    //printf("Is base\n");
    return param->compressed_size;
  }
  ocache->vertical_not_base_count++;
  int base_type = dmap_find_compressed(ocache->dmap, base_oid, addr, base_buf);
  // If base compression type is different from the type of the new line just use normal compression
  if(base_type == BDI_TYPE_NOT_FOUND) {
    //printf("No base\n");
    return param->compressed_size;
  }
  // At this line the base type can be invalid or valid, but must be found
  assert((base_type == BDI_TYPE_INVALID) || (base_type >= BDI_TYPE_BEGIN && base_type < BDI_TYPE_END));
  ocache->vertical_base_found_count++;
  // This covers invalid base type case
  if(base_type != in_type) {
    //printf("Type mismatch\n");
    return param->compressed_size;
  }
  ocache->vertical_same_type_count++;
  // Otherwise, perform vertical compression
  int vertical_size = BDI_vertical_comp(base_buf, in_buf, in_type);
  // Only use vertical compression when the size is smaller
  if(vertical_size >= param->compressed_size) {
    //printf("Vertical size larger\n");
    return param->compressed_size;
  }
  // Update vertical compression before and after size (effectiveness of vertical comp)
  ocache->vertical_before_size_sum += param->compressed_size;
  ocache->vertical_after_size_sum += vertical_size;
  ocache->vertical_success_count++;
  //printf("Success\n");
  return vertical_size;
}

// Helper function called by ocache_insert() 
// This function evicts the LRU entry, and make it a super block
// Note: This function may also be called by other helper functions to evict an existing line
// before adding the current line
static void ocache_insert_helper_compressed_miss_no_candid(
  ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, int dirty, int compressed_size,
  ocache_insert_result_t *insert_result, ocache_lookup_result_t *lookup_result) {
  // Called for both miss and write relocation
  assert(lookup_result->state == OCACHE_LOOKUP_MISS || lookup_result->state == OCACHE_LOOKUP_HIT_COMPRESSED);
  assert(lookup_result->lru_entry != NULL);
  // This is the entry we rewrite
  ocache_entry_t *entry = lookup_result->lru_entry;
  // If the entry is itself invalid, then we just return success
  if(ocache_entry_is_all_invalid(entry) == 1) {
    assert(entry->lru == 0);
    insert_result->state = OCACHE_INSERT_SUCCESS;
  } else {
    assert(entry->lru > 0);
    insert_result->state = OCACHE_INSERT_EVICT;
    // First copy the evicted entry to the insert result
    ocache_entry_copy(&insert_result->evict_entry, entry);
  }
  // Clear everything
  ocache_entry_inv(entry);
  int index = 0;
  // This will set entry->oid and entry->addr according to the shape
  ocache_get_sb_tag(ocache, oid, addr, shape, &entry->oid, &entry->addr, &index);
  entry->shape = shape;
  // Set valid bit
  ocache_entry_set_valid(entry, index);
  // Set compressed size field
  ocache_entry_set_size(entry, index, compressed_size);
  // Update dirty bit and LRU
  ocache_update_entry_lru(ocache, entry);
  if(dirty == 1) {
    ocache_entry_set_dirty(entry, index);
  }
  return;
}

// Called to place the new line into one of the candidates (assuming there was no previous hit)
// This function will also try vertical compression
static void ocache_insert_helper_compressed_miss_with_candid(
  ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, int dirty, int compressed_size,
  ocache_insert_result_t *insert_result, ocache_lookup_result_t *lookup_result) {
  // Could also be hit compressed since this function is also called for write relocation
  assert(lookup_result->state == OCACHE_LOOKUP_MISS || lookup_result->state == OCACHE_LOOKUP_HIT_COMPRESSED);
  //assert(lookup_result->hit_entry == NULL); // May not be always true for write relocation
  // Scan candidates
  for(int i = 0;i < lookup_result->count;i++) {
    // The entry and index if it were to be put to that slot
    ocache_entry_t *candidate = lookup_result->candidates[i];
    // Must be of the same shape
    assert(candidate->shape == shape);
    int index = lookup_result->indices[i];
    // The index in the super block must indicate invalid with size zero, since this is a miss
    assert(index >= 0 && index < 4);
    assert(ocache_entry_get_size(candidate, index) == 0);
    assert(ocache_entry_is_valid(candidate, index) == 0);
    // Case 1.2.1:
    // If could fit, then set valid bit, size
    if(ocache_entry_is_fit(candidate, compressed_size) == 1) {
      ocache_entry_set_valid(candidate, index);
      // Set dirty if needed
      if(dirty == 1) {
        ocache_entry_set_dirty(candidate, index);
      }
      ocache_entry_set_size(candidate, index, compressed_size);
      ocache_update_entry_lru(ocache, candidate);
      insert_result->state = OCACHE_INSERT_SUCCESS;
      return;
    }
  }
  // Case 1.2.2:
  // When control reaches here, none of the candidate can accommodate the line, so just evict the LRU
  ocache_insert_helper_compressed_miss_no_candid(
    ocache, oid, addr, shape, dirty, compressed_size, insert_result, lookup_result
  );
  return;
}

// This function assumes that clean lines will not be written back from upper levels only to hit a dirty line
// The caller should ensure this assumption
static void ocache_insert_helper_compressed_hit(
  ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, int dirty, int compressed_size,
  ocache_insert_result_t *insert_result, ocache_lookup_result_t *lookup_result) {
  assert(lookup_result->state == OCACHE_LOOKUP_HIT_COMPRESSED);
  assert(lookup_result->hit_entry != NULL);
  assert(lookup_result->hit_index >= 0 && lookup_result->hit_index < 4);
  ocache_entry_t *hit_entry = lookup_result->hit_entry;
  int hit_index = lookup_result->hit_index;
  // Don't do this - this complicates things a lot. Just rely on coherence controller to remove clean wb
  // X Ignore clean write back if the line hits the cache
  // X This will definitely be an insert from the upper level
  //if(ocache_entry_is_dirty(hit_entry, hit_index) == 1 && dirty == 0) {
    // Still update LRU
  //  ocache_update_entry_lru(ocache, hit_entry);
  //  insert_result->state = OCACHE_INSERT_SUCCESS;
  //  return;
  //}
  // If the entry is already dirty, then the result will be dirty either
  if(ocache_entry_is_dirty(hit_entry, hit_index) == 1) {
    dirty = 1;
  }
  // Invalidate the line first - need to keep dirty bits and OR to the incoming dirty bit
  // Note that we do not need to worry the case where this is never set back - if the hit_entry
  // only contains the current line, then the line will always be hit
  // Otherwise, there must be another line, making it impossible to be all invalid after this.
  ocache_entry_inv_index(hit_entry, hit_index);
  //if(ocache_entry_is_all_invalid(hit_entry) == 1) { <--- unnecessary, see above
  //  ocache_entry_inv(hit_entry);
  //}
  // Case 3.1
  if(ocache_entry_is_fit(hit_entry, compressed_size) == 1) {
    ocache_entry_set_valid(hit_entry, hit_index);
    if(dirty == 1) {
      ocache_entry_set_dirty(hit_entry, hit_index);
    }
    ocache_entry_set_size(hit_entry, hit_index, compressed_size);
    ocache_update_entry_lru(ocache, hit_entry);
    insert_result->state = OCACHE_INSERT_SUCCESS;
    return;
  }
  // Case 3.2
  // Complete the insert by either finding another candidate, or eviction
  ocache_insert_helper_compressed_miss_with_candid(
    ocache, oid, addr, shape, dirty, compressed_size, insert_result, lookup_result
  );
  return;
}

// Inserts a compressed line into the cache. TODO: This function also generates the timing
// Logic of this function
// -----------------------------------------------------------------------------
// Perform a lookup for write, which returns:
//   state, hit_entry (shape/valid included), hit_index, LRU entry, candidates
// - Always compress the line is shape argument is not none
// - Do not store compressed line into dmap
// 1. If miss
//   1.1 If there is no candidate, then evict LRU entry, and write the line. Set oid, addr, shape, drity, valid.
//     - This case can be both no shape and has shape
//     1.1.1 If shape argument is none, set new entry shape to none
//     1.1.2 Otherwise, set new entry shape to the shape argument
//   1.2 If there is candidate, then select the super block with vertical compression
//     - Must have a shape
//     - Try compressing the line using vertical compression
//     1.2.1 If a candidate can be found after the compression, then use that candidate
//     1.2.2 Otherwise, do 1.1.2
//     Set both the size array and total size. The line must not already exist in the LLC.
// The following always assumes a hit
// 2. If shape is none, meaning not compressed, insert as a regular line
//    - State must be hit normal
//    - Current entry must be an uncompressed line either (shape change must flush, so cannot be a different shape).
// 3. If shape is not none, meaning 2D compressed super block
//   - State must be hit compressed
//   - Compress the line using BDI
//   3.1 If still fit after compression, use the current hit super block
//   3.2 If not fit, do 1.2
// -----------------------------------------------------------------------------
void ocache_insert(ocache_t *ocache, uint64_t oid, uint64_t addr, 
                   int shape, int dirty, ocache_insert_result_t *insert_result) {
  assert(shape >= OCACHE_SHAPE_BEGIN && shape < OCACHE_SHAPE_END);
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0);
  assert(ocache->use_shape == 1);
  ocache_lookup_result_t lookup_result;
  // This will:
  //   (1) Set hit_entry and hit_index if hit
  //   (2) Always fill the candidate array with entries that store the same super block
  //   (3) state can be hit_normal, hit_compressed, or miss
  //   (4) LRU entry is also always assigned
  ocache_lookup_write(ocache, oid, addr, shape, &lookup_result);
  // Case 2 and Case 1.1.1
  if(shape == OCACHE_SHAPE_NONE) {
    ocache_insert_helper_uncompressed(ocache, oid, addr, shape, dirty, insert_result, &lookup_result);
    return;
  }
  // ---------------------------------------------------------------------------
  // The following assumes that compression is enabled for the requested line
  // ---------------------------------------------------------------------------
  int state = lookup_result.state;
  assert(state == OCACHE_LOOKUP_MISS || state == OCACHE_LOOKUP_HIT_COMPRESSED);
  assert(ocache->dmap != NULL);
  // This functions returns the size of the line after all kinds of compression
  int bdi_size = ocache_insert_helper_get_compressed_size(ocache, oid, addr, shape);
  if(state == OCACHE_LOOKUP_MISS) {
    if(lookup_result.count == 0) {
      // Case 1.1.2: Compressed line miss, no candidate, evict an existing entry and make a super block
      ocache_insert_helper_compressed_miss_no_candid(
        ocache, oid, addr, shape, dirty, bdi_size, insert_result, &lookup_result
      );
    } else {
      // Case 1.2: Compressed line miss, with candidate, try other super blocks
      ocache_insert_helper_compressed_miss_with_candid(
        ocache, oid, addr, shape, dirty, bdi_size, insert_result, &lookup_result
      );
    }
    return;
  }
  assert(state == OCACHE_LOOKUP_HIT_COMPRESSED);
  // Case 3: Compressed line hit
  ocache_insert_helper_compressed_hit(
    ocache, oid, addr, shape, dirty, bdi_size, insert_result, &lookup_result
  );
  return;
}

// Invalidate a single line, possibly in a super block
// There are three possible cases
//   1. Miss, return INV_SUCCESS
//   2. Hit a non-shaped block
//     2.1 return INV_SUCCESS if clean
//     2.2 Copy the block to eviction entry if it is dirty and return INV_EVICT
//   3. Hit a shaped block
//     3.1 return INV_SUCCESS if clean
//       3.1.1 If the line is the only line in the super block, invalidate the entire super block
//       3.2.2 Otherwise just invalidate the compressed line
//     3.2 return INV_EVICT if dirty
//       3.2.1 If the line is the only line in the super block, invalidate the entire super block
//       3.2.2 Otherwise just invalidate the compressed line
void ocache_inv(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, ocache_inv_result_t *result) {
  assert(ocache->use_shape == 1);
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0UL);
  result->state = OCACHE_INV_SUCCESS;
  ocache_lookup_result_t lookup_result;
  // This will change LRU, and we only reset LRU if the line is the only line
  ocache_lookup_read(ocache, oid, addr, shape, &lookup_result);
  if(lookup_result.state == OCACHE_LOOKUP_MISS) {
    return;
  }
  // Must be hit
  int state = lookup_result.state;
  assert(state == OCACHE_LOOKUP_HIT_NORMAL || state == OCACHE_LOOKUP_HIT_COMPRESSED);
  ocache_entry_t *hit_entry = lookup_result.hit_entry;
  assert(hit_entry != NULL);
  // If the line is NORMAL hit, then the hit_index will be zero
  int hit_index = lookup_result.hit_index;
  assert(hit_index >= 0 && hit_index < 4);
  // This is common to both cases
  if(ocache_entry_is_dirty(hit_entry, hit_index) == 1) {
    result->state = OCACHE_INV_EVICT;
    result->hit_index = hit_index;
    ocache_entry_copy(&result->evict_entry, hit_entry);
  }
  // This is also common to both cases
  ocache_entry_inv_index(hit_entry, hit_index);
  // If all lines are invalid, just invalidate the entire line (reset LRU, etc.)
  if(ocache_entry_is_all_invalid(hit_entry) == 1) {
    ocache_entry_inv(hit_entry);
  }
  return;
}

// Lookup without using shape, always assume index 0 stores the current block, if any
// Lookup result can only be hit or miss, and the entry pointer
void ocache_lookup_read_no_shape(ocache_t *ocache, uint64_t oid, uint64_t addr, ocache_lookup_result_t *result) {
  assert(ocache->use_shape == 0);
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0UL);
  ocache_entry_t *entry = ocache_get_set_begin(ocache, oid, addr, OCACHE_SHAPE_NONE);
  // Only set these two 
  result->state = OCACHE_LOOKUP_MISS;
  result->hit_entry = NULL;
  result->hit_index = 0;
  for(int i = 0;i < ocache->way_count;i++) {
    if(entry->oid == oid && entry->addr == addr && ocache_entry_is_valid(entry, 0)) {
      // Update hit information
      result->state = OCACHE_LOOKUP_HIT_NORMAL;
      result->hit_entry = entry;
      // Update LRU
      ocache_update_entry_lru(ocache, entry);
      return;
    }
    entry++;
  }
  return;
}

// This function returns LRU entry for eviction if misses
// It returns immediately when a hit is found
// Note: This function does not update LRU; Caller should update it
void ocache_lookup_write_no_shape(ocache_t *ocache, uint64_t oid, uint64_t addr, ocache_lookup_result_t *result) {
  assert(ocache->use_shape == 0);
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0UL);
  result->state = OCACHE_LOOKUP_MISS;
  result->hit_entry = NULL;
  result->hit_index = 0;
  result->lru_entry = NULL;
  ocache_entry_t *lru_entry = NULL;
  uint64_t lru_min = -1UL;
  ocache_entry_t *entry = ocache_get_set_begin(ocache, oid, addr, OCACHE_SHAPE_NONE);
  for(int i = 0;i < ocache->way_count;i++) {
    // Direct hit has highest priority
    if(entry->oid == oid && entry->addr == addr && ocache_entry_is_valid(entry, 0)) {
      result->state = OCACHE_LOOKUP_HIT_NORMAL;
      result->hit_entry = entry;
      return;
    }
    // Update LRU entry if hit has not been found. If there is an empty entry, it will always be
    // selected as the LRU since we clear LRU counter to zero when an entry is invalidated.
    if(entry->lru < lru_min) {
      lru_min = entry->lru;
      lru_entry = entry;
    }
    entry++;
  }
  // When reach here it must be a miss; The other two have been set at function begin
  result->lru_entry = lru_entry;
  return;
}

// This function calls lookup_write function to find a line that we can insert into
// There are three possible outcomes:
//   1. Hit an existing line, no eviction, return state is OCACHE_INSERT_SUCCESS
//   2. Find an invalid line, no eviction, return state is OCACHE_INSERT_SUCCESS
//   3. Evict an existing line, returned in evict_line, return state is OCACHE_INSERT_EVICT
// In all cases the LRU counter is updated
void ocache_insert_no_shape(ocache_t *ocache, uint64_t oid, uint64_t addr, int dirty, ocache_insert_result_t *result) {
  assert(ocache->use_shape == 0);
  assert(dirty == 0 || dirty == 1);
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0UL);
  ocache_lookup_result_t lookup_result;
  ocache_lookup_write_no_shape(ocache, oid, addr, &lookup_result);
  // By default, return success
  result->state = OCACHE_INSERT_SUCCESS;
  // For no shape cache there can only be two outcomes for lookup
  assert(lookup_result.state == OCACHE_LOOKUP_HIT_NORMAL || lookup_result.state == OCACHE_LOOKUP_MISS);
  // Update LRU and dirty bit using this pointer; Always point to the entry to be updated
  ocache_entry_t *entry = NULL;
  if(lookup_result.state == OCACHE_LOOKUP_HIT_NORMAL) {
    assert(lookup_result.hit_entry != NULL);
    entry = lookup_result.hit_entry;
  } else {
    ocache_entry_t *lru_entry = lookup_result.lru_entry;
    //printf("%d\n", (int)(lru_entry - ocache->data));
    assert(lru_entry != NULL);
    // Hits an invalid entry, just write the oid and addr, and update LRU
    if(ocache_entry_is_valid(lru_entry, 0) == 0) {
      // Nothing special
      assert(lru_entry->lru == 0UL);
    } else {
      result->state = OCACHE_INSERT_EVICT;
      // Evict the current entry, only need OID, addr, states, others are not needed
      //printf("Evict oid %lu addr %lu\n", lru_entry->oid, lru_entry->addr);
      ocache_entry_copy(&result->evict_entry, lru_entry);
      // Invalidate as if it were originally invalid
      ocache_entry_inv(lru_entry);
    }
    // Always update these two
    lru_entry->oid = oid;
    lru_entry->addr = addr;
    // This helps stat function
    lru_entry->shape = OCACHE_SHAPE_NONE;
    entry = lru_entry;
    // Set valid bit if it is off
    ocache_entry_set_valid(lru_entry, 0);
    ocache_entry_set_size(lru_entry, 0, UTIL_CACHE_LINE_SIZE);
  }
  // Update LRU since write lookup does not update it
  ocache_update_entry_lru(ocache, entry);
  // Update dirty bits accordingly - must be after copy the evict entry
  if(dirty == 1) {
    ocache_entry_set_dirty(entry, 0);
  } //else {
  //  ocache_entry_clear_dirty(entry, 0);
  //}
  return;
}

// Invalidates the exact OID + addr combination in the cache if it exists
// If the address does not exist, or is in clean state, then return OCACHE_INV_SUCCESS
// Otherwise return OCACHE_INV_EVICT
void ocache_inv_no_shape(ocache_t *ocache, uint64_t oid, uint64_t addr, ocache_inv_result_t *result) {
  assert(ocache->use_shape == 0);
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0UL);
  result->state = OCACHE_INV_SUCCESS;
  result->hit_index = 0; // Always zero, never set again in the body
  ocache_lookup_result_t lookup_result;
  // This will change LRU, but we move it to the bottom anyway
  ocache_lookup_read_no_shape(ocache, oid, addr, &lookup_result);
  if(lookup_result.state == OCACHE_LOOKUP_MISS) {
    return;
  }
  // Assumes hit
  assert(lookup_result.state == OCACHE_LOOKUP_HIT_NORMAL);
  ocache_entry_t *hit_entry = lookup_result.hit_entry;
  assert(hit_entry != NULL);
  if(ocache_entry_is_dirty(hit_entry, 0) == 1) {
    result->state = OCACHE_INV_EVICT;
    ocache_entry_copy(&result->evict_entry, hit_entry);
  }
  // Directly invalidate the entry and send it to the bottom of LRU
  ocache_entry_inv(hit_entry);
  return;
}

// Returns the MRU entry; Empty entry is also possible (LRU == 0)
// Argument way_start selects a starting way
// If there are several LRU, always select the first one
ocache_entry_t *ocache_get_mru(ocache_t *ocache, int set_index) {
  if(set_index < 0 || set_index >= ocache->set_count) {
    error_exit("Invalid set_index: %d\n", set_index);
  }
  ocache_entry_t *entry = ocache->data + set_index * ocache->way_count;
  ocache_entry_t *max_entry = entry;
  uint64_t max_lru = entry->lru;
  for(int i = 0;i < ocache->way_count;i++) {
    if(entry->lru > max_lru) {
      max_lru = entry->lru;
      max_entry = entry;
    }
    entry++;
  }
  return max_entry;
}

// Only the MRU for the entire cache; Only used for debugging
ocache_entry_t *ocache_get_mru_global(ocache_t *ocache) {
  ocache_entry_t *entry = ocache->data;
  ocache_entry_t *max_entry = entry;
  uint64_t max_lru = entry->lru;
  for(int i = 0;i < ocache->line_count;i++) {
    if(entry->lru > max_lru) {
      max_lru = entry->lru;
      max_entry = entry;
    }
    entry++;
  }
  return max_entry;
}

// Count the number of logical lines in the current ocache
void ocache_refresh_stat(ocache_t *ocache) {
  ocache_entry_t *entry = ocache->data;
  ocache->valid_line_count = 0UL;
  ocache->sb_count = 0UL;
  ocache->sb_line_count = 0UL;
  for(int i = 0;i < ocache->line_count;i++) {
    // Only do it when the line is valid
    if(ocache_entry_is_all_invalid(entry) == 0) {
      if(entry->shape == OCACHE_SHAPE_NONE) {
        ocache->valid_line_count++; // Valid line without compression
        ocache->no_shape_count++;
      } else {
        // Population of the state bit
        int lines = popcount_int32(entry->states & 0xF);
        ocache->valid_line_count += lines;
        ocache->sb_count++;
        ocache->sb_line_count += lines;
        switch(entry->shape) {
          case OCACHE_SHAPE_4_1: ocache->sb_4_1_count++; break;
          case OCACHE_SHAPE_1_4: ocache->sb_1_4_count++; break;
          case OCACHE_SHAPE_2_2: ocache->sb_2_2_count++; break;
        }
      }
    } else {
      assert(entry->lru == 0);
    }
    entry++;
  }
  return;
}

// Invalidate slots whose addresses lie in [addr_lo, addr_hi)
// addr_lo and addr_hi must be page aligned to avoid partially invalidating a slot
int ocache_inv_range(ocache_t *ocache, uint64_t addr_lo, uint64_t addr_hi) {
  if(addr_lo & (UTIL_PAGE_SIZE - 1) || addr_hi & (UTIL_PAGE_SIZE - 1)) {
    error_exit("Invalid alignment of low or high address [%lX, %lX) (must be page aligned)\n",
      addr_lo, addr_hi);
  }
  ocache_entry_t *curr = ocache->data;
  int count = 0;
  for(int i = 0;i < ocache->line_count;i++) {
    // Only check address if the entry has at least one valid entry
    // Note that since super block addresses are all aligned to block size,
    // if the base address is in the range, the entire block must also be in the range
    if(ocache_entry_is_all_invalid(curr) == 0) {
      if(curr->addr >= addr_lo && curr->addr < addr_hi) {
        ocache_entry_inv(curr);
        count++;
      }
    }
    curr++;
  }
  return count;
}

// Invalidate all cache lines in the ocache object
// This function does not perform write back; It simply resets all entries regardless of  
// entry type and dirtiness
int ocache_inv_all(ocache_t *ocache) {
  ocache_entry_t *curr = ocache->data;
  int count = 0;
  for(int i = 0;i < ocache->line_count;i++) {
    if(ocache_entry_is_all_invalid(curr) == 0) {
      ocache_entry_inv(curr);
      count++;
    }
    curr++;
  }
  return count;
}

void ocache_set_print(ocache_t *ocache, int set) {
  ocache_entry_t *curr = ocache->data + set * ocache->way_count;
  printf("---------- level %d name %s set %d ----------\n", 
    ocache->level, ocache->name ? ocache->name : "N/A", set);
  for(int i = 0;i < ocache->way_count;i++) {
    if(ocache_entry_is_all_invalid(curr) == 1) {
      assert(curr->lru == 0);
      curr++; // There was a bug...
      continue;
    }
    printf("Way %d oid 0x%lX addr 0x%lX LRU %lu state 0x%02X (dirty ",
      i, curr->oid, curr->addr, curr->lru, curr->states);
    for(int index = 3; index >= 0;index--) {
      printf("%d", ocache_entry_is_dirty(curr, index));
    }
    printf(" valid ");
    for(int index = 3; index >= 0;index--) {
      printf("%d", ocache_entry_is_valid(curr, index));
    }
    printf(")");
    printf(" sizes");
    for(int index = 3; index >= 0;index--) {
      printf(" %d", ocache_entry_get_size(curr, index));
    }
    putchar('\n');
    curr++;
  }
  printf("----------------------------\n");
  return;
}

void ocache_conf_print(ocache_t *ocache) {
  printf("---------- ocache_t conf ----------\n");
  printf("name \"%s\" level %d size %d bytes %d lines %d sets %d ways\n", 
    ocache->name ? ocache->name : "[No Name]", ocache->level, ocache->size, ocache->line_count, 
    ocache->set_count, ocache->way_count);
  printf("use_shape %d dmap %p line size %lu\n", ocache->use_shape, ocache->dmap, UTIL_CACHE_LINE_SIZE);
  for(int i = 0;i < OCACHE_SHAPE_COUNT;i++) {
    printf("name \"%s\" addr {mask 0x%lX shift %d} OID {mask 0x%lX shift %d}\n", 
      ocache_shape_names[i], ocache->indices[i].addr_mask, ocache->indices[i].addr_shift,
      ocache->indices[i].oid_mask, ocache->indices[i].oid_shift);
  }
  printf("-----------------------------------\n");
  return;
}

// This function will print vertical compression stats if use_shape is 1
void ocache_stat_print(ocache_t *ocache) {
  printf("---------- ocache_t stat ----------\n");
  // Vertical compression stats, only printed for LLC
  if(ocache->use_shape == 1) {
    printf("Total attempted %lu BDI success %lu not base %lu base found %lu type match %lu vertical success %lu\n",
      ocache->vertical_attempt_count, ocache->vertical_in_compressed_count, 
      ocache->vertical_not_base_count, ocache->vertical_base_found_count,
      ocache->vertical_same_type_count, ocache->vertical_success_count);
    printf("Vertical before size %lu after %lu (ratio %.2f%%)\n",
      ocache->vertical_before_size_sum, ocache->vertical_after_size_sum,
      100.0 * ocache->vertical_after_size_sum / ocache->vertical_before_size_sum);
    printf("BDI success %lu before %lu after %lu (ratio %.2f%%)\n", 
      ocache->BDI_success_count, ocache->BDI_before_size_sum, ocache->BDI_after_size_sum,
      100.0 * ocache->BDI_after_size_sum / ocache->BDI_before_size_sum);
  }
  // General stats refershed by ocache_refresh_stat()
  printf("Stat valid lines %lu sb %lu sb lines %lu ([4-1] %lu [1-4] %lu [2-2] %lu [no shape] %lu)\n", 
    ocache->valid_line_count, ocache->sb_count, ocache->sb_line_count,
    ocache->sb_4_1_count, ocache->sb_1_4_count, ocache->sb_2_2_count, ocache->no_shape_count);
  printf("-----------------------------------\n");
  return;
}

//* dram_t

dram_stat_t *dram_stat_init() {
  dram_stat_t *stat = (dram_stat_t *)malloc(sizeof(dram_stat_t));
  SYSEXPECT(stat != NULL);
  memset(stat, 0x00, sizeof(dram_stat_t));
  return stat;
}

void dram_stat_free(dram_stat_t *stat) {
  free(stat);
}

// Called by init, programmers need not call this function
static void dram_init_stats(dram_t *dram) {
  dram->stat_window = DRAM_STAT_WINDOW_SIZE;
  dram->stat_last_cycle = 0;
  dram->stat_read_count = 0;
  dram->stat_write_count = 0;
  dram->stat_write_size = 0;
  dram->stat_head = NULL;
  dram->stat_tail = NULL;
  dram->stat_count = 0;
  return;
}

dram_t *dram_init(conf_t *conf) {
  int bank_count = conf_find_int32_range(conf, "dram.bank_count", 1, INT_MAX, CONF_RANGE | CONF_POWER2);
  // Allocate more than just the struct; we need the banks array as well
  int size = sizeof(dram_t) + sizeof(uint64_t) * bank_count;
  dram_t *dram = (dram_t *)malloc(size);
  memset(dram, 0x00, size);
  dram->bank_count = bank_count;
  dram->addr_shift = UTIL_CACHE_LINE_BITS;
  dram->addr_mask = ((uint64_t)bank_count - 1) << UTIL_CACHE_LINE_BITS;
  dram->oid_mask = ((uint64_t)bank_count - 1);
  // Need this for debugging, e.g., address generation
  dram->bank_count_bits = popcount_int32(bank_count - 1);
  // Read read latency and write latency
  dram->read_latency = conf_find_int32_range(conf, "dram.read_latency", 1, INT_MAX, CONF_RANGE);
  dram->write_latency = conf_find_int32_range(conf, "dram.write_latency", 1, INT_MAX, CONF_RANGE);
  dram_init_stats(dram);
  return dram;
}

void dram_free(dram_t *dram) {
  // Free stat linked list
  dram_stat_t *curr = dram->stat_head;
  int count = 0;
  while(curr != NULL) {
    dram_stat_t *next = curr->next;
    dram_stat_free(curr);
    count++;
    curr = next;
  }
  assert(count == dram->stat_count);
  (void)count;
  free(dram);
  return;
}

// This function is shared by dram read and dram write
uint64_t dram_access(dram_t *dram, uint64_t cycle, uint64_t oid, uint64_t addr, int latency) {
  int index = dram_gen_bank_index(dram, oid, addr);
  assert(index >= 0 && index < dram->bank_count);
  if(dram->banks[index] < cycle) {
    // The bank is available at this moment, just serve the request immediately
    cycle += latency;
  } else {
    // Otherwise serve it in the earliest possible cycle
    cycle = dram->banks[index] + latency;
  }
  // Update available cycle
  dram->banks[index] = cycle;
  // Try taking stat
  if(cycle - dram->stat_last_cycle >= dram->stat_window) {
    dram_stat_t *stat = dram_stat_init();
    // Initialize stat object
    stat->cycle_count = cycle - dram->stat_last_cycle;
    stat->read_count = dram->stat_read_count;
    stat->write_count = dram->stat_write_count;
    stat->write_size = dram->stat_write_size;
    // Adding into linked list
    if(dram->stat_head == NULL) {
      assert(dram->stat_tail == NULL);
      dram->stat_head = dram->stat_tail = stat;
    } else {
      dram->stat_tail->next = stat;
      dram->stat_tail = stat;
    }
    // Clear for next window
    dram->stat_last_cycle = cycle;
    dram->stat_read_count = 0;
    dram->stat_write_count = 0;
    dram->stat_write_size = 0;
    dram->stat_count++;
  }
  return cycle;
}

// Write DRAM bandwidth usage to a file
void dram_stat_dump(dram_t *dram, const char *filename) {
  printf("Writing stats to output file \"%s\"\n", filename);
  FILE *fp = fopen(filename, "wb");
  SYSEXPECT(fp != NULL);
  dram_stat_t *curr = dram->stat_head;
  int count = 0;
  while(curr != NULL) {
    int ret = fwrite(curr, sizeof(dram_stat_t), 1, fp);
    if(ret != 1) {
      error_exit("fwrite() returns %d (expecting 1)\n", ret);
    }
    count++;
    curr = curr->next;
  }
  assert(count == dram->stat_count);
  (void)count;
  fclose(fp);
  printf("... Done\n");
  return;
}

void dram_conf_print(dram_t *dram) {
  printf("---------- dram_t conf ----------\n");
  printf("Addr mask 0x%016lX shift %d bank count bits %d OID mask 0x%016lX\n", 
    dram->addr_mask, dram->addr_shift, dram->bank_count_bits, dram->oid_mask);
  printf("Latancy read %d write %d\n", dram->read_latency, dram->write_latency);
  return;
}

void dram_stat_print(dram_t *dram) {
  printf("---------- dram_t stat ----------\n");
  printf("Stat window %lu count %d last cycle %lu\n",
    dram->stat_window, dram->stat_count, dram->stat_last_cycle);
  printf("Curr read %u write %u write size %u\n",
    dram->stat_read_count, dram->stat_write_count, dram->stat_write_size);
  return;
}

//* cc_t

cc_param_t *cc_param_init(conf_t *conf) {
  cc_param_t *param = (cc_param_t *)malloc(sizeof(cc_param_t));
  SYSEXPECT(param != NULL);
  memset(param, 0x00, sizeof(cc_param_t));
  int size, way_count, latency;
  // l1.size, l1.way_count and l1.latency
  cc_param_read_cache_conf(conf, "l1", "L1", &size, &way_count, &latency);
  cc_param_set_l1_size(param, size);
  cc_param_set_l1_way_count(param, way_count);
  cc_param_set_l1_latency(param, latency);
  // l2.size, l2.way_count and l2.latency
  cc_param_read_cache_conf(conf, "l2", "L2", &size, &way_count, &latency);
  cc_param_set_l2_size(param, size);
  cc_param_set_l2_way_count(param, way_count);
  cc_param_set_l2_latency(param, latency);
  // l3.size, l3.way_count and l3.latency
  cc_param_read_cache_conf(conf, "l3", "L3", &size, &way_count, &latency);
  cc_param_set_l3_size(param, size);
  cc_param_set_l3_way_count(param, way_count);
  cc_param_set_l3_latency(param, latency);
  // cpu.core_count, allow [1, CC_CORE_COUNT_MAX]
  int core_count = conf_find_int32_range(conf, "cpu.core_count", 1, CC_CORE_COUNT_MAX, CONF_RANGE);
  cc_param_set_core_count(param, core_count);
  if(core_count != 1) {
    error_exit("Only support 1 core for now\n");
  }
  int l3_compressed = conf_find_bool_mandatory(conf, "l3.compressed");
  cc_param_set_l3_compressed(param, l3_compressed);
  return param;
}

void cc_param_free(cc_param_t *param) {
  free(param);
  return;
}

// This function reads cache hierarchy parameters and verifies them
// Prefix is the prefix of the conf file key, which will be prepended to the rest of the key
// cache_name is the name of the cache that will be printed in error message
// The following is a list of conf keys that must be defined and will be read:
//   l1.size, l1.way_count, l1.latency 
//   l2.size, l2.way_count, l2.latency
//   l3.size, l3.way_count, l3.latency
//   cpu.core_count
//   l3.compressed
void cc_param_read_cache_conf(
  conf_t *conf, const char *prefix, const char *cache_name, int *size, int *way_count, int *latency) {
  char key[32];
  // Generate size key, e.g., l1.size
  snprintf(key, sizeof(key), "%s.size", prefix);
  *size = conf_find_int32_range(conf, key, 1, INT_MAX, CONF_RANGE);
  if(*size % UTIL_CACHE_LINE_SIZE != 0) {
    error_exit("%s must be a multiple of %lu (sees %d)\n", key, UTIL_CACHE_LINE_SIZE, *size);
  }
  int line_count = *size / UTIL_CACHE_LINE_SIZE;
  snprintf(key, sizeof(key), "%s.way_count", prefix);
  *way_count = conf_find_int32_range(conf, key, 1, INT_MAX, CONF_RANGE);
  if(line_count % *way_count != 0) {
    error_exit("%s must be a multiple of cache line count (sees %d and %d)\n",
      key, *way_count, line_count);
  }
  int set_count = line_count / *way_count;
  if(popcount_int32(set_count) != 1) {
    error_exit("%s's set count must be a power of two (sees %d)\n", cache_name, set_count);
  }
  snprintf(key, sizeof(key), "%s.latency", prefix);
  *latency = conf_find_int32_range(conf, key, 1, INT_MAX, CONF_RANGE);
  return;
}

void cc_param_conf_print(cc_param_t *param) {
  printf("---------- cc_param ----------\n");
  printf("Cores %d\n", param->core_count);
  printf("L1 size %d (lines %d) ways %d latency %d\n", cc_param_get_l1_size(param), 
    cc_param_get_l1_size(param) / (int)UTIL_CACHE_LINE_SIZE, cc_param_get_l1_way_count(param), 
    cc_param_get_l1_latency(param));
  printf("L2 size %d (lines %d) ways %d latency %d\n", cc_param_get_l2_size(param), 
    cc_param_get_l2_size(param) / (int)UTIL_CACHE_LINE_SIZE, cc_param_get_l2_way_count(param),
    cc_param_get_l2_latency(param));
  printf("L3 size %d (lines %d) ways %d latency %d\n", cc_param_get_l3_size(param), 
    cc_param_get_l3_size(param) / (int)UTIL_CACHE_LINE_SIZE, cc_param_get_l3_way_count(param),
    cc_param_get_l3_latency(param));
  printf("L3 compressed: %d\n", param->l3_compressed);
  return;
}

// Initialize CC's per-cache, per-level stat
static void cc_init_stat(cc_t *cc) {
  int core_count = cc_param_get_core_count(cc->param);
  // Always allocate and initialize this much
  int size = sizeof(uint64_t) * core_count;
  for(int level = CC_LEVEL_BEGIN;level < CC_LEVEL_END;level++) {
    // One element for each core on each level
    cc->read_count[level] = (uint64_t *)malloc(size);
    SYSEXPECT(cc->read_count[level] != NULL);
    memset(cc->read_count[level], 0x00, size);
    cc->write_count[level] = (uint64_t *)malloc(size);
    SYSEXPECT(cc->write_count[level] != NULL);
    memset(cc->write_count[level], 0x00, size);
    cc->read_hit_count[level] = (uint64_t *)malloc(size);
    SYSEXPECT(cc->read_hit_count[level] != NULL);
    memset(cc->read_hit_count[level], 0x00, size);
    cc->write_hit_count[level] = (uint64_t *)malloc(size);
    SYSEXPECT(cc->write_hit_count[level] != NULL);
    memset(cc->write_hit_count[level], 0x00, size);
    cc->read_miss_count[level] = (uint64_t *)malloc(size);
    SYSEXPECT(cc->read_miss_count[level] != NULL);
    memset(cc->read_miss_count[level], 0x00, size);
    cc->write_miss_count[level] = (uint64_t *)malloc(size);
    SYSEXPECT(cc->write_miss_count[level] != NULL);
    memset(cc->write_miss_count[level], 0x00, size);
  }
  return;
}

cc_t *cc_init(conf_t *conf) {
  cc_t *cc = (cc_t *)malloc(sizeof(cc_t));
  SYSEXPECT(cc != NULL);
  memset(cc, 0x00, sizeof(cc_t));
  // Initialize param object
  cc->param = cc_param_init(conf);
  // Use this one as a short hand
  cc_param_t *param = cc->param;
  for(int i = CC_LEVEL_BEGIN; i < CC_LEVEL_END;i++) {
    cc->caches[i] = (ocache_t **)malloc(sizeof(ocache_t **) * param->core_count);
    SYSEXPECT(cc->caches[i] != NULL);
    char name_buf[CC_CACHE_NAME_MAX];
    // All cores same level have the same cache
    int size = param->sizes[i];
    int way_count = param->way_counts[i];
    if(i != CC_L3) {
      for(int j = 0;j < param->core_count;j++) {
        snprintf(name_buf, CC_CACHE_NAME_MAX, "[Core %d Lvl %d]", j, i);
        cc->caches[i][j] = ocache_init(size, way_count);
        ocache_set_level(cc->caches[i][j], i);
        ocache_set_name(cc->caches[i][j], name_buf);
      }
    } else {
      cc->llc = ocache_init(size, way_count);
      snprintf(name_buf, CC_CACHE_NAME_MAX, "[Shared Lvl %d]", i);
      ocache_set_level(cc->llc, i);
      ocache_set_name(cc->llc, name_buf);
      // Set shared LLC
      for(int j = 0;j < param->core_count;j++) {
        cc->caches[i][j] = cc->llc;
      }
    }
  }
  cc_init_stat(cc);
  return cc;
}

void cc_free(cc_t *cc) {
  for(int i = CC_LEVEL_BEGIN;i < CC_LEVEL_END;i++) {
    for(int j = 0;j < cc->param->core_count;j++) {
      // LLC is a shared object
      if(i != CC_L3) {
        ocache_free(cc->caches[i][j]);
      }
    }
    // Free the ocache pointer array
    free(cc->caches[i]);
    // Free statistics
    free(cc->read_count[i]);
    free(cc->read_hit_count[i]);
    free(cc->read_miss_count[i]);
    free(cc->write_count[i]);
    free(cc->write_hit_count[i]);
    free(cc->write_miss_count[i]);
  }
  // This should only be freed once
  ocache_free(cc->llc);
  cc_param_free(cc->param);
  free(cc);
  return;
}

// Sets the dmap for compression; This compression is not enabled this is unnecessary
void cc_set_dmap(cc_t *cc, dmap_t *dmap) {
  cc_param_t *param = cc->param;
  assert(param != NULL);
  if(param->l3_compressed == 0) {
    error_exit("Compression is disabled; dmap is not needed\n");
  } else if(cc->dmap != NULL) {
    error_exit("dmap of cc_t has already been set\n");
  }
  cc->dmap = dmap;
  cc->use_shape[CC_L3] = 1;
  // This enables ocache compression
  ocache_set_dmap(cc->llc, dmap);
  assert(ocache_is_use_shape(cc->llc) == 1);
  return;
}

// Set the DRAM object, which will be used for DRAM call back. This is optional if the call back is
// overridden (not the default one)
void cc_set_dram(cc_t *cc, dram_t *dram) {
  if(cc->dram != NULL) {
    error_exit("The DRAM object has already been set in the cc_t\n");
  }
  cc->dram = dram;
  return;
}

// Recursively call invalide function from upper level. If result is dirty, then change the size field
// of result 
// This function assumes that:
//   (1) oid and addr in arg list are the exact, uncompressed address to invalidate
//   (2) This function will set valid, dirty and size field if the upper level line is dirty;
//   (3) Upper level lines will override existing compressed line's size (if the line exists)
static uint64_t cc_inv_llc_recursive_helper(
  cc_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int index, 
  ocache_entry_t *entry, ocache_entry_t *result) {
  int dirty = 0;
  // Invalidate from L2 and above
  cc_inv_recursive(cc, id, CC_L2, cycle, oid, addr, &dirty);
  // If either the upper level line, or the LLC line, is dirty, then mark eviction
  if(ocache_entry_is_dirty(entry, index) == 1 || dirty == 1) {
    ocache_entry_set_valid(result, index);
    ocache_entry_set_dirty(result, index);
    if(dirty == 1) {
      ocache_entry_set_size(result, index, UTIL_CACHE_LINE_SIZE);
    } else {
      assert(ocache_entry_is_valid(entry, index) == 1);
      ocache_entry_set_size(result, index, ocache_entry_get_size(entry, index));
    }
  }
  return cycle;
}

// This function recursively invalidates an entire LLC entry in the hierarchy to maintain inclusiveness
// The "entry" argument is the entry to be invalidated
// The "result" argument returns the result of invalidation; 
// Note: 
//   (1) The result may contain either evicted lines from upper level, or from the LLC entry;
//   (2) The "size" field records the actual size of the evicted line, i.e., if an upper level dirty line overrides 
//       an LLC line, the "size" field will be 64 bytes
uint64_t cc_inv_llc_recursive(cc_t *cc, int id, uint64_t cycle, ocache_entry_t *entry, ocache_entry_t *result) {
  assert(id == 0);
  ocache_entry_inv(result); // Clear all bits and size fields
  uint64_t base_oid = entry->oid;
  uint64_t base_addr = entry->addr;
  int shape = entry->shape;
  result->oid = base_oid;
  result->addr = base_addr;
  result->shape = shape;
  // Easy case: Uncompressed line in LLC, just invalidate from the upper level
  if(shape == OCACHE_SHAPE_NONE) {
    // This may override upper LLC lines with upper level lines
    cc_inv_llc_recursive_helper(cc, id, cycle, entry->oid, entry->addr, 0, entry, result);
    //printf("LLC recursive inv oid %lX addr %lX\n", entry->oid, entry->addr);
    ocache_entry_inv(entry);
    return cycle;
  }
  // Shaped entry: loop over all valid lines (inclusive LLC), issue invalidation to upper level, and 
  // if there is dirty write back, then override the LLC entry
  for(int index = 0;index < 4;index++) {
    if(ocache_entry_is_valid(entry, index) == 1) {
      uint64_t oid, addr;
      // Generate address for the valid logical line in the sb
      ocache_gen_addr_in_sb(cc->llc, base_oid, base_addr, index, shape, &oid, &addr);
      //printf("LLC recursive inv oid %lX addr %lX\n", oid, addr);
      cc_inv_llc_recursive_helper(cc, id, cycle, oid, addr, index, entry, result);
    }
  }
  ocache_entry_inv(entry);
  return cycle;
}

// Evict a line from cache at [level][id]
// This function is recursive, such that the same line in all upper level caches are also evicted
// This function will write the evicted line, if dirty, to the lower level. This operation is guaranteed
// to hit the lower level, since we assume inclusive. The compressed LLC, however, may still evict a line
// for insertion hit, due to compressed size changes.
// Note 
//   (1) This function must not be called on L3 cache, since it is compressed. The L3 eviction will be done
//       on insertion.
//   (2) This function may indicate that the line is dirty in one of the caches.
//   (3) Return value is finish cycle. Currently we assume it takes zero cycle
//   (4) This function destrories existing "dirty" value regardless of the result
uint64_t cc_inv_recursive(
  cc_t *cc, int id, int level, uint64_t cycle, uint64_t oid, uint64_t addr, int *dirty) {
  assert(id == 0);
  *dirty = 0;
  for(int i = level;i >= CC_LEVEL_BEGIN;i--) {
    // This function must not be called on LLC, since LLC invalidations are more complicated
    assert(i != CC_L3);
    ocache_t *ocache = cc->caches[i][id];
    ocache_inv_result_t inv_result;
    //printf("Inv level %d oid %lX addr %lX\n", i, oid, addr);
    ocache_inv_no_shape(ocache, oid, addr, &inv_result);
    //ocache_set_print(ocache, 0);
    // Just log dirty, and that's it. We know the OID and addr for non-compressed caches
    if(inv_result.state == OCACHE_INV_EVICT) {
      *dirty = 1;
    }
  }
  return cycle;
}

// This function recursively inserts into the LLC, and handles evictions.
// If the insertion results in cache lines being evicted from the LLC, this function will
// recursively call evict function to also invalidate the lines in the upper level
// If both upper level lines and the LLC lines are dirty, then only one copy (upper level copy) is written back
// using the main memory call back
// Note:
//   (1) Only dirty line will be written back to the main memory
//   (2) Dirty lines from the upper level (to maintain inclusiveness) will override dirty lines in the LLC
//   (3) Dirty lines from the upper level is written back in uncompressed form
//   (4) This function will also issue write to the main memory
uint64_t cc_insert_llc_recursive(
  cc_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int shape, int dirty) {
  assert(id == 0); (void)id;
  ocache_t *ocache = cc->llc; // Just use LLC for less memory access
  ocache_insert_result_t insert_result;
  ocache_insert(ocache, oid, addr, shape, dirty, &insert_result);
  // If no eviction happens then return
  if(insert_result.state == OCACHE_INSERT_SUCCESS) {
    return cycle;
  }
  assert(insert_result.state == OCACHE_INSERT_EVICT);
  ocache_entry_t *evict_entry = &insert_result.evict_entry;
  assert(evict_entry != NULL);
  // This stores the result of LLC insert
  ocache_entry_t result;
  cc_inv_llc_recursive(cc, id, cycle, evict_entry, &result);
  cc->main_mem_cb(cc, cycle, CC_WRITE, &result);
  return cycle;
}

// Recursively insert lines from the given starting level till L1
// Note:
//   (1) This function may insert into LLC twice, once for inserting the line, second time for inserting 
//       an evicted line from L2. The second insertion may also incur an LLC eviction.
//   (2) Shape argument is only used if LLC will also be inserted into. This argument is merely a short hand
//       by avoiding querying pmap again
uint64_t cc_insert_recursive(
  cc_t *cc, int id, uint64_t cycle, int start_level, uint64_t oid, uint64_t addr, int shape, int is_write) {
  assert(id == 0);
  if(start_level < CC_LEVEL_BEGIN) {
    return cycle;
  }
  assert(start_level < CC_L3 || shape != -1);
  assert(start_level >= CC_LEVEL_BEGIN && start_level < CC_LEVEL_END);
  for(int level = start_level;level >= CC_LEVEL_BEGIN;level--) {
    if(cc->use_shape[level] == 0) {
      ocache_t *ocache = cc->caches[level][id];
      ocache_insert_result_t insert_result;
      int insert_dirty = (is_write == 1) && (level == CC_L1);
      assert(ocache_is_use_shape(ocache) == 0);
      ocache_insert_no_shape(ocache, oid, addr, insert_dirty, &insert_result);
      // Invalidate from upper level, and insert into next level
      if(insert_result.state == OCACHE_INSERT_EVICT) {
        int next_level = level + 1;
        assert(next_level > CC_LEVEL_BEGIN && next_level < CC_LEVEL_END);
        ocache_entry_t *evict_entry = &insert_result.evict_entry;
        uint64_t evict_oid = evict_entry->oid;
        uint64_t evict_addr = evict_entry->addr;
        int next_level_insert_dirty = 0;
        // First recursively invalidate from upper level
        cc_inv_recursive(cc, id, level - 1, cycle, evict_oid, evict_addr, &next_level_insert_dirty);
        // Insert into next ocache, if the evicted line is dirty
        if(next_level_insert_dirty == 1 || ocache_entry_is_dirty(evict_entry, 0) == 1) {
          if(cc->use_shape[next_level] == 0) {
            ocache_t *next_ocache = cc->caches[next_level][id];
            ocache_insert_result_t next_level_insert_result;
            // In inclusive cacles, this will merely be setting an existing entry to dirty, since we
            // know the address must exist
            ocache_insert_no_shape(next_ocache, evict_oid, evict_addr, 1, &next_level_insert_result);
            // Inclusive cache, must be insert hit
            assert(next_level_insert_result.state == OCACHE_INSERT_SUCCESS);
          } else {
            pmap_entry_t *pmap_entry = pmap_find(cc->dmap, evict_addr);
            //assert(pmap_entry != NULL);
            int evict_shape;
            if(pmap_entry != NULL) {
              evict_shape = pmap_entry->shape;
            } else {
              evict_shape = OCACHE_SHAPE_NONE;
            }
            // Always insert dirty since we know the line is dirty
            cc_insert_llc_recursive(cc, id, cycle, evict_oid, evict_addr, evict_shape, 1);
          }
        }
      }
    } else {
      // The caller must have already queried the shape of the inserted line
      assert(shape != -1);
      // The insert of a newly fetched line is never dirty (insert from the bottom)
      // This function may issue writes to the main memory
      cc_insert_llc_recursive(cc, id, cycle, oid, addr, shape, 0);
    }
  }
  return cycle;
}

// Simulates load on the cache hierarchy. The main memory call back may also be invoked if the requests
// goes into the DRAM
// This function assumes that:
//   (1) The dmap entry has been created, but data has not been updated, if it is a store, since we perform 
//       write back with old data
//   (2) The pmap entry has been created, with shape info entered
//   (3) This function is shared by load and store, the dirty argument dictates whether the inserted line is 
//       dirty (for store) or clean (for load) in L1, and only L1
uint64_t cc_access(cc_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int is_write) {
  assert(id == 0); // Only support single core
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0);
  assert(cc_main_mem_cb != NULL); // DRAM call back must be present
  // Use this as the shape for LLC
  int shape = -1;
  int hit_level = CC_LEVEL_END; // This means hit main memory
  for(int level = CC_LEVEL_BEGIN;level < CC_LEVEL_END;level++) {
    // Update stat on reads and writes
    if(is_write == 1) {
      cc->write_count[level][id]++;
    } else {
      cc->read_count[level][id]++;
    }
    // The latency is always paid as long as the cache is accessed
    cycle += cc->param->latencies[level];
    ocache_lookup_result_t lookup_result;
    ocache_t *ocache = cc->caches[level][id];
    if(cc->use_shape[level] == 0) {
      ocache_lookup_read_no_shape(ocache, oid, addr, &lookup_result);
    } else {
      // Only read pmap and get shape when we query LLC
      pmap_entry_t *pmap_entry = pmap_find(cc->dmap, addr);
      //assert(pmap_entry != NULL);
      // If shape is defined, then use it. Otherwise it is not compressed
      if(pmap_entry != NULL) {
        shape = pmap_entry->shape;
      } else {
        shape = OCACHE_SHAPE_NONE;
      }
      ocache_lookup_read(ocache, oid, addr, shape, &lookup_result);
    }
    // Note that the state can either be HIT NORMAL or HIT COMPRESSED
    if(lookup_result.state != OCACHE_LOOKUP_MISS) {
      assert(lookup_result.state == OCACHE_LOOKUP_HIT_NORMAL || 
             lookup_result.state == OCACHE_LOOKUP_HIT_COMPRESSED);
      // Update stat, either hits L1 or lower
      if(is_write == 1) {
        cc->write_hit_count[level][id]++;
      } else {
        cc->read_hit_count[level][id]++;
      }
      // Special case: if hit L1 then just mark dirty, if the dirty argument is dirty
      // Also we can return early if hit L1 since there is no insertion
      if(level == CC_L1) {
        assert(lookup_result.hit_entry != NULL);
        if(is_write == 1) {
          ocache_entry_set_dirty(lookup_result.hit_entry, 0);
        }
        return cycle;
      }
      // If hit lower level or miss hierarchy, update the level, and exit loop
      hit_level = level;
      break;
    } else {
      // Miss current level, update stat
      if(is_write == 1) {
        cc->write_miss_count[level][id]++;
      } else {
        cc->read_miss_count[level][id]++;
      }
    }
  }
  // Read main memory, if the cache hierarchy misses
  if(hit_level == CC_LEVEL_END) {
    // Use a temporary entry object for read call back
    ocache_entry_t read_entry;
    ocache_entry_inv(&read_entry);
    // Entry needs this three fields for reads
    read_entry.oid = oid;
    read_entry.addr = addr;
    // Must have already tried LLC, so shape must have been set
    assert(shape != -1);
    // Return completion cycle for read, since we will block on this
    cycle = cc->main_mem_cb(cc, cycle, CC_READ, &read_entry);
  }
  // Insert from the lowest miss level to the L1; If hit L1 this function will return immediately
  // If miss LLC, then shape will be set, otherwise it is -1
  cc_insert_recursive(cc, id, cycle, hit_level - 1, oid, addr, shape, is_write);
  return cycle;
}

uint64_t cc_load(cc_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr) {
  assert(id == 0);
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0);
  // Insert with dirty bit set to 0
  cycle = cc_access(cc, id, cycle, oid, addr, 0);
  return cycle;
}

// Simulate store. For single core store, it uses the same procedure as load, except that the 
// cache line is marked as dirty.
// No coherence action is required, since all lines have exclusive permission
// Note:
//   (1) dmap is not updated by this function. It always assume that pmap and dmap entries have been inserted
//   (2) Logical line in the dmap should be updated after this function returns since write back is processed 
//       using dmap data
uint64_t cc_store(cc_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr) {
  assert(id == 0);
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0);
  // Insert with dirty bit set to 1, or if hit, set dirty bit to 1
  cycle = cc_access(cc, id, cycle, oid, addr, 1);
  return cycle;
}

// This is the default DRAM timing model with CC
// This call back assumes that the DRAM object in the CC object has been set
uint64_t cc_main_mem_cb(cc_t *cc, uint64_t cycle, int op, ocache_entry_t *entry) {
  dram_t *dram = cc->dram;
  assert(dram != NULL);
  if(op == CC_READ) {
    uint64_t read_oid = entry->oid;
    uint64_t read_addr = entry->addr;
    cycle = dram_read(dram, cycle, read_oid, read_addr);
  } else {
    assert(op == CC_WRITE);
    uint64_t max_cycle = cycle;
    for(int index = 0;index < 4;index++) {  
      // Only write dirty lines
      if(ocache_entry_is_valid(entry, index) && ocache_entry_is_dirty(entry, index)) {
        uint64_t write_oid;
        uint64_t write_addr;
        ocache_gen_addr_in_sb(cc->llc, entry->oid, entry->addr, index, entry->shape, &write_oid, &write_addr);
        int size = ocache_entry_get_size(entry, index);
        // Issue writes in parallel using the same cycle
        uint64_t finish_cycle = dram_write(dram, cycle, write_oid, write_addr, size);
        if(finish_cycle > max_cycle) {
          max_cycle = finish_cycle;
        }
      }
    }
    cycle = max_cycle;
  }
  return cycle;
}

void cc_conf_print(cc_t *cc) {
  cc_param_conf_print(cc->param);
  return;
}

// If verbose is set, print per-core on each level. Otherwise, just print level stat
void cc_stat_print(cc_t *cc, int verbose) {
  printf("---------- cc_t stat ----------\n");
  for(int level = CC_LEVEL_BEGIN;level < CC_LEVEL_END;level++) {
    uint64_t read_count = 0;
    uint64_t read_hit_count = 0;
    uint64_t read_miss_count = 0;
    uint64_t write_count = 0;
    uint64_t write_hit_count = 0;
    uint64_t write_miss_count = 0;
    for(int id = 0;id < cc->param->core_count;id++) {
      if(verbose == 1) {
        printf("Level %d id %d reads %lu hits %lu misses %lu writes %lu hits %lu misses %lu\n", level, id,
          cc->read_count[level][id], cc->read_hit_count[level][id], cc->read_miss_count[level][id], 
          cc->write_count[level][id], cc->write_hit_count[level][id], cc->write_miss_count[level][id]);
      }
      read_count += cc->read_count[level][id];
      read_hit_count += cc->read_hit_count[level][id];
      read_miss_count += cc->read_miss_count[level][id];
      write_count += cc->write_count[level][id];
      write_hit_count += cc->write_hit_count[level][id];
      write_miss_count += cc->write_miss_count[level][id];
    }
    printf("Level %d total reads %lu hits %lu misses %lu writes %lu hits %lu misses %lu\n", level, 
      read_count, read_hit_count, read_miss_count, write_count, write_hit_count, write_miss_count);
  }
  return;
}

//* oc_t

// Keys:
//   l1.size, l1.way_count, l1.latency 
//   l2.size, l2.way_count, l2.latency
//   l3.size, l3.way_count, l3.latency
//   cpu.core_count
//   l3.compressed
//   ---
//   dram.bank_count
//   dram.read_latency
//   dram.write_latency
oc_t *oc_init(conf_t *conf) {
  oc_t *oc = (oc_t *)malloc(sizeof(oc_t));
  SYSEXPECT(oc != NULL);
  memset(oc, 0x00, sizeof(oc_t));
  oc->dmap = dmap_init();
  oc->dram = dram_init(conf); // Read bank count, read and write latency from the conf
  oc->cc = cc_init(conf);     // Read cache hierarchy topology and latency
  if(cc_param_get_core_count(oc->cc->param) != 1) {
    error_exit("2DOC only supports single core simulation\n");
  }
  // Initialize cc with dmap, DRAM and main mem cb
  cc_set_dmap(oc->cc, oc->dmap);
  cc_set_dram(oc->cc, oc->dram);
  cc_set_main_mem_cb(oc->cc, cc_main_mem_cb);
  // Only as short hands
  oc->l1 = cc_get_ocache(oc->cc, CC_L1, 0);
  oc->l2 = cc_get_ocache(oc->cc, CC_L2, 0);
  oc->l3 = cc_get_ocache(oc->cc, CC_L3, 0);
  return oc; 
}

void oc_free(oc_t *oc) {
  dmap_free(oc->dmap);
  dram_free(oc->dram);
  cc_free(oc->cc);
  free(oc);
  return;
}

// Generate cache aligned access plans given an arbitrary address and size
void oc_gen_line_addr(oc_t *oc, uint64_t addr, int size, uint64_t *base_addr, int *line_count) {
  assert(size > 0);
  uint64_t end_addr = (addr + size - 1) & ~(UTIL_CACHE_LINE_SIZE - 1);
  addr = addr & ~(UTIL_CACHE_LINE_SIZE - 1);
  *line_count = ((end_addr - addr) / UTIL_CACHE_LINE_SIZE) + 1;
  *base_addr = addr;
  assert(*line_count > 0);
  assert(*base_addr % UTIL_CACHE_LINE_SIZE == 0);
  assert(end_addr % UTIL_CACHE_LINE_SIZE == 0);
  (void)oc;
  return;
}

// The following two functions accepts a potentially unaligned address, and a size argument, and issues
// one or more coherence requests to the underlying cc object
// For oc_store, the data to be written is also updated after coherence completes

// buf is not used
uint64_t oc_load(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int size, void *buf) {
  int line_count;
  uint64_t base_addr;
  oc_gen_line_addr(oc, addr, size, &base_addr, &line_count);
  // Most accesses will not cross cache line boundary, so this has minimum impact on accuracy
  uint64_t max_cycle = 0UL;
  for(int i = 0;i < line_count;i++) {
    // Create data entry if not already exist - this is required for loading data into LLC
    dmap_insert(oc->dmap, oid, base_addr);
    cycle = cc_load(oc->cc, id, cycle, oid, base_addr);
    // Update max cycle
    if(cycle > max_cycle) {
      max_cycle = cycle;
    }
    base_addr += UTIL_CACHE_LINE_SIZE;
  }
  // This does not change data, so order is irrelevant
  // Do not need data, since the application will read from virtual address space anyway
  //dmap_read(oc->dmap, oid, addr, size, buf);
  (void)buf; (void)size;
  return max_cycle;
}

// Note that we must call cc->store() to perform coherence action first, and then update dmap(). The reason is that
// during coherence actions, the LLC may perform compression, which should use data before the store operation.
uint64_t oc_store(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int size, void *buf) {
  int line_count;
  uint64_t base_addr;
  oc_gen_line_addr(oc, addr, size, &base_addr, &line_count);
  uint64_t max_cycle = 0UL;
  for(int i = 0;i < line_count;i++) {
    dmap_insert(oc->dmap, oid, base_addr);
    cycle = cc_store(oc->cc, id, cycle, oid, base_addr);
    if(cycle > max_cycle) {
      max_cycle = cycle;
    }
    base_addr += UTIL_CACHE_LINE_SIZE;
  }
  // This is called once, since dmap_write() will take care of addr and size
  //printf("Writing oid %lX addr %lX size %d\n", oid, addr, size);
  dmap_write(oc->dmap, oid, addr, size, buf);
  return cycle;
}

// Print all member object's conf
void oc_conf_print(oc_t *oc) {
  printf("---------- oc_t conf ------------\n");
  printf("dmap:\n");
  dmap_conf_print(oc->dmap);
  // Print conf for L1, L2 and L3
  printf("L1:\n");
  ocache_conf_print(oc->l1);
  printf("L2:\n");
  ocache_conf_print(oc->l2);
  printf("L3:\n");
  ocache_conf_print(oc->l3);
  printf("cc:\n");
  cc_conf_print(oc->cc);
  printf("DRAM:\n");
  dram_conf_print(oc->dram);
  printf("---------------------------------\n");
  return;
}

void oc_stat_print(oc_t *oc) {
  printf("---------- oc_t stat ----------\n");
  printf("dmap:\n");
  dmap_stat_print(oc->dmap);
  // Only print stat for LLC since that is what we care about
  printf("L3:\n");
  ocache_stat_print(oc->l3);
  // Print verbose
  printf("cc:\n");
  cc_stat_print(oc->cc, 1);
  printf("DRAM:\n");
  dram_stat_print(oc->dram);
  // Generate a file name for dumping
  char filename[256];
  memset(filename, 0x00, sizeof(filename));
  snprintf(filename, sizeof(filename), "dram-stat-dump-%lu.dump", (uint64_t)time(NULL));
  printf("Dumping DRAM stats to file \"%s\"\n", filename);
  dram_stat_dump(oc->dram, filename);
  return;
}

//* main_addr_map_t

main_addr_map_entry_t *main_addr_map_entry_init() {
  main_addr_map_entry_t *entry = (main_addr_map_entry_t *)malloc(sizeof(main_addr_map_entry_t));
  SYSEXPECT(entry != NULL);
  memset(entry, 0x00, sizeof(main_addr_map_entry_t));
  return entry;
}

void main_addr_map_entry_free(main_addr_map_entry_t *entry) {
  free(entry);
  return;
}

main_addr_map_t *main_addr_map_init() {
  main_addr_map_t *addr_map = (main_addr_map_t *)malloc(sizeof(main_addr_map_t));
  SYSEXPECT(addr_map != NULL);
  memset(addr_map, 0x00, sizeof(main_addr_map_t));
  return addr_map;
}

// Free all nodes in the hash table
void main_addr_map_free(main_addr_map_t *addr_map) {
  for(int i = 0;i < MAIN_ADDR_MAP_INIT_COUNT;i++) {
    main_addr_map_entry_t *curr = addr_map->entries[i];
    while(curr != NULL) {
      main_addr_map_entry_t *next = curr->next;
      main_addr_map_entry_free(curr);
      curr = next;
    }
  }
  free(addr_map);
  return;
}

// Addr must be aligned to cache line boundaries
// Note that this function will overwrite entry content if it already exists, which is different
// from dmap_insert()
main_addr_map_entry_t *main_addr_map_insert(
  main_addr_map_t *addr_map, uint64_t addr_1d, uint64_t oid_2d, uint64_t addr_2d) {
  assert((addr_1d & (UTIL_CACHE_LINE_SIZE - 1)) == 0);
  assert((addr_2d & (UTIL_CACHE_LINE_SIZE - 1)) == 0);
  addr_map->insert_count++;
  // Compiler should translate this to bit wise AND
  int h = hash_64(addr_1d) % MAIN_ADDR_MAP_INIT_COUNT;
  main_addr_map_entry_t *entry = addr_map->entries[h];
  while(entry != NULL) {
    if(entry->addr_1d == addr_1d) {
      entry->addr_1d = addr_1d;
      entry->addr_2d = addr_2d;
      entry->oid_2d = oid_2d;
      return entry;
    }
    addr_map->step_count++;
    entry = entry->next;
  }
  // Make a new linked list at this slot
  if(addr_map->entries[h] == NULL) {
    addr_map->list_count++;
  }
  entry = main_addr_map_entry_init();
  entry->addr_1d = addr_1d;
  entry->addr_2d = addr_2d;
  entry->oid_2d = oid_2d;
  entry->next = addr_map->entries[h];
  addr_map->entries[h] = entry;
  // Only increment count here
  addr_map->entry_count++;
  return entry;
}

// Insert translation for a range.
// The inserted entries will be: 
//   (1) Always on the same OID
//   (2) 2D addr will have the same offset as 1D addr, i.e., if [addr_1d, addr_1d + size) spans more than one 
//       lines, then the inserted addr_2d will also span more than one line in the 2D address space
// Input addr_1d and addr_2d must be cache line aligned, but size can be arbitrary
void main_addr_map_insert_range(
  main_addr_map_t *addr_map, uint64_t addr_1d, int size, uint64_t oid_2d, uint64_t addr_2d) {
  assert((addr_1d & (UTIL_CACHE_LINE_SIZE - 1)) == 0);
  assert((addr_2d & (UTIL_CACHE_LINE_SIZE - 1)) == 0);
  assert(size > 0);
  // Number of lines to insert
  int count = (size + UTIL_CACHE_LINE_SIZE - 1) / UTIL_CACHE_LINE_SIZE;
  assert(count > 0);
  for(int i = 0;i < count;i++) {
    main_addr_map_insert(addr_map, addr_1d, oid_2d, addr_2d);
    addr_1d += UTIL_CACHE_LINE_SIZE;
    addr_2d += UTIL_CACHE_LINE_SIZE;
  }
  return;
}

main_addr_map_entry_t *main_addr_map_find(main_addr_map_t *addr_map, uint64_t addr_1d) {
  assert((addr_1d & (UTIL_CACHE_LINE_SIZE - 1)) == 0);
  addr_map->find_count++;
  int h = hash_64(addr_1d) % MAIN_ADDR_MAP_INIT_COUNT;
  main_addr_map_entry_t *entry = addr_map->entries[h];
  while(entry != NULL) {
    if(entry->addr_1d == addr_1d) {
      return entry;
    }
    addr_map->step_count++;
    entry = entry->next;
  }
  return NULL;
}

void main_addr_map_print(main_addr_map_t *addr_map) {
  for(int i = 0;i < MAIN_ADDR_MAP_INIT_COUNT;i++) {
    main_addr_map_entry_t *entry = addr_map->entries[i];
    while(entry != NULL) {
      printf("Addr %lX -> oid %lX addr %lX\n", entry->addr_1d, entry->oid_2d, entry->addr_2d);
      entry = entry->next;
    }
  }
  return;
}

void main_addr_map_conf_print(main_addr_map_t *addr_map) {
  (void)addr_map;
  printf("---------- main_addr_map_t conf ----------\n");
  printf("Init count %d\n", MAIN_ADDR_MAP_INIT_COUNT);
  return;
}

void main_addr_map_stat_print(main_addr_map_t *addr_map) {
  printf("---------- main_addr_map_t stat ----------\n");
  printf("Count entry %d insert %lu find %lu list %lu step %lu\n", 
    addr_map->entry_count, addr_map->insert_count, addr_map->find_count, addr_map->list_count, addr_map->step_count);
  printf("Avg steps %lf\n", 
    (double)addr_map->step_count / (double)(addr_map->insert_count + addr_map->find_count));
  return;
}

//* main_latency_list_t

main_latency_list_t *main_latency_list_init() {
  main_latency_list_t *list = (main_latency_list_t *)malloc(sizeof(main_latency_list_t));
  SYSEXPECT(list != NULL);
  memset(list, 0x00, sizeof(main_latency_list_t));
  list->data = (main_latency_list_entry_t *)malloc(sizeof(main_latency_list_entry_t) * MAIN_LATENCY_LIST_INIT_COUNT);
  SYSEXPECT(list->data != NULL);
  list->capacity = MAIN_LATENCY_LIST_INIT_COUNT;
  list->count = 0;
  return list;
}

void main_latency_list_free(main_latency_list_t *list) {
  free(list->data);
  free(list);
  return;
}

// Append a new value to the list
void main_latency_list_append(main_latency_list_t *list, int op, uint64_t addr, int size, void *data) {
  assert(op == MAIN_READ || op == MAIN_WRITE);
  // Realloc if the list is full
  if(list->count == list->capacity) {
    main_latency_list_entry_t *old_data = list->data;
    list->capacity *= 2;
    list->data = (main_latency_list_entry_t *)malloc(sizeof(main_latency_list_entry_t) * list->capacity);
    SYSEXPECT(list->data != NULL);
    memcpy(list->data, old_data, sizeof(main_latency_list_entry_t) * list->count);
    free(old_data);
  }
  assert(list->count < list->capacity);
  int count = list->count;
  list->data[count].op = op;
  list->data[count].addr = addr;
  list->data[count].size = size;
  // For writes, also log data
  if(op == MAIN_WRITE) {
    memcpy(list->data[count].data, data, size);
  }
  list->count++;
  return;
}

// Returns an element on the given index. This function performs mandatory index check
// to avoid out-of-bound access
main_latency_list_entry_t *main_latency_list_get(main_latency_list_t *list, int index) {
  assert(index >= 0);
  if(index >= list->count) {
    error_exit("List size %d index %d\n", list->count, index);
  }
  return &list->data[index];
}

void main_latency_list_conf_print(main_latency_list_t *list) {
  printf("---------- main_latency_list_t conf ----------\n");
  printf("Init size %d\n", MAIN_LATENCY_LIST_INIT_COUNT);
  (void)list;
  return;
}

void main_latency_list_stat_print(main_latency_list_t *list) {
  printf("---------- main_latency_list_t stat ----------\n");
  printf("Count %d capacity %d reset count %lu\n", list->count, list->capacity, list->reset_count);
  return;
}

//* main_t

// Read configuration file
// main.max_inst_count - The simulation upper bound
// main.logging - Whether all requests are logged
// main.logging_filename - The file name of the log file; Only read if logging = 1
main_param_t *main_param_init(conf_t *conf) {
  main_param_t *param = (main_param_t *)malloc(sizeof(main_param_t));
  SYSEXPECT(param != NULL);
  memset(param, 0x00, sizeof(main_param_t));
  param->max_inst_count = conf_find_uint64_range(conf, "main.max_inst_count", 1, UINT64_MAX, CONF_RANGE | CONF_ABBR);
  param->logging = conf_find_bool_mandatory(conf, "main.logging");
  assert(param->logging == 0 || param->logging == 1);
  if(param->logging == 1) {
    param->logging_filename = strclone(conf_find_str_mandatory(conf, "main.logging_filename"));
  } else {
    param->logging_filename = NULL;
  }
  return param;
}

void main_param_free(main_param_t *param) {
  if(param->logging_filename != NULL) {
    free(param->logging_filename);
  }
  free(param);
  return;
}

void main_param_conf_print(main_param_t *param) {
  printf("---------- main_t param ----------\n");
  printf("Inst count %lu\n", param->max_inst_count);
  printf("Logging %d file %s\n", param->logging, param->logging ? param->logging_filename : "N/A");
  return;
}

// Keys:
//   l1.size, l1.way_count, l1.latency 
//   l2.size, l2.way_count, l2.latency
//   l3.size, l3.way_count, l3.latency
//   cpu.core_count
//   l3.compressed
//   ---
//   dram.bank_count
//   dram.read_latency
//   dram.write_latency
//   ---
//   main.max_inst_count
//   main.logging
//   main.logging_filename
main_t *main_init_from_conf(conf_t *conf) {
  main_t *main = (main_t *)malloc(sizeof(main_t));
  SYSEXPECT(main != NULL);
  memset(main, 0x00, sizeof(main_t));
  main->conf = conf;
  // If not set by upper level function, then allocate a string here
  if(main->conf_filename == NULL) {
    main->conf_filename = (char *)malloc(16);
    SYSEXPECT(main->conf_filename != NULL);
    strcpy(main->conf_filename, "[N/A]");
  }
  // Initialize param
  main->param = main_param_init(conf);
  // Initialize main class components
  main->oc = oc_init(main->conf);
  main->addr_map = main_addr_map_init();
  main->latency_list = main_latency_list_init();
  return main;
}

main_t *main_init(const char *conf_filename) {
  conf_t *conf = conf_init(conf_filename);
  main_t *main = main_init_from_conf(conf);
  // This has been allocated in main_init_from_conf() 
  free(main->conf_filename);
  // Copy file name to the name buffer
  int len = strlen(conf_filename);
  main->conf_filename = (char *)malloc(len + 1);
  SYSEXPECT(main->conf_filename != NULL);
  strcpy(main->conf_filename, conf_filename);
  return main;
}

void main_free(main_t *main) {
  // Free components
  main_latency_list_free(main->latency_list);
  main_addr_map_free(main->addr_map);
  oc_free(main->oc);
  // Free owned classes
  free(main->conf_filename);
  main_param_free(main->param);
  conf_free(main->conf);
  free(main);
  return;
}

void main_sim_begin(main_t *main) {
  // Print conf first
  main_conf_print(main);
  printf("========== simulation begin ==========\n");
  main->finished = 0;
  main->progress = 0;
  // Initialize log file
  if(main->param->logging == 1) {
    main->logging_fp = fopen(main->param->logging_filename, "wb");
    SYSEXPECT(main->logging_fp != NULL);
  } else {
    main->logging_fp = NULL;
  }
  return;
}

void main_sim_end(main_t *main) {
  printf("========== simulation end ==========\n");
  printf("Completed simulation with %lu instructions and %lu cycles (conf max %lu)\n", 
      main->last_inst_count, main->last_cycle_count, main->param->max_inst_count);
  main_stat_print(main);
  // Close logging file pointer
  if(main->logging_fp != NULL) {
    fclose(main->logging_fp);
  }
  // Just terminate here
  exit(0);
  return;
}

// Append log object and data to the opened log file
// Must only be called when logging is enabled
void main_append_log(main_t *main, uint64_t cycle, int op, uint64_t addr, int size, void *data) {
  assert(main->logging_fp != NULL && main->param->logging == 1);
  assert(op == MAIN_READ || op == MAIN_WRITE);
  assert(size > 0 && size < MAIN_MEM_OP_MAX_SIZE); // Must not exceed maximum write size
  main_request_t request;
  request.op = op;
  request.size = size;
  request.cycle = cycle;
  request.addr = addr;
  int ret;
  ret = fwrite(&request, 1, sizeof(main_request_t), main->logging_fp);
  if(ret != (int)sizeof(main_request_t)) {
    error_exit("Error writing main logging object (ret %d expect %d)\n", ret, (int)sizeof(main_request_t));
  }
  ret = fwrite(data, 1, size, main->logging_fp);
  if(ret != size) {
    error_exit("Error writing main logging data (ret %d expect %d)\n", ret, size);
  }
  return;
}

// Called by the simulator to report current inst and cycle count to the main class, such that it
// determines whether the simulation should come to an end
void main_report_progress(main_t *main, uint64_t inst, uint64_t cycle) {
  assert(inst >= main->last_inst_count);
  assert(cycle >= main->last_cycle_count);
  main->last_inst_count = inst;
  main->last_cycle_count = cycle;
  if(inst >= main->param->max_inst_count) {
    main->progress = 100; // Indicating finished
    main->finished = 1;
    // Print stats and exit the program
    main_sim_end(main);
  } else {
    main->progress = (int)(100.0 * (double)inst / (double)main->param->max_inst_count);
    printf("\rProgress: %d%%", main->progress);
  }
  return;
}

void main_bb_sim_finish(main_t *main) {
  int list_count = main_latency_list_get_count(main->latency_list);
  // Force these two to be identical
  if(list_count != main->mem_op_index) {
    error_exit("Latency list size and mem op index mismatch: %d and %d\n", list_count, main->mem_op_index);
  }
  main->mem_op_index = 0;
  main_latency_list_reset(main->latency_list);
  return;
}

// Translating from 1D to 2D
// If the 1D address is not registered in the addr map, then just return without translation with OID = 0
void main_1d_to_2d(main_t *main, uint64_t addr_1d, uint64_t *oid_2d, uint64_t *addr_2d) {
  main_addr_map_entry_t *addr_map_entry = main_addr_map_find(main->addr_map, addr_1d);
  if(addr_map_entry != NULL) {
    *addr_2d = addr_map_entry->addr_2d;
    *oid_2d = addr_map_entry->oid_2d;
  } else {
    // If not 2D address, then just use flat address
    *addr_2d = addr_1d;
    *oid_2d = 0UL;
  }
  return;
}

// Calls main_read and main_write() after preparing for memory operations
// Return OID and addr via argument
uint64_t main_mem_op(main_t *main, uint64_t cycle) {
  int index = main->mem_op_index;
  main->mem_op_index++;
  // This checks bound
  main_latency_list_entry_t *entry = main_latency_list_get(main->latency_list, index);
  uint64_t addr_1d = entry->addr;
  int op = entry->op;
  assert(op == MAIN_READ || op == MAIN_WRITE);
  int size = entry->size;
  void *data = entry->data;
  uint64_t addr_2d, oid_2d;
  // First translate address
  main_1d_to_2d(main, addr_1d, &oid_2d, &addr_2d);
  // Then call coherence controller
  if(op == MAIN_READ) {
    cycle = oc_load(main->oc, 0, cycle, oid_2d, addr_2d, size, NULL);
  } else {
    // Pass data as argument to update dmap
    // This function also updates the dmap
    cycle = oc_store(main->oc, 0, cycle, oid_2d, addr_2d, size, data);
  }
  return cycle;
}

// addr and size need not be aligned, but inserted info is always page (4KB) aligned
// The address should be 2D address, not 1D flat address
void main_update_shape(main_t *main, uint64_t addr, int size, int shape) {
  pmap_insert_range(oc_get_pmap(main->oc), addr, size, shape);
  return;
}

// addr_1d and addr_2d must be cache aligned, otherwise we do not know how to align
// size need not be multiple of 64 bytes, though, but inserted info is always cache line aligned
void main_update_2d_addr(main_t *main, uint64_t addr_1d, int size, uint64_t oid_2d, uint64_t addr_2d) {
  main_addr_map_insert_range(main->addr_map, addr_1d, size, oid_2d, addr_2d);
  return;
}

// Update dmap, bypassing cache herarchy
void main_update_data(main_t *main, uint64_t addr_1d, int size, void *data) {
  uint64_t addr_2d, oid_2d;
  main_1d_to_2d(main, addr_1d, &oid_2d, &addr_2d);
  dmap_write(oc_get_dmap(main->oc), oid_2d, addr_2d, size, data);
  return;
}

void main_zsim_debug_print_all(main_t *main) {
  printf("dmap:\n");
  dmap_print(oc_get_dmap(main->oc));
  printf("pmap:\n");
  pmap_print(oc_get_pmap(main->oc));
  printf("addr map:\n");
  main_addr_map_print(main->addr_map);
  return;
}

void main_conf_print(main_t *main) {
  main_param_conf_print(main->param);
  printf("---------- main_t conf ----------\n");
  // Read from conf file
  main_param_conf_print(main->param);
  printf("conf file name \"%s\"\n", main->conf_filename);
  printf("oc:\n");
  oc_conf_print(main->oc);
  printf("addr map:\n");
  main_addr_map_conf_print(main->addr_map);
  printf("latency list:\n");
  main_latency_list_conf_print(main->latency_list);
  return;
}

void main_stat_print(main_t *main) {
  printf("---------- main_t stat ----------\n");
  printf("oc:\n");
  oc_stat_print(main->oc);
  printf("addr map:\n");
  main_addr_map_stat_print(main->addr_map);
  printf("latency list:\n");
  main_latency_list_stat_print(main->latency_list);
  return;
}

