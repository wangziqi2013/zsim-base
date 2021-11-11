
#include "2DOC.h"
#include "third_party/compression.h"

//* Thesaurus

#ifdef THESAURUS_LSH
#include "Thesaurus_hash_data.cpp"
#else 
inline static uint32_t Thesaurus_hash_data(int32_t *data) {
  return 0;
}
#endif

// Initialize to all-NULL, meaning no LSH is present
dmap_entry_struct_t *Thesaurus_base_table[4096] = {NULL, };

//* SIMD file inclusion

#ifdef INTEL_ISA

#include "BDI_comp_SIMD.cpp"

#endif

//* BDI

// Indexed using BDI types. Note that the first two entries are not used
BDI_param_t BDI_types[8] = {
  {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0},
  {BDI_8_1, 8, 1, 17, 1, 2, 9, 0xFFFFFFFFFFFFFF80UL}, // BDI_8_1 17   1     2
  {BDI_8_2, 8, 2, 25, 1, 3, 9, 0xFFFFFFFFFFFF8000UL}, // BDI_8_2 25   3     3
  {BDI_8_4, 8, 4, 41, 1, 4, 9, 0xFFFFFFFF80000000UL}, // BDI_8_4 41   6     4
  {BDI_4_1, 4, 1, 22, 2, 5, 6, 0x00000000FFFFFF80UL}, // BDI_4_1 22   2     5
  {BDI_4_2, 4, 2, 38, 2, 6, 6, 0x00000000FFFF8000UL}, // BDI_4_2 38   4     6
  {BDI_2_1, 2, 1, 38, 4, 7, 6, 0x000000000000FF80UL}, // BDI_2_1 38   5     7
};

// String names of BDI types; indexed using runtime type
const char *BDI_names[8] = {
  "ZERO", "DUP", "8-1", "8-2", "8-4", "4-1", "4-2", "2-1",
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
  // Whether BDI explicit base has been found (should be the first value not compressed with implicit base)
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
  // 6 different BDI to try
  int iter = (int)(sizeof(BDI_comp_order) / sizeof(int));
  assert(iter == 6); 
  int ret;
  for(int i = 0;i < iter;i++) {
    int type = BDI_comp_order[i];
    // Maps type to param
    BDI_param_t *param = &BDI_types[type];
    assert(param != NULL);
    // This function returns the run time type of the compression scheme
#ifdef INTEL_ISA
    ret = BDI_comp_AVX2(out_buf, in_buf, param, 0);
#else
    ret = BDI_comp_scalar(out_buf, in_buf, param, 0);
#endif
    //printf("i %d type %d ret %d\n", i, type, ret);
    // Use the first one that can actually compress the input
    if(ret != BDI_TYPE_INVALID) {
      assert(ret == type);
      break;
    }
  }
  return ret;
}

// Only return comp size, do not need data after compression
// Returns BDI runtime type (not the macro)
int BDI_get_comp_size(void *in_buf) {
  int iter = (int)(sizeof(BDI_comp_order) / sizeof(int));
  assert(iter == 6); 
  int ret = BDI_TYPE_INVALID;
  BDI_param_t *param = NULL;
  for(int i = 0;i < iter;i++) {
    int type = BDI_comp_order[i];
    // Maps type to param
    param = &BDI_types[type];
    assert(param != NULL);
    // dry_run == 1, out_buf == NULL
#ifdef INTEL_ISA
    ret = BDI_comp_AVX2(NULL, in_buf, param, 1);
#else
    ret = BDI_comp_scalar(NULL, in_buf, param, 1);
#endif
    // Use the first one that can actually compress the input
    // If any of the compression param succeeds, ret will not be BDI_TYPE_INVALID
    if(ret != BDI_TYPE_INVALID) {
      assert(ret == type);
      break;
    }
  }
  // ret will either be INVALID, meaning none of the above params succeeds, or the runtime type
  return ret;
}

void BDI_decomp(void *out_buf, void *in_buf, int type) {
  assert(type >= BDI_TYPE_BEGIN && type < BDI_TYPE_END);
  BDI_param_t *param = &BDI_types[type];
  assert(param != NULL);
#ifdef INTEL_ISA
  BDI_decomp_AVX2(out_buf, in_buf, param);
#else 
  BDI_decomp_scalar(out_buf, in_buf, param);
#endif
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
  return;
}

// Returns compressed size (in bytes) using delta compression (note that delta compression is always doable)
// This function assumes that both are compressed with the same type
// No compressed data is generated, since we only need uncompressed size
int BDI_vertical_comp(void *_base_buf, void *_in_buf, int type) {
  assert(type >= BDI_TYPE_BEGIN && type < BDI_TYPE_END);
  BDI_param_t *param = &BDI_types[type];
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

const char *FPC_type_names[8] = {
  "ZERO", "4b", "8b", "16b", "LOW Z", "HALF WORD", "REPEAT BYTE", "UNCOMP",
};

// This function is limited, as it only handles byte aligned source word (but not necessarily 8-multiple bits)
// Also bits must be less than or equal to 64 bits; src must also be 64 bit aligned
// Returns the new dest_offset
// Note: Higher bits of the last word are undefined. Do not rely on them being cleared
int FPC_pack_bits_normal(void *dest, int dest_offset, uint64_t *src, int bits) {
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


// The reverse of the pack function. This function only handles byte-aligned dest buffer, and only supports
// less than or equal to 64 bit reads
// Note: dest should be pre-initialized to zero, higher bits are not guaranteed to be cleared
int FPC_unpack_bits_normal(uint64_t *dest, void *src, int src_offset, int bits) {
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

#ifdef INTEL_ISA

// Using BMI bit extraction interface
// dest_offset is the bit offset into the dest buffer, which can be arbitrary large
// bits in the number of bits to be appended at dest_offset, which must be <= 64
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

#endif

// Use immediate number (numeric literals) instead of the source
inline static int FPC_pack_bits_imm(void *dest, int dest_offset, uint64_t src, int bits) {
  return FPC_pack_bits(dest, dest_offset, &src, bits);
}

// Returns compressed size in bits
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
        offset = FPC_pack_bits(out_buf, offset, &type, 3);
        offset = FPC_pack_bits(out_buf, offset, &data, 3);
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
    offset = FPC_pack_bits(out_buf, offset, &type, 3);
    offset = FPC_pack_bits(out_buf, offset, &data, FPC_data_bits[type]);
  }
  return offset;
}

// Dry run mode of FPC. Only runs over input data without generating compressed bits
// Returns compressed size in bits
int _FPC_get_comp_size_bits(void *_in_buf, FPC_type_stat_t *stats) {
  uint32_t *in_buf = (uint32_t *)_in_buf;
  int offset = 0;
  int zero_count = 0; // Track zero runs
  int state = FPC_STATE_NORMAL;
  // These two are universally used for writing the three-bit type and the data after it
  uint64_t type;
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
        offset += 6;
        zero_count = 0; // Not necessary but let it be for readability
        state = FPC_STATE_NORMAL;
        stats->zero_run++;
      }
      continue;
    }
    // Type 111: Repeated bytes
    uint8_t *byte_p = (uint8_t *)&curr;
    if(byte_p[0] == byte_p[1] && byte_p[1] == byte_p[2] && byte_p[2] == byte_p[3]) {
      type = 6UL;
      stats->repeated_bytes++;
    } else if((curr & 0xFFFFFFF8) == 0 || (curr & 0xFFFFFFF8) == 0xFFFFFFF8) {
      type = 1UL; // Sign extended 4 bits
      stats->small_value_4_bits++;
    } else if((curr & 0xFFFFFF80) == 0 || (curr & 0xFFFFFF80) == 0xFFFFFF80) {
      type = 2UL; // Sign extended 8 bits
      stats->small_value_8_bits++;
    } else if((curr & 0xFFFF8000) == 0 || (curr & 0xFFFF8000) == 0xFFFF8000) {
      type = 3UL; // Sign extended 16 bits 
      stats->small_value_16_bits++;
    } else if((curr & 0x0000FFFF) == 0) {
      type = 4UL; // Lower bits are zero
      stats->lower_zero++;
    } else if((curr & 0xFF80FF80) == 0 || (curr & 0xFF80FF80) == 0xFF80FF80 || 
              (curr & 0xFF80FF80) == 0xFF800000 || (curr & 0xFF80FF80) == 0x0000FF80) {
      type = 5UL; // Two 16-bit integer compressed to 8 bytes independently
      stats->half_word_to_8_bits++;
    } else {
      type = 7UL; // Uncompressable 32 bits
      stats->uncomp++;
    }
    // Always write 3 bit type and some data according to the lookup table
    assert(type < 8);
    offset += (3 + FPC_data_bits[type]);
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
    offset = FPC_unpack_bits(&type, in_buf, offset, 3);
    assert(type < 8UL);
    offset = FPC_unpack_bits(&_data, in_buf, offset, FPC_data_bits[type]);
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
    offset = FPC_unpack_bits(&type, in_buf, offset, 3);
    assert(type < 8UL);
    offset = FPC_unpack_bits(&_data, in_buf, offset, FPC_data_bits[type]);
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

//* CPACK

const char *CPACK_type_names[6] = {
  "zzzz", "zzzx", "mmmm", "mmmx", "mmxx", "xxxx",
};

int CPACK_search(CPACK_dict_t *dict, uint32_t word, uint64_t *output, CPACK_type_stat_t *type_stat) {
  if(word == 0) {
    // Case zzzz, code 2'b00
    *output = 0;
    type_stat->zzzz++;
    return 2;
  } else if((word & 0xFFFFFF00) == 0) {
    // Case zzzx, code 4'0111 + lowest byte
    *output = (((uint64_t)word & 0xFFUL) << 4) | 0x7UL;
    type_stat->zzzx++;
    return 12;
  }
  assert(dict->count >= 0 && dict->count <= CPACK_DICT_COUNT);
  // Generate the best result
  int min_bits = 32;
  for(int i = 0;i < dict->count;i++) {
    uint32_t data = dict->data[i];
    if(data == word) {
      // case mmmm, code 2'b10 + 4-bit index
      // Special case: This is always the min, so we can return immediately
      *output = ((uint64_t)i << 2) | 0x2UL;
      type_stat->mmmm++;
      return 6;
    } else if((data & 0xFFFFFF00) == (word & 0xFFFFFF00)) {
      // case mmmx, code 4'1011 + 4-bit index + 1-byte low
      if(min_bits > 16) {
        *output = ((word & 0xFF) << 8) | ((uint64_t)i << 4) | 0xBUL;
        min_bits = 16;
      }
    } else if((data & 0xFFFF0000) == (word & 0xFFFF0000)) {
      // case mmxx, code 4'b0011 + 4-bit index + 2-byte low
      if(min_bits > 24) {
        *output = ((word & 0xFFFF) << 8) | ((uint64_t)i << 4) | 0x3UL;
        min_bits = 24;
      }
    }
  }
  if(min_bits != 32) {
    if(min_bits == 16) {
      type_stat->mmmx++;
    } else if(min_bits == 24) {
      type_stat->mmxx++;
    }
    return min_bits;
  }
  // Case xxxx, code 2'b01 + 4 byte
  *output = ((uint64_t)word << 2) | 0x1UL;
  type_stat->xxxx++;
  return 34;
}

int CPACK_comp(void *out_buf, void *_in_buf) {
  uint32_t *in_buf = (uint32_t *)_in_buf;
  int bits = 0;
  CPACK_dict_t dict;
  CPACK_dict_invalidate(&dict);
  CPACK_type_stat_t dummy_stat; // Will not be used
  memset(&dummy_stat, 0x00, sizeof(CPACK_type_stat_t));
  for(int i = 0;i < CPACK_WORD_COUNT;i++) {
    uint64_t output;
    // Find a match
    uint32_t word = *in_buf;
    int ret = CPACK_search(&dict, word, &output, &dummy_stat);
    //printf("ret %d\n", ret);
    FPC_pack_bits(out_buf, bits, &output, ret);
    bits += ret;
    // Insert the word into the dictionary, if word fails zero pattern matching
    // Hack: If it fails the encoded length is 2 or 12
    // Full match also does not insert
    if(ret != 2 && ret != 12) {
      CPACK_dict_insert(&dict, word);
    }
    in_buf++;
  }
  return bits;
}

void CPACK_decomp(void *_out_buf, void *in_buf) {
  uint32_t *out_buf = (uint32_t *)_out_buf;
  int bits = 0;
  CPACK_dict_t dict;
  CPACK_dict_invalidate(&dict);
  for(int i = 0;i < CPACK_WORD_COUNT;i++) {
    uint64_t output = 0UL;
    uint32_t word;
    // This is cleared if the recovered word is zero matched
    int need_insert = 1;
    // 34 bits is the maximum compressed size of a word
    FPC_unpack_bits(&output, in_buf, bits, 34);
    switch((int)output & 0x3) {
      case 0: bits += 2; word = 0; need_insert = 0; break;
      case 1: bits += 34; word = (uint32_t)(output >> 2); break;
      case 2: bits += 6; word = dict.data[(int)(output >> 2) & 0xF]; break;
      default: {
        switch((int)output & 0xF) {
          case 0x3: {
            int index = ((int)output >> 4) & 0xF;
            word = (dict.data[index] & 0xFFFF0000) | (((int)output >> 8) & 0x0000FFFF);
            bits += 24;
          } break;
          case 0x7: {
            word = (uint32_t)((output >> 4) & 0xFF);
            bits += 12;
            need_insert = 0;
          } break;
          case 0xB: {
            int index = ((int)output >> 4) & 0xF;
            word = (dict.data[index] & 0xFFFFFF00) | (((int)output >> 8) & 0x000000FF);
            bits += 16;
          } break;
          default: {
            error_exit("Undefined case during CPACK decompression\n");
          } break;
        }
      } break;
    }
    *out_buf = word;
    // Insert the word into the dictionary, if the decoded word is not matched
    if(need_insert == 1) {
      CPACK_dict_insert(&dict, word);
    }
    out_buf++;
  }
  return;
}

int _CPACK_get_comp_size_bits(void *_in_buf, CPACK_type_stat_t *type_stat) {
  uint32_t *in_buf = (uint32_t *)_in_buf;
  int bits = 0;
  CPACK_dict_t dict;
  CPACK_dict_invalidate(&dict);
  for(int i = 0;i < CPACK_WORD_COUNT;i++) {
    uint64_t output;
    uint32_t word = *in_buf;
    int ret = CPACK_search(&dict, word, &output, type_stat);
    (void)output;
    bits += ret;
    if(ret != 2 && ret != 12) {
      CPACK_dict_insert(&dict, word);
    }
    in_buf++;
  }
  return bits;
}

void CPACK_print_compressed(void *in_buf) {
  int bits = 0;
  CPACK_dict_t dict;
  CPACK_dict_invalidate(&dict);
  for(int i = 0;i < CPACK_WORD_COUNT;i++) {
    uint64_t output = 0UL;
    uint32_t word;
    int need_insert = 1;
    int code;
    FPC_unpack_bits(&output, in_buf, bits, 34);
    switch((int)output & 0x3) {
      case 0: bits += 2; word = 0; need_insert = 0; code = 0; break;
      case 1: bits += 34; word = (uint32_t)(output >> 2); code = 1; break;
      case 2: bits += 6; word = dict.data[(int)(output >> 2) & 0xF]; code = 2; break;
      default: {
        switch((int)output & 0xF) {
          case 0x3: {
            int index = ((int)output >> 4) & 0xF;
            word = (dict.data[index] & 0xFFFF0000) | (((int)output >> 8) & 0x0000FFFF);
            bits += 24;
            code = 0x3;
          } break;
          case 0x7: {
            word = (uint32_t)((output >> 4) & 0xFF);
            bits += 12;
            need_insert = 0;
            code = 0x7;
          } break;
          case 0xB: {
            int index = ((int)output >> 4) & 0xF;
            word = (dict.data[index] & 0xFFFFFF00) | (((int)output >> 8) & 0x000000FF);
            bits += 16;
            code = 0xB;
          } break;
          default: {
            error_exit("Undefined case during CPACK decompression\n");
          } break;
        }
      } break;
    }
    printf("Index %d code 0x%X word %u (0x%X)\n", i, code, word, word);
    if(need_insert == 1) {
      CPACK_dict_insert(&dict, word);
    }
  }
  return;
}

//* MBD

int MBD_bits[11] = {
  2, 6, 34, // zero match uncomp
  12, 16, 20, 24, // Delta 4, 8, 12, 16
  8, 12, 16, 20,  // Small 4, 8, 12, 16
};

// MBD.runahead
int MBD_RUNAHEAD = 2;
// MBD.throughput
int MBD_THROUGHPUT = 2;

void MBD_init(conf_t *conf) {
  int found = 0;
  int value = 0;
  // Read runahead
  found = conf_find_int32(conf, "MBD.runahead", &value);
  if(found == 1) {
    if(value < 0 || value > 8) {
      error_exit("Invalid value of MBD.runahead: %d (expecting [0, 8])\n", value);
    }
    MBD_RUNAHEAD = value;
  }
  // Read throughput
  found = conf_find_int32(conf, "MBD.throughput", &value);
  if(found == 1) {
    if(value < 0 || value > 8) {
      error_exit("Invalid value of MBD.throughput: %d (expecting [0, 8])\n", value);
    }
    MBD_THROUGHPUT = value;
  }
  return;
}

#ifdef MBD_DICT_LRU

// Move index to LRU; Called on compression and decompression when an entry is hit for comp / used for decomp
void MBD_update_LRU(MBD_dict_t *dict, int index) {
  assert(index >= 0 && index < dict->count);
  if(dict->head_index == index) {
    return;
  } else if(dict->tail_index == index) {
    assert(dict->next[index] == -1);
    int prev_index = dict->prev[index];
    assert(dict->next[prev_index] == index);
    // Remove from LRU
    dict->next[prev_index] = -1;
    dict->tail_index = prev_index;
  } else {
    // index has successor and predecessor
    int prev_index = dict->prev[index];
    int next_index = dict->next[index];
    dict->next[prev_index] = next_index;
    dict->prev[next_index] = prev_index;
  }
  // Insert into MRU
  dict->prev[dict->head_index] = index;
  dict->next[index] = dict->head_index;
  dict->prev[index] = -1;
  dict->head_index = index;
  return;
}

int MBD_dict_insert(MBD_dict_t *dict, uint32_t data) {
  // If there is already one, then just promote it to LRU
  for(int i = 0;i < dict->count;i++) {
    if(dict->data[i] == data) {
      MBD_update_LRU(dict, i);
      return i;
    }
  }
  dict->insert_count++;
  if(dict->count < MBD_DICT_COUNT) {
    // Allocate index as dict->count and move it to MRU
    int index = dict->count;
    dict->count++;
    dict->data[index] = data;
    assert(dict->prev[dict->head_index] == -1);
    assert(dict->next[dict->tail_index] == -1);
    dict->prev[dict->head_index] = index;
    dict->next[index] = dict->head_index;
    dict->prev[index] = -1;
    dict->head_index = index;
  } else {
    assert(dict->count == MBD_DICT_COUNT);
    // Evict LRU and move it to MRU
    int index = dict->tail_index;
    assert(index != -1);
    assert(dict->next[index] == -1);
    assert(dict->prev[index] != -1);
    // Evict and overwrite data
    dict->data[index] = data;
    int prev_index = dict->prev[index];
    assert(dict->next[prev_index] == index);
    // Remove from LRU
    dict->next[prev_index] = -1;
    dict->tail_index = prev_index;
    // Insert into MRU
    dict->prev[dict->head_index] = index;
    dict->next[index] = dict->head_index;
    dict->prev[index] = -1;
    dict->head_index = index;
  }
  return index;
}

#else 

// Just a dummy function that does nothing
void MBD_update_LRU(MBD_dict_t *dict, int index) {
  (void)dict; (void)index;
  return;
}

int MBD_dict_insert(MBD_dict_t *dict, uint32_t data) {
  dict->insert_count++;
  if(dict->next_slot_index == MBD_DICT_COUNT) {
    dict->next_slot_index = 1; // Does not overwrite zero
  }
  int index = dict->next_slot_index++;
  dict->data[index] = data;
  if(dict->count != MBD_DICT_COUNT) {
    dict->count++;
  }
  return index;
}

#endif

// Given a 32-bit word, find the shortest encoding that preserves its value
// Return value is # of bits
// If could not find it then return 0
int MBD_find_shortest_encoding(uint32_t word) {
  uint32_t masked_delta;
  masked_delta = word & ~0x7U;
  if(masked_delta == 0 || masked_delta == ~0x7U) {
    return 4;
  }
  masked_delta = word & ~0x7FU;
  if(masked_delta == 0 || masked_delta == ~0x7FU) {
    return 8;
  }
  masked_delta = word & ~0x7FFU;
  if(masked_delta == 0 || masked_delta == ~0x7FFU) {
    return 12;
  } 
  masked_delta = word & ~0x7FFFU;
  if(masked_delta == 0 || masked_delta == ~0x7FFFU) {
    return 16;
  }
  return 0; 
}

int MBD_search(MBD_dict_t *dict, uint32_t word, uint64_t *output) {
  if(word == 0) {
    // All zero, code 2'b00
    *output = 0;
    dict->zero_count++;
    dict->word_type = MBD_ZERO;
    return 2;
  }
  assert(dict->count >= 1 && dict->count <= MBD_DICT_COUNT);
  // Generate the best result
  int min_bits = 34;
  int min_index;        // Minimum ref entry, if it is a delta
  uint32_t min_word;    // Minimum encoding word
  int is_min_delta = 1; // Whether minimum word is a delta or small value
  int bits;
  for(int i = 0;i < dict->count;i++) {
    uint32_t data = dict->data[i];
    if(data == word) {
      //printf("Hit dict index %d (count %d)\n", i, dict->count);
      // Direct match, code 2'b01 + 3/4-bit index
      // Special case: This is always the min, so we can return immediately
      *output = ((uint64_t)i << 2) | 0x1UL;
      dict->match_count++;
      dict->word_type = MBD_MATCH;
      MBD_update_LRU(dict, i);
      return 2 + MBD_DICT_INDEX_BITS;
    }
    uint32_t delta = word - data;
    bits = MBD_find_shortest_encoding(delta);
    if(bits != 0) {
      int delta_bits = bits + 4 + MBD_DICT_INDEX_BITS;
      if(delta_bits < min_bits) {
        min_bits = delta_bits;
        min_index = i;
        min_word = delta;
      }
    }
  } // loop through valid words in the dict
  if(min_bits != 34) {
    if(is_min_delta == 1) {
      switch(min_bits) {
        case 8 + MBD_DICT_INDEX_BITS: {
          *output = ((min_word & 0xF) << (MBD_DICT_INDEX_BITS + 4)) | ((uint64_t)min_index << 4) | 0x3UL;
          dict->delta_size_dist[0]++; 
          dict->word_type = MBD_DELTA_4;
        } break;
        case 12 + MBD_DICT_INDEX_BITS: {
          *output = ((min_word & 0xFFU) << (MBD_DICT_INDEX_BITS + 4)) | ((uint64_t)min_index << 4) | 0x7UL;
          dict->delta_size_dist[1]++; 
          dict->word_type = MBD_DELTA_8;
        } break;
        case 16 + MBD_DICT_INDEX_BITS: {
          *output = ((min_word & 0xFFFU) << (MBD_DICT_INDEX_BITS + 4)) | ((uint64_t)min_index << 4) | 0xBUL;
          dict->delta_size_dist[2]++; 
          dict->word_type = MBD_DELTA_12;
        } break;
        case 20 + MBD_DICT_INDEX_BITS: {
          *output = ((min_word & 0xFFFFU) << (MBD_DICT_INDEX_BITS + 4)) | ((uint64_t)min_index << 4) | 0xFUL;
          dict->delta_size_dist[3]++; 
          dict->word_type = MBD_DELTA_16;
        } break;
        default: assert(0); break;
      }
    }
    MBD_update_LRU(dict, min_index);
    return min_bits;
  }
  // Uncompressable, code 2'b10 + 4 byte
  *output = ((uint64_t)word << 2) | 0x2UL;
  //printf("Miss dict count %d\n", dict->count);
  dict->uncomp_count++;
  dict->word_type = MBD_UNCOMP;
  return 34;
}

// This function processes a block consisting of 32-bit words. Input size must be aligned
int __MBD_comp(void *out_buf, void *_in_buf, int size, MBD_dict_t *dict) {
  uint32_t *in_buf = (uint32_t *)_in_buf;
  int bits = 0;
  assert(size > 0 && size % 4 == 0);
  int count = size >> 2;
  for(int i = 0;i < count;i++) {
    uint64_t output;
    // Find a match
    uint32_t word = *in_buf;
    int ret = MBD_search(dict, word, &output);
    //printf("ret %d\n", ret);
    FPC_pack_bits(out_buf, bits, &output, ret);
    bits += ret;
    // Return value 2 means that it is zero
    if(ret != 2) {
      MBD_dict_insert(dict, word);
    }
    //for(int i = 0;i < dict.count;i++) printf("0x%X, ", dict.data[i]);
    //putchar('\n');
    in_buf++;
  }
  return bits;
}

void _MBD_decomp(void *_out_buf, void *in_buf, int size) {
  uint32_t *out_buf = (uint32_t *)_out_buf;
  int bits = 0;
  MBD_dict_t dict;
  MBD_dict_invalidate(&dict);
  assert(size > 0 && size % 4 == 0);
  int count = size >> 2;
  for(int i = 0;i < count;i++) {
    uint64_t output = 0UL;
    uint32_t word;
    // This is cleared if the recovered word is zero matched
    int need_insert = 1;
    // 34 bits is the maximum compressed size of a word
    FPC_unpack_bits(&output, in_buf, bits, 34);
    switch((int)output & 0x3) {
      case 0: bits += 2; word = 0; need_insert = 0; break;
      case 1: {
        int index = ((int)output >> 2) & (MBD_DICT_COUNT - 1); 
        word = dict.data[index];
        bits += (2 + MBD_DICT_INDEX_BITS);
        MBD_update_LRU(&dict, index);
      } break;
      case 2: bits += 34; word = (uint32_t)(output >> 2); break;
      default: {
        switch((int)output & 0xF) {
          case 0x3: {
            int index = ((int)output >> 4) & (MBD_DICT_COUNT - 1); 
            uint32_t delta = (uint32_t)(output >> (4 + MBD_DICT_INDEX_BITS)) & 0xFU;
            word = dict.data[index] + ((delta & 0x8) ? (delta | ~0xFU) : delta);
            bits += (8 + MBD_DICT_INDEX_BITS);
            MBD_update_LRU(&dict, index);
          } break;
          case 0x7: {
            int index = ((int)output >> 4) & (MBD_DICT_COUNT - 1); 
            uint32_t delta = (uint32_t)(output >> (4 + MBD_DICT_INDEX_BITS)) & 0xFFU;
            word = dict.data[index] + ((delta & 0x80) ? (delta | ~0xFFU) : delta);
            bits += (12 + MBD_DICT_INDEX_BITS);
            MBD_update_LRU(&dict, index);
          } break;
          case 0xB: {
            int index = ((int)output >> 4) & (MBD_DICT_COUNT - 1); 
            uint32_t delta = (uint32_t)(output >> (4 + MBD_DICT_INDEX_BITS)) & 0xFFFU;
            word = dict.data[index] + ((delta & 0x800) ? (delta | ~0xFFFU) : delta);
            bits += (16 + MBD_DICT_INDEX_BITS);
            MBD_update_LRU(&dict, index);
          } break;
          case 0xF: {
            int index = ((int)output >> 4) & (MBD_DICT_COUNT - 1); 
            uint32_t delta = (uint32_t)(output >> (4 + MBD_DICT_INDEX_BITS)) & 0xFFFFU;
            word = dict.data[index] + ((delta & 0x8000) ? (delta | ~0xFFFFU) : delta);
            bits += (20 + MBD_DICT_INDEX_BITS);
            MBD_update_LRU(&dict, index);
          } break;
          default: {
            error_exit("Undefined case during CPACK decompression\n");
          } break;
        }
      } break;
    }
    *out_buf = word;
    // Insert the word into the dictionary, if the decoded word is not matched
    if(need_insert == 1) {
      MBD_dict_insert(&dict, word);
    }
    out_buf++;
  }
  return;
}

// This function processes a block consisting of 32-bit words. Input size must be aligned
// This function allows passing a customized dictionary object, which can be used as a pre-filled
// dict or for collecting stats
// zero_count counts the number of zeros in the input stream; ignored if NULL
int __MBD_get_comp_size_bits(void *_in_buf, int size, MBD_dict_t *dict) {
  uint32_t *in_buf = (uint32_t *)_in_buf;
  int bits = 0;
  assert(size > 0 && size % 4 == 0);
  int count = size >> 2;
  for(int i = 0;i < count;i++) {
    uint64_t output;
    // Find a match
    uint32_t word = *in_buf;
    int ret = MBD_search(dict, word, &output);
    (void)output;
    bits += ret;
    if(ret != 2) {
      // Do not insert zero into the dict (ret == 2 indicating zero)
      MBD_dict_insert(dict, word);
    }
    in_buf++;
  }
  // Hardcode the cycle count to eight without zero optimization
  dict->cycle_count = 8;
  return bits;
}

void _MBD_print_compressed(void *in_buf, int size) {
  int bits = 0;
  MBD_dict_t dict;
  MBD_dict_invalidate(&dict);
  assert(size > 0 && size % 4 == 0);
  int count = size >> 2;
  for(int i = 0;i < count;i++) {
    uint64_t output = 0UL;
    uint32_t word;
    int code;
    // This is cleared if the recovered word is zero matched
    int need_insert = 1;
    // 34 bits is the maximum compressed size of a word
    FPC_unpack_bits(&output, in_buf, bits, 34);
    switch((int)output & 0x3) {
      case 0: bits += 2; word = 0; need_insert = 0; code = 0; break;
      case 1: {
        int index = (int)(output >> 2) & (MBD_DICT_COUNT - 1);
        word = dict.data[index]; 
        bits += (2 + MBD_DICT_INDEX_BITS);
        MBD_update_LRU(&dict, index);
        code = 1; 
      } break;
      case 2: bits += 34; word = (uint32_t)(output >> 2); code = 2; break;
      default: {
        switch((int)output & 0xF) {
          case 0x3: {
            int index = ((int)output >> 4) & (MBD_DICT_COUNT - 1); 
            uint32_t delta = (uint32_t)(output >> (4 + MBD_DICT_INDEX_BITS)) & 0xFU;
            word = dict.data[index] + ((delta & 0x8) ? (delta | ~0xFU) : delta);
            bits += (8 + MBD_DICT_INDEX_BITS);
            MBD_update_LRU(&dict, index);
            code = 0x3; 
          } break;
          case 0x7: {
            int index = ((int)output >> 4) & (MBD_DICT_COUNT - 1); 
            uint32_t delta = (uint32_t)(output >> (4 + MBD_DICT_INDEX_BITS)) & 0xFFU;
            word = dict.data[index] + ((delta & 0x80) ? (delta | ~0xFFU) : delta);
            bits += (12 + MBD_DICT_INDEX_BITS);
            MBD_update_LRU(&dict, index);
            code = 0x7; 
          } break;
          case 0xB: {
            int index = ((int)output >> 4) & (MBD_DICT_COUNT - 1); 
            uint32_t delta = (uint32_t)(output >> (4 + MBD_DICT_INDEX_BITS)) & 0xFFFU;
            word = dict.data[index] + ((delta & 0x800) ? (delta | ~0xFFFU) : delta);
            bits += (16 + MBD_DICT_INDEX_BITS);
            MBD_update_LRU(&dict, index);
            code = 0xB; 
          } break;
          case 0xF: {
            int index = ((int)output >> 4) & (MBD_DICT_COUNT - 1); 
            uint32_t delta = (uint32_t)(output >> (4 + MBD_DICT_INDEX_BITS)) & 0xFFFFU;
            word = dict.data[index] + ((delta & 0x8000) ? (delta | ~0xFFFFU) : delta);
            bits += (20 + MBD_DICT_INDEX_BITS);
            MBD_update_LRU(&dict, index);
            code = 0xF; 
          } break;
          default: {
            error_exit("Undefined case during CPACK decompression\n");
          } break;
        }
      } break;
    }
    // Insert the word into the dictionary, if the decoded word is not matched
    if(need_insert == 1) {
      MBD_dict_insert(&dict, word);
    }
    printf("Index %d code 0x%X word %u (0x%X)\n", i, code, word, word);
  }
  return;
}

//* MBD_opt0

int __MBD_opt0_get_comp_size_bits(void *_in_buf, int size, MBD_dict_t *dict) {
  uint32_t *in_buf = (uint32_t *)_in_buf;
  int bits = 0;
  assert(size > 0 && size % 4 == 0);
  int count = size >> 2;
  // Number of non-zero words in the current cycle
  int non_zero_count = 0;
  // This is relative to currently decompressed line, not considering the two cycles run-ahead
  int current_cycle = 0;
  for(int i = 0;i < count;i++) {
    uint64_t output;
    // Find a match
    uint32_t word = *in_buf;
    int ret = MBD_search(dict, word, &output);
    (void)output;
    // Zeros will not be inserted into the dict, and will not be in the output stream
    if(dict->word_type != MBD_ZERO) {
      bits += ret;
      non_zero_count++;
      // Each two non-zero words will take one cycle (2 words / cycle throughput without zeros)
      if(non_zero_count == MBD_OPT0_THROUGHPUT) {
        current_cycle++;
        non_zero_count = 0;
      }
      MBD_dict_insert(dict, word);
    }
    in_buf++;
  }
  //printf("non-z count %d\n", non_zero_count);
  if(non_zero_count != 0) {
    current_cycle++;
  }
  dict->cycle_count = current_cycle;
  // 16 is the size of the zero optimization mask
  return bits + 16;
}

//* MBD_opt1

// Note that this returns the actual number of bits, with zero optimization
// cycles already have the runahead added
int __MBD_opt1_get_comp_size_bits(void *_in_buf, int size, MBD_dict_t *dict, int options) {
  uint32_t *in_buf = (uint32_t *)_in_buf;
  int bits = 0;
  assert(size > 0 && size % 4 == 0);
  int count = size >> 2;
  // Number of non-zero words in the current cycle
  int non_zero_count = 0;
  // This is relative to currently decompressed line, not considering the two cycles run-ahead
  int current_cycle = 0;
  for(int i = 0;i < count;i++) {
    uint64_t output;
    uint32_t word = *in_buf;
    // This will set word_type as the type of code word it will generate
    int ret = MBD_search(dict, word, &output);
    // Zeros will not be inserted into the dict, and will not be in the output stream
    if(dict->word_type != MBD_ZERO) {
      // Only update cycle for reference block
      if(MBD_OPTION_IS_REF(options)) {
        int insert_index = MBD_dict_insert(dict, word);
        assert(insert_index >= 1 && insert_index < MBD_DICT_COUNT);
        dict->cycle[insert_index] = current_cycle;
      } else {
        // Get the index that hits the dict, if it is a dict hit
        int dict_hit_index;
        int source_cycle;
        if(dict->word_type != MBD_UNCOMP) {
          if(dict->word_type == MBD_MATCH) {
            // Exact match: 3/4-bit index + 2'b10
            dict_hit_index = (int)(output >> 2) & ((1 << MBD_DICT_INDEX_BITS) - 1);
          } else {
            // Deltas: 3/4-bit index + 4'bxxxx
            dict_hit_index = (int)(output >> 4) & ((1 << MBD_DICT_INDEX_BITS) - 1);
          }
          assert(dict_hit_index >= 0 && dict_hit_index < MBD_DICT_COUNT);
          // The cycle that the dict entry to be referred to is available
          // -1 means it is always ready (generated by the current process)
          source_cycle = dict->cycle[dict_hit_index];
        } else {
          // Uncompressed word, just skip forwarding detection
          source_cycle = -1;
        } 
        // If hits zero then does not matter because zeroth element is hardwired
        if(dict_hit_index != 0 && source_cycle != -1) {
          // A forward from ref to target happens
          dict->forward_count++;
          // We use the same cycle scale as the ref block's cycle
          // If the source is available in the future of the drain (i.e., source cycle > drain cycle)
          int cycle_diff = (source_cycle + 1) - (current_cycle + MBD_OPT1_RUNAHEAD);
          if(cycle_diff > 0) {
            dict->stalled_cycle_count += cycle_diff;
            dict->forward_stalled_count++;
            current_cycle += cycle_diff;
            // Also reset the non-zero count, since as advanted to a new cycle, and the new cycle 
            // just processed the current word (++ will be performed below, so set it to zero)
            non_zero_count = 0;
          }
        }
        // Only insert after resolving the source entry (insert_index may be identical to dict_hit_index which
        // will incur inaccuracy in the simulation)
        int insert_index = MBD_dict_insert(dict, word);
        // For target block, the entry is always available, so it is -1
        dict->cycle[insert_index] = -1;
      }
      bits += ret;
      non_zero_count++;
      // Each two non-zero words will take one cycle (2 words / cycle throughput without zeros)
      if(non_zero_count == MBD_OPT1_THROUGHPUT) {
        current_cycle++;
        non_zero_count = 0;
      }
    }
    in_buf++;
  }
  //printf("non-z count %d\n", non_zero_count);
  if(non_zero_count != 0) {
    current_cycle++;
  }
  if(MBD_OPTION_IS_REF(options) == 0) {
    current_cycle += MBD_OPT1_RUNAHEAD;
  }
  dict->cycle_count = current_cycle;
  // 16 is the size of the zero optimization mask
  return bits + 16;
}

//* MBD_opt2

// Cycles already have the BDI latency added
int __MBD_opt2_get_comp_size_bits(void *_in_buf, int size, MBD_dict_t *dict, int options) {
  int bits;
  if(MBD_OPTION_IS_REF(options)) {
    // This function returns BDI type or invalid type
    int bdi_type = BDI_get_comp_size(_in_buf);
    assert(bdi_type == BDI_TYPE_INVALID || (bdi_type >= BDI_TYPE_BEGIN && bdi_type < BDI_TYPE_END));
    // BDI types store byte size
    bits = (bdi_type == BDI_TYPE_INVALID) ? \
      ((int)UTIL_CACHE_LINE_SIZE * 8) : BDI_types[bdi_type].compressed_size * 8;
    // This trains the dict, but we do not use its compressed size
    __MBD_opt0_get_comp_size_bits(_in_buf, size, dict);
    // Ref block compressed and decompressed with BDI latency
    dict->cycle_count = BDI_DECOMP_LATENCY;
  } else {
    // Compress with the given dictionary using opt0; This will also set cycle_count
    bits = __MBD_opt0_get_comp_size_bits(_in_buf, size, dict);
    dict->cycle_count += BDI_DECOMP_LATENCY;
  }
  return bits;
}

//* MBD_opt3

int MBD_opt3_search(MBD_dict_t *dict, uint32_t word, uint64_t *output) {
  if(word == 0) {
    *output = 0;
    dict->zero_count++;
    dict->word_type = MBD_ZERO;
    return 2;
  }
  assert(dict->count >= 0 && dict->count <= MBD_DICT_COUNT);
  int min_bits = 34;
  int min_index = 0;    // Minimum ref entry, if it is a delta
  uint32_t min_word;    // Minimum encoding word
  int is_min_delta = 1; // Whether minimum word is a delta or small value
  int bits;
  // Test for small values (small values have higher priority than deltas on the same bits)
  bits = MBD_find_shortest_encoding(word);
  if(bits != 0) {
    min_bits = bits + 4;
    min_word = word;
    is_min_delta = 0;
  }
  for(int i = 0;i < dict->count;i++) {
    uint32_t data = dict->data[i];
    if(data == word) {
      *output = ((uint64_t)i << 2) | 0x1UL;
      dict->match_count++;
      dict->word_type = MBD_MATCH;
      MBD_update_LRU(dict, i);
      return 2 + MBD_DICT_INDEX_BITS;
    }
    uint32_t delta = word - data;
    bits = MBD_find_shortest_encoding(delta);
    if(bits != 0) {
      int delta_bits = bits + 4 + MBD_DICT_INDEX_BITS;
      if(delta_bits < min_bits) {
        min_bits = delta_bits;
        min_index = i;
        min_word = delta;
      }
    }
  } // loop through valid words in the dict
  if(min_bits != 34) {
    if(is_min_delta == 1) {
      switch(min_bits) {
        case 8 + MBD_DICT_INDEX_BITS: {
          *output = ((min_word & 0xF) << (MBD_DICT_INDEX_BITS + 4)) | ((uint64_t)min_index << 4) | 0x3UL;
          dict->delta_size_dist[0]++; 
          dict->word_type = MBD_DELTA_4;
        } break;
        case 12 + MBD_DICT_INDEX_BITS: {
          *output = ((min_word & 0xFFU) << (MBD_DICT_INDEX_BITS + 4)) | ((uint64_t)min_index << 4) | 0x7UL;
          dict->delta_size_dist[1]++; 
          dict->word_type = MBD_DELTA_8;
        } break;
        case 16 + MBD_DICT_INDEX_BITS: {
          *output = ((min_word & 0xFFFU) << (MBD_DICT_INDEX_BITS + 4)) | ((uint64_t)min_index << 4) | 0xBUL;
          dict->delta_size_dist[2]++; 
          dict->word_type = MBD_DELTA_12;
        } break;
        case 20 + MBD_DICT_INDEX_BITS: {
          *output = ((min_word & 0xFFFFU) << (MBD_DICT_INDEX_BITS + 4)) | ((uint64_t)min_index << 4) | 0xFUL;
          dict->delta_size_dist[3]++; 
          dict->word_type = MBD_DELTA_16;
        } break;
        default: assert(0); break;
      }
    } else {
      switch(min_bits) {
        case 8: {
          *output = ((min_word & 0xF) << 4) | 0x0UL;
          dict->small_size_dist[0]++; 
          dict->word_type = MBD_SMALL_4;
        } break;
        case 12: {
          *output = ((min_word & 0xFFU) << 4) | 0x4UL;
          dict->small_size_dist[1]++; 
          dict->word_type = MBD_SMALL_8;
        } break;
        case 16: {
          *output = ((min_word & 0xFFFU) << 4) | 0x8UL;
          dict->small_size_dist[2]++; 
          dict->word_type = MBD_SMALL_12;
        } break;
        case 20: {
          *output = ((min_word & 0xFFFFU) << 4) | 0xCUL;
          dict->small_size_dist[3]++; 
          dict->word_type = MBD_SMALL_16;
        } break;
        default: assert(0); break;
      }
    }
    MBD_update_LRU(dict, min_index);
    return min_bits;
  }
  *output = ((uint64_t)word << 2) | 0x2UL;
  dict->uncomp_count++;
  dict->word_type = MBD_UNCOMP;
  return 34;
}

int __MBD_opt3_get_comp_size_bits(void *_in_buf, int size, MBD_dict_t *dict, int options) {
  uint32_t *in_buf = (uint32_t *)_in_buf;
  int bits = 0;
  assert(size > 0 && size % 4 == 0);
  int count = size >> 2;
  // Number of non-zero words in the current cycle
  int non_zero_count = 0;
  // This is relative to currently decompressed line, not considering the two cycles run-ahead
  int current_cycle = 0;
  for(int i = 0;i < count;i++) {
    uint64_t output;
    // Find a match
    uint32_t word = *in_buf;
    int ret = MBD_opt3_search(dict, word, &output);
    // Zeros will not be inserted into the dict, and will not be in the output stream
    if(dict->word_type != MBD_ZERO) {
      // Only update cycle for reference block
      if(MBD_OPTION_IS_REF(options)) {
        int insert_index = MBD_dict_insert(dict, word);
        assert(insert_index >= 0 && insert_index < MBD_DICT_COUNT);
        dict->cycle[insert_index] = current_cycle;
        dict->types[insert_index] = dict->word_type;
      } else {
        // Get the index that hits the dict, if it is a dict hit
        int dict_hit_index;
        int source_cycle = -1;
        int source_type = -1;
        switch(dict->word_type) {
          case MBD_MATCH: {
            dict_hit_index = (int)(output >> 2) & ((1 << MBD_DICT_INDEX_BITS) - 1);
            assert(dict_hit_index >= 0 && dict_hit_index < MBD_DICT_COUNT);
            source_cycle = dict->cycle[dict_hit_index];
            source_type = dict->types[dict_hit_index];
          } break;
          case MBD_DELTA_4: 
          case MBD_DELTA_8:
          case MBD_DELTA_12:
          case MBD_DELTA_16: {
            // Delta 4/8/12/16: 3/4-bit index + 4'bxxxx
            dict_hit_index = (int)(output >> 4) & ((1 << MBD_DICT_INDEX_BITS) - 1);
            assert(dict_hit_index >= 0 && dict_hit_index < MBD_DICT_COUNT);
            // This can still be -1 to indicate self-forwarding
            source_cycle = dict->cycle[dict_hit_index];
            source_type = dict->types[dict_hit_index];
          } break;
          case MBD_UNCOMP:
          case MBD_SMALL_4: 
          case MBD_SMALL_8:
          case MBD_SMALL_12:
          case MBD_SMALL_16: break; // No dependency, source cycle and type are set above
          default: assert(0); break;
        }
        // If hits zero then does not matter because zeroth element is hardwired
        if(source_cycle != -1) {
          // A forward from ref to target happens
          dict->forward_count++;
          assert(source_type != -1);
          if(source_type == MBD_UNCOMP) {
            dict->forward_uncomp_count++;
          }
          // We use the same cycle scale as the ref block's cycle
          // If the source is available in the future of the drain (i.e., source cycle > drain cycle)
          int cycle_diff = (source_cycle + 1) - (current_cycle + MBD_OPT1_RUNAHEAD);
          if(cycle_diff > 0) {
            dict->stalled_cycle_count += cycle_diff;
            dict->forward_stalled_count++;
            current_cycle += cycle_diff;
            // Also reset the non-zero count, since as advanted to a new cycle, and the new cycle 
            // just processed the current word (++ will be performed below, so set it to zero)
            non_zero_count = 0;
          }
        }
        // Only insert after resolving the source entry (insert_index may be identical to dict_hit_index which
        // will incur inaccuracy in the simulation)
        int insert_index = MBD_dict_insert(dict, word);
        // For target block, the entry is always available, so it is -1
        dict->cycle[insert_index] = -1;
        dict->types[insert_index] = dict->word_type;
      }
      bits += ret;
      non_zero_count++;
      // Each two non-zero words will take one cycle (2 words / cycle throughput without zeros)
      if(non_zero_count == MBD_OPT3_THROUGHPUT) {
        current_cycle++;
        non_zero_count = 0;
      }
    }
    in_buf++;
  }
  //printf("non-z count %d\n", non_zero_count);
  if(non_zero_count != 0) {
    current_cycle++;
  }
  // Add runahead cycle for target block
  if(MBD_OPTION_IS_REF(options) == 0) {
    current_cycle += MBD_OPT3_RUNAHEAD;
  }
  dict->cycle_count = current_cycle;
  // 16 is the size of the zero optimization mask
  return bits + 16;
}

//* MESI_t

const char *MESI_state_names[MESI_STATE_END] = {
  "I", "S", "E", "M",
};

const char *MESI_cache_names[MESI_CACHE_END] = {
  "L1", "L2", "LLC",
};

#ifndef NDEBUG
// This function uses assertion such that it can be caught by the debugger when it fails
void MESI_entry_verify(MESI_entry_t *entry) {
  // If L1 and/or L2 are on exclusive state, the sharer vector must be singular
  if(entry->l1_state == MESI_STATE_M || entry->l1_state == MESI_STATE_E) {
    assert(MESI_entry_count_sharer(entry, MESI_CACHE_L1) == 1);
    // If L1 is exclusive, L2 must also be exclusive
    assert(MESI_entry_get_exclusive_sharer(entry, MESI_CACHE_L1) == \
           MESI_entry_get_exclusive_sharer(entry, MESI_CACHE_L2));
  }
  if(entry->l2_state == MESI_STATE_M || entry->l2_state == MESI_STATE_E) {
    assert(MESI_entry_count_sharer(entry, MESI_CACHE_L2) == 1);
  } else if(entry->l2_state == MESI_STATE_S) {
    assert(entry->l1_state == MESI_STATE_S || entry->l1_state == MESI_STATE_I);
  } else {
    assert(entry->l2_state == MESI_STATE_I);
    assert(entry->l1_state == MESI_STATE_I);
  }
  return;
}
#else 
void MESI_entry_verify(MESI_entry_t *entry) { (void)entry; }
#endif

// Print sharer as a comma separated list, without new line at the end, no brackets either
// It prints nothing if the list is empty
void MESI_entry_print_sharer_list(MESI_entry_t *entry, int cache) {
  assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2);
  uint64_t sharer_list = entry->sharers[cache];
  for(int i = 0;i < MESI_MAX_SHARER;i++) {
    uint64_t mask = 0x1UL << i;
    if(sharer_list & mask) {
      printf("%d", i);
      // If there are higher bits still, then print a comma and space
      if((sharer_list & ((~(mask - 1)) << 1)) != 0UL) {
        printf(", ");
      }
    }
  }
  return;
}

// Print the content of the MESI entry in one line
void MESI_entry_print(MESI_entry_t *entry) {
  printf("MESI state L1 %s L2 %s L3 %s sharers L1 [", 
    MESI_state_names[entry->l1_state], MESI_state_names[entry->l2_state], MESI_state_names[entry->llc_state]);
  MESI_entry_print_sharer_list(entry, MESI_CACHE_L1);
  printf("] (0x%lX count %d) L2 [", entry->l1_sharer, MESI_entry_count_sharer(entry, MESI_CACHE_L1));
  MESI_entry_print_sharer_list(entry, MESI_CACHE_L2);
  printf("] (0x%lX count %d)", entry->l2_sharer, MESI_entry_count_sharer(entry, MESI_CACHE_L2));
  putchar('\n');
  return;
}

//* dmap_t

// This function will be called frequently, so do not init data
dmap_entry_t *dmap_entry_init() {
  dmap_entry_t *entry = (dmap_entry_t *)malloc(sizeof(dmap_entry_t));
  SYSEXPECT(entry != NULL);
  // Init all fields including data
  memset(entry, 0x00, sizeof(dmap_entry_t));
  // Hopefully this will be optimized out by the compiler, but let's put it here for safety
  MESI_entry_invalidate(&entry->MESI_entry);
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
// If the entry is just created, new_entry is set to 1; Otherwise zero
dmap_entry_t *_dmap_insert(dmap_t *dmap, uint64_t oid, uint64_t addr, int *new_entry) {
  int index = (int)dmap_gen_index(dmap, oid, addr);
  assert(index >= 0 && index < DMAP_INIT_COUNT);
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0UL);
  *new_entry = 0;
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
  *new_entry = 1;
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
// The "all_zero" argument is an optional flag for returning whether the line contains all zero
// It can be set to NULL, if don't care
int dmap_find_compressed(dmap_t *dmap, uint64_t oid, uint64_t addr, void *out_buf, int *all_zero) {
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0UL);
  if(all_zero != NULL) {
    *all_zero = 0; // Fix Valgrind warning
  }
  dmap_entry_t *entry = dmap_find(dmap, oid, addr);
  if(entry == NULL) {
    return BDI_TYPE_NOT_FOUND;
  }
  // Type can be BDI_TYPE_INVALID
  int type = BDI_comp(out_buf, entry->data);
  // type 2 is BDI_8_1, which all zero lines will be compressed to
  if(type == 2 && all_zero != NULL) {
    *all_zero = 1;
    for(int i = 0;i < 8;i++) {
      if(((uint64_t *)entry->data)[i] != 0x0UL) {
        *all_zero = 0;
        break;
      }
    }
  }
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
// The page address must be cache block aligned. The function computes page number
pmap_entry_t *pmap_insert(pmap_t *pmap, uint64_t addr, int shape) {
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0UL);
  assert(shape >= OCACHE_SHAPE_BEGIN && shape < OCACHE_SHAPE_END);
  // Mask off lower bits
  addr = UTIL_GET_PAGE_ADDR(addr);
  //printf("Insert 0x%lX\n", addr);
  // Most queries should return non-NULL for this
  pmap_entry_t *entry = dmap_find(pmap, PMAP_OID, addr);
  if(entry != NULL) {
    if(entry->shape != shape) {
      error_exit("Conflicting shapes on addr 0x%lX (before %d requested %d)\n", addr, entry->shape, shape);
    }
    return entry;
  }
  //printf("Insert 0x%lX\n", addr);
  // Initialize an entry by setting its shape
  entry = dmap_insert(pmap, PMAP_OID, addr);
  entry->shape = shape;
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
    pmap_entry_t *entry = pmap_insert(pmap, start_addr, shape);
    assert(entry->shape == shape);
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
  printf("Count %d pmap count %d data size %lu\n", dmap->count, dmap->pmap_count, 
    dmap_get_count(dmap) * UTIL_CACHE_LINE_SIZE);
  printf("queries %lu iters %lu (avg. %f)\n", 
    dmap->query_count, dmap->iter_count, (double)dmap->iter_count / dmap->query_count);
  return;
}

//* bcache_t

bcache_t *bcache_init(int line_count, int way_count, int shape) {
  int sz = sizeof(bcache_t) + sizeof(bcache_entry_t) * line_count;
  bcache_t *bcache = (bcache_t *)malloc(sz);
  SYSEXPECT(bcache != NULL);
  memset(bcache, 0x00, sz);
  for(int i = 0;i < line_count;i++) {
    bcache->data[i].lru = 0UL;
  }
  bcache->line_count = line_count;
  bcache->way_count = way_count;
  bcache->set_count = line_count / way_count;
  assert(shape >= OCACHE_SHAPE_BEGIN && shape < OCACHE_SHAPE_END);
  bcache->shape = shape;
  if(shape == OCACHE_SHAPE_NONE) {
    error_exit("OCACHE_SHAPE_NONE does not need the bcache\n");
  } else if(popcount_int32(bcache->set_count) != 1) {
    error_exit("Set count must be a power of two (see %d)\n", bcache->set_count);
  }
  bcache->set_bits = popcount_int32(bcache->set_count - 1);
  // Mask first, shift second
  if(shape == OCACHE_SHAPE_4_1) {
    // OID must be multiple of four, so rshift two bits
    bcache->oid_shift = 2;
    bcache->oid_mask = ((0x1UL << bcache->set_bits) - 1) << 2;
    bcache->addr_shift = 0;
    bcache->addr_mask = ((0x1UL << bcache->set_bits) - 1) << UTIL_CACHE_LINE_BITS;
  } else if(shape == OCACHE_SHAPE_2_2) {
    bcache->oid_shift = 1;
    bcache->oid_mask = ((0x1UL << bcache->set_bits) - 1) << 1;
    bcache->addr_shift = UTIL_CACHE_LINE_BITS;
    bcache->addr_mask = ((0x1UL << bcache->set_bits) - 1) << (UTIL_CACHE_LINE_BITS + 1);
  } else if(shape == OCACHE_SHAPE_1_4) {
    // This is special case; addresses are 4-block aligned
    bcache->oid_shift = 0;
    bcache->oid_mask = ((0x1UL << bcache->set_bits) - 1);
    bcache->addr_shift = UTIL_CACHE_LINE_BITS + 2;
    bcache->addr_mask = ((0x1UL << bcache->set_bits) - 1) << (UTIL_CACHE_LINE_BITS + 2);
  } 
  // LRU starts at 1, using 0 as invalid mark
  bcache->lru_counter = 1UL;
  return bcache;
}

void bcache_free(bcache_t *bcache) {
  free(bcache);
  return;
}

// This function does not update stat. This function updates LRU of the hit entry.
// lru_entry is undefined on a hit; On a miss it points to the LRU entry
bcache_entry_t *bcache_lookup(bcache_t *bcache, uint64_t oid, uint64_t addr, bcache_entry_t **lru_entry) {
  int index = bcache_gen_index(bcache, oid, addr);
  assert(index >= 0 && index < bcache->set_count);
  uint64_t min_lru = -1UL;
  bcache_entry_t *curr = bcache->data + index * bcache->way_count;
  for(int i = 0;i < bcache->way_count;i++) {
    // LRU being 0 means invalid
    if(curr->lru != 0UL && curr->oid == oid && curr->addr == addr) {
      curr->lru = bcache->lru_counter++; // Update LRU
      return curr;
    }
    // This will also return invalid entry
    if(curr->lru < min_lru) {
      min_lru = curr->lru;
      *lru_entry = curr; // Update LRU entry
    }
    curr++;
  }
  return NULL;
}

// Returns 1 if read hit, 0 if read miss
int bcache_read(bcache_t *bcache, uint64_t oid, uint64_t addr) {
  bcache->read_count++;
  bcache_entry_t *lru_entry = NULL;
  // lru_entry is undefined on a hit
  bcache_entry_t *entry = bcache_lookup(bcache, oid, addr, &lru_entry);
  int hit = (entry != NULL);
  if(hit == 0) {
    assert(lru_entry != NULL);
    // Read miss, silent eviction
    lru_entry->lru = bcache->lru_counter++;
    lru_entry->oid = oid;
    lru_entry->addr = addr;
    bcache->read_miss_count++;
  } else {
    assert(hit == 1);
    bcache->read_hit_count++;
  }
  return hit;
}

void bcache_refresh_stat(bcache_t *bcache) {
  bcache->valid_line_count = 0UL;
  bcache_entry_t *curr = bcache->data;
  for(int i = 0;i < bcache->line_count;i++) {
    if(curr->lru != 0UL) {
      bcache->valid_line_count++;
    }
    curr++;
  }
  return;
}

void bcache_conf_print(bcache_t *bcache) {
  printf("---------- bcache_t conf ----------\n");
  printf("Lines %d ways %d sets %d (size %d bytes, 64B entry) shape %d (%s) set bits %d\n", 
    bcache->line_count, bcache->way_count, bcache->set_count, bcache->line_count * 64,
    bcache->shape, 
    ocache_shape_names[bcache->shape], bcache->set_bits);
  printf("OID mask 0x%lX shift %d ADDR mask 0x%lX shift %d\n",
    bcache->oid_mask, bcache->oid_shift, bcache->addr_mask, bcache->addr_shift);
  return;
}

void bcache_stat_print(bcache_t *bcache) {
  printf("---------- bcache_t stat ----------\n");
  printf("Reads %lu hit %lu miss %lu (hit rate %.2lf%%)\n",
    bcache->read_count, bcache->read_hit_count, bcache->read_miss_count,
    100.0 * (double)bcache->read_hit_count / (double)(bcache->read_miss_count + bcache->read_hit_count));
  bcache_refresh_stat(bcache);
  printf("Valid line count %lu\n", bcache->valid_line_count);
  return;
}

//* ocache_stat_snapshot_t

ocache_stat_snapshot_t *ocache_stat_snapshot_init() {
  ocache_stat_snapshot_t *snapshot = (ocache_stat_snapshot_t *)malloc(sizeof(ocache_stat_snapshot_t));
  SYSEXPECT(snapshot != NULL);
  memset(snapshot, 0x00, sizeof(ocache_stat_snapshot_t));
  return snapshot;
} 

void ocache_stat_snapshot_free(ocache_stat_snapshot_t *snapshot) {
  free(snapshot);
  return;
}

//* ocache_t

const char *ocache_shape_names[4] = {
  "None", "4_1", "1_4", "2_2",
};

const char *ocache_MBD_type_names[4] = {
  "MBDv0", "MBDv2", "MBDv4", "MBD_Thesaurus",
};

ocache_t *ocache_init(int size, int data_way_count, int tag_way_count) {
  // First allocate the cache control part, and then slots using realloc
  ocache_t *ocache = (ocache_t *)malloc(sizeof(ocache_t));
  SYSEXPECT(ocache != NULL);
  // This also clears stats
  memset(ocache, 0x00, sizeof(ocache_t));
  // Report error if args are invalid; Call this before using arguments
  ocache_init_param(ocache, size, data_way_count, tag_way_count);
  // Use tag line count
  int alloc_size = sizeof(ocache_t) + sizeof(ocache_entry_t) * ocache->tag_line_count;
  // The control part is preserved
  ocache = (ocache_t *)realloc(ocache, alloc_size);
  SYSEXPECT(ocache != NULL);
  // All LRU and state bits will be set to zero
  memset(ocache->data, 0x00, sizeof(ocache_entry_t) * ocache->tag_line_count);
  for(int i = 0;i < ocache->tag_line_count;i++) {
    ocache_entry_inv(&ocache->data[i]);
  }
  ocache->level = OCACHE_LEVEL_NONE;
  // LRU entry has this zero, so normal entry should always have one
  ocache->lru_counter = 1UL;
  // By default, ocache operates as a sector cache
  ocache->insert_cb = &ocache_insert_sb_cb;
  return ocache;
}

void ocache_free(ocache_t *ocache) {
  if(ocache->name != NULL) {
    free(ocache->name);
  }
  // Free snapshots, if any
  ocache_stat_snapshot_t *curr = ocache->stat_snapshot_head;
  while(curr != NULL) {
    ocache_stat_snapshot_t *next = curr->next; 
    ocache_stat_snapshot_free(curr);
    curr = next;
  }
  free(ocache);
  return;
}

// This is how we specify a cache: giving its total size in bytes, and number of ways
// other params are automatically derived
// Latency is configured elsewhere
// This function sets:
//   size, line_count, data_way_count, tag_way_count, set_count, size_bits, indices[]
void ocache_init_param(ocache_t *ocache, int size, int data_way_count, int tag_way_count) {
  if(size <= 0 || data_way_count < 1 || tag_way_count < 1) {
    error_exit("Illegal size/data way/tag way combination: %d/%d/%d\n", 
      size, data_way_count, tag_way_count);
  }
  ocache->size = size;
  ocache->data_way_count = data_way_count;
  ocache->tag_way_count = tag_way_count;
  // Number of bytes in a set's storage, use data way count
  // This is implicitly for data, not for set, since it is in bytes
  ocache->set_size = (int)UTIL_CACHE_LINE_SIZE * data_way_count; 
  if(size % UTIL_CACHE_LINE_SIZE != 0) {
    error_exit("Size must be a multiple of %d (see %lu)\n", size, UTIL_CACHE_LINE_SIZE);
  }
  ocache->data_line_count = size / UTIL_CACHE_LINE_SIZE;
  // This computes data store size, so use data way count
  if(ocache->data_line_count % ocache->data_way_count != 0) {
    error_exit("Cache line count must be a multiple of data ways (%d %% %d == 0)\n",
      ocache->data_line_count, ocache->data_way_count);
  }
  // Same, line count is derived from data way count, so use the same
  // This is the same for data and tag, so only one variable
  ocache->set_count = ocache->data_line_count / ocache->data_way_count;
  ocache->tag_line_count = ocache->set_count * tag_way_count;
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
  if(ocache->name != NULL) {
    error_exit("Cache \"%s\" has already been named\n", ocache->name);
  }
  ocache->name = strclone(name);
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

void ocache_set_insert_type(ocache_t *ocache, const char *name) {
  if(streq(name, "sb") == 1) {
    ocache->insert_cb = ocache_insert_sb_cb;
  } else if(streq(name, "seg") == 1) {
    ocache->insert_cb = ocache_insert_seg_cb;
  } else {
    error_exit("Unknown ocache_t insert type: \"%s\"\n", name);
  }
  return;
}

// Sets the compression type with a string type name
// Valid values are: "BDI", "None"
void ocache_set_compression_type(ocache_t *ocache, const char *name) {
  // Only allow setting it once
  if(ocache->get_compressed_size_cb != NULL) {
    error_exit("Compression type has already been set\n");
  }
  if(streq(name, "BDI") == 1) {
    ocache_set_get_compressed_size_cb(ocache, ocache_get_compressed_size_BDI_cb);
    ocache->access_hit_cb = ocache_access_hit_BDI_cb;
  } else if(streq(name, "BDI_third_party") == 1) {
    ocache_set_get_compressed_size_cb(ocache, ocache_get_compressed_size_BDI_third_party_cb);
    ocache->access_hit_cb = ocache_access_hit_BDI_cb;
  } else if(streq(name, "FPC") == 1) {
    ocache_set_get_compressed_size_cb(ocache, ocache_get_compressed_size_FPC_cb);
    ocache->access_hit_cb = ocache_access_hit_FPC_cb;
  } else if(streq(name, "FPC_third_party") == 1) {
    ocache_set_get_compressed_size_cb(ocache, ocache_get_compressed_size_FPC_third_party_cb);
    ocache->access_hit_cb = ocache_access_hit_FPC_cb;
  } else if(streq(name, "CPACK") == 1) {
    ocache_set_get_compressed_size_cb(ocache, ocache_get_compressed_size_CPACK_cb);
    ocache->access_hit_cb = ocache_access_hit_CPACK_cb;
  } else if(streq(name, "MBD") == 1 || streq(name, "MBDv0") == 1) {
    ocache_set_get_compressed_size_cb(ocache, ocache_get_compressed_size_MBD_cb);
    ocache->access_hit_cb = ocache_access_hit_MBD_cb;
    ocache->MBD_type = OCACHE_MBD_V0;
  } else if(streq(name, "MBDv2") == 1) {
    ocache_set_get_compressed_size_cb(ocache, ocache_get_compressed_size_MBD_cb);
    ocache->access_hit_cb = ocache_access_hit_MBD_cb;
    ocache->MBD_type = OCACHE_MBD_V2;
  } else if(streq(name, "MBDv4") == 1) {
    ocache_set_get_compressed_size_cb(ocache, ocache_get_compressed_size_MBD_cb);
    ocache->access_hit_cb = ocache_access_hit_MBD_cb;
    ocache->MBD_type = OCACHE_MBD_V4;
  } else if(streq(name, "MBD_Thesaurus") == 1) {
#ifndef THESAURUS_LSH
    error_exit("Macro THESAURUS_LSH is not defined, Thesaurus is disabled\n");
#endif
    ocache_set_get_compressed_size_cb(ocache, ocache_get_compressed_size_MBD_cb);
    ocache->access_hit_cb = ocache_access_hit_MBD_cb;
    ocache->MBD_type = OCACHE_MBD_Thesaurus;
  } else if(streq(name, "None") == 1) {
    ocache_set_get_compressed_size_cb(ocache, ocache_get_compressed_size_None_cb);
    ocache->access_hit_cb = ocache_access_hit_None_cb;
  } else {
    error_exit("Unknown ocache compression type: \"%s\"\n", name);
  }
  return;
}

// Sets MBD optimization type; This is orthogonal to the MBD inter-block comp type
// The optimization type is ignored if incompatible
void ocache_set_MBD_opt_type(ocache_t *ocache, const char *name) {
  if(streq(name, "None") == 1) {
    ocache->MBD_opt_type = OCACHE_MBD_OPT_NONE;
  } else if(streq(name, "opt0") == 1 || streq(name, "OPT0") == 1) {
    ocache->MBD_opt_type = OCACHE_MBD_OPT0;
  } else if(streq(name, "opt1") == 1 || streq(name, "OPT1") == 1) {
    ocache->MBD_opt_type = OCACHE_MBD_OPT1;
  } else if(streq(name, "opt2") == 1 || streq(name, "OPT2") == 1) {
    ocache->MBD_opt_type = OCACHE_MBD_OPT2;
  } else if(streq(name, "opt3") == 1 || streq(name, "OPT3") == 1) {
    ocache->MBD_opt_type = OCACHE_MBD_OPT3;
  } else {
    error_exit("Unknown MBD opt type: \"%s\"\n", name);
  }
  return;
}

// Set evict opt type from a string
void ocache_set_evict_opt_type(ocache_t *ocache, const char *name) {
  if(streq(name, "None") == 1) {
    ocache->evict_opt_type = OCACHE_EVICT_OPT_NONE;
  } else if(streq(name, "opt0") == 1 || streq(name, "OPT0") == 1) {
    ocache->evict_opt_type = OCACHE_EVICT_OPT0;
  } else if(streq(name, "opt1") == 1 || streq(name, "OPT1") == 1) {
    ocache->evict_opt_type = OCACHE_EVICT_OPT1;
  } else {
    error_exit("Unknown evict opt type: \"%s\"\n", name);
  }
  return;
}

void ocache_init_bcache(ocache_t *ocache, int line_count, int way_count, const char *shape_str) {
  int shape;
  if(line_count <= 0) {
    error_exit("Invalid line_count for bcache (see %d)\n", line_count);
  } else if(way_count <= 0) {
    error_exit("Invalid way_count for bcache (see %d)\n", way_count);
  } else if(line_count % way_count != 0) {
    error_exit("Line count must be a multiple of way count (%d and %d)\n", line_count, way_count);
  }
  // Parse shape_str
  if(streq(shape_str, "1_4") == 1) {
    shape = OCACHE_SHAPE_1_4;
  } else if(streq(shape_str, "4_1") == 1) {
    shape = OCACHE_SHAPE_4_1;
  } else if(streq(shape_str, "2_2") == 1) {
    shape = OCACHE_SHAPE_2_2;
  } else if(streq(shape_str, "None") == 1) {
    shape = OCACHE_SHAPE_NONE;
  } else {
    error_exit("Invalid shape for bcache (see \"%s\")\n", shape_str);
  }
  ocache->bcache = bcache_init(line_count, way_count, shape);
  return;
}

// Generate SB base address with implicit OID being zero
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
// It generates the base address (bottom-left) of the sb given the OID and tag. OID must be aligned to the 
// given shape.
uint64_t ocache_gen_addr_with_oid(ocache_t *ocache, uint64_t oid, uint64_t tag, uint64_t set_id, int shape) {
  switch(shape) {
    case OCACHE_SHAPE_4_1: assert((oid & 0x3) == 0); oid >>= 2; break;
    case OCACHE_SHAPE_2_2: assert((oid & 0x1) == 0); oid >>= 1; break;
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
  //assert(ocache->use_shape != 0 || shape == OCACHE_SHAPE_NONE);
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
  // Use tag way count since we iterate over tags
  ocache_entry_t *entry = ocache->data + ocache->tag_way_count * index;
  assert(entry >= ocache->data && entry < ocache->data + ocache->tag_line_count);
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
      // Use tag way count since this is tag offset
      printf("entry ptr %p offset (index) %d OID 0x%lX addr 0x%lX set %d\n", 
        entry, (int)(entry - ocache->data), oid, addr, 
        (int)(entry - ocache->data) / ocache->tag_way_count);
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
  //assert(shape != OCACHE_SHAPE_NONE);
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
    case OCACHE_SHAPE_NONE: {
      *oid_base = oid;
      *addr_base = addr;
      *index = 0;
    } break;
    default: assert(0); break;
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

// This function performs multi-purpose lookup
// If scan_all is one, then this function fills candidates arraym LRU entry and hit entry (if hits). This is usually
// called for performing a write
// Otherwise the function returns by only setting hit_entry and state
// If update_lru is set, this function will update the LRU of the hit entry once a hit is found; Otherwise no LRU
// will be updated
// Note that scan_all == 1 and update_lru == 1 is illegal combination since there can be potentially more than one
// entry that gets "hit", so the semantics is unclear
// The combination scan_all == 0 and update_lru == 0 is used for probing without side effects, e.g., debugging
// The combination scan_all == 1 and update_lru == 0 is used for write probe
// The combination scan_all == 0 and update_lru == 1 is used for regular lookup / clean eviction
void _ocache_lookup(
  ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, int update_lru, int scan_all, ocache_op_result_t *result) {
  assert(shape >= OCACHE_SHAPE_BEGIN && shape < OCACHE_SHAPE_END);
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0); // Address must be aligned
  assert(ocache->use_shape == 1 || shape == OCACHE_SHAPE_NONE);
  // If scan_all is 1, then update_lru must be zero
  assert(scan_all == 0 || update_lru == 0);
  // The candidate array is filled even if the result is hit
  result->cand_count = 0;
  result->state = OCACHE_MISS;
  result->hit_entry = NULL;
  result->lru_entry = NULL;
  result->total_aligned_size = 0; // This is used by segmented ocache
  // These two tracks the LRU in the current set
  uint64_t lru_min = -1UL;
  ocache_entry_t *lru_entry = NULL;
  ocache_entry_t *entry = ocache_get_set_begin(ocache, oid, addr, shape);
  // Use tag way count
  for(int i = 0;i < ocache->tag_way_count;i++) {
    if(entry->shape == OCACHE_SHAPE_NONE) {
      // Case 1: Normal hit on an uncompressed line
      if(entry->oid == oid && entry->addr == addr && ocache_entry_is_valid(entry, 0)) {
        // At most one hit per lookup, as a design invariant
        assert(result->state == OCACHE_MISS);
        assert(shape == OCACHE_SHAPE_NONE); // Requested shape must be consistent with actual shape
        result->state = OCACHE_HIT_NORMAL;
        result->hit_entry = entry;
        result->hit_index = 0;
        // For read a hit is sufficient
        if(scan_all == 0) {
          if(update_lru == 1) {
            ocache_update_entry_lru(ocache, entry);
          }
          return;
        }
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
          assert(result->state == OCACHE_MISS);
          assert(shape == entry->shape); // Requested shape must be consistent with actual shape
          result->state = OCACHE_HIT_COMPRESSED;
          result->hit_entry = entry;
          result->hit_index = hit_index;
          // Same as above
          if(scan_all == 0) {
            if(update_lru == 1) {
              ocache_update_entry_lru(ocache, entry);
            }
            return;
          }
        }
        // Case 2 and Case 3: In both cases we push the entry and hit index as a potential candidate
        result->candidates[result->cand_count] = entry;
        result->indices[result->cand_count] = hit_index;
        result->cand_count++;
        assert(result->cand_count <= 4);
      }
    }
    // Update LRU information (LRU entry can be a candidate)
    if(entry->lru < lru_min) {
      lru_min = entry->lru;
      lru_entry = entry;
    }
    // In all cases, use the total aligned size field of the entry which is maintained with size set
    result->total_aligned_size += entry->total_aligned_size;
    entry++;
  }
  // Always return valid LRU entry of the set, even on compressed hits, since it can be that none of these
  // compressed SB will fit the block, so we will evict the one, which is the LRU
  result->lru_entry = lru_entry;
  return;
}

// Helper function called by ocache_insert for better readability
// Line to be inserted is uncompressed
static void ocache_insert_helper_uncompressed(
  ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, int dirty, 
  ocache_op_result_t *insert_result, ocache_op_result_t *lookup_result) {
  (void)shape; // Maintain consistency of interface
  int state = lookup_result->state;
  assert(state == OCACHE_HIT_NORMAL || state == OCACHE_MISS);
  // This is the entry that needs to be updated after the insert
  ocache_entry_t *entry = NULL;
  if(lookup_result->state == OCACHE_HIT_NORMAL) {
    // Case 2: Hit uncompressed line
    ocache->insert_sb_hit_count++;
    assert(lookup_result->hit_entry != NULL);
    assert(lookup_result->hit_entry->shape == OCACHE_SHAPE_NONE);
    assert(lookup_result->cand_count == 0);
    entry = lookup_result->hit_entry;
    insert_result->state = OCACHE_SUCCESS; // Do not need eviction
  } else {
    // Case 1.1.1 Miss uncompressed line (evicted line may still be compressed)
    ocache->insert_sb_miss_count++;
    ocache_entry_t *lru_entry = lookup_result->lru_entry;
    assert(lru_entry != NULL);
    // Note that we must use this function to determine whether it is an invalid entry
    if(ocache_entry_is_all_invalid(lru_entry) == 1) {
      assert(lru_entry->lru == 0);
      insert_result->state = OCACHE_SUCCESS; // Invalid entry miss
    } else {
      assert(lru_entry->lru > 0);
      insert_result->state = OCACHE_EVICT;   // Eviction
      // Copy LRU entry to the insert result
      ocache_entry_dup(&insert_result->insert_evict_entries[0], lru_entry);
      // Always evict exactly one for sector cache
      insert_result->insert_evict_count = 1;
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
  insert_result->insert_entry = entry;
  insert_result->insert_index = 0;
  return;
}

// None compression type, always stores uncompressed data
int ocache_get_compressed_size_None_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape) {
  (void)ocache; (void)oid; (void)addr; (void)shape;
  return (int)UTIL_CACHE_LINE_SIZE;
}

int ocache_get_compressed_size_FPC_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape) {
  assert(ocache->dmap != NULL); (void)shape;
  ocache->comp_attempt_count++;
  ocache->comp_attempt_size_sum += UTIL_CACHE_LINE_SIZE;
  dmap_entry_t *entry = dmap_find(ocache->dmap, oid, addr);
  if(entry == NULL) {
    error_exit("Entry for FPC compression is not found\n");
  }
  int FPC_bits = _FPC_get_comp_size_bits(entry->data, &ocache->FPC_type_stat);
  if(FPC_bits >= (int)UTIL_CACHE_LINE_SIZE * 8) {
    ocache->FPC_fail_count++;
    ocache->FPC_uncomp_size_sum += UTIL_CACHE_LINE_SIZE;
    return UTIL_CACHE_LINE_SIZE;
  }
  int FPC_bytes = (FPC_bits + 7) / 8;
  ocache->FPC_success_count++;
  ocache->FPC_before_size_sum += UTIL_CACHE_LINE_SIZE;
  ocache->FPC_after_size_sum += FPC_bytes;
  assert(FPC_bytes > 0 && FPC_bytes <= 64);
  // Update statistics for size classes. Note we must minus one to make value domain [0, 64)
  ocache->FPC_size_counts[(FPC_bytes - 1) / 8]++;
  return FPC_bytes;
}

int ocache_get_compressed_size_FPC_third_party_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape) {
  assert(ocache->dmap != NULL); (void)shape;
  ocache->comp_attempt_count++;
  ocache->comp_attempt_size_sum += UTIL_CACHE_LINE_SIZE;
  dmap_entry_t *entry = dmap_find(ocache->dmap, oid, addr);
  if(entry == NULL) {
    error_exit("Entry for FPC compression is not found\n");
  }
  // No metadata to pass for FPC
  int FPC_bytes = GeneralCompress(entry->data, COMPRESSION_FPC, NULL);
  if(FPC_bytes >= (int)UTIL_CACHE_LINE_SIZE) {
    ocache->FPC_fail_count++;
    ocache->FPC_uncomp_size_sum += UTIL_CACHE_LINE_SIZE;
    return UTIL_CACHE_LINE_SIZE;
  }
  ocache->FPC_success_count++;
  ocache->FPC_before_size_sum += UTIL_CACHE_LINE_SIZE;
  ocache->FPC_after_size_sum += FPC_bytes;
  assert(FPC_bytes > 0 && FPC_bytes <= 64);
  // Update statistics for size classes. Note we must minus one to make value domain [0, 64)
  ocache->FPC_size_counts[(FPC_bytes - 1) / 8]++;
  return FPC_bytes;
}

int ocache_get_compressed_size_CPACK_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape) {
  assert(ocache->dmap != NULL); (void)shape;
  ocache->comp_attempt_count++;
  ocache->comp_attempt_size_sum += UTIL_CACHE_LINE_SIZE;
  dmap_entry_t *entry = dmap_find(ocache->dmap, oid, addr);
  if(entry == NULL) {
    error_exit("Entry for CPACK compression is not found\n");
  }
  int bits = _CPACK_get_comp_size_bits(entry->data, &ocache->CPACK_type_stat);
  if(bits >= (int)UTIL_CACHE_LINE_SIZE * 8) {
    ocache->CPACK_fail_count++;
    ocache->CPACK_uncomp_size_sum += UTIL_CACHE_LINE_SIZE;
    return UTIL_CACHE_LINE_SIZE;
  }
  int bytes = (bits + 7) / 8;
  ocache->CPACK_success_count++;
  ocache->CPACK_before_size_sum += UTIL_CACHE_LINE_SIZE;
  ocache->CPACK_after_size_sum += bytes;
  assert(bytes > 0 && bytes <= 64);
  // Update statistics for size classes. Note we must minus one to make value domain [0, 64)
  ocache->CPACK_size_counts[(bytes - 1) / 8]++;
  return bytes;
}

int ocache_get_compressed_size_MBD_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape) {
  assert(ocache->dmap != NULL); (void)shape;
  ocache->comp_attempt_count++;
  ocache->comp_attempt_size_sum += UTIL_CACHE_LINE_SIZE;
  dmap_entry_t *entry = dmap_find(ocache->dmap, oid, addr);
  if(entry == NULL) {
    error_exit("Entry for MBD compression is not found\n");
  }
  // We use our own dictionary object for collecting stats
  MBD_dict_t dict;
  // OPT3 initializes the dictionary differently
  if(ocache->MBD_opt_type != OCACHE_MBD_OPT3) {
    MBD_dict_invalidate(&dict);
  } else {
    MBD_opt3_dict_invalidate(&dict);
  }
  uint64_t base_addr = addr;
  if(ocache->MBD_type == OCACHE_MBD_V4) {
    base_addr = addr & (~0x3UL << UTIL_CACHE_LINE_BITS);
  } else if(ocache->MBD_type == OCACHE_MBD_V2) {
    base_addr = addr & (~0x1UL << UTIL_CACHE_LINE_BITS);
  } else if(ocache->MBD_type == OCACHE_MBD_Thesaurus) {
    assert(oid == 0UL);
    uint32_t lsh = Thesaurus_hash_data((int32_t *)entry->data);
    assert(lsh < (sizeof(Thesaurus_base_table) / sizeof(void *)));
    // If there is already an entry, then use it; Otherwise ignore this and just perform normal compression
    if(Thesaurus_base_table[lsh] != NULL) {
      base_addr = Thesaurus_base_table[lsh]->addr;
    }
    Thesaurus_base_table[lsh] = entry; // Evict on every compression attempt to keep it "fresh"
  }
  // For MBDv0 this branch is never executed
  if(base_addr != addr) {
    dmap_entry_t *base_entry = dmap_find(ocache->dmap, oid, base_addr);
    if(base_entry != NULL) {
      // Run comp on the ref block
      switch(ocache->MBD_opt_type) {
        case OCACHE_MBD_OPT_NONE: {
          __MBD_get_comp_size_bits(base_entry->data, UTIL_CACHE_LINE_SIZE, &dict); 
        } break;
        case OCACHE_MBD_OPT0: {
          __MBD_opt0_get_comp_size_bits(base_entry->data, UTIL_CACHE_LINE_SIZE, &dict); 
        } break;
        case OCACHE_MBD_OPT1: {
          __MBD_opt1_get_comp_size_bits(base_entry->data, UTIL_CACHE_LINE_SIZE, &dict, MBD_REF_BLOCK); 
        } break;
        case OCACHE_MBD_OPT2: {
          __MBD_opt2_get_comp_size_bits(base_entry->data, UTIL_CACHE_LINE_SIZE, &dict, MBD_REF_BLOCK); 
        } break;
        case OCACHE_MBD_OPT3: {
          __MBD_opt3_get_comp_size_bits(base_entry->data, UTIL_CACHE_LINE_SIZE, &dict, MBD_REF_BLOCK); 
        } break;
        default: {
          error_exit("Unknown MBD opt type: %d\n", ocache->MBD_opt_type);
        } break;
      }
      // Invalidate stat, but keep the dictionary content
      MBD_dict_invalidate_stat(&dict);
      // Simulate bcache and ref block hit/miss
      if(base_entry->MESI_entry.llc_state == MESI_STATE_I) {
        ocache->MBD_insert_base_miss++;
        // Access bcache if it is initialized
        if(ocache->bcache != NULL) {
          int bcache_hit = bcache_read(ocache->bcache, oid, base_addr);
          if(bcache_hit == 1) {
            ocache->MBD_insert_bcache_hit++;
          } else {
            ocache->MBD_insert_bcache_miss++;
          } 
        }
      } else {
        ocache->MBD_insert_base_hit++;
      }
    }
  } else {
    ocache->MBD_insert_base_hit++;
  }
  // Run comp on target block
  int bits;
  switch(ocache->MBD_opt_type) {
    case OCACHE_MBD_OPT_NONE: {
      bits = __MBD_get_comp_size_bits(entry->data, UTIL_CACHE_LINE_SIZE, &dict); 
    } break;
    case OCACHE_MBD_OPT0: {
      bits = __MBD_opt0_get_comp_size_bits(entry->data, UTIL_CACHE_LINE_SIZE, &dict); 
    } break;
    case OCACHE_MBD_OPT1: {
      // If it is the ref block itself, then just perform normal compression / decompression
      int options = (base_addr == addr) ? MBD_REF_BLOCK : 0;
      bits = __MBD_opt1_get_comp_size_bits(entry->data, UTIL_CACHE_LINE_SIZE, &dict, options); 
    } break;
    case OCACHE_MBD_OPT2: {
      int options = (base_addr == addr) ? MBD_REF_BLOCK : 0;
      bits = __MBD_opt2_get_comp_size_bits(entry->data, UTIL_CACHE_LINE_SIZE, &dict, options); 
    } break;
    case OCACHE_MBD_OPT3: {
      int options = (base_addr == addr) ? MBD_REF_BLOCK : 0;
      bits = __MBD_opt3_get_comp_size_bits(entry->data, UTIL_CACHE_LINE_SIZE, &dict, options); 
    } break;
    default: {
      error_exit("Unknown MBD opt type: %d\n", ocache->MBD_opt_type);
    } break;
  }
  // Update compression stats
  ocache->MBD_zero_count += dict.zero_count;
  ocache->MBD_uncomp_count += dict.uncomp_count;
  ocache->MBD_match_count += dict.match_count;
  ocache->MBD_delta_size_dist[0] += dict.delta_size_dist[0];
  ocache->MBD_delta_size_dist[1] += dict.delta_size_dist[1];
  ocache->MBD_delta_size_dist[2] += dict.delta_size_dist[2];
  ocache->MBD_delta_size_dist[3] += dict.delta_size_dist[3];
  ocache->MBD_small_size_dist[0] += dict.small_size_dist[0];
  ocache->MBD_small_size_dist[1] += dict.small_size_dist[1];
  ocache->MBD_small_size_dist[2] += dict.small_size_dist[2];
  ocache->MBD_small_size_dist[3] += dict.small_size_dist[3];
  ocache->MBD_dict_insert_count += dict.insert_count;
  // Update decompression stats
  // Derive decompression cycle based on compression cycles, and store them in the dmap entry
  // Note that compression and decompression are symmetric, so we can use comp stats for decompression later
  // Note that we do not need decompression latency on insert (which is when this function is called), 
  // because insertion happens either on lower level fetch or upper level write back.
  // In the former case, we just provide the uncompressed block to the upper level. In the latter case,
  // decompression is not on the critical path.
  if(ocache->MBD_opt_type == OCACHE_MBD_OPT1 || ocache->MBD_opt_type == OCACHE_MBD_OPT3) { 
    entry->decomp_stalled_cycle_count = dict.stalled_cycle_count;
    entry->forward_count = dict.forward_count;
    entry->forward_stalled_count = dict.forward_stalled_count;
    entry->forward_uncomp_count = dict.forward_uncomp_count;
  }
  // dict.cycle_count already includes the runahead cycles, if any
  entry->decomp_cycle_count = (dict.cycle_count < 2) ? 2 : dict.cycle_count;
  entry->zero_count = dict.zero_count;
  // Check whether compression succeeds or not. Below is comp failed branch
  if(bits >= (int)UTIL_CACHE_LINE_SIZE * 8) {
    ocache->MBD_fail_count++;
    ocache->MBD_uncomp_size_sum += UTIL_CACHE_LINE_SIZE;
    return UTIL_CACHE_LINE_SIZE;
  }
  // Compression succeeds, compute byte size and update general comp ratio stat
  int bytes = (bits + 7) / 8;
  ocache->MBD_success_count++;
  ocache->MBD_before_size_sum += UTIL_CACHE_LINE_SIZE;
  ocache->MBD_after_size_sum += bytes;
  assert(bytes > 0 && bytes <= 64);
  // Update statistics for size classes. Note we must minus one to make value domain [0, 64)
  ocache->MBD_size_counts[(bytes - 1) / 8]++;
  return bytes;
}

// This function implements vertical compression policy
// The cache line can be processed with one of the following policies:
//   1. Not compressed at all
//   2. Only compressed with BDI
//   3. Compressed with BDI + vertical
//   4. Compressed with raw vertical
// Not all arguments will be used by this argument
// This function updates five statistics counters
// This function is the default compression algorithm for ocache (do not need to be set explicitly), but it can
// be replaced by calling ocache_set_get_compressed_size_cb()
int ocache_get_compressed_size_BDI_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape) {
  assert(ocache->dmap != NULL);
  // Total number of insertions
  ocache->comp_attempt_count++;
  ocache->comp_attempt_size_sum += UTIL_CACHE_LINE_SIZE;
  uint8_t in_buf[UTIL_CACHE_LINE_SIZE];
  // The data must already exist in data map
  int all_zero;
  int in_type = dmap_find_compressed(ocache->dmap, oid, addr, in_buf, &all_zero);
  assert(in_type != BDI_TYPE_NOT_FOUND);
  // If the line could not be BDI compressed, then we do not do anything
  // This could be changed by doing delta encoding with the base
  if(in_type == BDI_TYPE_INVALID) {
    ocache->BDI_failed_count++;
    ocache->BDI_uncomp_size_sum += UTIL_CACHE_LINE_SIZE;
    //printf("Uncompressable\n");
    return UTIL_CACHE_LINE_SIZE;
  }
  //
  // Past this point, the line is either compressed with BDI, or compressed with BDI + vertical
  // So we can increment BDI success count here
  //
  assert(in_type >= BDI_TYPE_BEGIN && in_type < BDI_TYPE_END);
  // Increment BDI type statistics
  ocache->BDI_compress_type_counts[in_type]++;
  // Update zero line stats. We do not treat zero lines specially
  if(all_zero == 1) {
    ocache->BDI_compress_type_counts[BDI_ZERO]++;
  }
  BDI_param_t *param = &BDI_types[in_type];
  ocache->BDI_success_count++;
  // The following two are only for compressable lines; Uncompressable lines are not counted
  // Total inserted size, logical
  ocache->BDI_before_size_sum += UTIL_CACHE_LINE_SIZE;
  // Size after BDI compression
  ocache->BDI_after_size_sum += param->compressed_size;
  // Update size distribution
  ocache->BDI_size_counts[(param->compressed_size - 1) / 8]++;
  assert(param != NULL);
  // This is the BDI compressed base cache line for vertical compression
  uint8_t base_buf[UTIL_CACHE_LINE_SIZE];
  uint64_t base_oid = ocache_get_vertical_base_oid(ocache, oid, shape);
  // If this happens then the line itself is a base cache line
  if(base_oid == oid) {
    ocache->vertical_uncomp_size_sum += param->compressed_size;
    ocache->vertical_uncomp_count++;
    //printf("Is base\n");
    return param->compressed_size;
  }
  ocache->vertical_not_base_count++;
  // Pass NULL to all_zero, don't care whether base is all-zero
  int base_type = dmap_find_compressed(ocache->dmap, base_oid, addr, base_buf, NULL);
  // If base compression type is different from the type of the new line just use normal compression
  if(base_type == BDI_TYPE_NOT_FOUND) {
    ocache->vertical_uncomp_size_sum += param->compressed_size;
    ocache->vertical_uncomp_count++;
    //printf("No base\n");
    return param->compressed_size;
  }
  // At this line the base type can be invalid or valid, but must be found
  assert((base_type == BDI_TYPE_INVALID) || (base_type >= BDI_TYPE_BEGIN && base_type < BDI_TYPE_END));
  ocache->vertical_base_found_count++;
  // This covers invalid base type case
  if(base_type != in_type) {
    ocache->vertical_uncomp_size_sum += param->compressed_size;
    ocache->vertical_uncomp_count++;
    //printf("Type mismatch\n");
    return param->compressed_size;
  }
  ocache->vertical_same_type_count++;
  // Otherwise, perform vertical compression
  int vertical_size = BDI_vertical_comp(base_buf, in_buf, in_type);
  // Only use vertical compression when the size is smaller
  if(vertical_size >= param->compressed_size) {
    ocache->vertical_uncomp_size_sum += param->compressed_size;
    ocache->vertical_uncomp_count++;
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

int ocache_get_compressed_size_BDI_third_party_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape) {
  assert(ocache->dmap != NULL);
  (void)shape;
  ocache->comp_attempt_count++;
  ocache->comp_attempt_size_sum += UTIL_CACHE_LINE_SIZE;
  dmap_entry_t *dmap_entry = dmap_find(ocache->dmap, oid, addr);
  assert(dmap_entry != NULL);
  int type;
  // type field is COMPRESSION_FAIL, COMPRESSION_ZERO, COMPRESSION_DUP or COMPRESSION_ type
  unsigned int comp_size = GeneralCompress(dmap_entry->data, COMPRESSION_BDI, &type);
  if(type == COMPRESSION_FAIL) {
    assert(comp_size == UTIL_CACHE_LINE_SIZE);
    ocache->BDI_failed_count++;
    ocache->BDI_uncomp_size_sum += UTIL_CACHE_LINE_SIZE;
    //printf("Uncompressable\n");
    return UTIL_CACHE_LINE_SIZE;
  }
  // This is compatible with the type above (array size is 8)
  ocache->BDI_compress_type_counts[type]++;
  ocache->BDI_success_count++;
  ocache->BDI_before_size_sum += UTIL_CACHE_LINE_SIZE;
  ocache->BDI_after_size_sum += comp_size;
  ocache->BDI_size_counts[(comp_size - 1) / 8]++;
  return comp_size;
}

uint64_t ocache_access_hit_BDI_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape) {
  (void)ocache; (void)oid; (void)addr; (void)shape;
  return OCACHE_BDI_DECOMP_LATENCY;
}

uint64_t ocache_access_hit_FPC_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape) {
  (void)ocache; (void)oid; (void)addr; (void)shape;
  return OCACHE_FPC_DECOMP_LATENCY;
}

uint64_t ocache_access_hit_CPACK_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape) {
  (void)ocache; (void)oid; (void)addr; (void)shape;
  return OCACHE_CPACK_DECOMP_LATENCY;
}

// This function is called by coherence controllers when an access hits an LLC line
// This function checks whether the base line is present in the LLC
// Returns extra cycles the read will incur
uint64_t ocache_access_hit_MBD_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape) {
  uint64_t base_addr = addr;
  if(ocache->MBD_type == OCACHE_MBD_V4) {
    base_addr = addr & (~0x3UL << UTIL_CACHE_LINE_BITS);
  } else if(ocache->MBD_type == OCACHE_MBD_V2) {
    base_addr = addr & (~0x1UL << UTIL_CACHE_LINE_BITS);
  } else if(ocache->MBD_type == OCACHE_MBD_Thesaurus) {
    assert(oid == 0UL);
  }
  // Simulate bcache hit on read requests
  // For MBDv0 and Thesaurus this is never executed
  if(base_addr != addr) {
    dmap_entry_t *base_entry = dmap_find(ocache->dmap, oid, base_addr);
    if(base_entry != NULL) {
      if(base_entry->MESI_entry.llc_state == MESI_STATE_I) {
        ocache->MBD_read_base_is_missing++;
        if(ocache->bcache != NULL) {
          int bcache_hit = bcache_read(ocache->bcache, oid, base_addr);
          if(bcache_hit == 1) {
            ocache->MBD_read_bcache_hit++;
          } else {
            ocache->MBD_read_bcache_miss++;
          } 
        }
      } else {
        ocache->MBD_read_base_is_present++;
      }
    }
  } else {
    // Just accessing the base itself
    ocache->MBD_read_base_is_present++;
  }
  (void)shape;
  // Get data
  dmap_entry_t *dmap_entry = dmap_find(ocache->dmap, oid, addr);
  // One more decompression on cached block hit
  ocache->MBD_decomp_count++;
  // Update stat from the dmap entry directly
  // This is capped at >= 2 by the get compressed size function
  assert(dmap_entry->decomp_cycle_count >= 2UL);
  ocache->MBD_decomp_zero_count += dmap_entry->zero_count;
  ocache->MBD_decomp_cycle_count += dmap_entry->decomp_cycle_count;
  ocache->MBD_decomp_forward_count += dmap_entry->forward_count;
  ocache->MBD_decomp_forward_stalled_count += dmap_entry->forward_stalled_count;
  ocache->MBD_decomp_forward_uncomp_count += dmap_entry->forward_uncomp_count;
  ocache->MBD_decomp_stalled_cycle_count += dmap_entry->decomp_stalled_cycle_count;
  // TODO: Zero hits is short circuited
  // Update dist stat
  // This must remain between [2, 16], 17 elements in the array with first two always being zero
  if(dmap_entry->decomp_cycle_count > 16) {
    error_exit("Decompression cycle too large: %u\n", dmap_entry->decomp_cycle_count);
  }
  ocache->MBD_decomp_cycle_dist[dmap_entry->decomp_cycle_count]++;
  // [0, 8], 9 elements
  if(dmap_entry->decomp_stalled_cycle_count > 8) {
    error_exit("Decompression stalled cycle too large: %u\n", dmap_entry->decomp_stalled_cycle_count);
  }
  ocache->MBD_decomp_stalled_cycle_dist[dmap_entry->decomp_stalled_cycle_count]++;
  return dmap_entry->decomp_cycle_count;
}

uint64_t ocache_access_hit_None_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape) {
  (void)ocache; (void)oid; (void)addr; (void)shape;
  return 0;
}

// Helper function called by ocache_insert() 
// This function evicts the LRU entry, and make it a super block
// Note: This function may also be called by other helper functions to evict an existing line
// before adding the current line
static void ocache_insert_helper_compressed_miss_no_candid(
  ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, int dirty, int compressed_size,
  ocache_op_result_t *insert_result, ocache_op_result_t *lookup_result) {
  // Called for both miss and write relocation
  assert(lookup_result->state == OCACHE_MISS || lookup_result->state == OCACHE_HIT_COMPRESSED);
  assert(lookup_result->lru_entry != NULL);
  // This is the entry we rewrite
  ocache_entry_t *entry = lookup_result->lru_entry;
  // If the entry is itself invalid, then we just return success
  if(ocache_entry_is_all_invalid(entry) == 1) {
    assert(entry->lru == 0);
    insert_result->state = OCACHE_SUCCESS;
  } else {
    assert(entry->lru > 0);
    insert_result->state = OCACHE_EVICT;
    // First copy the evicted entry to the insert result
    ocache_entry_dup(&insert_result->insert_evict_entries[0], entry);
    // Always evict exactly one entry for sector cache design
    insert_result->insert_evict_count = 1;
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
  // Use this to access the entry we just insert into
  insert_result->insert_entry = entry;
  insert_result->insert_index = index;
  return;
}

// Called to place the new line into one of the candidates (assuming there was no previous hit)
// This function will invoke vertical compression
static void ocache_insert_helper_compressed_miss_with_candid(
  ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, int dirty, int compressed_size,
  ocache_op_result_t *insert_result, ocache_op_result_t *lookup_result) {
  // Could also be hit compressed since this function is also called for write relocation
  assert(lookup_result->state == OCACHE_MISS || lookup_result->state == OCACHE_HIT_COMPRESSED);
  //assert(lookup_result->hit_entry == NULL); // May not be always true for write relocation
  // Scan candidates
  for(int i = 0;i < lookup_result->cand_count;i++) {
    // The entry and index if it were to be put to that slot
    ocache_entry_t *candidate = lookup_result->candidates[i];
    // Must be of the same shape
    assert(candidate->shape == shape);
    // The index has already been known by the lookup process, if candidate is hit
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
      insert_result->state = OCACHE_SUCCESS;
      // Inserted entry can be accessed using these two
      insert_result->insert_entry = candidate;
      insert_result->insert_index = index;
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
// The caller should ensure this assumption is being followed
static void ocache_insert_helper_compressed_hit(
  ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, int dirty, int compressed_size,
  ocache_op_result_t *insert_result, ocache_op_result_t *lookup_result) {
  assert(lookup_result->state == OCACHE_HIT_COMPRESSED);
  assert(lookup_result->hit_entry != NULL);
  assert(lookup_result->hit_index >= 0 && lookup_result->hit_index < 4);
  ocache_entry_t *hit_entry = lookup_result->hit_entry;
  int hit_index = lookup_result->hit_index;
  // If the entry is already dirty, then the result will be dirty either
  if(ocache_entry_is_dirty(hit_entry, hit_index) == 1) {
    dirty = 1;
  }
  // Invalidate the line first - need to keep dirty bits and OR to the incoming dirty bit
  // Note that we do not need to worry the case where this is never set back - if the hit_entry
  // only contains the current line, then the line will always be hit
  // Otherwise, there must be another line, making it impossible to be all invalid after this.
  ocache_entry_inv_index(hit_entry, hit_index);
  //if(ocache_entry_is_all_invalid(hit_entry) == 1) { // <--- unnecessary, see above
  //  ocache_entry_inv(hit_entry);
  //}
  // Case 3.1 - Hit the slot, and current slot could still hold the line
  if(ocache_entry_is_fit(hit_entry, compressed_size) == 1) {
    ocache_entry_set_valid(hit_entry, hit_index);
    if(dirty == 1) {
      ocache_entry_set_dirty(hit_entry, hit_index);
    }
    ocache_entry_set_size(hit_entry, hit_index, compressed_size);
    ocache_update_entry_lru(ocache, hit_entry);
    insert_result->state = OCACHE_SUCCESS;
    insert_result->insert_entry = hit_entry;
    insert_result->insert_index = hit_index;
    return;
  }
  // Case 3.2 - Hit the slot, but size is too large
  // Complete the insert by either finding another candidate, or eviction
  ocache_insert_helper_compressed_miss_with_candid(
    ocache, oid, addr, shape, dirty, compressed_size, insert_result, lookup_result
  );
  return;
}

// Inserts a compressed line into the cache. 
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
void ocache_insert_sb_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, 
                         int shape, int dirty, ocache_op_result_t *insert_result) {
  assert(shape >= OCACHE_SHAPE_BEGIN && shape < OCACHE_SHAPE_END);
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0);
  // Only for compressed caches do we allow a shape
  assert(ocache->use_shape == 1 || shape == OCACHE_SHAPE_NONE);
  ocache->insert_count++;
  ocache_op_result_t lookup_result;
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
  assert(state == OCACHE_MISS || state == OCACHE_HIT_COMPRESSED);
  // This functions returns the size of the line after compression
  int comp_size = ocache->get_compressed_size_cb(ocache, oid, addr, shape);
  if(state == OCACHE_MISS) {
    if(lookup_result.cand_count == 0) {
      // Case 1.1.2: Compressed line miss, no candidate, evict an existing entry and make a super block
      ocache->insert_sb_miss_count++;
      ocache_insert_helper_compressed_miss_no_candid(
        ocache, oid, addr, shape, dirty, comp_size, insert_result, &lookup_result
      );
    } else {
      // Case 1.2: Compressed line miss, with candidate, try other super blocks
      ocache->insert_sb_hit_count++;
      ocache_insert_helper_compressed_miss_with_candid(
        ocache, oid, addr, shape, dirty, comp_size, insert_result, &lookup_result
      );
    }
    return;
  }
  assert(state == OCACHE_HIT_COMPRESSED);
  ocache->insert_sb_hit_count++;
  // Case 3: Compressed line hit
  ocache_insert_helper_compressed_hit(
    ocache, oid, addr, shape, dirty, comp_size, insert_result, &lookup_result
  );
  // Re-adjust lines in the SB entries
  ocache_after_insert_adjust(ocache, insert_result, &lookup_result);
  return;
}

// Evict a given entry by copying it into the given result object and increment its insert_evict_count by one
// This function will:
//   (1) Update total_aligned_size based on the total aligned size of the entry
//   (2) Update insert_evict_count
//   (3) Update insert_evict_entries
static void ocache_insert_helper_evict(ocache_t *ocache, ocache_entry_t *entry, ocache_op_result_t *result) {
  result->total_aligned_size -= ocache_entry_get_total_aligned_size(entry);
  assert(result->total_aligned_size >= 0);
  ocache_entry_dup(result->insert_evict_entries + result->insert_evict_count, entry);
  ocache_entry_inv(entry);
  result->insert_evict_count++;
  assert(result->insert_evict_count <= OCACHE_MAX_EVICT_ENTRY);
  (void)ocache;
  return;
}

// This is called on data contention
// This function may only partially evict an SB; The evict order is large to small to minimize number of evictions
// Evict optimization is added:
//   (1) For OPT1, if the ref block is evicted, then we evict the entire tag
static void ocache_insert_helper_evict_valid_lru(
  ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, int aligned_comp_size, ocache_op_result_t *result) {
  // This is caused by data contention, so increment the counter
  ocache->insert_evict_data_contention_count++;
  while(result->total_aligned_size + aligned_comp_size > ocache->set_size) {
    ocache_entry_t *valid_lru_entry = NULL; // Must be a valid entry having the smallest LRU
    uint64_t min_lru = -1UL;
    ocache_entry_t *entry = ocache_get_set_begin(ocache, oid, addr, shape);
    // Use tag way count
    for(int i = 0;i < ocache->tag_way_count;i++) {
      if(ocache_entry_is_all_invalid(entry) == 0) {
        assert(entry->lru != 0UL);
        if(entry->lru < min_lru) {
          min_lru = entry->lru;
          valid_lru_entry = entry;
        }
      }
      entry++;
    } // for
    assert(valid_lru_entry != NULL);
    const int tag_line_count = ocache_entry_get_valid_line_count(valid_lru_entry);
    assert(tag_line_count > 0);
    // Number of lines actually selected for eviction for the current tag
    int evict_line_count = 0;
    // Number of aligned bytes that should be evicted
    int size_delta = \
      result->total_aligned_size + aligned_comp_size - ocache->set_size;
    assert(size_delta > 0);
    // If the entire entry should be evicted, then just use the fast path
    // Three conditions:
    //   (1) The tag's data combined is just larger than required size
    //   (2) There is only a single line in the tag
    //   (3) OPT0 is enabled
    if(size_delta >= ocache_entry_get_total_aligned_size(valid_lru_entry) || tag_line_count == 1 || \
       ocache->evict_opt_type == OCACHE_EVICT_OPT0) {
      ocache_insert_helper_evict(ocache, valid_lru_entry, result);
      evict_line_count = tag_line_count;
    } else {
      // Copy all entries into the temp entry, and then mark off those that are *NOT* evicted
      // Then we call tag-level eviction using the temp entry, not the original entry
      // This greatly simplifies the process, as the tag-level eviction function is destructive
      ocache_entry_t temp;
      ocache_entry_dup(&temp, valid_lru_entry);
      int evict_mask = 0x0; // 0x1, 0x2, 0x4, 0x8 represents the four blocks
      for(int i = 0;i < tag_line_count;i++) {
        int largest_size = 0;
        int largest_index = -1;
        // Pick largest line 
        for(int j = 0;j < 4;j++) {
          if(ocache_entry_is_valid(valid_lru_entry, j) == 1) {
            int aligned_size = ocache_entry_get_aligned_size(valid_lru_entry, j);
            if(aligned_size > largest_size) {
              largest_size = aligned_size;
              largest_index = j;
            }
          }
        }
        assert(largest_index != -1);
        if(largest_index == 0 && ocache->evict_opt_type == OCACHE_EVICT_OPT1) {
          // If evict OPT1 and the ref block is selected, then evict the entire tag
          evict_mask = 0xF;
          // This operation is local and will not affect total aligned size in result object
          ocache_entry_inv_index(valid_lru_entry, 0);
          ocache_entry_inv_index(valid_lru_entry, 1);
          ocache_entry_inv_index(valid_lru_entry, 2);
          ocache_entry_inv_index(valid_lru_entry, 3);
          evict_line_count = tag_line_count;
          break;
        } else {
          evict_line_count++;
          evict_mask |= (0x1 << largest_index);
        }
        // Clear the entry from LRU entry since as the point we know it will surely be evicted
        ocache_entry_inv_index(valid_lru_entry, largest_index);
        // If after evicting the line we have enough storage then that's it
        if(size_delta <= largest_size) {
          break;
        }
        size_delta -= largest_size;
        assert(size_delta > 0);
      } // for loop selecting largest line in the tag
      // Clear those that are not evicted (not set in the mask)
      for(int i = 0;i < 4;i++) {
        // If the line is not evicted, then inv from temp
        // If the line is evicted, then inv from the LRU entry
        if((evict_mask & (0x1 << i)) == 0) {
          ocache_entry_inv_index(&temp, i);
        }
      }
      // Must use temp, since this function will clear the given entry
      ocache_insert_helper_evict(ocache, &temp, result);
    } // if-else whether to fully or partially evict the entry
    // Lines that are actually evicted
    ocache->insert_evict_data_contention_line_count += evict_line_count;
    // Lines that are scanned during this process but some are not evicted
    ocache->insert_evict_data_contention_iter_line_count += tag_line_count;
    // Number of tags scanned
    ocache->insert_evict_data_contention_iter_count++;
  } // while
  assert(result->insert_evict_count > 0);
  return;
}

// Segment cache insert
// This function will:
//   (1) Return the inserted entry and index in insert_entry and insert_index
//   (2) Return the new total aligned size of the set in total_aligned_size
//   (3) Return evicted entries in insert_evict_entries and insert_evict_count
void ocache_insert_seg_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, 
  int shape, int dirty, ocache_op_result_t *result) {
  ocache->insert_count++;
  // Do not update LRU & always scan all tags in the set, such that total_aligned_size is always valid
  ocache_lookup_write(ocache, oid, addr, shape, result);
  // Compressed size of the line
  assert(ocache->get_compressed_size_cb != NULL);
  int comp_size = ocache->get_compressed_size_cb(ocache, oid, addr, shape);
  int aligned_comp_size = ocache_entry_align_size(NULL, comp_size);
  uint64_t base_oid = 0UL, base_addr = 0UL;
  int index = 0;
  ocache_get_sb_tag(ocache, oid, addr, shape, &base_oid, &base_addr, &index);
  if(result->state != OCACHE_MISS) {
    assert(result->state == OCACHE_HIT_NORMAL || result->state == OCACHE_HIT_COMPRESSED);
    assert(result->cand_count == 1 || result->state == OCACHE_HIT_NORMAL);
    ocache->insert_sb_hit_count++;
    result->insert_evict_count = 0;
    result->state = OCACHE_SUCCESS;
    ocache_entry_t *hit_entry = result->hit_entry;
    int hit_index = result->hit_index;
    assert(hit_entry != NULL);
    assert(hit_index == index && hit_entry->oid == base_oid && hit_entry->addr == base_addr);
    assert(hit_entry->shape == shape);
    assert(result->hit_index == 0 || result->state == OCACHE_HIT_COMPRESSED);
    int aligned_old_size = ocache_entry_align_size(NULL, ocache_entry_get_size(hit_entry, hit_index));
    // Remember the dirty bit of the existing entry
    dirty = dirty || ocache_entry_is_dirty(hit_entry, hit_index);
    // Invalidate the entry, i.e., an insert hit is equivalent to removing and re-inserting it
    ocache_entry_inv_index(hit_entry, hit_index);
    // TODO: TEST WHETHER THE COMPRESSED SIZE OF OTHER BLOCKS CHANGE DUE TO THE INSERTION OF THE BLOCK,
    // IF THE INSERTION IS A REFERENCE BLOCK
    // Need to update aligned size, which should be done before the invalidation
    result->total_aligned_size -= aligned_old_size;
    if(result->total_aligned_size + aligned_comp_size > ocache->set_size) {
      // This evict lines until the above condition is met, and copies entry into result
      // result->total_aligned_size is also updated accordingly
      ocache_insert_helper_evict_valid_lru(ocache, oid, addr, shape, aligned_comp_size, result);
      result->state = OCACHE_EVICT;
    }
    // Then re-insert the hit_entry using the dirty bit (which could have be changed) and new size
    // Note: We should set oid, addr, shape, since the hit block could just be evicted
    hit_entry->oid = base_oid;
    hit_entry->addr = base_addr;
    hit_entry->shape = shape;
    ocache_entry_set_valid(hit_entry, hit_index);
    if(dirty == 1) {
      ocache_entry_set_dirty(hit_entry, hit_index);
    }
    ocache_entry_set_size(hit_entry, hit_index, comp_size);
    ocache_update_entry_lru(ocache, hit_entry);
    result->total_aligned_size += aligned_comp_size;
    result->insert_entry = hit_entry;
    result->insert_index = hit_index;
  } else {
    result->insert_evict_count = 0;
    result->state = OCACHE_SUCCESS;
    ocache_entry_t *insert_entry = NULL;
    if(result->cand_count > 0) {
      // This is SB hit, always insert into the same SB
      assert(result->cand_count == 1);
      ocache->insert_sb_hit_count++;
      insert_entry = result->candidates[0];
      assert(insert_entry->oid == base_oid && insert_entry->addr == base_addr);
      assert(insert_entry->shape == shape);
    } else {
      ocache->insert_sb_miss_count++;
      // This is full miss, must claim the LRU entry and init to be the current base addr / shape
      insert_entry = result->lru_entry;
      // Claim a tag entry from LRU. If LRU entry is valid, evict it first; This will change total_aligned_size
      // Tag contention case
      if(ocache_entry_is_all_invalid(insert_entry) == 0) {
        ocache->insert_evict_tag_contention_count++;
        // This will update result->total_aligned_size
        ocache_insert_helper_evict(ocache, insert_entry, result);
        result->state = OCACHE_EVICT; // Eviction for tag
      }
      // LRU entry must be invalidated at this point
      assert(insert_entry->lru == 0UL);
      assert(ocache_entry_get_total_aligned_size(insert_entry) == 0);
      // Install the new entry
      insert_entry->oid = base_oid;
      insert_entry->addr = base_addr;
      insert_entry->shape = shape;
    }
    // If need more storage then evict more entries (data contention after tag contention)
    if(result->total_aligned_size + aligned_comp_size > ocache->set_size) {
      // This will update lookup_result.total_aligned_size
      ocache_insert_helper_evict_valid_lru(ocache, oid, addr, shape, aligned_comp_size, result);
      result->state = OCACHE_EVICT; // Eviction for storage
    }
    // This is the size after insertion
    result->total_aligned_size += aligned_comp_size;
    assert(result->total_aligned_size <= ocache->set_size);
    ocache_entry_set_valid(insert_entry, index);
    if(dirty == 1) {
      ocache_entry_set_dirty(insert_entry, index);
    }
    ocache_entry_set_size(insert_entry, index, comp_size);
    // Update LRU
    ocache_update_entry_lru(ocache, insert_entry);
    // Pass insert entry and index info
    result->insert_entry = insert_entry;
    result->insert_index = index;
  }
  return;
}

// Adjusts cache line layouts after insertion
// This function tries to sorts all lines in the SB and minimize the layout
// Can potentially free a few slots (at most three)
// If the inserted entry is freed, then the result is also updated accordingly
void ocache_after_insert_adjust(
  ocache_t *ocache, ocache_op_result_t *insert_result, ocache_op_result_t *lookup_result) {
  ocache_entry_t *entries[4];
  int entry_count = 0;
  int insert_cand = 0; // If insert into one of the candidates, this will be set
  for(int i = 0;i < lookup_result->cand_count;i++) {
    entries[entry_count++] = lookup_result->candidates[i];
    if(lookup_result->candidates[i] == insert_result->insert_entry) {
      insert_cand = 1;
    }
  }
  if(insert_cand == 0) {
    entries[entry_count++] = insert_result->insert_entry;
  }
  assert(entry_count >= 1 && entry_count <= 4);
  // Cannot be optimized
  if(entry_count == 1) {
    return;
  }
  int indices[4];
  int sizes[4];
  int dirty_bits[4];
  int line_count = 0;
  // Enumerate sizes, entries and indices
  for(int i = 0;i < entry_count;i++) {
    for(int index = 0;index < 4;index++) {
      int line_size = ocache_entry_get_size(entries[i], index);
      if(line_size != 0) {
        assert(ocache_entry_is_valid(entries[i], index) == 1);
        sizes[line_count] = line_size;
        indices[line_count] = index;
        dirty_bits[line_count] = ocache_entry_is_dirty(entries[i], index);
        line_count++;
      }
    }
  }
  assert(line_count <= 4);
  int sorted_indices[4] = {0, 1, 2, 3}; // This stores sorted indices as an indirection array
  int sense = 0; // Whether min or max
  // Sort based on size, use simple insertion sort
  for(int i = 0;i < line_count - 1;i++) {
    int extreme_index = -1;
    int extreme_size = (sense == 0) ? (UTIL_CACHE_LINE_SIZE + 1) : 0;
    for(int j = i;j < line_count;j++) {
      if(sense == 0) {
        if(sizes[sorted_indices[j]] < extreme_size) {
          extreme_size = sizes[sorted_indices[j]];
          extreme_index = j;
        }
      } else {
        if(sizes[sorted_indices[j]] > extreme_size) {
          extreme_size = sizes[sorted_indices[j]];
          extreme_index = j;
        }
      }
    }
    assert(extreme_index != -1);
    int temp = sorted_indices[i];
    sorted_indices[i] = sorted_indices[extreme_index];
    sorted_indices[extreme_index] = temp;
    sense = 1 - sense;
    /*
    printf("i %d min_index %d actual %d ", i, min_index, sorted_indices[min_index]);
    for(int j = 0;j < 4;j++) printf("%d, ", sorted_indices[j]);
    putchar('\n');
    */
  }
  /*
  printf("Sorted (%d lines): ", line_count);
  for(int i = 0;i < line_count;i++) {
    printf("%d, ", sorted_indices[i]);
  }
  putchar('\n');
  */
  // Arrange slots
  int curr_entry_index = 0;
  int curr_size = 0;
  for(int i = 0;i < line_count;i++) {
    curr_size += sizes[sorted_indices[i]];
    if(curr_size > (int)UTIL_CACHE_LINE_SIZE) {
      curr_entry_index++;
      curr_size = sizes[sorted_indices[i]];
    }
  }
  // Must be no worse after adjustment
  // The above is wrong - actually we can do worse
  //assert(curr_entry_index < entry_count);
  // Do nothing if it does not improve
  if(curr_entry_index >= entry_count - 1) {
    return;
  }
  /*
  printf("Adjusted from %d to %d entries\n    Lines: ", entry_count, after_entry_count);
  for(int i = 0;i < line_count;i++) {
    printf("idx %d dirty %d sz %d;", indices[i], dirty_bits[i], sizes[i]);
  }
  putchar('\n');
  */
  // Then start adjusting lines in entries
  uint64_t oid = entries[0]->oid;
  uint64_t addr = entries[0]->addr;
  int shape = entries[0]->shape;
  curr_entry_index = 0; // Current entry's index in the "entries" array being inserted into
  curr_size = 0; // Current entry's size
  ocache_entry_t *curr_entry = entries[0];
  uint64_t lru = curr_entry->lru;
  ocache_entry_inv(curr_entry);
  for(int i = 0;i < line_count;i++) {
    curr_size += sizes[sorted_indices[i]];
    if(curr_size > (int)UTIL_CACHE_LINE_SIZE) {
      curr_entry_index++;
      curr_entry = entries[curr_entry_index];
      lru = curr_entry->lru; // Read this before inv the entry
      // Invalidate the entry on every switch; This will also set LRU to zero
      ocache_entry_inv(curr_entry);
      curr_size = sizes[sorted_indices[i]];
    }
    int line_index = indices[sorted_indices[i]]; // Current line lidex in the SB
    // Update LRU if it is just inserted, otherwise just restore the old LRU
    //printf("oid %lX addr %lX line idx %d entry idx %d curr size %d\n", 
    //  oid, addr, line_index, curr_entry_index, curr_size);
    if(line_index == insert_result->insert_index) {
      ocache_update_entry_lru(ocache, curr_entry);
      insert_result->insert_entry = curr_entry; // Update result's insert index
      //printf("Setting curr entry line idx %d sorted idx %d\n", line_index, sorted_indices[i]);
    } else {
      assert(lru != 0);
      curr_entry->lru = lru;
    }
    ocache_entry_set_valid(curr_entry, line_index);
    if(dirty_bits[sorted_indices[i]] == 1) {
      ocache_entry_set_dirty(curr_entry, line_index);
    }
    ocache_entry_set_size(curr_entry, line_index, sizes[sorted_indices[i]]);
    curr_entry->oid = oid;
    curr_entry->addr = addr;
    curr_entry->shape = shape;
  }
  //printf("curr entry index %d, entry count %d\n", curr_entry_index, entry_count);
  // Invalidate the rest of the entries
  for(int i = curr_entry_index + 1;i < entry_count;i++) {
    ocache_entry_inv(entries[i]);
  }
  return;
}

// Invalidate a single line, possibly in a super block
// Returns OCACHE_SUCCESS if misses; return OCACHE_EVICT if hits, no matter dirty or clean
// The evicted entry will be the entire entry, and evict_index indicates which exact entry is evicted
// Only the requested logical line is invalidated, with other lines being normal
// NOTE: This function only invalidates a single line; It may still leave the cache unable to insert after the
//       invalidation succeeds
void ocache_inv(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, ocache_op_result_t *result) {
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0UL);
  ocache_op_result_t lookup_result;
  // This will change LRU, and we only reset LRU if the line is the only line
  ocache_lookup_read(ocache, oid, addr, shape, &lookup_result);
  if(lookup_result.state == OCACHE_MISS) {
    result->state = OCACHE_SUCCESS;
    return;
  }
  // Must be hit
  int state = lookup_result.state;
  assert(state == OCACHE_HIT_NORMAL || state == OCACHE_HIT_COMPRESSED);
  ocache_entry_t *hit_entry = lookup_result.hit_entry;
  int hit_index = lookup_result.hit_index;
  assert(hit_entry != NULL);
  assert(hit_index >= 0 && hit_index < 4);
  // Always copy and return evict if the query hits
  result->state = OCACHE_EVICT;
  result->evict_index = hit_index;
  ocache_entry_dup(&result->evict_entry, hit_entry);
  // Only invalidate a single logical line
  ocache_entry_inv_index(hit_entry, hit_index);
  // If all lines are invalid, just invalidate the entire line (reset LRU, etc.)
  if(ocache_entry_is_all_invalid(hit_entry) == 1) {
    ocache_entry_inv(hit_entry);
  }
  return;
}

// Downgrades a line by clearing its dirty bit, if set
// Always returns OCACHE_SUCCESS
// If the line is not found, the downgrade_entry is NULL; Otherwise it points to the entry just downgraded
// and the index is set accordingly
// Note that there is no way to know if the line was dirty or not using this function
void ocache_downgrade(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, ocache_op_result_t *result) {
  ocache_op_result_t downgrade_result;
  // Do not update LRU and do not scan all
  _ocache_lookup(ocache, oid, addr, shape, 0, 0, &downgrade_result);
  if(downgrade_result.state == OCACHE_MISS) {
    result->downgrade_entry = NULL;
    result->downgrade_index = -1;
  } else {
    assert(downgrade_result.state == OCACHE_HIT_NORMAL || downgrade_result.state == OCACHE_HIT_COMPRESSED);
    assert(downgrade_result.hit_entry != NULL);
    ocache_entry_t *hit_entry = downgrade_result.hit_entry;
    int hit_index = downgrade_result.hit_index;
    assert(ocache_entry_is_valid(hit_entry, hit_index) == 1);
    ocache_entry_clear_dirty(hit_entry, hit_index);
    result->downgrade_entry = hit_entry;
    result->downgrade_index = hit_index;
  }
  result->state = OCACHE_SUCCESS;
  return;
}

// Returns the MRU entry; Empty entry is also possible (LRU == 0)
// Argument way_start selects a starting way
// If there are several LRU, always select the first one
ocache_entry_t *ocache_get_mru(ocache_t *ocache, int set_index) {
  if(set_index < 0 || set_index >= ocache->set_count) {
    error_exit("Invalid set_index: %d\n", set_index);
  }
  ocache_entry_t *entry = ocache->data + set_index * ocache->tag_way_count;
  ocache_entry_t *max_entry = entry;
  uint64_t max_lru = entry->lru;
  for(int i = 0;i < ocache->tag_way_count;i++) {
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
  for(int i = 0;i < ocache->tag_line_count;i++) {
    if(entry->lru > max_lru) {
      max_lru = entry->lru;
      max_entry = entry;
    }
    entry++;
  }
  return max_entry;
}

void ocache_reset(ocache_t *ocache) {
  // Invalidate lines
  for(int i = 0;i < ocache->tag_line_count;i++) {
    ocache_entry_inv(ocache->data + i);
  }
  // Byte size since the type is uint8_t
  int size = ocache->stat_end - ocache->stat_begin;
  memset(ocache->stat_begin, 0x00, size);
  ocache->lru_counter = 1UL;
  return;
}

// Count the number of logical lines in the current ocache
void ocache_refresh_stat(ocache_t *ocache) {
  ocache_entry_t *entry = ocache->data;
  // Clear previous values of the status variable
  ocache->logical_line_count = 0UL;
  ocache->sb_slot_count = 0UL;
  ocache->sb_logical_line_count = 0UL;
  ocache->sb_logical_line_size_sum = 0UL;
  ocache->sb_aligned_logical_line_size_sum = 0UL;
  ocache->base_missing_sb_count = 0UL;
  ocache->base_found_sb_count = 0UL;
  ocache->no_shape_line_count = 0UL;
  ocache->sb_4_1_count = 0UL;
  ocache->sb_1_4_count = 0UL;
  ocache->sb_2_2_count = 0UL;
  memset(ocache->size_histogram, 0x00, sizeof(ocache->size_histogram));
  memset(ocache->sb_logical_line_count_dist, 0x00, sizeof(ocache->sb_logical_line_count_dist));
  //
  for(int i = 0;i < ocache->tag_line_count;i++) {
    // Only do it when the line is valid
    if(ocache_entry_is_all_invalid(entry) == 0) {
      if(entry->shape == OCACHE_SHAPE_NONE) {
        ocache->logical_line_count++; // Valid line without compression, so only single line per slot
        ocache->no_shape_line_count++;
        ocache->size_histogram[7]++;
      } else {
        // Population of the state bit
        int lines = popcount_int32(entry->states & 0xF);
        ocache->logical_line_count += lines; // Multiple logical lines per slot
        ocache->sb_slot_count++;
        ocache->sb_logical_line_count += lines;
        if(ocache_entry_is_valid(entry, 0) == 1) {
          ocache->base_found_sb_count++;
        } else {
          ocache->base_missing_sb_count++;
        }
        assert(lines >= 1 && lines <= 4);
        // Subtract by one to make to [0, 3]
        ocache->sb_logical_line_count_dist[lines - 1]++;
        switch(entry->shape) {
          case OCACHE_SHAPE_4_1: ocache->sb_4_1_count++; break;
          case OCACHE_SHAPE_1_4: ocache->sb_1_4_count++; break;
          case OCACHE_SHAPE_2_2: ocache->sb_2_2_count++; break;
        }
        // Update compressed line size
        for(int index = 0;index < 4;index++) {
          ocache->sb_logical_line_size_sum += entry->sizes[index];
          ocache->sb_aligned_logical_line_size_sum += ocache_entry_align_size(NULL, entry->sizes[index]);
          if(entry->sizes[index] != 0) ocache->size_histogram[(entry->sizes[index] - 1) / 8]++;
        }
      }
    } else {
      assert(entry->lru == 0);
    }
    entry++;
  }
  return;
}

// Create a new stat snapshot node and push it to the tail of the linked list
void ocache_append_stat_snapshot(ocache_t *ocache) {
  ocache_stat_snapshot_t *snapshot = ocache_stat_snapshot_init();
  snapshot->next = NULL;
  if(ocache->stat_snapshot_head == NULL) {
    assert(ocache->stat_snapshot_tail == NULL);
    ocache->stat_snapshot_head = ocache->stat_snapshot_tail = snapshot;
  } else {
    assert(ocache->stat_snapshot_tail != NULL);
    assert(ocache->stat_snapshot_tail->next == NULL);
    ocache->stat_snapshot_tail->next = snapshot;
    ocache->stat_snapshot_tail = snapshot;
  }
  ocache_refresh_stat(ocache);  // Scan the tag array and update stats. This will clear the stat
  snapshot->logical_line_count = ocache->logical_line_count;
  snapshot->sb_slot_count = ocache->sb_slot_count;
  snapshot->sb_logical_line_count = ocache->sb_logical_line_count;
  snapshot->sb_logical_line_size_sum = ocache->sb_logical_line_size_sum;
  snapshot->sb_aligned_logical_line_size_sum = ocache->sb_aligned_logical_line_size_sum;
  snapshot->base_missing_sb_count = ocache->base_missing_sb_count;
  snapshot->base_found_sb_count = ocache->base_found_sb_count;
  snapshot->no_shape_line_count = ocache->no_shape_line_count;
  snapshot->sb_4_1_count = ocache->sb_4_1_count;
  snapshot->sb_1_4_count = ocache->sb_1_4_count;
  snapshot->sb_2_2_count = ocache->sb_2_2_count;
  // Also copy the histogram array
  memcpy(snapshot->size_histogram, ocache->size_histogram, sizeof(ocache->size_histogram));
  memcpy(snapshot->sb_logical_line_count_dist, 
         ocache->sb_logical_line_count_dist, 
         sizeof(ocache->sb_logical_line_count_dist));
  return;
}

// Saves snapshots into a text file
// The format is as follows:
// Each entry of the snapshot is one line, comma separated list
// This function does not create or write any file if there is no snapshot
void ocache_save_stat_snapshot(ocache_t *ocache, const char *filename) {
  FILE *fp = fopen(filename, "w");
  SYSEXPECT(fp != NULL);
  fprintf(fp, "logical_line_count, "
              "effective_size, "
              "sb_slot_count, sb_logical_line_count, "
              "sb_logical_line_size_sum, "
              "data_occupancy (unaligned), "
              "sb_aligned_logical_line_size_sum, "
              "data_occupancy (aligned), "
              "base_missing_sb_count, base_found_sb_count, "
              "base_missing_ratio, "
              "sb_4_1_count, sb_1_4_count, sb_2_2_count, no_shape_line_count, "
              // Size distribution
              "1-8, 9-16, 17-24, 25-32, 33-40, 41-48, 49-56, 57-64, "
              // Logical line count distribution
              "singleton, pair, trio, quad\n");
  ocache_stat_snapshot_t *snapshot = ocache->stat_snapshot_head;
  while(snapshot != NULL) {
    fprintf(fp, "%lu, "    // logical line count
                "%.2lf, "  // Effective size
                "%lu, %lu, " // sb slot count, sb line count
                "%lu, "    // Unaligned line size sum
                "%.2lf, "  // Unaligned occupancy
                "%lu, "    // Aligned line size sum
                "%.2lf, "  // Aligned occupancy
                "%lu, %lu, " // Base missing/found count
                "%.2lf, "  // Base missing ratio
                "%lu, %lu, %lu, %lu", // sb type count
      snapshot->logical_line_count, 
      (double)snapshot->logical_line_count / (double)ocache->data_line_count, // Use data line count here for uncomp
      snapshot->sb_slot_count, snapshot->sb_logical_line_count,
      snapshot->sb_logical_line_size_sum,
      100.0 * (double)snapshot->sb_logical_line_size_sum / ((double)ocache->data_line_count * UTIL_CACHE_LINE_SIZE),
      snapshot->sb_aligned_logical_line_size_sum,
      100.0 * (double)snapshot->sb_aligned_logical_line_size_sum / ((double)ocache->data_line_count * UTIL_CACHE_LINE_SIZE),
      snapshot->base_missing_sb_count, snapshot->base_found_sb_count, 
      100.0 * (double)snapshot->base_missing_sb_count / (double)snapshot->sb_slot_count,
      snapshot->sb_4_1_count, snapshot->sb_1_4_count, snapshot->sb_2_2_count, snapshot->no_shape_line_count);
    // Then print histogram elements, note that we prepend a comma before the number
    for(int i = 0;i < 8;i++) {
      fprintf(fp, ", %lu", snapshot->size_histogram[i]);
    }
    // Print logical line count distribution
    for(int i = 0;i < 4;i++) {
      fprintf(fp, ", %lu", snapshot->sb_logical_line_count_dist[i]);
    }
    fputc('\n', fp);
    snapshot = snapshot->next;
  }
  fclose(fp);
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
  for(int i = 0;i < ocache->tag_line_count;i++) {
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
  for(int i = 0;i < ocache->tag_line_count;i++) {
    if(ocache_entry_is_all_invalid(curr) == 0) {
      ocache_entry_inv(curr);
      count++;
    }
    curr++;
  }
  return count;
}

int ocache_is_line_invalid(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape) {
  ocache_op_result_t lookup_result;
  _ocache_lookup(ocache, oid, addr, shape, 0, 0, &lookup_result);
  return lookup_result.state == OCACHE_MISS;
}

// Reports error if the line is not found
int ocache_is_line_dirty(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape) {
  ocache_op_result_t lookup_result;
  _ocache_lookup(ocache, oid, addr, shape, 0, 0, &lookup_result);
  if(lookup_result.state == OCACHE_MISS) {
    error_exit("The line is not present in the cache\n");
  }
  return ocache_entry_is_dirty(lookup_result.hit_entry, lookup_result.hit_index);
}

int ocache_get_line_size(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape) {
  ocache_op_result_t lookup_result;
  _ocache_lookup(ocache, oid, addr, shape, 0, 0, &lookup_result);
  if(lookup_result.state == OCACHE_MISS) {
    error_exit("The line is not present in the cache\n");
  }
  return ocache_entry_get_size(lookup_result.hit_entry, lookup_result.hit_index);
}

int ocache_get_valid_tag_count(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape) {
  int count = 0;
  ocache_entry_t *curr = ocache_get_set_begin(ocache, oid, addr, shape);
  for(int i = 0;i < ocache->tag_way_count;i++) {
    if(ocache_entry_is_all_invalid(curr) == 0) {
      count++;
    }
    curr++;
  }
  return count;
}

void ocache_set_print(ocache_t *ocache, int set) {
  ocache_entry_t *curr = ocache->data + set * ocache->tag_way_count;
  printf("---------- level %d name %s set %d ----------\n", 
    ocache->level, ocache->name ? ocache->name : "N/A", set);
  for(int i = 0;i < ocache->tag_way_count;i++) {
    if(ocache_entry_is_all_invalid(curr) == 1) {
      assert(curr->lru == 0);
      curr++; // There was a bug...
      continue;
    }
    printf("[Way %d] oid 0x%lX addr 0x%lX shape %s LRU %lu state 0x%02X (dirty ",
      i, curr->oid, curr->addr, ocache_shape_names[curr->shape], curr->lru, curr->states);
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
  printf("State bits and sizes are print from index 3 to index 0\n");
  printf("----------------------------\n");
  return;
}

void ocache_conf_print(ocache_t *ocache) {
  printf("---------- ocache_t conf ----------\n");
  printf("name \"%s\" level %d size %d bytes "
         "data lines %d tag lines %d "
         "sets %d "
         "data ways %d tag ways %d set size %d bytes\n", 
    ocache->name ? ocache->name : "[No Name]", ocache->level, ocache->size, 
    ocache->data_line_count, ocache->tag_line_count,
    ocache->set_count, 
    ocache->data_way_count, ocache->tag_way_count, 
    ocache->set_size);
  printf("use_shape %d dmap %p line size %lu\n", ocache->use_shape, ocache->dmap, UTIL_CACHE_LINE_SIZE);
  for(int i = 0;i < OCACHE_SHAPE_COUNT;i++) {
    printf("name \"%s\" addr {mask 0x%lX shift %d} OID {mask 0x%lX shift %d}\n", 
      ocache_shape_names[i], ocache->indices[i].addr_mask, ocache->indices[i].addr_shift,
      ocache->indices[i].oid_mask, ocache->indices[i].oid_shift);
  }
  printf("Evict opt type: ");
  switch(ocache->evict_opt_type) {
    case OCACHE_EVICT_OPT_NONE: printf("None\n"); break;
    case OCACHE_EVICT_OPT0: printf("OPT0 (always evict entire tag)\n"); break;
    case OCACHE_EVICT_OPT1: printf("OPT1 (evict entire tag if base is selected as victim)\n"); break;
    default: printf("Unknown type (internal error)\n"); break;
  }
  printf("Algorithm: ");
  if(ocache->get_compressed_size_cb == ocache_get_compressed_size_BDI_cb) {
    printf("BDI\n");
  } else if(ocache->get_compressed_size_cb == ocache_get_compressed_size_BDI_third_party_cb) {
    printf("BDI third party\n");
  } else if(ocache->get_compressed_size_cb == ocache_get_compressed_size_FPC_cb) {
    printf("FPC\n");
  } else if(ocache->get_compressed_size_cb == ocache_get_compressed_size_FPC_third_party_cb) {
    printf("FPC third party\n");
  } else if(ocache->get_compressed_size_cb == ocache_get_compressed_size_CPACK_cb) {
    printf("CPACK\n");
  } else if(ocache->get_compressed_size_cb == ocache_get_compressed_size_MBD_cb) {
    printf("MBD (dict size %d bits %d Repl.Algo. %s runahead %d throughput %d type %s)\n", 
      MBD_DICT_COUNT, MBD_DICT_INDEX_BITS,
      #ifdef MBD_DICT_LRU
        "LRU",
      #else
        "FIFO",
      #endif
      MBD_RUNAHEAD, MBD_THROUGHPUT,
      ocache_MBD_type_names[ocache->MBD_type]);
  } else if(ocache->get_compressed_size_cb == ocache_get_compressed_size_None_cb) {
    printf("None\n");
  } else {
    printf("Unknown (debugging?)\n");
  }
  // MBD optimization
  printf("MBD opt: ");
  if(ocache->MBD_opt_type == OCACHE_MBD_OPT_NONE) {
    printf("None (fixed 8 cycle decomp)\n");
  } else if(ocache->MBD_opt_type == OCACHE_MBD_OPT0) {
    printf("OPT0 (zero optimization)\n");
  } else if(ocache->MBD_opt_type == OCACHE_MBD_OPT1) {
    printf("OPT1 (zero opt + runahead forwarding)\n");
  } else if(ocache->MBD_opt_type == OCACHE_MBD_OPT2) {
    printf("OPT2 (BDI for ref and MBD opt0 for target)\n");
  } else if(ocache->MBD_opt_type == OCACHE_MBD_OPT3) {
    printf("OPT3 (zero opt + runahead forwarding) + new encoding for zero-base small delta\n");
  } else {
    printf("Unknown type (internal error)\n");
  }
  // Insert type
  printf("Insert type: ");
  if(ocache->insert_cb == ocache_insert_sb_cb) {
    printf("sector cache (super-block based)\n");
  } else if(ocache->insert_cb == ocache_insert_seg_cb) {
    printf("segmented cache\n");
  } else {
    printf("Unknown insertion method call back (likely an error)\n");
  }
  if(ocache->bcache != NULL) {
    bcache_conf_print(ocache->bcache);
  }
  printf("-----------------------------------\n");
  return;
}

// This function will print vertical compression stats if use_shape is 1
void ocache_stat_print(ocache_t *ocache) {
  printf("---------- ocache_t stat ----------\n");
  // Insert stat
  printf("Insert %lu SB hit %lu SB miss %lu (%.2lf%% rate)\n",
    ocache->insert_count, ocache->insert_sb_hit_count, ocache->insert_sb_miss_count,
    100.0 * (double)ocache->insert_sb_hit_count / (double)ocache->insert_count);
  printf("Evict opt type: ");
  switch(ocache->evict_opt_type) {
    case OCACHE_EVICT_OPT_NONE: printf("None\n"); break;
    case OCACHE_EVICT_OPT0: printf("OPT0 (always evict entire tag)\n"); break;
    case OCACHE_EVICT_OPT1: printf("OPT1 (evict entire tag if base is selected as victim)\n"); break;
    default: printf("Unknown type (internal error)\n"); break;
  }
  printf("Evict reasons tag %lu data %lu\n", 
    ocache->insert_evict_tag_contention_count, ocache->insert_evict_data_contention_count);
  printf("Evict data contention lines %lu (%.2lf%% iter lines) iters %lu iter lines %lu\n",
    ocache->insert_evict_data_contention_line_count,
    100.0 * (double)ocache->insert_evict_data_contention_line_count / (double)ocache->insert_evict_data_contention_iter_line_count,
    ocache->insert_evict_data_contention_iter_count,
    ocache->insert_evict_data_contention_iter_line_count);
  // Vertical compression stats, only printed for LLC
  if(ocache->get_compressed_size_cb == &ocache_get_compressed_size_BDI_cb || 
     ocache->get_compressed_size_cb == ocache_get_compressed_size_BDI_third_party_cb) {
    printf("Total attempted %lu BDI success %lu (failed %lu, rate %.2lf%%) not base %lu base found %lu"
           " type match %lu vertical success %lu (rate from BDI %.2lf%%, overall %.2lf%%)\n",
      ocache->comp_attempt_count, ocache->BDI_success_count, ocache->BDI_failed_count,
      100.0 * ocache->BDI_success_count / (double)ocache->comp_attempt_count,
      ocache->vertical_not_base_count, ocache->vertical_base_found_count,
      ocache->vertical_same_type_count, ocache->vertical_success_count,
      100.0 * ocache->vertical_success_count / (double)ocache->BDI_success_count,
      100.0 * ocache->vertical_success_count / (double)ocache->comp_attempt_count);
    printf("Vertical success only before size %lu after %lu (ratio %.2f%%, %.2fx)\n",
      ocache->vertical_before_size_sum, ocache->vertical_after_size_sum,
      100.0 * ocache->vertical_after_size_sum / (double)ocache->vertical_before_size_sum,
      (double)ocache->vertical_before_size_sum / (double)ocache->vertical_after_size_sum);
    printf("BDI success only %lu before size %lu after %lu (ratio %.2f%%, %.2fx)\n", 
      ocache->BDI_success_count, ocache->BDI_before_size_sum, ocache->BDI_after_size_sum,
      100.0 * ocache->BDI_after_size_sum / (double)ocache->BDI_before_size_sum,
      (double)ocache->BDI_before_size_sum / (double)ocache->BDI_after_size_sum);
    printf("BDI overall success rate %.2f%% before size %lu after %lu (ratio %.2f%%, %.2fx)\n",
      (100.0 * ocache->BDI_success_count) / (double)ocache->comp_attempt_count,
      ocache->comp_attempt_size_sum, 
      ocache->BDI_after_size_sum + ocache->BDI_uncomp_size_sum,
      100.0 * (ocache->BDI_after_size_sum + ocache->BDI_uncomp_size_sum) / 
        (double)ocache->comp_attempt_size_sum, 
      (double)ocache->comp_attempt_size_sum / 
        (double)(ocache->BDI_after_size_sum + ocache->BDI_uncomp_size_sum));
    printf("Vertical overall success rate %.2f%% before size %lu after %lu (ratio %.2f%%, %.2fx) \n",
      (100.0 * ocache->vertical_success_count) / (double)ocache->BDI_success_count,
      ocache->BDI_after_size_sum, 
      ocache->vertical_uncomp_size_sum + ocache->vertical_after_size_sum,
      100.0 * (ocache->vertical_uncomp_size_sum + ocache->vertical_after_size_sum) / 
        (double)ocache->BDI_after_size_sum,
      (double)ocache->BDI_after_size_sum / 
        (double)(ocache->vertical_uncomp_size_sum + ocache->vertical_after_size_sum));
    printf("Overall success rate %.2f%% before size %lu after size %lu (ratio %.2f%%, %.2fx)\n",
      100.0 * (ocache->vertical_success_count) / (double)ocache->comp_attempt_count,
      ocache->comp_attempt_size_sum,
      ocache->BDI_uncomp_size_sum + ocache->vertical_uncomp_size_sum + ocache->vertical_after_size_sum,
      100.0 * (ocache->BDI_uncomp_size_sum + ocache->vertical_uncomp_size_sum + ocache->vertical_after_size_sum) / 
        (double)ocache->comp_attempt_size_sum, 
      (double)ocache->comp_attempt_size_sum / 
        (double)(ocache->BDI_uncomp_size_sum + ocache->vertical_uncomp_size_sum + ocache->vertical_after_size_sum));
    printf("BDI types");
    for(int i = 0;i < BDI_TYPE_END;i++) {
      printf(" [%s] %lu", BDI_names[i], ocache->BDI_compress_type_counts[i]);
    }
    putchar('\n');
    printf("BDI size classes: ");
    for(int i = 0;i < 8;i++) {
      printf("[%d-%d] %lu ", i * 8 + 1, i * 8 + 8, ocache->BDI_size_counts[i]);
    }
    putchar('\n');
    assert(ocache->BDI_compress_type_counts[0] == 0 && ocache->BDI_compress_type_counts[1] == 0);
    // These two constitute all cases
    assert(ocache->BDI_success_count + ocache->BDI_failed_count == ocache->comp_attempt_count);
    assert(ocache->vertical_success_count + ocache->vertical_uncomp_count == ocache->BDI_success_count);
  } else if(ocache->get_compressed_size_cb == &ocache_get_compressed_size_FPC_cb || 
            ocache->get_compressed_size_cb == &ocache_get_compressed_size_FPC_third_party_cb) {
    printf("FPC attempt %lu success %lu fail %lu (rate %.2lf%%)\n",
      ocache->comp_attempt_count, ocache->FPC_success_count, ocache->FPC_fail_count,
      100.0 * ocache->FPC_success_count / (double)ocache->comp_attempt_count);
    printf("FPC success only before %lu after %lu (ratio %.2lf%%, %.2lfx)\n",
      ocache->FPC_before_size_sum, ocache->FPC_after_size_sum,
      100.0 * ocache->FPC_after_size_sum / (double)ocache->FPC_before_size_sum,
      (double)ocache->FPC_before_size_sum / (double)ocache->FPC_after_size_sum);
    printf("FPC overall before %lu after %lu (ratio %.2lf%%, %.2lfx)\n",
      ocache->comp_attempt_size_sum, ocache->FPC_uncomp_size_sum + ocache->FPC_after_size_sum,
      100.0 * (ocache->FPC_uncomp_size_sum + ocache->FPC_after_size_sum) / (double)ocache->comp_attempt_size_sum,
      (double)ocache->comp_attempt_size_sum / (double)(ocache->FPC_uncomp_size_sum + ocache->FPC_after_size_sum));
    double FPC_type_stat_total = 0.0;
    for(int i = 0;i < 8;i++) {
      FPC_type_stat_total += (double)ocache->FPC_type_stat.type_counts[i];
    }
    printf("FPC type stats:");
    for(int i = 0;i < 8;i++) {
      printf(" [%s] %lu (%.2lf%%)", 
        FPC_type_names[i], ocache->FPC_type_stat.type_counts[i], 
        100.0 * (double)ocache->FPC_type_stat.type_counts[i] / FPC_type_stat_total);
    }
    putchar('\n');
    printf("FPC size classes ");
    for(int i = 0;i < 8;i++) {
      printf("[%d-%d] %lu ", i * 8 + 1, i * 8 + 8, ocache->FPC_size_counts[i]);
    }
    putchar('\n');
  } else if(ocache->get_compressed_size_cb == &ocache_get_compressed_size_CPACK_cb) {
    printf("CPACK attempt %lu success %lu fail %lu (rate %.2lf%%)\n",
      ocache->comp_attempt_count, ocache->CPACK_success_count, ocache->CPACK_fail_count,
      100.0 * ocache->CPACK_success_count / (double)ocache->comp_attempt_count);
    printf("CPACK success only before %lu after %lu (ratio %.2lf%%, %.2lfx)\n",
      ocache->CPACK_before_size_sum, ocache->CPACK_after_size_sum,
      100.0 * ocache->CPACK_after_size_sum / (double)ocache->CPACK_before_size_sum,
      (double)ocache->CPACK_before_size_sum / (double)ocache->CPACK_after_size_sum);
    printf("CPACK overall before %lu after %lu (ratio %.2lf%%, %.2lfx)\n",
      ocache->comp_attempt_size_sum, ocache->CPACK_uncomp_size_sum + ocache->CPACK_after_size_sum,
      100.0 * (ocache->CPACK_uncomp_size_sum + ocache->CPACK_after_size_sum) / (double)ocache->comp_attempt_size_sum,
      (double)ocache->comp_attempt_size_sum / (double)(ocache->CPACK_uncomp_size_sum + ocache->CPACK_after_size_sum));
    printf("CPACK types ");
    uint64_t CPACK_type_total = 0UL;
    for(int i = 0;i < 6;i++) {
      CPACK_type_total += ocache->CPACK_type_stat.type_counts[i];
    }
    for(int i = 0;i < 6;i++) {
      printf("[%s] %lu (%.2lf%%) ", 
        CPACK_type_names[i], ocache->CPACK_type_stat.type_counts[i],
        100.0 * (double)ocache->CPACK_type_stat.type_counts[i] / (double)CPACK_type_total);
    }
    putchar('\n');
    printf("CPACK size classes ");
    for(int i = 0;i < 8;i++) {
      printf("[%d-%d] %lu ", i * 8 + 1, i * 8 + 8, ocache->CPACK_size_counts[i]);
    }
    putchar('\n');
  } else if(ocache->get_compressed_size_cb == &ocache_get_compressed_size_MBD_cb) {
    printf("MBD dict entries %d bits %d Repl.Algo. %s runahead %d throughput %d type %s\n", 
      MBD_DICT_COUNT, MBD_DICT_INDEX_BITS, 
      #ifdef MBD_DICT_LRU
        "LRU",
      #else
        "FIFO",
      #endif
      MBD_RUNAHEAD, MBD_THROUGHPUT,
      ocache_MBD_type_names[ocache->MBD_type]);
    printf("MBD attempt %lu success %lu fail %lu (rate %.2lf%%)\n",
      ocache->comp_attempt_count, ocache->MBD_success_count, ocache->MBD_fail_count,
      100.0 * ocache->MBD_success_count / (double)ocache->comp_attempt_count);
    printf("MBD success only before %lu after %lu (ratio %.2lf%%, %.2lfx)\n",
      ocache->MBD_before_size_sum, ocache->MBD_after_size_sum,
      100.0 * ocache->MBD_after_size_sum / (double)ocache->MBD_before_size_sum,
      (double)ocache->MBD_before_size_sum / (double)ocache->MBD_after_size_sum);
    printf("MBD overall before %lu after %lu (ratio %.2lf%%, %.2lfx)\n",
      ocache->comp_attempt_size_sum, ocache->MBD_uncomp_size_sum + ocache->MBD_after_size_sum,
      100.0 * (ocache->MBD_uncomp_size_sum + ocache->MBD_after_size_sum) / (double)ocache->comp_attempt_size_sum,
      (double)ocache->comp_attempt_size_sum / (double)(ocache->MBD_uncomp_size_sum + ocache->MBD_after_size_sum));
    printf("MBD decomp %lu cycles %lu (avg %.2lf) zeros %lu (avg %.2lf) zero hits %lu (%.2lf%%)\n",
      ocache->MBD_decomp_count, 
      ocache->MBD_decomp_cycle_count, (double)ocache->MBD_decomp_cycle_count / (double)ocache->MBD_decomp_count,
      ocache->MBD_decomp_zero_count, (double)ocache->MBD_decomp_zero_count / (double)ocache->MBD_decomp_count,
      ocache->MBD_decomp_hit_zero_count, 100.0 * (double)ocache->MBD_decomp_hit_zero_count / (double)ocache->MBD_decomp_count);
    // cycle distribution
    int decomp_cycle_dist_begin = 0;
    int decomp_cycle_dist_end = 17; 
    // Zero optimization is only computed for opt none
    if(ocache->MBD_opt_type == OCACHE_MBD_OPT_NONE) {
      printf("MBD opt type: None\n");
      // Print dist [2, 8]
      decomp_cycle_dist_begin = 2;
      decomp_cycle_dist_end = 9;
    } else if(ocache->MBD_opt_type == OCACHE_MBD_OPT0) {
      printf("MBD opt type: OPT0\n");
      // dist range [2, 8]
      decomp_cycle_dist_begin = 2;
      decomp_cycle_dist_end = 9;
    } else if(ocache->MBD_opt_type == OCACHE_MBD_OPT1 || ocache->MBD_opt_type == OCACHE_MBD_OPT3) {
      printf("MBD opt type: %s\n", (ocache->MBD_opt_type == OCACHE_MBD_OPT1) ? "OPT1" : "OPT3");
      printf("MBD forwards %lu (%.2lf%% total words)\n"
             "    stalled forward %lu (%.2lf%% total forwarded) match %lu (%.2lf%% total forwarded)\n"
             "    stalled cycles %lu (%.2lf%% total cycles)\n",
        ocache->MBD_decomp_forward_count, 
        100.0 * (double)ocache->MBD_decomp_forward_count / ((double)ocache->MBD_decomp_count * 16.0),
        ocache->MBD_decomp_forward_stalled_count,
        100.0 * (double)ocache->MBD_decomp_forward_stalled_count / (double)ocache->MBD_decomp_forward_count,
        ocache->MBD_decomp_forward_uncomp_count,
        100.0 * (double)ocache->MBD_decomp_forward_uncomp_count / (double)ocache->MBD_decomp_forward_count,
        ocache->MBD_decomp_stalled_cycle_count,
        100.0 * (double)ocache->MBD_decomp_stalled_cycle_count / (double)ocache->MBD_decomp_cycle_count);
      printf("Stalled cycle dist:");
      uint64_t stalled_cycle_dist_total = 0UL;
      for(int i = 0;i <= 8;i++) {
        stalled_cycle_dist_total += ocache->MBD_decomp_stalled_cycle_dist[i];
      }
      for(int i = 0;i <= 8;i++) {
        printf(" [%d] %lu (%.2lf%%)", i, ocache->MBD_decomp_stalled_cycle_dist[i],
          100.0 * (double)ocache->MBD_decomp_stalled_cycle_dist[i] / (double)stalled_cycle_dist_total);
      }
      putchar('\n');
      // dist range [2, 16]
      decomp_cycle_dist_begin = 2;
      decomp_cycle_dist_end = 17;
    } else if(ocache->MBD_opt_type == OCACHE_MBD_OPT2) {
      printf("MBD opt type: OPT2\n");
      // dist range [2, 8]
      decomp_cycle_dist_begin = 2;
      decomp_cycle_dist_end = 9;
    } else {
      error_exit("Unknown MBD opt type: %d\n", ocache->MBD_opt_type);
    }
    printf("MBD decomp cycle dist:");
    uint64_t decomp_cycle_dist_total = 0UL;
    for(int i = decomp_cycle_dist_begin;i < decomp_cycle_dist_end;i++) {
      decomp_cycle_dist_total += ocache->MBD_decomp_cycle_dist[i];
    }
    for(int i = decomp_cycle_dist_begin;i < decomp_cycle_dist_end;i++) {
      printf(" [%d] %lu (%.2lf%%)", i, ocache->MBD_decomp_cycle_dist[i], 
        100.0 * (double)ocache->MBD_decomp_cycle_dist[i] / (double)decomp_cycle_dist_total);
    }
    putchar('\n');
    // base cache hit/miss
    printf("MBD insert base hit %lu missing %lu (base hit percentage %.2lf%%)\n",
      ocache->MBD_insert_base_hit, ocache->MBD_insert_base_miss,
      100.0 * ocache->MBD_insert_base_hit / 
        (double)(ocache->MBD_insert_base_hit + ocache->MBD_insert_base_miss));
    if(ocache->bcache != NULL) {
      printf("    bcache hit %lu miss %lu (rate %.2lf%%), actual base hit %.2lf%%\n",
        ocache->MBD_insert_bcache_hit, ocache->MBD_insert_bcache_miss,
        100.0 * ocache->MBD_insert_bcache_hit / 
          (double)(ocache->MBD_insert_bcache_hit + ocache->MBD_insert_bcache_miss),
        100.0 * (ocache->MBD_insert_base_hit + ocache->MBD_insert_bcache_hit) / 
        (double)(ocache->MBD_insert_base_hit + ocache->MBD_insert_base_miss));
    }
    printf("MBD read base hit %lu missing %lu (base hit percentage %.2lf%%)\n",
    ocache->MBD_read_base_is_present, ocache->MBD_read_base_is_missing,
    100.0 * ocache->MBD_read_base_is_present / 
      (double)(ocache->MBD_read_base_is_present + ocache->MBD_read_base_is_missing));
    if(ocache->bcache != NULL) {
      printf("    bcache hit %lu miss %lu (rate %.2lf%%), actual base hit %.2lf%%\n",
        ocache->MBD_read_bcache_hit, ocache->MBD_read_bcache_miss,
        100.0 * ocache->MBD_read_bcache_hit / 
          (double)(ocache->MBD_read_bcache_hit + ocache->MBD_read_bcache_miss),
        100.0 * (ocache->MBD_read_base_is_present + ocache->MBD_read_bcache_hit) / 
        (double)(ocache->MBD_read_base_is_present + ocache->MBD_read_base_is_missing));
    }
    uint64_t MBD_total_case_count = \
      ocache->MBD_zero_count + ocache->MBD_match_count + ocache->MBD_uncomp_count + \
      ocache->MBD_delta_size_dist[0] + ocache->MBD_delta_size_dist[1] + ocache->MBD_delta_size_dist[2] + \
      ocache->MBD_delta_size_dist[3] + 
      ocache->MBD_small_size_dist[0] + ocache->MBD_small_size_dist[1] + ocache->MBD_small_size_dist[2] + \
      ocache->MBD_small_size_dist[3];
    printf("MBD Per-word stats [ZERO] %lu (%.2lf%%) [MATCH] %lu (%.2lf%%) [UNCOMP] %lu (%.2lf%%)\n"
           "    delta [4-bit] %lu (%.2lf%%) [8-bit] %lu (%.2lf%%) [12-bit] %lu (%.2lf%%) [16-bit] %lu (%.2lf%%)\n"
           "    small [4-bit] %lu (%.2lf%%) [8-bit] %lu (%.2lf%%) [12-bit] %lu (%.2lf%%) [16-bit] %lu (%.2lf%%)\n",
      ocache->MBD_zero_count, 100.0 * (double)ocache->MBD_zero_count / (double)MBD_total_case_count,
      ocache->MBD_match_count, 100.0 * (double)ocache->MBD_match_count / (double)MBD_total_case_count,
      ocache->MBD_uncomp_count, 100.0 * (double)ocache->MBD_uncomp_count / (double)MBD_total_case_count,
      ocache->MBD_delta_size_dist[0], 100.0 * (double)ocache->MBD_delta_size_dist[0] / (double)MBD_total_case_count,
      ocache->MBD_delta_size_dist[1], 100.0 * (double)ocache->MBD_delta_size_dist[1] / (double)MBD_total_case_count,
      ocache->MBD_delta_size_dist[2], 100.0 * (double)ocache->MBD_delta_size_dist[2] / (double)MBD_total_case_count,
      ocache->MBD_delta_size_dist[3], 100.0 * (double)ocache->MBD_delta_size_dist[3] / (double)MBD_total_case_count,
      ocache->MBD_small_size_dist[0], 100.0 * (double)ocache->MBD_small_size_dist[0] / (double)MBD_total_case_count,
      ocache->MBD_small_size_dist[1], 100.0 * (double)ocache->MBD_small_size_dist[1] / (double)MBD_total_case_count,
      ocache->MBD_small_size_dist[2], 100.0 * (double)ocache->MBD_small_size_dist[2] / (double)MBD_total_case_count,
      ocache->MBD_small_size_dist[3], 100.0 * (double)ocache->MBD_small_size_dist[3] / (double)MBD_total_case_count);
    printf("MBD dict inserts %lu\n", ocache->MBD_dict_insert_count);
    printf("MBD size classes ");
    for(int i = 0;i < 8;i++) {
      printf("[%d-%d] %lu ", i * 8 + 1, i * 8 + 8, ocache->MBD_size_counts[i]);
    }
    putchar('\n');
  } else {
    printf("Unknown compression method (debugging?)\n");
  }
  // General stats refershed by ocache_refresh_stat(). These stats are common to all compression types.
  ocache_refresh_stat(ocache);
  // Then compute sum if there are snapshots
  int stat_snapshot_count = 1; // The current data is counted as one
  ocache_stat_snapshot_t *curr = ocache->stat_snapshot_head;
  while(curr != NULL) {
    ocache->logical_line_count += curr->logical_line_count;
    ocache->sb_slot_count += curr->sb_slot_count;
    ocache->sb_logical_line_count += curr->sb_logical_line_count;
    ocache->sb_logical_line_size_sum += curr->sb_logical_line_size_sum;
    ocache->sb_aligned_logical_line_size_sum += curr->sb_aligned_logical_line_size_sum;
    ocache->base_missing_sb_count += curr->base_missing_sb_count;
    ocache->base_found_sb_count += curr->base_found_sb_count;
    ocache->sb_4_1_count += curr->sb_4_1_count;
    ocache->sb_1_4_count += curr->sb_1_4_count;
    ocache->sb_2_2_count += curr->sb_2_2_count;
    ocache->no_shape_line_count += curr->no_shape_line_count;
    for(int i = 0;i < 8;i++) {
      ocache->size_histogram[i] += curr->size_histogram[i];
    }
    for(int i = 0;i < 4;i++) {
      ocache->sb_logical_line_count_dist[i] += curr->sb_logical_line_count_dist[i];
    }
    stat_snapshot_count++;
    curr = curr->next;
  }
  // Compute average 
  ocache->logical_line_count /= stat_snapshot_count;
  ocache->sb_slot_count /= stat_snapshot_count;
  ocache->sb_logical_line_count /= stat_snapshot_count;
  ocache->sb_logical_line_size_sum /= stat_snapshot_count;
  ocache->sb_aligned_logical_line_size_sum /= stat_snapshot_count;
  ocache->base_missing_sb_count /= stat_snapshot_count;
  ocache->base_found_sb_count /= stat_snapshot_count;
  ocache->sb_4_1_count /= stat_snapshot_count;
  ocache->sb_1_4_count /= stat_snapshot_count;
  ocache->sb_2_2_count /= stat_snapshot_count;
  ocache->no_shape_line_count /= stat_snapshot_count;
  for(int i = 0;i < 8;i++) {
    ocache->size_histogram[i] /= stat_snapshot_count;
  }
  for(int i = 0;i < 4;i++) {
    ocache->sb_logical_line_count_dist[i] /= stat_snapshot_count;
  }
  printf("Snapshot stats (avg. over %d snapshots):\n", stat_snapshot_count);
  printf("Logical lines %lu (%.2lfx uncomp) sb %lu sb logical lines %lu (avg. %.2lf lines per sb)"
         " sb line size %lu (data occupancy %.2lf%%) aligned sb line size %lu (aligned data occupancy %.2lf%%)\n",
    ocache->logical_line_count, 
    (double)ocache->logical_line_count / (double)ocache->data_line_count,
    ocache->sb_slot_count, ocache->sb_logical_line_count,
    (double)ocache->sb_logical_line_count / (double)ocache->sb_slot_count,
    ocache->sb_logical_line_size_sum,
    100.0 * (double)ocache->sb_logical_line_size_sum / ((double)ocache->data_line_count * UTIL_CACHE_LINE_SIZE),
    ocache->sb_aligned_logical_line_size_sum,
    100.0 * (double)ocache->sb_aligned_logical_line_size_sum / ((double)ocache->data_line_count * UTIL_CACHE_LINE_SIZE));
  printf("Base missing SB %lu (%.2lf%%) found %lu (%.2lf%%)\n",
    ocache->base_missing_sb_count, 
    100.0 * (double)ocache->base_missing_sb_count / (double)ocache->sb_slot_count,
    ocache->base_found_sb_count,
    100.0 * (double)ocache->base_found_sb_count / (double)ocache->sb_slot_count);
  printf("[4-1] %lu [1-4] %lu [2-2] %lu [no shape] %lu\n", 
    ocache->sb_4_1_count, ocache->sb_1_4_count, ocache->sb_2_2_count, ocache->no_shape_line_count);
  printf("Size histogram: ");
  for(int i = 0;i < 8;i++) {
    printf("[%d-%d] %lu ", i * 8 + 1, (i + 1) * 8, ocache->size_histogram[i]);
  }
  putchar('\n');
  printf("Logical line count dist: ");
  for(int i = 0;i < 4;i++) {
    printf("[%d] %lu ", i + 1, ocache->sb_logical_line_count_dist[i]);
  }
  putchar('\n');
  // Print bcache stat
  if(ocache->bcache != NULL) {
    bcache_stat_print(ocache->bcache);
  }
  printf("-----------------------------------\n");
  return;
}

//* scache_stat_snapshot_t

scache_stat_snapshot_t *scache_stat_snapshot_init() {
  scache_stat_snapshot_t *snapshot = (scache_stat_snapshot_t *)malloc(sizeof(scache_stat_snapshot_t));
  SYSEXPECT(snapshot != NULL);
  memset(snapshot, 0x00, sizeof(scache_stat_snapshot_t));
  return snapshot;
}

void scache_stat_snapshot_free(scache_stat_snapshot_t *snapshot) {
  free(snapshot);
  return;
}

//* scache_t

void scache_init_param(scache_t *scache, int size, int physical_way_count, int ratio) {
  if(size % UTIL_CACHE_LINE_SIZE != 0) {
    error_exit("Size must be a multiple of cache line size (see %d)\n", size);
  }
  scache->physical_line_count = size / UTIL_CACHE_LINE_SIZE;
  if(scache->physical_line_count % physical_way_count != 0) {
    error_exit("Line count (%d) is not a multiple of number of ways (%d)\n", 
      scache->physical_line_count, physical_way_count);
  }
  scache->set_count = scache->physical_line_count / physical_way_count;
  if(popcount_int32(scache->set_count) != 1) {
    error_exit("Set count (%d) must be a power of two\n", scache->set_count);
  }
  // Number of bits in the set mask
  scache->set_bits = popcount_int32(scache->set_count - 1);
  scache->size = size;
  scache->physical_way_count = physical_way_count;
  if(ratio < 1) {
    error_exit("Compression ratio must >= 1 (see %d)\n", ratio);
  }
  scache->ratio = ratio;
  scache->way_count = physical_way_count * ratio; // Only need logical way count
  scache->set_size = physical_way_count * UTIL_CACHE_LINE_SIZE; // This must use physical way count
  scache->line_count = scache->way_count * scache->set_count; // Actual tag count
  // Initialize mask and shift
  scache->addr_shift = UTIL_CACHE_LINE_BITS;
  scache->addr_mask = ((uint64_t)scache->set_count - 1) << UTIL_CACHE_LINE_BITS;
  return;
}

scache_t *scache_init(int size, int physical_way_count, int ratio) {
  // Use a temp stack object first for allocation sizes
  scache_t temp;
  scache_init_param(&temp, size, physical_way_count, ratio);
  int alloc_size = sizeof(scache_t) + temp.line_count * sizeof(scache_entry_t);
  scache_t *scache = (scache_t *)malloc(alloc_size);
  SYSEXPECT(scache != NULL);
  memset(scache, 0x00, alloc_size);
  // Init param on the heap object
  scache_init_param(scache, size, physical_way_count, ratio);
  // Init all entries
  for(int i = 0;i < scache->line_count;i++) {
    scache_entry_invalidate(&scache->data[i]);
  }
  // LRU starts at 1, and invalid entry always has 0 as LRU
  scache->lru_counter = 1;
  return scache;
}

// Initialize a cache by giving number of sets
scache_t *scache_init_by_set_count(int set_count, int physical_way_count, int ratio) {
  assert(set_count > 0);
  if(popcount_int32(set_count) != 1) {
    error_exit("Invalid set_count for scache init (see %d)\n", set_count);
  }
  int size = set_count * physical_way_count * (int)UTIL_CACHE_LINE_SIZE;
  return scache_init(size, physical_way_count, ratio);
}

void scache_free(scache_t *scache) {
  // Free linked list
  scache_stat_snapshot_t *curr = scache->stat_snapshot_head;
  while(curr != NULL) {
    scache_stat_snapshot_t *next = curr->next;
    scache_stat_snapshot_free(curr);
    curr = next;
  }
  free(scache);
  return;
}

// get compressed size using BDI as compression engine
// Returns exact number of bytes
static int scache_get_compressed_size_BDI_cb(scache_t *scache, uint64_t addr) {
  scache->BDI_attempt_count++;
  scache->BDI_attempt_size += UTIL_CACHE_LINE_SIZE;
  // The data must already exist in data map
  int all_zero;
  // OID must be zero, meaning flat address space, and the addr is address
  dmap_entry_t *dmap_entry = dmap_find(scache->dmap, 0UL, addr);
  if(dmap_entry == NULL) {
    error_exit("Data to be compressed cannot be found on addr 0x%lX\n", addr);
  }
  int type = BDI_get_comp_size(dmap_entry->data);
  int size;
  if(type == BDI_TYPE_INVALID) {
    scache->BDI_fail_count++;
    scache->BDI_uncomp_size += UTIL_CACHE_LINE_SIZE;
    size = UTIL_CACHE_LINE_SIZE;
  } else {
    // Including BDI and all-zero lines
    scache->BDI_success_count++;
    assert(type >= BDI_TYPE_BEGIN && type < BDI_TYPE_END);
    size = BDI_types[type].compressed_size;
    if(type == 2) {
      all_zero = zero_comp(dmap_entry->data);
      if(all_zero == 1) {
        scache->all_zero_count++;
        size = 0;
      } else {
        scache->BDI_type_counts[type]++;
      }
    } else {
      scache->BDI_type_counts[type]++;
    }
    // Only count this for successful lines
    scache->BDI_before_size += UTIL_CACHE_LINE_SIZE;
    scache->BDI_after_size += size;
  }
  return size;
}

// Returns compressed line size in the nearest number of bytes using FPC (FPC itself returns bit size)
static int scache_get_compressed_size_FPC_cb(scache_t *scache, uint64_t addr) {
  scache->FPC_attempt_count++;
  scache->FPC_attempt_size += UTIL_CACHE_LINE_SIZE;
  // OID must be zero, meaning flat address space, and the addr is address
  dmap_entry_t *dmap_entry = dmap_find(scache->dmap, 0UL, addr);
  if(dmap_entry == NULL) {
    error_exit("Data to be compressed cannot be found on addr 0x%lX\n", addr);
  }
  int FPC_bits = FPC_get_comp_size_bits(dmap_entry->data);
  if(FPC_bits >= (int)UTIL_CACHE_LINE_SIZE * 8) {
    scache->FPC_fail_count++;
    scache->FPC_uncomp_size += UTIL_CACHE_LINE_SIZE;
    return UTIL_CACHE_LINE_SIZE;
  }
  int FPC_bytes = (FPC_bits + 7) / 8;
  scache->FPC_success_count++;
  scache->FPC_before_size += UTIL_CACHE_LINE_SIZE;
  scache->FPC_after_size += FPC_bytes;
  assert(FPC_bytes > 0 && FPC_bytes <= 64);
  // Update statistics for size classes. Note we must minus one to make value domain [0, 64)
  scache->FPC_size_counts[(FPC_bytes - 1) / 8]++;
  return FPC_bytes;
}

static int scache_get_compressed_size_CPACK_cb(scache_t *scache, uint64_t addr) {
  scache->CPACK_attempt_count++;
  scache->CPACK_attempt_size += UTIL_CACHE_LINE_SIZE;
  dmap_entry_t *dmap_entry = dmap_find(scache->dmap, 0UL, addr);
  if(dmap_entry == NULL) {
    error_exit("Data to be compressed cannot be found on addr 0x%lX\n", addr);
  }
  int bits = _CPACK_get_comp_size_bits(dmap_entry->data, &scache->CPACK_type_stat);
  if(bits >= (int)UTIL_CACHE_LINE_SIZE * 8) {
    scache->CPACK_fail_count++;
    scache->CPACK_uncomp_size += UTIL_CACHE_LINE_SIZE;
    return UTIL_CACHE_LINE_SIZE;
  }
  int bytes = (bits + 7) / 8;
  scache->CPACK_success_count++;
  scache->CPACK_before_size += UTIL_CACHE_LINE_SIZE;
  scache->CPACK_after_size += bytes;
  assert(bytes > 0 && bytes <= 64);
  // Update statistics for size classes. Note we must minus one to make value domain [0, 64)
  scache->CPACK_size_counts[(bytes - 1) / 8]++;
  return bytes;
}

static int scache_get_compressed_size_MBD_cb(scache_t *scache, uint64_t addr) {
  scache->MBD_attempt_count++;
  scache->MBD_attempt_size += UTIL_CACHE_LINE_SIZE;
  dmap_entry_t *dmap_entry = dmap_find(scache->dmap, 0UL, addr);
  if(dmap_entry == NULL) {
    error_exit("Data to be compressed cannot be found on addr 0x%lX\n", addr);
  }
  int bits = MBD_get_comp_size_bits(dmap_entry->data);
  if(bits >= (int)UTIL_CACHE_LINE_SIZE * 8) {
    scache->MBD_fail_count++;
    scache->MBD_uncomp_size += UTIL_CACHE_LINE_SIZE;
    return UTIL_CACHE_LINE_SIZE;
  }
  int bytes = (bits + 7) / 8;
  scache->MBD_success_count++;
  scache->MBD_before_size += UTIL_CACHE_LINE_SIZE;
  scache->MBD_after_size += bytes;
  assert(bytes > 0 && bytes <= 64);
  // Update statistics for size classes. Note we must minus one to make value domain [0, 64)
  scache->MBD_size_counts[(bytes - 1) / 8]++;
  return bytes;
}

// Trivial call back; No compression, always return full size cache line
static int scache_get_compressed_size_None_cb(scache_t *scache, uint64_t addr) {
  (void)addr; (void)scache;
  return (int)UTIL_CACHE_LINE_SIZE;
}

static uint64_t scache_access_hit_BDI_cb(scache_t *scache, uint64_t addr) {
  (void)addr; (void)scache;
  return SCACHE_BDI_DECOMP_LATENCY;
}

static uint64_t scache_access_hit_FPC_cb(scache_t *scache, uint64_t addr) {
  (void)addr; (void)scache;
  return SCACHE_FPC_DECOMP_LATENCY;
}

static uint64_t scache_access_hit_CPACK_cb(scache_t *scache, uint64_t addr) {
  (void)addr; (void)scache;
  return SCACHE_CPACK_DECOMP_LATENCY;
}

static uint64_t scache_access_hit_MBD_cb(scache_t *scache, uint64_t addr) {
  (void)addr; (void)scache;
  error_exit("Not supported\n");
  return 0UL;
}

static uint64_t scache_access_hit_None_cb(scache_t *scache, uint64_t addr) {
  (void)addr; (void)scache;
  return 0UL;
}

// This is called by external callers; The other method that directly specifies the function is called 
// by test code for instrumenting compression algorithm
void scache_set_compression_type(scache_t *scache, const char *name) {
  if(scache->get_compressed_size_cb != NULL) {
    error_exit("The compression call back has already been set\n");
  }
  if(streq(name, "BDI") == 1) {
    scache->get_compressed_size_cb = scache_get_compressed_size_BDI_cb;
    scache->access_hit_cb = scache_access_hit_BDI_cb;
  } else if(streq(name, "FPC") == 1) {
    scache->get_compressed_size_cb = scache_get_compressed_size_FPC_cb;
    scache->access_hit_cb = scache_access_hit_FPC_cb;
  } else if(streq(name, "CPACK") == 1) {
    scache->get_compressed_size_cb = scache_get_compressed_size_CPACK_cb;
    scache->access_hit_cb = scache_access_hit_CPACK_cb;
  } else if(streq(name, "MBD") == 1) {
    error_exit("scache with MBD is not supported yet (need to sync with ocache_t's call backs)\n");
    scache->get_compressed_size_cb = scache_get_compressed_size_MBD_cb;
    scache->access_hit_cb = scache_access_hit_MBD_cb;
  } else if(streq(name, "None") == 1) {
    scache->get_compressed_size_cb = scache_get_compressed_size_None_cb;
    scache->access_hit_cb = scache_access_hit_None_cb;
  } else {
    error_exit("Unknown compression type name: \"%s\"\n", name);
  }
  return;
}

// Lookup the given address in the set. There are several cases:
//   1. Hit. State is SCACHE_HIT, hit_entry is set to the entry that gets hit
//   2. Miss. State is SCACHE_MISS, lru_entry is set to the LRU entry, which could be an invalid one
// In all cases, result->total_aligned_size will be the size of entries that we have currently scanned
// This function may also optionally not update the LRU
// This function also does NOT update read_count
void _scache_lookup(scache_t *scache, uint64_t addr, int update_lru, int scan_all, scache_op_result_t *result) {
  scache_entry_t *entry = scache_get_set_begin(scache, addr);
  // LRU related variables
  scache_entry_t *lru_entry = NULL;
  uint64_t curr_lru = -1UL;
  // Should be set here to avoid mixing hit with miss
  result->state = SCACHE_MISS;
  result->total_aligned_size = 0;
  for(int i = 0;i < scache->way_count;i++) {
    // Update total size using aligned data; Must do it before the hit check
    result->total_aligned_size += scache_align_size(scache, entry->size);
    if(entry->addr == addr && entry->valid == 1) {
      assert(result->state == SCACHE_MISS);
      // Direct hit
      result->state = SCACHE_HIT;
      result->hit_entry = entry;
      // This is optional; for writes LRU is not updated since the write function itself
      // will update LRU to avoid repeated updates
      if(update_lru == 1) {
        scache_update_entry_lru(scache, entry);
      }
      // Fast-path for read-lookup, just exit when there is a hit
      if(scan_all == 0) {
        return;
      }
    }
    // Update LRU (can be invalid entry which always has LRU = 0)
    if(entry->lru < curr_lru) {
      curr_lru = entry->lru;
      lru_entry = entry;
    }
    entry++;
  }
  // At this point, we must have scanned all entries in the set, but it can still be hit
  if(result->state == SCACHE_MISS) {
    result->lru_entry = lru_entry;
  }
  // Invariant: Set data size must not exceed max set capacity
  assert(result->total_aligned_size <= scache->set_size);
  //printf("state %d\n", result->state);
  return;
}

// Evict an existing valid entry to the result object, and update total_aligned_size accordingly
static void scache_insert_helper_evict(scache_t *scache, scache_entry_t *entry, scache_op_result_t *result) {
  // Must be called before the eviction
  result->total_aligned_size -= scache_align_size(scache, entry->size);
  assert(result->total_aligned_size >= 0);
  // Then evict the entry by copying it to the result object and invalidating it
  scache_entry_dup(result->evict_entries + result->evict_count, entry);
  scache_entry_invalidate(entry);
  result->evict_count++;
  // Must not overflow
  assert(result->evict_count <= SCACHE_MAX_EVICT_ENTRY);
  // Update statistics
  scache->evict_line_count++;
  return;
}

// Evicts (copy + inv) valid LRU entry until (result->total_aligned_size + aligned_size <= scache->set_size)
// This function assumes that the total aligned size in result is the current size of the set
// If scache has no valid entry report error since in this case this function will not have been called
// This function fills result->evict_entries and result->evict_count;
// This function also updates result->total_aligned_size accordingly to reflect the eviction
static void scache_insert_helper_evict_valid_lru(
  scache_t *scache, uint64_t addr, int aligned_size, scache_op_result_t *result) {
  while(result->total_aligned_size + aligned_size > scache->set_size) {
    scache_entry_t *valid_lru_entry = NULL; // Must be valid entry; Invalid entries are ignored
    uint64_t min_lru = -1UL;
    scache_entry_t *entry = scache_get_set_begin(scache, addr);
    for(int i = 0;i < scache->way_count;i++) {
      if(entry->valid == 1) {
        if(entry->lru < min_lru) {
          min_lru = entry->lru;
          valid_lru_entry = entry;
        }
      }
      entry++;
    } // for
    // Call this function to evict the given entry
    scache_insert_helper_evict(scache, valid_lru_entry, result);
  } // while
  assert(result->evict_count > 0);
  return;
}

// Insert a block into the cache
// In all cases (miss, hit), insert_entry will be the entry just inserted
// If the insertion requires one or two evictions, their tag will be copied to the result object
// LRU of the inserted line is always updated, no matter it is a hit or miss
void scache_insert(scache_t *scache, uint64_t addr, int _dirty, scache_op_result_t *result) {
  result->evict_count = 0;
  scache->insert_count++;
  assert(scache->get_compressed_size_cb != NULL);
  // Actual byte size after compression
  int size = scache->get_compressed_size_cb(scache, addr);
  // Aligned size after compression
  int aligned_size = scache_align_size(scache, size);
  // Do not update LRU since we update it for write
  // scan_all is 1, for total aligned size, which is always accurate in the result object
  _scache_lookup(scache, addr, 0, 1, result);
  // Address hit, still need to check storage
  if(result->state == SCACHE_HIT) {
    result->state = SCACHE_SUCCESS;
    int dirty = _dirty || result->hit_entry->dirty; // This is used to rebuild the entry later
    // Need to update aligned size, which should be done before the invalidation
    result->total_aligned_size -= scache_align_size(scache, result->hit_entry->size);
    // Invalidate the entry, i.e., an insert hit is equivalent to removing and re-inserting it
    scache_entry_invalidate(result->hit_entry);
    if(result->total_aligned_size + aligned_size > scache->set_size) {
      // This evict lines until the above condition is met, and copies entry into result
      // result->total_aligned_size is also updated accordingly
      scache_insert_helper_evict_valid_lru(scache, addr, aligned_size, result);
      result->state = SCACHE_EVICT;
    }
    // Then re-insert the hit_entry using the dirty bit (which could have be changed) and new size
    result->hit_entry->addr = addr;
    result->hit_entry->valid = 1;
    result->hit_entry->dirty = dirty; // Restore dirty bit
    result->hit_entry->size = size;   // This is unaligned size
    scache_update_entry_lru(scache, result->hit_entry); // Update LRU
    result->total_aligned_size += aligned_size; // Update result object for consistency
    result->insert_entry = result->hit_entry; // Update insert_entry
  } else {
    result->state = SCACHE_SUCCESS;
    // Claim a tag entry from LRU. If LRU entry is valid, evict it first; This will change total_aligned_size
    if(result->lru_entry->valid == 1) {
      scache_insert_helper_evict(scache, result->lru_entry, result);
      result->state = SCACHE_EVICT; // Eviction for tag
    }
    // Same as above; Evict more lines if there is not sufficient storage in physical slot
    if(result->total_aligned_size + aligned_size > scache->set_size) {
      scache_insert_helper_evict_valid_lru(scache, addr, aligned_size, result);
      result->state = SCACHE_EVICT; // Eviction for storage
    }
    result->lru_entry->addr = addr;
    result->lru_entry->valid = 1;
    result->lru_entry->dirty = _dirty; // Use the arg dirty bit
    result->lru_entry->size = size;    // This is unaligned size
    scache_update_entry_lru(scache, result->lru_entry); // Update LRU
    result->total_aligned_size += aligned_size; // Update result object for consistency
    result->insert_entry = result->lru_entry; // Update insert_entry
  }
  return;
}

// Invalidate an address from the cache
// If result->state == SCACHE_EVICT line is found and evict_count == 1, and evict_entries[0] is the entry
// If result->state == SCACHE_SUCCESS line is not found and evict_count == 0
// Note that this function always invalidates one entry, if it hits
// Other entries are undefined, including total_aligned_size
void scache_inv(scache_t *scache, uint64_t addr, scache_op_result_t *result) {
  result->evict_count = 0;
  scache->inv_count++;
  // Do not update LRU even if hit the entry because we will invalidate it anyway
  _scache_lookup(scache, addr, 0, 0, result);
  if(result->state == SCACHE_HIT) {
    assert(result->hit_entry != NULL);
    // This updates total_aligned size
    scache_insert_helper_evict(scache, result->hit_entry, result);
    result->state = SCACHE_EVICT;
    scache->inv_success_count++;
    assert(result->evict_count == 1);
  } else {
    assert(result->state == SCACHE_MISS);
    assert(result->evict_count == 0);
    result->state = SCACHE_SUCCESS;
    // Must set this explicitly since the hit entry is actually LRU entry
    result->inv_entry = NULL;
  }
  return;
}

// Clear the dirty bit of the line
// This function does not change the LRU position of the line
// Always return SCACHE_SUCCESS, whether or not it misses the cache
// The downgrade_entry is NULL if miss; points to the entry downgraded if hit
// Other entries are undefined, including total_aligned_size
void scache_downgrade(scache_t *scache, uint64_t addr, scache_op_result_t *result) {
  result->evict_count = 0;
  scache->downgrade_count++;
  // Do not update LRU
  _scache_lookup(scache, addr, 0, 0, result);
  if(result->state == SCACHE_HIT) {
    assert(result->hit_entry != NULL);
    scache->downgrade_success_count++;
    result->hit_entry->dirty = 0;         // This performs downgrade
  } else {
    assert(result->state == SCACHE_MISS);
    result->downgrade_entry = NULL;       // This indicates downgrade failure, line not found
  }
  // Always return SUCCESS
  result->state = SCACHE_SUCCESS;
  return;
}

// Clear all entries and stats
void scache_reset(scache_t *scache) {
  // Invalidate all entries
  for(int i = 0;i < scache->line_count;i++) {
    scache_entry_invalidate(scache->data + i);
  }
  // Clear all stats
  memset(scache->stat_begin, 0x00, scache->stat_end - scache->stat_begin);
  // LRU counter is also reset
  scache->lru_counter = 1;
  return;
}

// Walk all tags (logical lines, not physical lines)
void scache_refresh_stat(scache_t *scache) {
  scache->logical_line_count = 0UL;
  scache->uncomp_logical_line_count = 0UL;
  scache->comp_line_size = 0UL;
  scache->aligned_comp_line_size = 0UL;
  scache_entry_t *entry = scache->data;
  //printf("Line count %d\n", scache->line_count);
  for(int i = 0;i < scache->line_count;i++) {
    if(entry->valid == 0) {
      entry++;
      continue;
    }
    //printf("Valid entry addr 0x%lX i %d\n", entry->addr, i);
    scache->logical_line_count++;
    if(entry->size == UTIL_CACHE_LINE_SIZE) {
      scache->uncomp_logical_line_count++;                 // Uncompressed logical line
    } else {
      scache->comp_line_size += (uint64_t)entry->size;     // Aggregate compressed line count
      scache->aligned_comp_line_size += (uint64_t)scache_align_size(scache, entry->size);
    }
    entry++;
  }
  return;
}

// This function saves:
//   logical_line_count, uncomp_logical_line_count, comp_line_size, aligned_comp_line_size
void scache_append_stat_snapshot(scache_t *scache) {
  scache_stat_snapshot_t *snapshot = scache_stat_snapshot_init();
  snapshot->next = NULL;
  if(scache->stat_snapshot_head == NULL) {
    assert(scache->stat_snapshot_tail == NULL);
    scache->stat_snapshot_head = scache->stat_snapshot_tail = snapshot;
  } else {
    assert(scache->stat_snapshot_tail != NULL);
    assert(scache->stat_snapshot_tail->next == NULL);
    scache->stat_snapshot_tail->next = snapshot;
    scache->stat_snapshot_tail = snapshot;
  }
  // This will clear all stats and perform tag scan
  scache_refresh_stat(scache);
  snapshot->logical_line_count = scache->logical_line_count;
  snapshot->uncomp_logical_line_count = scache->uncomp_logical_line_count;
  snapshot->comp_line_size = scache->comp_line_size;
  snapshot->aligned_comp_line_size = scache->aligned_comp_line_size;
  return;
}

// Saves the snapshots into a comma separated list, each row represents a snapshot
// The first row of the text file is the column names
// Note that this function prints more than snapshot actually saves, since some information
// can be derived
void scache_save_stat_snapshot(scache_t *scache, const char *name) {
  FILE *fp = fopen(name, "w");
  SYSEXPECT(fp != NULL);
  fprintf(fp, "logical_line_count, uncomp_logical_line_count, comp_line_size, aligned_comp_line_size, "
              "comp_logical_line_count, uncomp_line_size\n");
  scache_stat_snapshot_t *snapshot = scache->stat_snapshot_head;
  while(snapshot != NULL) {
    fprintf(fp, "%lu, %lu, %lu, %lu, %lu, %lu\n",
      snapshot->logical_line_count, snapshot->uncomp_logical_line_count, snapshot->comp_line_size,
      snapshot->aligned_comp_line_size,
      snapshot->logical_line_count - snapshot->uncomp_logical_line_count,
      UTIL_CACHE_LINE_SIZE * snapshot->uncomp_logical_line_count);
    snapshot = snapshot->next;
  }
  fclose(fp);
  return;
}

// Print valid entries of the given set
void scache_print_set(scache_t *scache, int set_index) {
  // Use physical way count
  scache_entry_t *entry = scache->data + set_index * scache->way_count;
  printf("---------- Set %d (total %d) ----------\n", set_index, scache->set_count);
  for(int i = 0;i < scache->way_count;i++) {
    if(entry->valid == 1) {
      printf("Way %2d addr 0x%lX (0x%lX|%d) dirty %d size %d (aligned %d) LRU %lu\n",
        i, entry->addr, entry->addr >> (UTIL_CACHE_LINE_BITS + scache->set_bits), set_index, entry->dirty,
        entry->size, scache_align_size(scache, entry->size), entry->lru);
    }
    entry++;
  }
  return;
}

void scache_conf_print(scache_t *scache) {
  printf("---------- scache_t conf ----------\n");
  printf("Physical size %d line count %d way count %d\n",
    scache->size, scache->physical_line_count, scache->physical_way_count);
  printf("Ratio %d ways %d lines %d sets %d set size %d bits %d\n", 
    scache->ratio, scache->way_count, scache->line_count, scache->set_count, scache->set_size,
    scache->set_bits);
  printf("Addr mask 0x%lX shift %d\n", scache->addr_mask, scache->addr_shift);
  // Print compression method
  printf("Compression method (call back): ");
  if(scache->get_compressed_size_cb == &scache_get_compressed_size_BDI_cb) {
    printf("BDI\n");
  } else if(scache->get_compressed_size_cb == &scache_get_compressed_size_FPC_cb) {
    printf("FPC\n");
  } else if(scache->get_compressed_size_cb == &scache_get_compressed_size_CPACK_cb) {
    printf("CPACK\n");
  } else if(scache->get_compressed_size_cb == &scache_get_compressed_size_MBD_cb) {
    printf("MBD\n");
  } else if(scache->get_compressed_size_cb == &scache_get_compressed_size_None_cb) {
    printf("None\n");
  } else if(scache->get_compressed_size_cb == NULL) {
    printf("Not set (initialization is incomplete)\n");
  } else {
    printf("Other (debugging mode?)\n");
  }
  return;
}

void scache_stat_print(scache_t *scache) {
  printf("---------- scache_t stat ----------\n");
  if(scache->get_compressed_size_cb == &scache_get_compressed_size_BDI_cb) {
    printf("BDI attempt %lu succeed %lu fail %lu (rate %.2lf%%) zero %lu\n",
      scache->BDI_attempt_count, scache->BDI_success_count, scache->BDI_fail_count, 
      (double)scache->BDI_success_count / (double)scache->BDI_attempt_count,
      scache->all_zero_count);
    // Print per-type BDI statistics
    uint64_t total_size = 0UL; // This aggregates per-type size, used to verify stats
    for(int i = BDI_TYPE_BEGIN; i < BDI_TYPE_END;i++) {
      printf("[%s] %lu ", BDI_names[i], scache->BDI_type_counts[i]);
      total_size += (scache->BDI_type_counts[i] * BDI_types[i].compressed_size);
    }
    putchar('\n');
    printf("BDI total size %lu (avg %.2lf bytes)\n",
      total_size, (double)total_size / (double)scache->BDI_success_count);
    printf("BDI success only before %lu after %lu (ratio %.2lf%%, %.2lfx)\n",
      scache->BDI_before_size, scache->BDI_after_size,
      100.0 * scache->BDI_after_size / (double)scache->BDI_before_size,
      (double)scache->BDI_before_size / (double)scache->BDI_after_size);
    printf("BDI overall before %lu after %lu (ratio %.2lf%%, %.2lfx)\n",
      scache->BDI_attempt_size, scache->BDI_uncomp_size + scache->BDI_after_size,
      100.0 * (scache->BDI_uncomp_size + scache->BDI_after_size) / (double)scache->BDI_attempt_size,
      (double)scache->BDI_attempt_size / (double)(scache->BDI_uncomp_size + scache->BDI_after_size));
  } else if(scache->get_compressed_size_cb == &scache_get_compressed_size_FPC_cb) { 
    printf("FPC attempt %lu success %lu fail %lu (rate %.2lf%%)\n",
      scache->FPC_attempt_count, scache->FPC_success_count, scache->FPC_fail_count,
      100.0 * scache->FPC_success_count / (double)scache->FPC_attempt_count);
    printf("FPC success only before %lu after %lu (ratio %.2lf%%, %.2lfx)\n",
      scache->FPC_before_size, scache->FPC_after_size,
      100.0 * scache->FPC_after_size / (double)scache->FPC_before_size,
      (double)scache->FPC_before_size / (double)scache->FPC_after_size);
    printf("FPC overall before %lu after %lu (ratio %.2lf%%, %.2lfx)\n",
      scache->FPC_attempt_size, scache->FPC_uncomp_size + scache->FPC_after_size,
      100.0 * (scache->FPC_uncomp_size + scache->FPC_after_size) / (double)scache->FPC_attempt_size,
      (double)scache->FPC_attempt_size / (double)(scache->FPC_uncomp_size + scache->FPC_after_size));
    printf("Size classes ");
    for(int i = 0;i < 8;i++) {
      printf("[%d-%d] %lu ", i * 8, i * 8 + 8, scache->FPC_size_counts[i]);
    }
    putchar('\n');
  } else if(scache->get_compressed_size_cb == &scache_get_compressed_size_CPACK_cb) { 
    printf("CPACK attempt %lu success %lu fail %lu (rate %.2lf%%)\n",
      scache->CPACK_attempt_count, scache->CPACK_success_count, scache->CPACK_fail_count,
      100.0 * scache->CPACK_success_count / (double)scache->CPACK_attempt_count);
    printf("CPACK success only before %lu after %lu (ratio %.2lf%%, %.2lfx)\n",
      scache->CPACK_before_size, scache->CPACK_after_size,
      100.0 * scache->CPACK_after_size / (double)scache->CPACK_before_size,
      (double)scache->CPACK_before_size / (double)scache->CPACK_after_size);
    printf("CPACK overall before %lu after %lu (ratio %.2lf%%, %.2lfx)\n",
      scache->CPACK_attempt_size, scache->CPACK_uncomp_size + scache->CPACK_after_size,
      100.0 * (scache->CPACK_uncomp_size + scache->CPACK_after_size) / (double)scache->CPACK_attempt_size,
      (double)scache->CPACK_attempt_size / (double)(scache->CPACK_uncomp_size + scache->CPACK_after_size));
    printf("CPACK types ");
    for(int i = 0;i < 6;i++) {
      printf("[%s] %lu ", CPACK_type_names[i], scache->CPACK_type_stat.type_counts[i]);
    }
    putchar('\n');
    printf("Size classes ");
    for(int i = 0;i < 8;i++) {
      printf("[%d-%d] %lu ", i * 8, i * 8 + 8, scache->CPACK_size_counts[i]);
    }
    putchar('\n');
  } else if(scache->get_compressed_size_cb == &scache_get_compressed_size_MBD_cb) { 
    printf("MBD attempt %lu success %lu fail %lu (rate %.2lf%%)\n",
      scache->MBD_attempt_count, scache->MBD_success_count, scache->MBD_fail_count,
      100.0 * scache->MBD_success_count / (double)scache->MBD_attempt_count);
    printf("MBD success only before %lu after %lu (ratio %.2lf%%, %.2lfx)\n",
      scache->MBD_before_size, scache->MBD_after_size,
      100.0 * scache->MBD_after_size / (double)scache->MBD_before_size,
      (double)scache->MBD_before_size / (double)scache->MBD_after_size);
    printf("MBD overall before %lu after %lu (ratio %.2lf%%, %.2lfx)\n",
      scache->MBD_attempt_size, scache->MBD_uncomp_size + scache->MBD_after_size,
      100.0 * (scache->MBD_uncomp_size + scache->MBD_after_size) / (double)scache->MBD_attempt_size,
      (double)scache->MBD_attempt_size / (double)(scache->MBD_uncomp_size + scache->MBD_after_size));
    printf("Size classes ");
    for(int i = 0;i < 8;i++) {
      printf("[%d-%d] %lu ", i * 8, i * 8 + 8, scache->MBD_size_counts[i]);
    }
    putchar('\n');
  } else if(scache->get_compressed_size_cb == &scache_get_compressed_size_None_cb) {
    printf("Cache is not compressed\n");
  } else if(scache->get_compressed_size_cb == NULL) {
    printf("No compression stat available (initialization is incomplete)\n");
  } else {
    printf("Other (debugging mode?)\n");
  }
  printf("Reads %lu inserts %lu invs %lu (success %lu) downgrades %lu (success %lu)"
         " evict lines %lu avg (per insert) %.2lf\n", 
    scache->read_count, scache->insert_count, scache->inv_count, scache->inv_success_count,
    scache->downgrade_count, scache->downgrade_success_count,
    scache->evict_line_count, (double)scache->evict_line_count / (double)scache->insert_count);
  // Print line distribution, etc.
  scache_refresh_stat(scache);
  printf("Total line count %lu (%.2lfx uncomp) comp line count %lu comp size sum (unaligned) %lu (aligned) %lu\n",
    scache->logical_line_count, (double)scache->logical_line_count / (double)scache->physical_line_count,
    scache->logical_line_count - scache->uncomp_logical_line_count,
    scache->comp_line_size, scache->aligned_comp_line_size);
  return;
}

//* dram_t

dram_stat_snapshot_t *dram_stat_snapshot_init() {
  dram_stat_snapshot_t *stat = (dram_stat_snapshot_t *)malloc(sizeof(dram_stat_snapshot_t));
  SYSEXPECT(stat != NULL);
  memset(stat, 0x00, sizeof(dram_stat_snapshot_t));
  return stat;
}

void dram_stat_snapshot_free(dram_stat_snapshot_t *stat) {
  free(stat);
  return;
}

// Called by init, programmers need not call this function
static void dram_init_stats(dram_t *dram) {
  dram->stat_read_count = 0;
  dram->stat_write_count = 0;
  dram->stat_write_size = 0;
  dram->stat_read_cycle = 0;
  dram->stat_write_cycle = 0;
  // Total size
  dram->total_read_count = 0;
  dram->total_write_count = 0;
  dram->total_write_size = 0;
  dram->total_read_cycle = 0;
  dram->total_write_cycle = 0;
  dram->total_sb_count = 0;
  dram->total_sb_line_count = 0;
  // Contended
  dram->contended_access_count = 0;
  dram->read_contended_access_count = 0;
  dram->write_contended_access_count = 0;
  dram->uncontended_access_count = 0;
  // Stat list
  dram->stat_snapshot_head = NULL;
  dram->stat_snapshot_head = NULL;
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
  // We allow 0 latency for easier testing
  dram->read_latency = conf_find_int32_range(conf, "dram.read_latency", 0, INT_MAX, CONF_RANGE);
  dram->write_latency = conf_find_int32_range(conf, "dram.write_latency", 0, INT_MAX, CONF_RANGE);
  if(dram->read_latency == 0) {
    printf("Note: DRAM is using zero read latency\n");
  }
  if(dram->write_latency == 0) {
    printf("Note: DRAM is using zero write latency\n");
  }
  dram_init_stats(dram);
  return dram;
}

void dram_free(dram_t *dram) {
  // Free stat linked list
  dram_stat_snapshot_t *curr = dram->stat_snapshot_head;
  while(curr != NULL) {
    dram_stat_snapshot_t *next = curr->next;
    dram_stat_snapshot_free(curr);
    curr = next;
  }
  free(dram);
  return;
}

// This function is shared by dram read and dram write
uint64_t dram_access(dram_t *dram, uint64_t cycle, uint64_t oid, uint64_t addr, int latency) {
  int index = dram_gen_bank_index(dram, oid, addr);
  assert(index >= 0 && index < dram->bank_count);
  if(dram->banks[index] <= cycle) {
    // The bank is available at this moment, just serve the request immediately
    cycle += latency;
    dram->uncontended_access_count++;
  } else {
    // Otherwise serve it in the earliest possible cycle
    cycle = dram->banks[index] + latency;
    dram->contended_access_count++;
  }
  // Update available cycle
  dram->banks[index] = cycle;
  return cycle;
}

// Returns the maximum bank cycle; This function is typically used for debugging functions to determine the 
// cycle where the next step test can start
uint64_t dram_get_max_cycle(dram_t *dram) {
  uint64_t max_cycle = 0UL;
  for(int i = 0;i < dram->bank_count;i++) {
    if(dram->banks[i] > max_cycle) {
      max_cycle = dram->banks[i];
    }
  }
  return max_cycle;
}

// Note that this function does not require calling any refresh function
void dram_append_stat_snapshot(dram_t *dram) {
  dram_stat_snapshot_t *snapshot = dram_stat_snapshot_init();
  // Initialize stat object
  snapshot->read_count = dram->stat_read_count;
  snapshot->write_count = dram->stat_write_count;
  snapshot->write_size = dram->stat_write_size;
  snapshot->read_cycle = dram->stat_read_cycle;
  snapshot->write_cycle = dram->stat_write_cycle;
  // Adding into linked list
  if(dram->stat_snapshot_head == NULL) {
    assert(dram->stat_snapshot_tail == NULL);
    dram->stat_snapshot_head = dram->stat_snapshot_tail = snapshot;
  } else {
    assert(dram->stat_snapshot_tail != NULL);
    assert(dram->stat_snapshot_tail->next == NULL);
    dram->stat_snapshot_tail->next = snapshot;
    dram->stat_snapshot_tail = snapshot;
  }
  // Clear for next window
  dram->stat_read_count = 0;
  dram->stat_write_count = 0;
  dram->stat_write_size = 0;
  dram->stat_read_cycle = 0;
  dram->stat_write_cycle = 0;
  return;
}

// Write DRAM bandwidth usage to a text file
// Fields are: Cycle count, read count, write count, write size
void dram_save_stat_snapshot(dram_t *dram, const char *filename) {
  FILE *fp = fopen(filename, "w");
  SYSEXPECT(fp != NULL);
  fprintf(fp, "cycle_count, read_count, write_count, write_size, read_cycle, write_cycle\n");
  dram_stat_snapshot_t *curr = dram->stat_snapshot_head;
  while(curr != NULL) {
    fprintf(fp, "%lu, %lu, %lu, %lu, %lu, %lu\n", 
      curr->cycle_count, curr->read_count, curr->write_count, curr->write_size,
      curr->read_cycle, curr->write_cycle);
    curr = curr->next;
  }
  fclose(fp);
  return;
}

void dram_conf_print(dram_t *dram) {
  printf("---------- dram_t conf ----------\n");
  printf("Addr mask 0x%016lX shift %d bank count %d bits %d OID mask 0x%016lX\n", 
    dram->addr_mask, dram->addr_shift, dram->bank_count,
    dram->bank_count_bits, dram->oid_mask);
  printf("Latancy read %d write %d\n", dram->read_latency, dram->write_latency);
  return;
}

void dram_stat_print(dram_t *dram) {
  printf("---------- dram_t stat ----------\n");
  printf("Total read %lu (cycles %lu, avg %.2lf) write %lu (cycles %lu, avg %.2lf) size %lu\n",
    dram->total_read_count, dram->total_read_cycle, (double)dram->total_read_cycle / dram->total_read_count,
    dram->total_write_count, dram->total_write_cycle, (double)dram->total_write_cycle / dram->total_write_count,
    dram->total_write_size);
  printf("Curr window read %lu write %lu write size %lu read cycle %lu write cycle %lu\n",
    dram->stat_read_count, dram->stat_write_count, dram->stat_write_size,
    dram->stat_read_cycle, dram->stat_write_cycle);
  printf("Write SBs %lu lines %lu avg lines %.2lf\n",
    dram->total_sb_count, dram->total_sb_line_count,
    (double)dram->total_sb_line_count / dram->total_sb_count);
  printf("Contended %lu (read %lu write %lu) uncontended %lu\n",
    dram->contended_access_count, dram->read_contended_access_count, dram->write_contended_access_count,
    dram->uncontended_access_count);
  return;
}

//* cc_scache_t

const char *cc_common_stat_names[CC_STAT_END] = {
  "LOOKUP", "INSERT", "EVICT", "INV", "DOWNGRADE", 
  "LOAD", "STORE", "LOAD_HIT", "STORE_HIT", "LOAD_MISS", "STORE_MISS",
  "LOAD_CYCLE", "STORE_CYCLE",
};

// Initialize stat arrays in cc common structure
void cc_common_init_stats(cc_common_t *commons, int core_count) {
  for(int i = MESI_CACHE_BEGIN;i < MESI_CACHE_END;i++) {
    int alloc_size = sizeof(uint64_t) * core_count * CC_STAT_END;
    commons->stats[i] = (uint64_t *)malloc(alloc_size);
    SYSEXPECT(commons->stats[i] != NULL);
    memset(commons->stats[i], 0x00, alloc_size);
  }
  return;
}

void cc_common_free_stats(cc_common_t *commons) {
  free(commons->stats[0]);
  free(commons->stats[1]);
  free(commons->stats[2]);
  return;
}

// end_level indicates the level that the access hits, must not be DRAM
// end_cycle is the return value of the access function
// Note: LLC may have variable access latency, but since it is the last level, the value is still accurate
// as we just add the rest after subtracting L1 + L2 to LLC
void cc_common_update_access_cycles(
  cc_common_t *commons, int id, int is_store, int end_level, uint64_t begin_cycle, uint64_t end_cycle) {
  assert(end_level >= MESI_CACHE_BEGIN && end_level < MESI_CACHE_END);
  assert(id >= 0 && id < commons->core_count);
  for(int level = MESI_CACHE_L1;level <= end_level;level++) {
    if(is_store == 1) {
      (*cc_common_get_stat_ptr(commons, level, id, CC_STAT_STORE_CYCLE)) += end_cycle - begin_cycle;
    } else {
      (*cc_common_get_stat_ptr(commons, level, id, CC_STAT_LOAD_CYCLE)) += end_cycle - begin_cycle;
    }
    // This is the begin cycle for the next level
    begin_cycle += commons->cache_latencies[level];
  } 
  return;
}

// Print per-level stat in the common structure, with a new line at the end
// This function does not print the header line, so it can be called within another stat print function
void cc_common_print_stats(cc_common_t *commons) {
  // First level index is cache level, then cache ID, then stat type
  // Here we aggregate all cache's stat for the level
  for(int cache = MESI_CACHE_BEGIN;cache < MESI_CACHE_END;cache++) {
    printf("%s | ", MESI_cache_names[cache]);
    // This tracks the total number of value for the entire level, indexed by stat type
    uint64_t total_counts[CC_STAT_END];
    for(int type = CC_STAT_BEGIN;type < CC_STAT_END;type++) {  
      uint64_t total = 0UL;
      // Aggregate across all cores
      for(int id = 0;id < commons->core_count;id++) {
        uint64_t count = *cc_common_get_stat_ptr(commons, cache, id, type);  
        total += count;
      }
      printf("%s %lu ", cc_common_stat_names[type], total);
      total_counts[type] = total;
    }
    putchar('\n');
    // Print hit and miss rate
    assert(total_counts[CC_STAT_LOAD_HIT] + total_counts[CC_STAT_LOAD_MISS] == total_counts[CC_STAT_LOAD]);
    assert(total_counts[CC_STAT_STORE_HIT] + total_counts[CC_STAT_STORE_MISS] == total_counts[CC_STAT_STORE]);
    for(int i = 0;i < (int)strlen(MESI_cache_names[cache]);i++) putchar(' ');
    printf(" | ");
    printf("Load hit %.2lf%% miss %.2lf%% Store hit %.2lf%% miss %.2lf%% (total hit %.2lf%% miss %.2lf%%)\n",
      100.0 * (double)total_counts[CC_STAT_LOAD_HIT] / (double)total_counts[CC_STAT_LOAD],
      100.0 * (double)total_counts[CC_STAT_LOAD_MISS] / (double)total_counts[CC_STAT_LOAD],
      100.0 * (double)total_counts[CC_STAT_STORE_HIT] / (double)total_counts[CC_STAT_STORE],
      100.0 * (double)total_counts[CC_STAT_STORE_MISS] / (double)total_counts[CC_STAT_STORE],
      100.0 * (double)(total_counts[CC_STAT_LOAD_HIT] + total_counts[CC_STAT_STORE_HIT]) / 
        (double)(total_counts[CC_STAT_LOAD] + total_counts[CC_STAT_STORE]),
      100.0 * (double)(total_counts[CC_STAT_LOAD_MISS] + total_counts[CC_STAT_STORE_MISS]) / 
        (double)(total_counts[CC_STAT_LOAD] + total_counts[CC_STAT_STORE]));
    for(int i = 0;i < (int)strlen(MESI_cache_names[cache]);i++) putchar(' ');
    printf(" | ");
    printf("Load cycle %.2lf store cycle %.2lf\n",
      (double)total_counts[CC_STAT_LOAD_CYCLE] / (double)total_counts[CC_STAT_LOAD],
      (double)total_counts[CC_STAT_STORE_CYCLE] / (double)total_counts[CC_STAT_STORE]);
  }
  // Print DRAM stats
  printf("DRAM | reads %lu writes %lu rd_cycle %lu wr_bytes %lu\n",
    commons->dram_read_count, commons->dram_write_count, commons->dram_read_cycle, commons->dram_write_bytes);
  printf("     | Avg. rd_latency %.2lf wr_size %.2lf (%.2lf%%, %.2lfx)\n",
    (double)commons->dram_read_cycle / (double)commons->dram_read_count,
    (double)commons->dram_write_bytes / (double)commons->dram_write_count,
    ((double)commons->dram_write_bytes / (double)commons->dram_write_count) / (double)UTIL_CACHE_LINE_SIZE,
    (double)UTIL_CACHE_LINE_SIZE / ((double)commons->dram_write_bytes / (double)commons->dram_write_count));
  return;
}

// Initialize the caches array without filling the cache
void cc_scache_init_cores(cc_scache_t *cc, int core_count) {
  cc->commons.core_count = core_count;
  cc->cores = (cc_scache_core_t *)malloc(core_count * sizeof(cc_scache_core_t));
  SYSEXPECT(cc->cores != NULL);
  memset(cc->cores, 0x00, core_count * sizeof(cc_scache_core_t));
  // Then init the three-dimensional stat
  cc_common_init_stats(&cc->commons, core_count);
  return;
}

// Initialize the LLC object
void cc_scache_init_llc(
  cc_scache_t *cc, int size, int physical_way_count, int ratio, int latency, const char *algorithm) {
  cc->shared_llc = scache_init(size, physical_way_count, ratio);
  scache_set_compression_type(cc->shared_llc, algorithm);
  // cores must have been already initialized
  assert(cc->cores != NULL);
  assert(cc->commons.core_count > 0);
  for(int i = 0;i < cc->commons.core_count;i++) {
    cc->cores[i].llc = cc->shared_llc;
  }
  cc->commons.llc_size = size;
  cc->commons.llc_physical_way_count = physical_way_count;
  cc->commons.llc_latency = latency;
  cc->llc_ratio = ratio;
  return;
}

// Initialize L1 and L2 objects
void cc_scache_init_l1_l2(cc_scache_t *cc, int cache, int size, int way_count, int latency) {
  assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2);
  assert(cc->commons.core_count > 0);
  for(int i = 0;i < cc->commons.core_count;i++) {
    scache_t *scache = scache_init(size, way_count, 1);
    // Upper level caches do not use compression
    scache_set_compression_type(scache, "None");
    cc->cores[i].scaches[cache] = scache;
    if(cc->shared_llc != NULL) {
      cc->cores[i].llc = cc->shared_llc;
    }
    cc->cores[i].id = i;
  }
  // Store conf in the cc structure
  cc->commons.cache_sizes[cache] = size;
  cc->commons.cache_way_counts[cache] = way_count;
  cc->commons.cache_latencies[cache] = latency;
  return;
}

// Initialize a given empty structure using configuration file
// This function reads the following:
//    cc.scache.core_count
//    cc.scache.l1.size, cc.scache.l1.way_count, cc.scache.l1.latency
//    cc.scache.l2.size, cc.scache.l2.way_count, cc.scache.l2.latency
//    cc.scache.llc.size, cc.scache.llc.physical_way_count, cc.scache.llc.ratio, cc.scache.llc.latency
//    cc.scache.llc.algorithm (BDI/FPC/None)
cc_scache_t *cc_scache_init_conf(conf_t *conf) {
  cc_scache_t *cc = cc_scache_init();
  cc->commons.conf = conf;
  // Init core count
  int core_count = conf_find_int32_range(
    conf, "cc.scache.core_count", 1, CONF_INT32_MAX, CONF_RANGE);
  cc_scache_init_cores(cc, core_count);
  // L1
  int l1_size = conf_find_int32_range(
    conf, "cc.scache.l1.size", UTIL_CACHE_LINE_SIZE, CONF_INT32_MAX, CONF_RANGE);
  int l1_way_count = conf_find_int32_range(
    conf, "cc.scache.l1.way_count", 1, CONF_INT32_MAX, CONF_RANGE);
  int l1_latency = conf_find_int32_range(
    conf, "cc.scache.l1.latency", 0, CONF_INT32_MAX, CONF_RANGE);
  cc_scache_init_l1_l2(cc, MESI_CACHE_L1, l1_size, l1_way_count, l1_latency);
  // L2
  int l2_size = conf_find_int32_range(
    conf, "cc.scache.l2.size", UTIL_CACHE_LINE_SIZE, CONF_INT32_MAX, CONF_RANGE);
  int l2_way_count = conf_find_int32_range(
    conf, "cc.scache.l2.way_count", 1, CONF_INT32_MAX, CONF_RANGE);
  int l2_latency = conf_find_int32_range(
    conf, "cc.scache.l2.latency", 0, CONF_INT32_MAX, CONF_RANGE);
  cc_scache_init_l1_l2(cc, MESI_CACHE_L2, l2_size, l2_way_count, l2_latency);
  // LLC
  int llc_size = conf_find_int32_range(
    conf, "cc.scache.llc.size", UTIL_CACHE_LINE_SIZE, CONF_INT32_MAX, CONF_RANGE);
  int llc_physical_way_count = conf_find_int32_range(
    conf, "cc.scache.llc.physical_way_count", 1, CONF_INT32_MAX, CONF_RANGE);
  int llc_latency = conf_find_int32_range(
    conf, "cc.scache.llc.latency", 0, CONF_INT32_MAX, CONF_RANGE);
  int llc_ratio = conf_find_int32_range(
    conf, "cc.scache.llc.ratio", 1, CONF_INT32_MAX, CONF_RANGE);
  const char *llc_algorithm = conf_find_str_mandatory(conf, "cc.scache.llc.algorithm");
  cc_scache_init_llc(cc, llc_size, llc_physical_way_count, llc_ratio, llc_latency, llc_algorithm);
  return cc;
}

// Initialize the structure itself, but does not initialize any of the internal structs
// There are two ways to initialize the members of this struct
//   1. Ad-hoc initialization by manually calling functions; Used by debugging functions
//   2. Initialization from a conf file
cc_scache_t *cc_scache_init() {
  cc_scache_t *cc = (cc_scache_t *)malloc(sizeof(cc_scache_t));
  SYSEXPECT(cc != NULL);
  memset(cc, 0x00, sizeof(cc_scache_t));
  return cc;
}

void cc_scache_free(cc_scache_t *cc) {
  // Free all caches
  for(int i = 0;i < cc->commons.core_count;i++) {
    scache_free(cc->cores[i].l1);
    scache_free(cc->cores[i].l2);
  }
  free(cc->cores);
  // Free shared LLC
  free(cc->shared_llc);
  // Free per-core stats
  cc_common_free_stats(&cc->commons);
  free(cc);
  return;
}

// If the dmap already exists then report error
void cc_scache_set_dmap(cc_scache_t *cc, dmap_t *dmap) {
  if(cc->commons.dmap != NULL) {
    error_exit("The dmap has already been set\n");
  } else if(cc->shared_llc == NULL) {
    error_exit("dmap must be set after the LLC is initialized\n");
  }
  cc->commons.dmap = dmap;
  // LLC also needs the dmap to get compressed line size
  scache_set_dmap(cc->shared_llc, dmap);
  return;
}

void cc_scache_set_dram(cc_scache_t *cc, dram_t *dram) {
  if(cc->commons.dram != NULL) {
    error_exit("The DRAM has already been set\n");
  }
  cc->commons.dram = dram;
  return;
}

// Returns 1 if the line does not exist in the cache; Used for debugging
// This function does not change LRU
int cc_scache_is_line_invalid(cc_scache_t *cc, int cache, int id, uint64_t addr) {
  scache_t *scache = cc_scache_get_core_cache(cc, cache, id);
  scache_op_result_t lookup_result;
  _scache_lookup(scache, addr, 0, 0, &lookup_result);
  return lookup_result.state == SCACHE_MISS;
}

// Do not change LRU; This function assumes line must exist, other wise report error
int cc_scache_is_line_dirty(cc_scache_t *cc, int cache, int id, uint64_t addr) {
  scache_t *scache = cc_scache_get_core_cache(cc, cache, id);
  scache_op_result_t lookup_result;
  _scache_lookup(scache, addr, 0, 0, &lookup_result);
  if(lookup_result.state == SCACHE_MISS) {
    error_exit("The line does not exist in the hierarchy (cache %s id %d addr 0x%lX)\n",
      MESI_cache_names[cache], id, addr);
  }
  return lookup_result.hit_entry->dirty;
}

// Handles recursive LLC eviction, i.e., first recursively invalidate from the upper levels, and then write back
// to DRAM if any of the lines is dirty
// Assumes that the block given by addr is no longer in LLC
// Note that dirty lines will be ignored, but we still need to check the 
// This function updates the MESI entry of the address evicted in L1, L2 and LLC
// This function also updates the inv count of the private cores
static uint64_t cc_scache_llc_evict_recursive(cc_scache_t *cc, int id, uint64_t cycle, uint64_t addr, int dirty) {
  (void)id;
  MESI_entry_t *MESI_entry = dmap_find_MESI_entry(cc->commons.dmap, 0UL, addr);
  assert(MESI_entry != NULL);
  assert(dirty == 0 || dirty == 1);
  // If LLC wb line is dirty, it must also be dirty in the directory
  assert(dirty == 0 || MESI_entry->llc_state == MESI_STATE_M);
  // Check the consistency between M state and number of sharers
  assert(MESI_entry->l1_state != MESI_STATE_M || MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L1) == 1);
  assert(MESI_entry->l2_state != MESI_STATE_M || MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L2) == 1);
  if(MESI_entry->l1_state == MESI_STATE_M || MESI_entry->l2_state == MESI_STATE_M) {
    dirty = 1;
  }
  // Loop over L1 and L2 sharers and invalidate
  for(int cache = MESI_CACHE_L1;cache <= MESI_CACHE_L2;cache++) {
    int curr = -1;
    while(1) {
      // Return value is the ID of the sharer; or -1 if reaches the end
      curr = MESI_entry_get_next_sharer(MESI_entry, cache, curr + 1);
      if(curr != -1) {
        // Update the inv stat counter
        (*cc_common_get_stat_ptr(&cc->commons, cache, curr, CC_STAT_INV))++;
        scache_op_result_t inv_result;
        scache_t *inv_cache = cc_scache_get_core_cache(cc, cache, curr);
        scache_inv(inv_cache, addr, &inv_result);
        // Ignore dirty bits of the evicted entries since we have already done that above
        assert(inv_result.state == SCACHE_SUCCESS || inv_result.state == SCACHE_EVICT);
      } else {
        break;
      }
    }
  }
  // Clear all states and remove all sharers
  MESI_entry_invalidate(MESI_entry);
  // Call DRAM to write data back; scache always writes back full sized data block
  if(dirty == 1) {
    cc->commons.dram_write_count++;
    cc->commons.dram_write_bytes += UTIL_CACHE_LINE_SIZE;
    dram_write(cc->commons.dram, cycle, 0UL, addr, UTIL_CACHE_LINE_SIZE);
    if(cc->commons.dram_debug_cb != NULL) {
      cc->commons.dram_debug_cb(DRAM_WRITE, id, cycle, 0UL, addr, UTIL_CACHE_LINE_SIZE);
    }
  }
  return cycle;
}

// Insert into LLC recursive; This function handles:
//   1. Eviction from the LLC as a result of insertion;
//   2. Recursive eviction of cache lines from L1 and L2 by querying the dmap
// This function handles both clean and dirty eviction; Clean insertion will NOT be ignored; The upper level
// should be responsible for masking off clean insertion
// This function does *NOT* update MESI states for inserted line, but it will update MESI state for evicted line
// This function also updates the eviction stat
uint64_t cc_scache_llc_insert_recursive(cc_scache_t *cc, int id, uint64_t cycle, uint64_t addr, int dirty) {
  scache_t *llc = cc->shared_llc;
  scache_op_result_t insert_result;
  scache_insert(llc, addr, dirty, &insert_result);
  if(insert_result.state == SCACHE_EVICT) {
    // Evict count is not zero, need to evict recursively from upper level
    for(int i = 0;i < insert_result.evict_count;i++) {
      (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_LLC, id, CC_STAT_EVICT))++;
      assert(insert_result.evict_entries[i].valid == 1);
      uint64_t evict_addr = insert_result.evict_entries[i].addr;
      int evict_dirty = insert_result.evict_entries[i].dirty;
      cycle = cc_scache_llc_evict_recursive(cc, id, cycle, evict_addr, evict_dirty);
    }
  } else {
    assert(insert_result.evict_count == 0);
    assert(insert_result.state == SCACHE_SUCCESS);
  }
  return cycle;
}

// Insert into the L2 cache; If evicts, then recursively insert into LLC
// This function updates MESI entry for the evicted line and for L1 line, but *NOT* for inserted line
uint64_t cc_scache_l2_insert_recursive(cc_scache_t *cc, int id, uint64_t cycle, uint64_t addr, int dirty) {
  scache_op_result_t insert_result;
  scache_t *l2 = cc_scache_get_core_cache(cc, MESI_CACHE_L2, id);
  scache_insert(l2, addr, dirty, &insert_result);
  assert(insert_result.state == SCACHE_SUCCESS || insert_result.state == SCACHE_EVICT);
  (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L2, id, CC_STAT_INSERT))++;
  if(insert_result.state == SCACHE_EVICT) {
    (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L2, id, CC_STAT_EVICT))++;
    assert(insert_result.evict_count == 1);
    uint64_t evict_addr = insert_result.evict_entries[0].addr;
    int evict_dirty = insert_result.evict_entries[0].dirty;
    // Invalidate the line in L1
    MESI_entry_t *MESI_entry = dmap_find_MESI_entry(cc->commons.dmap, 0UL, evict_addr);
    assert(MESI_entry != NULL);
    // If evicted line is dirty, then it must be in M state in the L2 cache
    assert(evict_dirty == 0 || MESI_entry_get_state(MESI_entry, MESI_CACHE_L2) == MESI_STATE_M);
    // The line must also exist in LLC
    assert(MESI_entry_get_state(MESI_entry, MESI_CACHE_LLC) != MESI_STATE_I);
    // The line must has a sharer bit set in the bitmap
    assert(MESI_entry_is_sharer(MESI_entry, MESI_CACHE_L2, id) == 1);
    // If also in L1, invalidate, and inherit the dirty bit, if it is dirty in L1
    if(MESI_entry_is_sharer(MESI_entry, MESI_CACHE_L1, id) == 1) {
      (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L1, id, CC_STAT_INV))++;
      scache_op_result_t inv_result;
      scache_t *l1 = cc_scache_get_core_cache(cc, MESI_CACHE_L1, id);
      // SUCCESS if not found; EVICT if found, either dirty or clean
      scache_inv(l1, evict_addr, &inv_result);
      assert(inv_result.state == SCACHE_EVICT && inv_result.evict_count == 1);
      if(inv_result.evict_entries[0].dirty == 1) {
        assert(MESI_entry_get_state(MESI_entry, MESI_CACHE_L1) == MESI_STATE_M);
        evict_dirty = 1;
      }
      // Clear L1 bitmap. State is only changed if we inv'ed the last sharer
      // This works for E and M state in L1 since it is guaranteed that M/E state lines have one sharer
      MESI_entry_clear_sharer(MESI_entry, MESI_CACHE_L1, id);
      if(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L1) == 0) {
        MESI_entry->l1_state = MESI_STATE_I;
      }
    }
    // Clear L2 state and bitmap
    MESI_entry_clear_sharer(MESI_entry, MESI_CACHE_L2, id);
    if(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L2) == 0) {
      MESI_entry->l2_state = MESI_STATE_I;
    }
    if(evict_dirty == 1) {
      // Insert recursively into LLC, may incur extra evictions; Only insert if dirty
      cycle = cc_scache_llc_insert_recursive(cc, id, cycle, evict_addr, evict_dirty);
      // Update LLC state for evicted line, if it is dirty
      MESI_entry->llc_state = MESI_STATE_M;
    } else {
      // Otherwise, still need to update LRU, so just perform a lookup
      scache_op_result_t lookup_result;
      scache_lookup(cc_scache_get_core_cache(cc, MESI_CACHE_LLC, id), evict_addr, &lookup_result);
      assert(lookup_result.state == SCACHE_HIT);
    }
  }
  return cycle;
}

// Insert into L1, and optionally insert into L2; This function does *NOT* update inserted line state, but will update
// the MESI state of evicted line
uint64_t cc_scache_l1_insert_recursive(cc_scache_t *cc, int id, uint64_t cycle, uint64_t addr, int dirty) {
  scache_t *l1 = cc_scache_get_core_cache(cc, MESI_CACHE_L1, id);
  scache_op_result_t insert_result;
  scache_insert(l1, addr, dirty, &insert_result);
  assert(insert_result.state == SCACHE_SUCCESS || insert_result.state == SCACHE_EVICT);
  (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L1, id, CC_STAT_INSERT))++;
  if(insert_result.state == SCACHE_EVICT) {
    (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L1, id, CC_STAT_EVICT))++;
    assert(insert_result.evict_count == 1);
    uint64_t evict_addr = insert_result.evict_entries[0].addr;
    int evict_dirty = insert_result.evict_entries[0].dirty;
    MESI_entry_t *MESI_entry = dmap_find_MESI_entry(cc->commons.dmap, 0UL, evict_addr);
    assert(MESI_entry != NULL);
    // Must be included in both current L1 and L2
    assert(MESI_entry_is_sharer(MESI_entry, MESI_CACHE_L1, id) == 1);
    assert(MESI_entry_is_sharer(MESI_entry, MESI_CACHE_L2, id) == 1);
    if(evict_dirty == 1) {
      // If line is dirty then must be M in L1 state
      assert(MESI_entry->l1_state == MESI_STATE_M);
      cycle = cc_scache_l2_insert_recursive(cc, id, cycle, evict_addr, evict_dirty);
      // Update L2 state if it is a dirty eviction; Bitmap is not changed the cache is inclusive
      MESI_entry->l2_state = MESI_STATE_M;
    } else {
      // Otherwise, still need to update LRU, so just perform a lookup
      scache_op_result_t lookup_result;
      scache_lookup(cc_scache_get_core_cache(cc, MESI_CACHE_L2, id), evict_addr, &lookup_result);
      assert(lookup_result.state == SCACHE_HIT);
    }
    // Clear sharer's bitmap; state transitions is the same as L2 insertion
    MESI_entry_clear_sharer(MESI_entry, MESI_CACHE_L1, id);
    if(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L1) == 0) {
      MESI_entry->l1_state = MESI_STATE_I;
    }
  }
  return cycle;
}

uint64_t cc_scache_load(cc_scache_t *cc, int id, uint64_t cycle, uint64_t addr) {
  MESI_entry_t *MESI_entry = dmap_find_MESI_entry(cc->commons.dmap, 0UL, addr);
  assert(MESI_entry != NULL);
  // Following two for cycle stats
  uint64_t begin_cycle = cycle;
  int end_level = MESI_CACHE_LLC; // If hit DRAM then end_level will be LLC
  int cache; // This is the hit level after the following loop
  for(cache = MESI_CACHE_L1;cache <= MESI_CACHE_LLC;cache++) {
    (*cc_common_get_stat_ptr(&cc->commons, cache, id, CC_STAT_LOOKUP))++;
    (*cc_common_get_stat_ptr(&cc->commons, cache, id, CC_STAT_LOAD))++;
    // Add latency no matter hit or not
    cycle += cc->commons.cache_latencies[cache];
    // Simulate cache lookup to update LRU
    scache_op_result_t lookup_result;
    scache_t *scache = cc_scache_get_core_cache(cc, cache, id);
    scache_lookup(scache, addr, &lookup_result);
    // If hit, exit the loop, or miss all levels and exit loop naturally
    if(lookup_result.state == SCACHE_HIT) {
      (*cc_common_get_stat_ptr(&cc->commons, cache, id, CC_STAT_LOAD_HIT))++;
      end_level = cache; // If hit DRAM this will never be set, so use the default value set on init
      if(cache == MESI_CACHE_LLC) {
        uint64_t extra_cycles = scache->access_hit_cb(scache, addr);
        cycle += extra_cycles;
      }
      break;
    }
    (*cc_common_get_stat_ptr(&cc->commons, cache, id, CC_STAT_LOAD_MISS))++;
  }
  // Whether to grant E state in upper levels; Only do this if read from DRAM or LLC and there is no sharer
  int e_state = 0;
  if(cache == MESI_CACHE_LLC) { // Hits LLC; check if need downgrade; issue downgrade and possibly write back
    int l1_wb = 0;
    // Check L1 state on whether the line is exclusively owned by another L1
    if(MESI_entry->l1_state == MESI_STATE_M || MESI_entry->l1_state == MESI_STATE_E) {
      assert(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L1) == 1);
      assert(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L2) == 1);
      assert(MESI_entry_get_exclusive_sharer(MESI_entry, MESI_CACHE_L1) == \
             MESI_entry_get_exclusive_sharer(MESI_entry, MESI_CACHE_L2));
      int sharer_id = MESI_entry_get_exclusive_sharer(MESI_entry, MESI_CACHE_L1);
      (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L1, sharer_id, CC_STAT_DOWNGRADE))++;
      // Write back the line to LLC, if L1 is in M state
      if(MESI_entry->l1_state == MESI_STATE_M) {
        cc_scache_llc_insert_recursive(cc, sharer_id, cycle, addr, 1);
        MESI_entry->llc_state = MESI_STATE_M;
        l1_wb = 1;
      }
      scache_op_result_t downgrade_result;
      scache_downgrade(cc_scache_get_core_cache(cc, MESI_CACHE_L1, sharer_id), addr, &downgrade_result);
      assert(downgrade_result.state == SCACHE_SUCCESS);
      MESI_entry->l1_state = MESI_STATE_S;
    }
    // Then do the same with L2, but only issue write back if there is no L1 write back
    if(MESI_entry->l2_state == MESI_STATE_M || MESI_entry->l2_state == MESI_STATE_E) {
      assert(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L2) == 1);
      int sharer_id = MESI_entry_get_exclusive_sharer(MESI_entry, MESI_CACHE_L2);
      (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L2, sharer_id, CC_STAT_DOWNGRADE))++;
      // If there is no L1 write back, and L2 is M state, then write back
      if(MESI_entry->l2_state == MESI_STATE_M && l1_wb == 0) {
        cc_scache_llc_insert_recursive(cc, sharer_id, cycle, addr, 1);
        MESI_entry->llc_state = MESI_STATE_M;
      }
      scache_op_result_t downgrade_result;
      scache_downgrade(cc_scache_get_core_cache(cc, MESI_CACHE_L2, sharer_id), addr, &downgrade_result);
      assert(downgrade_result.state == SCACHE_SUCCESS);
      MESI_entry->l2_state = MESI_STATE_S;
    }
    // If there is no sharer, then grant E state when inserting upwards
    if(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L2) == 0) {
      assert(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L1) == 0);
      e_state = 1;
    }
    cache = MESI_CACHE_L2; // Inserting point
  } else if(cache == MESI_CACHE_END) { // Misses LLC, bring line into LLC by reading DRAM
    assert(MESI_entry->llc_state == MESI_STATE_I);
    assert(MESI_entry->l1_state == MESI_STATE_I);
    assert(MESI_entry->l2_state == MESI_STATE_I);
    assert(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L1) == 0);
    assert(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L2) == 0);
    // Misses all levels, issue DRAM read, and then insert into LLC in read-only mode
    uint64_t old_cycle = cycle;
    cycle = dram_read(cc->commons.dram, cycle, 0UL, addr);
    cc->commons.dram_read_count++;
    cc->commons.dram_read_cycle += (cycle - old_cycle);
    if(cc->commons.dram_debug_cb != NULL) {
      cc->commons.dram_debug_cb(DRAM_READ, id, cycle, 0UL, addr, UTIL_CACHE_LINE_SIZE);
    }
    cc_scache_llc_insert_recursive(cc, id, cycle, addr, 0);
    MESI_entry->llc_state = MESI_STATE_S;
    // The line will be installed as E state lines in upper level
    e_state = 1;
    cache = MESI_CACHE_L2; // Inserting point
  } else {
    assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2);
    // If hit L2, and L2 state is E or M, then E state can also be granted
    if(cache == MESI_CACHE_L2 && 
       (MESI_entry->l2_state == MESI_STATE_E || MESI_entry->l2_state == MESI_STATE_M)) {
      assert(MESI_entry_get_exclusive_sharer(MESI_entry, MESI_CACHE_L2) == id);
      e_state = 1;
    }
    cache--; // One level above
  }
  if(cache == MESI_CACHE_L2) {
    cc_scache_l2_insert_recursive(cc, id, cycle, addr, 0);
    MESI_entry->l2_state = (e_state == 1) ? MESI_STATE_E : MESI_STATE_S;
    MESI_entry_set_sharer(MESI_entry, MESI_CACHE_L2, id);
    cache = MESI_CACHE_L1;
  }
  if(cache == MESI_CACHE_L1) {
    cc_scache_l1_insert_recursive(cc, id, cycle, addr, 0);
    MESI_entry->l1_state = (e_state == 1) ? MESI_STATE_E : MESI_STATE_S;
    MESI_entry_set_sharer(MESI_entry, MESI_CACHE_L1, id);
  }
  // Update access cycle stats
  cc_common_update_access_cycles(&cc->commons, id, 0, end_level, begin_cycle, cycle);
  return cycle;
}

// Send invalidation to the given cache, checks return value and dirty bit by assertion
// This function does not update statistics
// This function takes "dirty" argument for assertion
static void cc_scache_store_helper_send_inv(cc_scache_t *cc, int cache, int id, uint64_t addr, int dirty) {
  // Invalidate in the actual cache object, regardless of dirty or non-dirty
  scache_t *inv_cache = cc_scache_get_core_cache(cc, cache, id);
  scache_op_result_t inv_result;
  // Returns SCACHE_EVICT if the line is found; SUCCESS if line misses
  scache_inv(inv_cache, addr, &inv_result);
  // Must be clean eviction (dirty is handled in the previous branch)
  assert(inv_result.state == SCACHE_EVICT && inv_result.evict_count == 1 && \
         inv_result.evict_entries[0].dirty == dirty);
  (void)dirty;
  return;
}

static int cc_scache_store_helper_LLC_inv(
  cc_scache_t *cc, int cache, int id, uint64_t cycle, uint64_t addr, int need_wb) {
  MESI_entry_t *MESI_entry = dmap_find_MESI_entry(cc->commons.dmap, 0UL, addr);
  assert(MESI_entry != NULL);
  if(MESI_entry->states[cache] == MESI_STATE_M) {
    assert(MESI_entry_count_sharer(MESI_entry, cache) == 1);
    if(cache == MESI_CACHE_L1) {
      assert(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L2) == 1);
    }
    int sharer_id = MESI_entry_get_exclusive_sharer(MESI_entry, cache);
    assert(sharer_id != id);
    (*cc_common_get_stat_ptr(&cc->commons, cache, sharer_id, CC_STAT_INV))++;
    if(need_wb == 1) {
      cc_scache_llc_insert_recursive(cc, sharer_id, cycle, addr, 1);
      MESI_entry->llc_state = MESI_STATE_M;  
    }
    MESI_entry_clear_sharer(MESI_entry, cache, sharer_id);
    assert(MESI_entry_count_sharer(MESI_entry, cache) == 0);
    cc_scache_store_helper_send_inv(cc, cache, sharer_id, addr, 1);
    MESI_entry->states[cache] = MESI_STATE_I;
    return 1;
  }
  int curr = -1;
  while(1) {
    curr = MESI_entry_get_next_sharer(MESI_entry, cache, curr + 1);
    if(curr == -1) {
      break;
    } else if(curr == id) {
      continue;
    }
    (*cc_common_get_stat_ptr(&cc->commons, cache, curr, CC_STAT_INV))++;
    cc_scache_store_helper_send_inv(cc, cache, curr, addr, 0);
  }
  MESI_entry_clear_all_sharer(MESI_entry, cache);
  return 0;
}

uint64_t cc_scache_store(cc_scache_t *cc, int id, uint64_t cycle, uint64_t addr) {
  MESI_entry_t *MESI_entry = dmap_find_MESI_entry(cc->commons.dmap, 0UL, addr);
  assert(MESI_entry != NULL);
  uint64_t begin_cycle = cycle;
  int end_level = MESI_CACHE_LLC; // If hit DRAM then end_level will be LLC
  int cache;
  // Hit entries from lookup results; Indexed by cache identifier
  scache_entry_t *hit_entries[3] = {NULL, NULL, NULL};
  for(cache = MESI_CACHE_L1;cache <= MESI_CACHE_LLC;cache++) {
    (*cc_common_get_stat_ptr(&cc->commons, cache, id, CC_STAT_LOOKUP))++;
    (*cc_common_get_stat_ptr(&cc->commons, cache, id, CC_STAT_STORE))++;
    cycle += cc->commons.cache_latencies[cache];
    scache_op_result_t lookup_result;
    scache_t *scache = cc_scache_get_core_cache(cc, cache, id);
    scache_lookup(scache, addr, &lookup_result);
    int MESI_state = MESI_entry->states[cache];
    if(lookup_result.state == SCACHE_HIT) {
      // Save entries in the cache for future updates (do not insert if line already exists)
      // But only do this when it is an actual hit, since otherwise hit_entry will be LRU entry
      hit_entries[cache] = lookup_result.hit_entry;
      // Hit on LLC can always be resolved with invalidation
      if(cache == MESI_CACHE_LLC || MESI_state == MESI_STATE_E || MESI_state == MESI_STATE_M) {
        (*cc_common_get_stat_ptr(&cc->commons, cache, id, CC_STAT_STORE_HIT))++;
        end_level = cache;
        if(cache == MESI_CACHE_LLC) {
          uint64_t extra_cycles = scache->access_hit_cb(scache, addr);
          cycle += extra_cycles;
        }
        break;
      }
    }
    (*cc_common_get_stat_ptr(&cc->commons, cache, id, CC_STAT_STORE_MISS))++;
  }
  if(cache == MESI_CACHE_LLC) { // Hits LLC; check if need inv; issue inv and possibly write back
    int l1_wb = cc_scache_store_helper_LLC_inv(cc, MESI_CACHE_L1, id, cycle, addr, 1);
    cc_scache_store_helper_LLC_inv(cc, MESI_CACHE_L2, id, cycle, addr, l1_wb == 0);
    cache = MESI_CACHE_L2;
  } else if(cache == MESI_CACHE_END) { // Misses LLC, bring line into LLC by reading DRAM
    assert(MESI_entry->llc_state == MESI_STATE_I);
    assert(MESI_entry->l1_state == MESI_STATE_I);
    assert(MESI_entry->l2_state == MESI_STATE_I);
    assert(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L1) == 0);
    assert(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L2) == 0);
    assert(hit_entries[2] == NULL);
    uint64_t old_cycle = cycle;
    cycle = dram_read(cc->commons.dram, cycle, 0UL, addr);
    cc->commons.dram_read_count++;
    cc->commons.dram_read_cycle += (cycle - old_cycle);
    if(cc->commons.dram_debug_cb != NULL) {
      cc->commons.dram_debug_cb(DRAM_READ, id, cycle, 0UL, addr, UTIL_CACHE_LINE_SIZE);
    }
    cc_scache_llc_insert_recursive(cc, id, cycle, addr, 0);
    MESI_entry->llc_state = MESI_STATE_S;
    cache = MESI_CACHE_L2;
  } else {
    assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2);
    // Hits L1, but in E state, needs special handling to update MESI state to M and set dirty bit
    if(cache == MESI_CACHE_L1 && MESI_entry->l1_state != MESI_STATE_M) {
      assert(MESI_entry->l1_state == MESI_STATE_E);
      assert(hit_entries[0] != NULL && hit_entries[0]->dirty == 0 && hit_entries[0]->addr == addr);
      hit_entries[0]->dirty = 1;
      MESI_entry->l1_state = MESI_STATE_M;
    }
    // If hits L1, then the hit logic is already processed above. This will make sure that L1 hit will not
    // be processed below
    cache--; 
  }
  // L2 insert or upgrade
  if(cache == MESI_CACHE_L2) {
    // Only insert if the entry does not exist
    if(hit_entries[1] == NULL) {
      cc_scache_l2_insert_recursive(cc, id, cycle, addr, 0); // Do not set dirty bit and transit to E state
    } else {
      assert(hit_entries[1]->addr == addr);
    }
    // Since LLC has already invalidated all copies, L2 must be in E state
    MESI_entry->l2_state = MESI_STATE_E;
    MESI_entry_set_sharer(MESI_entry, MESI_CACHE_L2, id);
    cache = MESI_CACHE_L1;
  }
  // L1 insert or upgrade (but not from E state)
  if(cache == MESI_CACHE_L1) {
    if(hit_entries[0] == NULL) {
      // Insert into L1 with dirty bit set, if the line does not exist
      cc_scache_l1_insert_recursive(cc, id, cycle, addr, 1);
    } else {
      assert(hit_entries[0]->addr == addr);
      // Just set dirty bit if line exists
      hit_entries[0]->dirty = 1;
    }
    MESI_entry->l1_state = MESI_STATE_M;
    MESI_entry_set_sharer(MESI_entry, MESI_CACHE_L1, id);
  }
  cc_common_update_access_cycles(&cc->commons, id, 1, end_level, begin_cycle, cycle);
  return cycle;
}

// Prints the particular set in a given cache
void cc_scache_print_set(cc_scache_t *cc, int cache, int id, int set_index) {
  scache_t *scache = cc_scache_get_core_cache(cc, cache, id);
  printf("[%s scache id %d]\n", MESI_cache_names[cache], id);
  scache_print_set(scache, set_index);
  return;
}

// Wrapper for printing MESI entry
void cc_scache_print_MESI_entry(cc_scache_t *cc, uint64_t addr) {
  MESI_entry_t *MESI_entry = dmap_find_MESI_entry(cc->commons.dmap, 0UL, addr);
  if(MESI_entry == NULL) {
    error_exit("MESI entry for addr 0x%lX does not exist\n", addr);
  }
  MESI_entry_print(MESI_entry);
  return;
}

// Insert into a given level, following inclusiveness and MESI states
// The state_vec specifies the states; Dirty bits are derived from states
// Note that this fuction reports error if eviction happens at any level
// Note: state_vec starts at the given level
void cc_scache_insert_levels(cc_scache_t *cc, int begin, int id, uint64_t addr, const int *state_vec) {
  dmap_t *dmap = cc->commons.dmap;
  MESI_entry_t *MESI_entry = &dmap_insert(dmap, 0UL, addr)->MESI_entry;
  scache_t *llc = cc->shared_llc;
  scache_t *l2 = cc_scache_get_core_cache(cc, MESI_CACHE_L2, id);
  scache_t *l1 = cc_scache_get_core_cache(cc, MESI_CACHE_L1, id);
  scache_t *caches[3] = {l1, l2, llc};
  scache_op_result_t insert_result;
  assert(begin >= MESI_CACHE_L1 && begin <= MESI_CACHE_LLC);
  for(int i = begin;i < MESI_CACHE_END;i++) {
    int state = state_vec[i - begin];
    assert(state >= MESI_STATE_BEGIN && state < MESI_STATE_END);
    assert(state != MESI_STATE_I);
    scache_insert(caches[i], addr, state == MESI_STATE_M, &insert_result);
    if(insert_result.state == SCACHE_EVICT) {
      error_exit("%s Insert results in eviction\n", MESI_cache_names[i]);
    }
    // Only set sharer bit when the cache is in L1 and L2
    if(i == MESI_CACHE_L1 || i == MESI_CACHE_L2) {
      // S and M/E states are incompatible
      if(MESI_entry->states[i] == MESI_STATE_S && state != MESI_STATE_S) {
        MESI_entry_print(MESI_entry);
        error_exit("Inserting exclusive lines while existing state is S\n");
      } else if((MESI_entry->states[i] == MESI_STATE_M || MESI_entry->states[i] == MESI_STATE_E) && \
                state == MESI_STATE_S) {
        MESI_entry_print(MESI_entry);
        error_exit("Inserting S lines while existing state is exclusive\n");
      }
      MESI_entry_set_sharer(MESI_entry, i, id);
    } else {
      assert(i == MESI_CACHE_LLC);
      // LLC state must not be changed
      if(MESI_entry->llc_state != MESI_STATE_I && MESI_entry->llc_state != state) {
        error_exit("Insertion will change LLC state from %s to %s\n",
          MESI_state_names[MESI_entry->llc_state], MESI_state_names[state]);
      } else if(state == MESI_STATE_E) {
        error_exit("E state is not allowed in LLC\n");
      }
    }
    MESI_entry->states[i] = state;
  }
  return;
}

void cc_scache_conf_print(cc_scache_t *cc) {
  printf("---------- cc_scache_t conf ----------\n");
  printf("Cores %d\n", cc->commons.core_count);
  for(int i = MESI_CACHE_BEGIN;i < MESI_CACHE_END;i++) {
    printf("[%s] size %d ways %d latency %d\n", 
      MESI_cache_names[i], cc->commons.cache_sizes[i], cc->commons.cache_way_counts[i], 
      cc->commons.cache_latencies[i]);
  }
  printf("LLC ratio %d\n", cc->llc_ratio);
  if(cc->commons.dmap == NULL) {
    printf("dmap is not initialized\n");
  }
  if(cc->commons.dram == NULL) {
    printf("DRAM is not initialized\n");
  }
  scache_conf_print(cc->shared_llc);
  return;
}

void cc_scache_stat_print(cc_scache_t *cc) {
  printf("---------- cc_scache_t stat ----------\n");
  cc_common_print_stats(&cc->commons);
  // Also print LLC stat since it contains BDI/FPC stats
  scache_stat_print(cc->shared_llc);
  return;
}

//* cc_ocache_t

// Initialize core structure without initializing any of the cores
void cc_ocache_init_cores(cc_ocache_t *cc, int core_count) {
  cc->commons.core_count = core_count;
  int size = sizeof(cc_ocache_core_t) * core_count;
  cc->cores = (cc_ocache_core_t *)malloc(size);
  SYSEXPECT(cc->cores != NULL);
  memset(cc->cores, 0x00, size);
  cc_common_init_stats(&cc->commons, core_count);
  return;
}

void cc_ocache_init_llc(
  cc_ocache_t *cc, int size, int physical_way_count, int tag_way_count, int latency, const char *algorithm) {
  cc->shared_llc = ocache_init(size, physical_way_count, tag_way_count);
  assert(cc->cores != NULL);
  for(int i = 0;i < cc->commons.core_count;i++) {
    cc->cores[i].llc = cc->shared_llc;
  }
  cc->commons.llc_latency = latency;
  cc->commons.llc_size = size;
  cc->commons.llc_physical_way_count = physical_way_count;
  // This may report error
  ocache_set_compression_type(cc->shared_llc, algorithm);
  ocache_set_level(cc->shared_llc, MESI_CACHE_LLC);
  ocache_set_name(cc->shared_llc, "Shared LLC");
  return;
}

void cc_ocache_init_l1_l2(cc_ocache_t *cc, int cache, int size, int way_count, int latency) {
  int core_count = cc->commons.core_count;
  assert(cc->cores != NULL);
  assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2);
  for(int i = 0;i < core_count;i++) {
    ocache_t *ocache = ocache_init(size, way_count, way_count);
    cc->cores[i].ocaches[cache] = ocache;
    // Disable compression
    ocache_set_compression_type(ocache, "None");
    ocache_set_MBD_opt_type(ocache, "None");
    ocache_set_evict_opt_type(ocache, "None");
    if(cc->shared_llc != NULL) {
      cc->cores[i].llc = cc->shared_llc;
    }
    cc->cores[i].id = i;
    // This means that the ocache object will report error when a shaped operation is called on it
    assert(cc->cores[i].ocaches[cache]->use_shape == 0);
    ocache_set_level(ocache, cache);
    char name[256];
    snprintf(name, sizeof(name), "Core %d Level %d", i, cache);
    ocache_set_name(ocache, name);
  }
  // Per-level structure
  cc->commons.cache_latencies[cache] = latency;
  cc->commons.cache_sizes[cache] = size;
  cc->commons.cache_way_counts[cache] = way_count;
  return;
}

// This function requires the following conf keys:
//   cc.ocache.core_count
//   cc.ocache.l1.size cc.ocache.l1.way_count cc.ocache.l1.latency
//   cc.ocache.l2.size cc.ocache.l2.way_count cc.ocache.l2.latency
//   cc.ocache.llc.size cc.ocache.llc.physical_way_count cc.ocache.llc.tag_way_count (optional) cc.ocache.llc.latency
//   cc.ocache.llc.insert_type ("sb", "seg"), optional
//   cc.ocache.llc.bcache.line_count cc.ocache.llc.bcache.way_count cc.ocache.llc.bcache.shape
//   cc.ocache.default_shape (4_1, 1_4, 2_2, None)
cc_ocache_t *cc_ocache_init_conf(conf_t *conf) {
  cc_ocache_t *cc = cc_ocache_init();
  cc->commons.conf = conf;
  // Core count
  int core_count = conf_find_int32_range(conf, "cc.ocache.core_count", 1, CONF_INT32_MAX, CONF_RANGE);
  cc_ocache_init_cores(cc, core_count);
  // L1
  int l1_size = conf_find_int32_range(conf, "cc.ocache.l1.size", (int)UTIL_CACHE_LINE_SIZE, CONF_INT32_MAX, CONF_RANGE);
  int l1_way_count = conf_find_int32_range(conf, "cc.ocache.l1.way_count", 1, CONF_INT32_MAX, CONF_RANGE);
  int l1_latency = conf_find_int32_range(conf, "cc.ocache.l1.latency", 0, CONF_INT32_MAX, CONF_RANGE);
  cc_ocache_init_l1_l2(cc, MESI_CACHE_L1, l1_size, l1_way_count, l1_latency);
  // L2
  int l2_size = conf_find_int32_range(conf, "cc.ocache.l2.size", (int)UTIL_CACHE_LINE_SIZE, CONF_INT32_MAX, CONF_RANGE);
  int l2_way_count = conf_find_int32_range(conf, "cc.ocache.l2.way_count", 1, CONF_INT32_MAX, CONF_RANGE);
  int l2_latency = conf_find_int32_range(conf, "cc.ocache.l2.latency", 0, CONF_INT32_MAX, CONF_RANGE);
  cc_ocache_init_l1_l2(cc, MESI_CACHE_L2, l2_size, l2_way_count, l2_latency);
  // LLC
  int llc_size = \
    conf_find_int32_range(conf, "cc.ocache.llc.size", (int)UTIL_CACHE_LINE_SIZE, CONF_INT32_MAX, CONF_RANGE);
  int llc_physical_way_count = \
    conf_find_int32_range(conf, "cc.ocache.llc.physical_way_count", 1, CONF_INT32_MAX, CONF_RANGE);
  int llc_tag_way_count = llc_physical_way_count;
  int llc_tag_way_count_found = \
    conf_find_int32(conf, "cc.ocache.llc.tag_way_count", &llc_tag_way_count);
  if(llc_tag_way_count_found == 0) {
    llc_tag_way_count = llc_physical_way_count;
  }
  int llc_latency = \
    conf_find_int32_range(conf, "cc.ocache.llc.latency", 0, CONF_INT32_MAX, CONF_RANGE);
  const char *llc_algorithm = conf_find_str_mandatory(conf, "cc.ocache.llc.algorithm");
  cc_ocache_init_llc(cc, llc_size, llc_physical_way_count, llc_tag_way_count, llc_latency, llc_algorithm);
  // LLC MBD optimization type, optional, default to None
  char *MBD_opt_type_str = NULL;
  int MBD_opt_type_found = conf_find_str(conf, "cc.ocache.llc.MBD_opt_type", &MBD_opt_type_str);
  if(MBD_opt_type_found == 1) {
    assert(MBD_opt_type_str != NULL);
    ocache_set_MBD_opt_type(cc->shared_llc, MBD_opt_type_str);
  } else {
    ocache_set_MBD_opt_type(cc->shared_llc, "None");
  }
  // LLC evict optimization type, optional, default to None
  char *evict_opt_type_str = NULL;
  int evict_opt_type_found = conf_find_str(conf, "cc.ocache.llc.evict_opt_type", &evict_opt_type_str);
  if(evict_opt_type_found == 1) {
    assert(evict_opt_type_str != NULL);
    ocache_set_evict_opt_type(cc->shared_llc, evict_opt_type_str);
  } else {
    ocache_set_evict_opt_type(cc->shared_llc, "None");
  }
  // insert type, optional
  char *insert_type_str;
  int insert_type_found = conf_find_str(conf, "cc.ocache.llc.insert_type", &insert_type_str);
  if(insert_type_found == 1) {
    ocache_set_insert_type(cc->shared_llc, insert_type_str);
  }
  // bcache, if given
  int bcache_line_count, bcache_way_count;
  char *bcache_shape_str;
  int bcache_ret = 0;
  bcache_ret += conf_find_int32(conf, "cc.ocache.llc.bcache.line_count", &bcache_line_count);
  bcache_ret += conf_find_int32(conf, "cc.ocache.llc.bcache.way_count", &bcache_way_count);
  bcache_ret += conf_find_str(conf, "cc.ocache.llc.bcache.shape", &bcache_shape_str);
  if(bcache_ret == 3) {
    ocache_init_bcache(cc->shared_llc, bcache_line_count, bcache_way_count, bcache_shape_str);
  } else if(bcache_ret > 0) {
    printf("bcache will not be initialized because some essential configurations are missing\n");
  }
  // Default shape
  const char *default_shape = conf_find_str_mandatory(conf, "cc.ocache.default_shape");
  assert(default_shape != NULL);
  if(streq(default_shape, "None") == 1) {
    cc->default_shape = OCACHE_SHAPE_NONE;
  } else if(streq(default_shape, "4_1") == 1) {
    cc->default_shape = OCACHE_SHAPE_4_1;
  } else if(streq(default_shape, "1_4") == 1) {
    cc->default_shape = OCACHE_SHAPE_1_4;
  } else if(streq(default_shape, "2_2") == 1) {
    cc->default_shape = OCACHE_SHAPE_2_2;
  } else {
    error_exit("Unknown default_shape: \"%s\"\n", default_shape);
  }
  return cc;
}

cc_ocache_t *cc_ocache_init() {
  cc_ocache_t *cc = (cc_ocache_t *)malloc(sizeof(cc_ocache_t));
  SYSEXPECT(cc != NULL);
  memset(cc, 0x00, sizeof(cc_scache_t));
  return cc;
}

void cc_ocache_free(cc_ocache_t *cc) {
  for(int i = 0;i < cc->commons.core_count;i++) {
    ocache_free(cc->cores[i].l1);
    ocache_free(cc->cores[i].l2);
  }
  free(cc->cores);
  ocache_free(cc->shared_llc);
  cc_common_free_stats(&cc->commons);
  free(cc);
  return;
}

// Only sets dmap for the LLC; Also save the pointer in its own commons structure
void cc_ocache_set_dmap(cc_ocache_t *cc, dmap_t *dmap) {
  if(cc->commons.dmap != NULL) {
    error_exit("The dmap has already been set\n");
  } else if(cc->shared_llc == NULL) {
    error_exit("dmap must be set after the LLC is initialized\n");
  }
  cc->commons.dmap = dmap;
  // This function also sets use_shape to 1 in the LLC
  ocache_set_dmap(cc->shared_llc, dmap);
  return;
}

void cc_ocache_set_dram(cc_ocache_t *cc, dram_t *dram) {
  if(cc->commons.dram != NULL) {
    error_exit("The DRAM has already been set\n");
  }
  cc->commons.dram = dram;
  return;
}

// Does not change the LRU of lines; shape arg is ignored for non-LLC cache
int cc_ocache_is_line_invalid(cc_ocache_t *cc, int cache, int id, uint64_t oid, uint64_t addr, int shape) {
  ocache_t *ocache = cc_ocache_get_core_cache(cc, cache, id);
  ocache_op_result_t lookup_result;
  if(cache != MESI_CACHE_LLC) {
    shape = OCACHE_SHAPE_NONE;
  }
  _ocache_lookup(ocache, oid, addr, shape, 0, 0, &lookup_result);
  return lookup_result.state == OCACHE_MISS;
}

int cc_ocache_is_line_dirty(cc_ocache_t *cc, int cache, int id, uint64_t oid, uint64_t addr, int shape) {
  ocache_t *ocache = cc_ocache_get_core_cache(cc, cache, id);
  ocache_op_result_t lookup_result;
  if(cache != MESI_CACHE_LLC) {
    shape = OCACHE_SHAPE_NONE;
  }
  _ocache_lookup(ocache, oid, addr, shape, 0, 0, &lookup_result);
  if(lookup_result.state == OCACHE_MISS) {
    error_exit("The line (oid %lu addr %lX shape %d) does not exist on level \"%s\" id %d\n",
      oid, addr, shape, MESI_cache_names[cache], id);
  }
  assert(lookup_result.hit_entry != NULL);
  return ocache_entry_is_dirty(lookup_result.hit_entry, lookup_result.hit_index);
}

// Returns the shape of the line (the shape of the slot) in the cache
// The line must be already in the cache, otherwise report error
int cc_ocache_get_line_shape(cc_ocache_t *cc, int cache, int id, uint64_t oid, uint64_t addr, int shape) {
  ocache_t *ocache = cc_ocache_get_core_cache(cc, cache, id);
  ocache_op_result_t lookup_result;
  if(cache != MESI_CACHE_LLC) {
    shape = OCACHE_SHAPE_NONE;
  }
  _ocache_lookup(ocache, oid, addr, shape, 0, 0, &lookup_result);
  if(lookup_result.state == OCACHE_MISS) {
    error_exit("The line (oid %lu addr %lX shape %d) does not exist on level \"%s\" id %d\n",
      oid, addr, shape, MESI_cache_names[cache], id);
  }
  assert(lookup_result.hit_entry != NULL);
  return lookup_result.hit_entry->shape;
}

// The size argument is compressed size of the evicted line in the LLC. This value may be used as the 
// write back size
static uint64_t cc_ocache_llc_evict_recursive(
  cc_ocache_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int dirty, int size) {
  (void)id;
  MESI_entry_t *MESI_entry = dmap_find_MESI_entry(cc->commons.dmap, oid, addr);
  assert(MESI_entry != NULL);
  assert(dirty == 0 || dirty == 1);
  assert(dirty == 0 || MESI_entry->llc_state == MESI_STATE_M);
  assert(MESI_entry->l1_state != MESI_STATE_M || MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L1) == 1);
  assert(MESI_entry->l2_state != MESI_STATE_M || MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L2) == 1);
  // ocache needs to distinguish between lower level dirty and upper level dirty
  int upper_dirty = 0;
  if(MESI_entry->l1_state == MESI_STATE_M || MESI_entry->l2_state == MESI_STATE_M) {
    upper_dirty = 1;
  }
  for(int cache = MESI_CACHE_L1;cache <= MESI_CACHE_L2;cache++) {
    int curr = -1;
    while(1) {
      // Return value is the ID of the sharer; or -1 if reaches the end
      curr = MESI_entry_get_next_sharer(MESI_entry, cache, curr + 1);
      if(curr != -1) {
        // Update the inv stat counter
        (*cc_common_get_stat_ptr(&cc->commons, cache, curr, CC_STAT_INV))++;
        ocache_op_result_t inv_result;
        ocache_t *inv_cache = cc_ocache_get_core_cache(cc, cache, curr);
        // Upper level caches are always not compressed
        ocache_inv(inv_cache, oid, addr, OCACHE_SHAPE_NONE, &inv_result);
        // Ignore dirty bits of the evicted entries since we have already done that above
        assert(inv_result.state == SCACHE_SUCCESS || inv_result.state == SCACHE_EVICT);
      } else {
        break;
      }
    }
  }
  // Clear all states and remove all sharers
  MESI_entry_invalidate(MESI_entry);
  // Write back in both cases
  if(dirty == 1 || upper_dirty == 1) {
    // If upper dirty is one then the compressed data overrides the obsolete compressed line in LLC
    // which means that we write back entire cache line; Otherwise LLC dirty write back
    int wb_size = (upper_dirty == 1) ? UTIL_CACHE_LINE_SIZE : size;
    cc->commons.dram_write_count++;
    cc->commons.dram_write_bytes += wb_size;
    dram_write(cc->commons.dram, cycle, oid, addr, wb_size);
    if(cc->commons.dram_debug_cb != NULL) {
      cc->commons.dram_debug_cb(DRAM_WRITE, id, cycle, oid, addr, wb_size);
    }
  }
  return cycle;
}

uint64_t cc_ocache_llc_insert_recursive(
  cc_ocache_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int shape, int dirty) {
  ocache_t *llc = cc->shared_llc;
  ocache_op_result_t insert_result;
  ocache_insert(llc, oid, addr, shape, dirty, &insert_result);
  // Evict an entry consisting of at most four lines
  if(insert_result.state == OCACHE_EVICT) {
    (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_LLC, id, CC_STAT_EVICT))++;
    for(int entry_index = 0;entry_index < insert_result.insert_evict_count;entry_index++) {
      // This is the entry currently being processed
      ocache_entry_t *evict_entry = insert_result.insert_evict_entries + entry_index;
      for(int i = 0;i < 4;i++) {
        if(ocache_entry_is_valid(evict_entry, i) == 0) {
          continue;
        }
        uint64_t base_oid = evict_entry->oid;
        uint64_t base_addr = evict_entry->addr;
        int evict_dirty = ocache_entry_is_dirty(evict_entry, i);
        uint64_t evict_oid, evict_addr;
        // Note this is different from the inserted shape
        int evict_shape = evict_entry->shape;
        // Generates the logical line oid and addr using the block info
        ocache_gen_addr_in_sb(llc, base_oid, base_addr, i, evict_shape, &evict_oid, &evict_addr);
        // This is size in the LLC
        int size = ocache_entry_get_size(evict_entry, i);
        // Do not need shape here since upper level caches do not use compression
        cycle = cc_ocache_llc_evict_recursive(cc, id, cycle, evict_oid, evict_addr, evict_dirty, size);
      }
    }
  } else {
    assert(insert_result.state == OCACHE_SUCCESS);
  }
  return cycle;
}

// Shape will be ignored
uint64_t cc_ocache_l2_insert_recursive(
  cc_ocache_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int shape, int dirty) {
  assert(shape == OCACHE_SHAPE_NONE);
  ocache_op_result_t insert_result;
  ocache_t *l2 = cc_ocache_get_core_cache(cc, MESI_CACHE_L2, id);
  ocache_insert(l2, oid, addr, shape, dirty, &insert_result);
  assert(insert_result.state == OCACHE_SUCCESS || insert_result.state == OCACHE_EVICT);
  (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L2, id, CC_STAT_INSERT))++;
  if(insert_result.state == OCACHE_EVICT) {
    (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L2, id, CC_STAT_EVICT))++;
    assert(insert_result.insert_evict_count == 1);
    uint64_t evict_oid = insert_result.insert_evict_entries[0].oid;
    uint64_t evict_addr = insert_result.insert_evict_entries[0].addr;
    // Whether the L2 copy is dirty
    int evict_dirty = ocache_entry_is_dirty(&insert_result.insert_evict_entries[0], 0);
    pmap_entry_t *pmap_entry = pmap_find(cc->commons.dmap, evict_addr);
    // If shape is defined, then use it. Otherwise use default shape in the cc
    int evict_shape = (pmap_entry == NULL) ? shape = cc->default_shape : pmap_entry->shape;
    MESI_entry_t *MESI_entry = dmap_find_MESI_entry(cc->commons.dmap, evict_oid, evict_addr);
    assert(MESI_entry != NULL);
    assert(evict_dirty == 0 || MESI_entry_get_state(MESI_entry, MESI_CACHE_L2) == MESI_STATE_M);
    assert(MESI_entry_get_state(MESI_entry, MESI_CACHE_LLC) != MESI_STATE_I);
    assert(MESI_entry_is_sharer(MESI_entry, MESI_CACHE_L2, id) == 1);
    if(MESI_entry_is_sharer(MESI_entry, MESI_CACHE_L1, id) == 1) {
      (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L1, id, CC_STAT_INV))++;
      ocache_op_result_t inv_result;
      ocache_t *l1 = cc_ocache_get_core_cache(cc, MESI_CACHE_L1, id);
      // Must return EVICT since we know the line exists in L1 according to the bitmap
      // L1 eviction always uses NONE shape since L1 is uncompressed
      ocache_inv(l1, evict_oid, evict_addr, OCACHE_SHAPE_NONE, &inv_result);
      assert(inv_result.state == SCACHE_EVICT && inv_result.evict_index == 0);
      // Whether the L1 copy is dirty
      if(ocache_entry_is_dirty(&inv_result.evict_entry, 0) == 1) {
        assert(MESI_entry_get_state(MESI_entry, MESI_CACHE_L1) == MESI_STATE_M);
        evict_dirty = 1;
      }
      // Clear L1 bitmap. If the eviction results in the sharer being none, then state also transits to invalid
      MESI_entry_clear_sharer(MESI_entry, MESI_CACHE_L1, id);
      if(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L1) == 0) {
        MESI_entry->l1_state = MESI_STATE_I;
      }
    }
    // Clear L2 state and bitmap
    MESI_entry_clear_sharer(MESI_entry, MESI_CACHE_L2, id);
    if(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L2) == 0) {
      MESI_entry->l2_state = MESI_STATE_I;
    }
    if(evict_dirty == 1) {
      // Insert recursively into LLC, may incur extra evictions; Only insert if dirty
      cycle = cc_ocache_llc_insert_recursive(cc, id, cycle, evict_oid, evict_addr, evict_shape, evict_dirty);
      // Update LLC state for evicted line, if it is dirty
      MESI_entry->llc_state = MESI_STATE_M;
    } else {
      // Otherwise, still need to update LRU, so just perform a lookup
      ocache_op_result_t lookup_result;
      ocache_lookup_read(
        cc_ocache_get_core_cache(cc, MESI_CACHE_LLC, id), 
        evict_oid, evict_addr, evict_shape, &lookup_result);
      // Shape must be consistent
      if(evict_shape == OCACHE_SHAPE_NONE) {
        assert(lookup_result.state == OCACHE_HIT_NORMAL);
      } else {
        assert(lookup_result.state == OCACHE_HIT_COMPRESSED);
      }
    }
  }
  return cycle;
}

// Shape will be ignored
uint64_t cc_ocache_l1_insert_recursive(
  cc_ocache_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int shape, int dirty) {
  assert(shape == OCACHE_SHAPE_NONE);
  ocache_t *l1 = cc_ocache_get_core_cache(cc, MESI_CACHE_L1, id);
  ocache_op_result_t insert_result;
  ocache_insert(l1, oid, addr, shape, dirty, &insert_result);
  assert(insert_result.state == OCACHE_SUCCESS || insert_result.state == OCACHE_EVICT);
  (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L1, id, CC_STAT_INSERT))++;
  if(insert_result.state == OCACHE_EVICT) {
    (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L1, id, CC_STAT_EVICT))++;
    assert(insert_result.insert_evict_count == 1);
    uint64_t evict_oid = insert_result.insert_evict_entries[0].oid;
    uint64_t evict_addr = insert_result.insert_evict_entries[0].addr;
    // Whether the L1 copy is dirty
    int evict_dirty = ocache_entry_is_dirty(&insert_result.insert_evict_entries[0], 0);
    MESI_entry_t *MESI_entry = dmap_find_MESI_entry(cc->commons.dmap, evict_oid, evict_addr);
    assert(MESI_entry != NULL);
    // Must be included in both current L1 and L2 according to inclusiveness
    assert(MESI_entry_is_sharer(MESI_entry, MESI_CACHE_L1, id) == 1);
    assert(MESI_entry_is_sharer(MESI_entry, MESI_CACHE_L2, id) == 1);
    if(evict_dirty == 1) {
      assert(MESI_entry->l1_state == MESI_STATE_M);
      // Note that we do not need evict shape here since L2 is also not compressed
      cycle = cc_ocache_l2_insert_recursive(cc, id, cycle, evict_oid, evict_addr, OCACHE_SHAPE_NONE, evict_dirty);
      MESI_entry->l2_state = MESI_STATE_M;
    } else {
      ocache_op_result_t lookup_result;
      ocache_lookup_read(cc_ocache_get_core_cache(cc, MESI_CACHE_L2, id), 
        evict_oid, evict_addr, OCACHE_SHAPE_NONE, &lookup_result);
      // L2 is always not compressed, so we can only hit normal
      assert(lookup_result.state == OCACHE_HIT_NORMAL);
    }
    MESI_entry_clear_sharer(MESI_entry, MESI_CACHE_L1, id);
    if(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L1) == 0) {
      MESI_entry->l1_state = MESI_STATE_I;
    }
  }
  return cycle;
}

uint64_t cc_ocache_load(cc_ocache_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr) {
  MESI_entry_t *MESI_entry = dmap_find_MESI_entry(cc->commons.dmap, oid, addr);
  assert(MESI_entry != NULL);
  uint64_t begin_cycle = cycle;
  int end_level = MESI_CACHE_LLC;
  int cache;
  int shape = OCACHE_SHAPE_NONE;
  for(cache = MESI_CACHE_L1;cache <= MESI_CACHE_LLC;cache++) {
    (*cc_common_get_stat_ptr(&cc->commons, cache, id, CC_STAT_LOOKUP))++;
    (*cc_common_get_stat_ptr(&cc->commons, cache, id, CC_STAT_LOAD))++;
    cycle += cc->commons.cache_latencies[cache];
    ocache_op_result_t lookup_result;
    if(cache == MESI_CACHE_LLC) {
      pmap_entry_t *pmap_entry = pmap_find(cc->commons.dmap, addr);
      shape = (pmap_entry == NULL) ? cc->default_shape : pmap_entry->shape;
    }
    ocache_t *ocache = cc_ocache_get_core_cache(cc, cache, id);
    ocache_lookup_read(ocache, oid, addr, shape, &lookup_result);
    // This is exclusive to ocache: May hit full or compressed lines
    if(lookup_result.state != OCACHE_MISS) {
      assert(lookup_result.state == OCACHE_HIT_NORMAL || lookup_result.state == OCACHE_HIT_COMPRESSED);
      (*cc_common_get_stat_ptr(&cc->commons, cache, id, CC_STAT_LOAD_HIT))++;
      end_level = cache;
      if(cache == MESI_CACHE_LLC) {
        uint64_t extra_cycles = ocache->access_hit_cb(ocache, oid, addr, shape);
        cycle += extra_cycles;
      }
      break;
    }
    (*cc_common_get_stat_ptr(&cc->commons, cache, id, CC_STAT_LOAD_MISS))++;
  }
  int e_state = 0;
  if(cache == MESI_CACHE_LLC) {
    int l1_wb = 0;
    if(MESI_entry->l1_state == MESI_STATE_M || MESI_entry->l1_state == MESI_STATE_E) {
      assert(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L1) == 1);
      assert(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L2) == 1);
      assert(MESI_entry_get_exclusive_sharer(MESI_entry, MESI_CACHE_L1) == \
             MESI_entry_get_exclusive_sharer(MESI_entry, MESI_CACHE_L2));
      int sharer_id = MESI_entry_get_exclusive_sharer(MESI_entry, MESI_CACHE_L1);
      (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L1, sharer_id, CC_STAT_DOWNGRADE))++;
      if(MESI_entry->l1_state == MESI_STATE_M) {
        cc_ocache_llc_insert_recursive(cc, sharer_id, cycle, oid, addr, shape, 1);
        MESI_entry->llc_state = MESI_STATE_M;
        l1_wb = 1;
      }
      ocache_op_result_t downgrade_result;
      // L1 downgrade should use None shape
      ocache_downgrade(cc_ocache_get_core_cache(cc, MESI_CACHE_L1, sharer_id), 
        oid, addr, OCACHE_SHAPE_NONE, &downgrade_result);
      assert(downgrade_result.state == OCACHE_SUCCESS);
      MESI_entry->l1_state = MESI_STATE_S;
    }
    if(MESI_entry->l2_state == MESI_STATE_M || MESI_entry->l2_state == MESI_STATE_E) {
      assert(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L2) == 1);
      int sharer_id = MESI_entry_get_exclusive_sharer(MESI_entry, MESI_CACHE_L2);
      (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L2, sharer_id, CC_STAT_DOWNGRADE))++;
      // If there is no L1 write back, and L2 is M state, then write back
      if(MESI_entry->l2_state == MESI_STATE_M && l1_wb == 0) {
        cc_ocache_llc_insert_recursive(cc, sharer_id, cycle, oid, addr, shape, 1);
        MESI_entry->llc_state = MESI_STATE_M;
      }
      ocache_op_result_t downgrade_result;
      // TODO: THIS IS WRONG, SHAPE MUST BE NONE
      ocache_downgrade(cc_ocache_get_core_cache(cc, MESI_CACHE_L2, sharer_id), 
        oid, addr, OCACHE_SHAPE_NONE, &downgrade_result);
      assert(downgrade_result.state == OCACHE_SUCCESS);
      MESI_entry->l2_state = MESI_STATE_S;
    }
    if(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L2) == 0) {
      assert(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L1) == 0);
      e_state = 1;
    }
    cache = MESI_CACHE_L2;
  } else if(cache == MESI_CACHE_END) {
    assert(MESI_entry->llc_state == MESI_STATE_I);
    assert(MESI_entry->l1_state == MESI_STATE_I);
    assert(MESI_entry->l2_state == MESI_STATE_I);
    assert(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L1) == 0);
    assert(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L2) == 0);
    // Misses all levels, issue DRAM read, and then insert into LLC in read-only mode
    uint64_t old_cycle = cycle;
    cycle = dram_read(cc->commons.dram, cycle, oid, addr);
    cc->commons.dram_read_count++;
    cc->commons.dram_read_cycle += (cycle - old_cycle);
    if(cc->commons.dram_debug_cb != NULL) {
      cc->commons.dram_debug_cb(DRAM_READ, id, cycle, oid, addr, UTIL_CACHE_LINE_SIZE);
    }
    cc_ocache_llc_insert_recursive(cc, id, cycle, oid, addr, shape, 0);
    MESI_entry->llc_state = MESI_STATE_S;
    e_state = 1;
    cache = MESI_CACHE_L2;
  } else {
    assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2);
    if(cache == MESI_CACHE_L2 && 
       (MESI_entry->l2_state == MESI_STATE_E || MESI_entry->l2_state == MESI_STATE_M)) {
      assert(MESI_entry_get_exclusive_sharer(MESI_entry, MESI_CACHE_L2) == id);
      e_state = 1;
    }
    cache--;
  }
  if(cache == MESI_CACHE_L2) {
    cc_ocache_l2_insert_recursive(cc, id, cycle, oid, addr, OCACHE_SHAPE_NONE, 0);
    MESI_entry->l2_state = (e_state == 1) ? MESI_STATE_E : MESI_STATE_S;
    MESI_entry_set_sharer(MESI_entry, MESI_CACHE_L2, id);
    cache = MESI_CACHE_L1;
  }
  if(cache == MESI_CACHE_L1) {
    cc_ocache_l1_insert_recursive(cc, id, cycle, oid, addr, OCACHE_SHAPE_NONE, 0);
    MESI_entry->l1_state = (e_state == 1) ? MESI_STATE_E : MESI_STATE_S;
    MESI_entry_set_sharer(MESI_entry, MESI_CACHE_L1, id);
  }
  cc_common_update_access_cycles(&cc->commons, id, 0, end_level, begin_cycle, cycle);
  return cycle;
}

// Send invalidation to the given cache, checks return value and dirty bit by assertion
// This function does not update statistics
// This function takes "dirty" argument for assertion
static void cc_ocache_store_helper_send_inv(
  cc_ocache_t *cc, int cache, int id, uint64_t oid, uint64_t addr, int shape, int dirty) {
  // LLC is never coherence invalidated
  assert(cache != MESI_CACHE_LLC);
  ocache_t *inv_cache = cc_ocache_get_core_cache(cc, cache, id);
  ocache_op_result_t inv_result;
  ocache_inv(inv_cache, oid, addr, OCACHE_SHAPE_NONE, &inv_result);
  // evict index must from upper level, so it must be not compressed
  assert(inv_result.state == OCACHE_EVICT && inv_result.evict_index == 0);
  // The evicted line must be dirty in the upper level cache
  assert(ocache_entry_is_dirty(&inv_result.evict_entry, 0) == dirty);
  (void)dirty; (void)shape;
  return;
}

// Find caches that need to be invalidated at LLC coherence controller
// Return value indicates whether wb has been performed
// The "need_wb" argument indicates whether dirty wb should be performed
static int cc_ocache_store_helper_LLC_inv(
  cc_ocache_t *cc, int cache, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int shape, int need_wb) {
  MESI_entry_t *MESI_entry = dmap_find_MESI_entry(cc->commons.dmap, oid, addr);
  assert(MESI_entry != NULL);
  if(MESI_entry->states[cache] == MESI_STATE_M) {
    assert(MESI_entry_count_sharer(MESI_entry, cache) == 1);
    if(cache == MESI_CACHE_L1) {
      assert(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L2) == 1);
    }
    int sharer_id = MESI_entry_get_exclusive_sharer(MESI_entry, cache);
    assert(sharer_id != id);
    (*cc_common_get_stat_ptr(&cc->commons, cache, sharer_id, CC_STAT_INV))++;
    if(need_wb == 1) {
      cc_ocache_llc_insert_recursive(cc, sharer_id, cycle, oid, addr, shape, 1);
      MESI_entry->llc_state = MESI_STATE_M;  
    }
    MESI_entry_clear_sharer(MESI_entry, cache, sharer_id);
    assert(MESI_entry_count_sharer(MESI_entry, cache) == 0);
    cc_ocache_store_helper_send_inv(cc, cache, sharer_id, oid, addr, shape, 1);
    MESI_entry->states[cache] = MESI_STATE_I;
    return 1;
  }
  int curr = -1;
  while(1) {
    curr = MESI_entry_get_next_sharer(MESI_entry, cache, curr + 1);
    if(curr == -1) {
      break;
    } else if(curr == id) {
      continue;
    }
    (*cc_common_get_stat_ptr(&cc->commons, cache, curr, CC_STAT_INV))++;
    // TODO: THIS IS WRONG, SHAPE MUST BE NONE
    cc_ocache_store_helper_send_inv(cc, cache, curr, oid, addr, shape, 0);
  }
  MESI_entry_clear_all_sharer(MESI_entry, cache);
  return 0;
}

uint64_t cc_ocache_store(cc_ocache_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr) {
  MESI_entry_t *MESI_entry = dmap_find_MESI_entry(cc->commons.dmap, oid, addr);
  assert(MESI_entry != NULL);
  uint64_t begin_cycle = cycle;
  int end_level = MESI_CACHE_LLC;
  int cache;
  ocache_entry_t *hit_entries[3] = {NULL, NULL, NULL};
  int shape = OCACHE_SHAPE_NONE; // L1/L2 use none shape, LLC must query the pmap
  for(cache = MESI_CACHE_L1;cache <= MESI_CACHE_LLC;cache++) {
    (*cc_common_get_stat_ptr(&cc->commons, cache, id, CC_STAT_LOOKUP))++;
    (*cc_common_get_stat_ptr(&cc->commons, cache, id, CC_STAT_STORE))++;
    cycle += cc->commons.cache_latencies[cache];
    ocache_op_result_t lookup_result;
    if(cache == MESI_CACHE_LLC) {
      pmap_entry_t *pmap_entry = pmap_find(cc->commons.dmap, addr);
      shape = (pmap_entry == NULL) ? cc->default_shape : pmap_entry->shape;
    }
    ocache_t *ocache = cc_ocache_get_core_cache(cc, cache, id);
    ocache_lookup_read(ocache, oid, addr, shape, &lookup_result);
    int MESI_state = MESI_entry->states[cache];
    if(lookup_result.state != OCACHE_MISS) {
      assert(lookup_result.state == OCACHE_HIT_NORMAL || lookup_result.state == OCACHE_HIT_COMPRESSED);
      hit_entries[cache] = lookup_result.hit_entry;
      if(cache == MESI_CACHE_LLC || MESI_state == MESI_STATE_E || MESI_state == MESI_STATE_M) {
        (*cc_common_get_stat_ptr(&cc->commons, cache, id, CC_STAT_STORE_HIT))++;
        end_level = cache;
        if(cache == MESI_CACHE_LLC) {
          uint64_t extra_cycles = ocache->access_hit_cb(ocache, oid, addr, shape);
          cycle += extra_cycles;
        }
        break;
      }
    }
    (*cc_common_get_stat_ptr(&cc->commons, cache, id, CC_STAT_STORE_MISS))++;
  }
  if(cache == MESI_CACHE_LLC) {
    int l1_wb = cc_ocache_store_helper_LLC_inv(cc, MESI_CACHE_L1, id, cycle, oid, addr, shape, 1);
    cc_ocache_store_helper_LLC_inv(cc, MESI_CACHE_L2, id, cycle, oid, addr, shape, l1_wb == 0);
    cache = MESI_CACHE_L2;
  } else if(cache == MESI_CACHE_END) {
    assert(MESI_entry->llc_state == MESI_STATE_I);
    assert(MESI_entry->l1_state == MESI_STATE_I);
    assert(MESI_entry->l2_state == MESI_STATE_I);
    assert(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L1) == 0);
    assert(MESI_entry_count_sharer(MESI_entry, MESI_CACHE_L2) == 0);
    assert(hit_entries[2] == NULL);
    uint64_t old_cycle = cycle;
    cycle = dram_read(cc->commons.dram, cycle, oid, addr);
    cc->commons.dram_read_count++;
    cc->commons.dram_read_cycle += (cycle - old_cycle);
    if(cc->commons.dram_debug_cb != NULL) {
      cc->commons.dram_debug_cb(DRAM_READ, id, cycle, oid, addr, UTIL_CACHE_LINE_SIZE);
    }
    cc_ocache_llc_insert_recursive(cc, id, cycle, oid, addr, shape, 0);
    MESI_entry->llc_state = MESI_STATE_S;
    cache = MESI_CACHE_L2;
  } else {
    assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2);
    if(cache == MESI_CACHE_L1 && MESI_entry->l1_state != MESI_STATE_M) {
      assert(MESI_entry->l1_state == MESI_STATE_E);
      assert(hit_entries[0] != NULL && hit_entries[0]->addr == addr);
      assert(ocache_entry_is_dirty(hit_entries[0], 0) == 0);
      ocache_entry_set_dirty(hit_entries[0], 0);
      MESI_entry->l1_state = MESI_STATE_M;
    }
    cache--; 
  }
  if(cache == MESI_CACHE_L2) {
    if(hit_entries[1] == NULL) {
      cc_ocache_l2_insert_recursive(cc, id, cycle, oid, addr, OCACHE_SHAPE_NONE, 0);
    } else {
      assert(hit_entries[1]->addr == addr);
    }
    MESI_entry->l2_state = MESI_STATE_E;
    MESI_entry_set_sharer(MESI_entry, MESI_CACHE_L2, id);
    cache = MESI_CACHE_L1;
  }
  if(cache == MESI_CACHE_L1) {
    if(hit_entries[0] == NULL) {
      cc_ocache_l1_insert_recursive(cc, id, cycle, oid, addr, OCACHE_SHAPE_NONE, 1);
    } else {
      assert(hit_entries[0]->addr == addr);
      ocache_entry_set_dirty(hit_entries[0], 0);
    }
    MESI_entry->l1_state = MESI_STATE_M;
    MESI_entry_set_sharer(MESI_entry, MESI_CACHE_L1, id);
  }
  cc_common_update_access_cycles(&cc->commons, id, 1, end_level, begin_cycle, cycle);
  return cycle;
}

void cc_ocache_print_set(cc_ocache_t *cc, int cache, int id, int set_index) {
  ocache_t *ocache = cc_ocache_get_core_cache(cc, cache, id);
  ocache_set_print(ocache, set_index);
  return;
}

void cc_ocache_print_MESI_entry(cc_ocache_t *cc, uint64_t oid, uint64_t addr) {
  MESI_entry_t *MESI_entry = dmap_find_MESI_entry(cc->commons.dmap, oid, addr);
  if(MESI_entry == NULL) {
    error_exit("The dmap entry for OID %lu addr %lX cannot be found", oid, addr);
  }
  MESI_entry_print(MESI_entry);
  return;
}

// Insert downwards given a starting level
// This function also inserts shape into page-level pmap of the cc; Note that the pmap may report error if
// shape information for the same page conflict
void cc_ocache_insert_levels(
  cc_ocache_t *cc, int begin, int id, uint64_t oid, uint64_t addr, int shape, const int *state_vec) {
  dmap_t *dmap = cc->commons.dmap;
  MESI_entry_t *MESI_entry = &dmap_insert(dmap, oid, addr)->MESI_entry;
  ocache_t *llc = cc->shared_llc;
  ocache_t *l2 = cc_ocache_get_core_cache(cc, MESI_CACHE_L2, id);
  ocache_t *l1 = cc_ocache_get_core_cache(cc, MESI_CACHE_L1, id);
  ocache_t *caches[3] = {l1, l2, llc};
  ocache_op_result_t insert_result;
  assert(begin >= MESI_CACHE_L1 && begin <= MESI_CACHE_LLC);
  for(int i = begin;i < MESI_CACHE_END;i++) {
    int state = state_vec[i - begin];
    assert(state >= MESI_STATE_BEGIN && state < MESI_STATE_END);
    assert(state != MESI_STATE_I);
    // Shape is ignored for all levels except LLC
    int insert_shape = (i == MESI_CACHE_LLC) ? shape : OCACHE_SHAPE_NONE;
    ocache_insert(caches[i], oid, addr, insert_shape, state == MESI_STATE_M, &insert_result);
    if(insert_result.state == OCACHE_EVICT) {
      error_exit("%s Insert results in eviction\n", MESI_cache_names[i]);
    }
    if(i == MESI_CACHE_L1 || i == MESI_CACHE_L2) {
      if(MESI_entry->states[i] == MESI_STATE_S && state != MESI_STATE_S) {
        MESI_entry_print(MESI_entry);
        error_exit("Inserting exclusive lines while existing state is S\n");
      } else if((MESI_entry->states[i] == MESI_STATE_M || MESI_entry->states[i] == MESI_STATE_E) && \
                state == MESI_STATE_S) {
        MESI_entry_print(MESI_entry);
        error_exit("Inserting S lines while existing state is exclusive\n");
      }
      MESI_entry_set_sharer(MESI_entry, i, id);
    } else {
      assert(i == MESI_CACHE_LLC);
      // LLC state must not be changed
      if(MESI_entry->llc_state != MESI_STATE_I && MESI_entry->llc_state != state) {
        error_exit("Insertion will change LLC state from %s to %s\n",
          MESI_state_names[MESI_entry->llc_state], MESI_state_names[state]);
      } else if(state == MESI_STATE_E) {
        error_exit("E state is not allowed in LLC\n");
      }
    }
    MESI_entry->states[i] = state;
  }
  // Insert page shape information such that the caches can use it
  pmap_insert(cc->commons.dmap, addr, shape);
  return;
}

void cc_ocache_conf_print(cc_ocache_t *cc) {
  printf("---------- cc_ocache conf ----------\n");
  printf("Cores %d\n", cc->commons.core_count);
  for(int i = MESI_CACHE_BEGIN;i < MESI_CACHE_END;i++) {
    printf("[%s] size %d ways %d latency %d\n", 
      MESI_cache_names[i], cc->commons.cache_sizes[i], cc->commons.cache_way_counts[i], 
      cc->commons.cache_latencies[i]);
  }
  // LLC exclusive property
  printf("Default shape: \"%s\"\n", ocache_shape_names[cc->default_shape]);
  if(cc->commons.dmap == NULL) {
    printf("dmap is not initialized\n");
  }
  if(cc->commons.dram == NULL) {
    printf("DRAM is not initialized\n");
  }
  ocache_conf_print(cc->shared_llc);
  return;
} 

void cc_ocache_stat_print(cc_ocache_t *cc) {
  printf("---------- cc_ocache stat ----------\n");
  cc_common_print_stats(&cc->commons);
  ocache_stat_print(cc->shared_llc);
  return;
}

//* cc_simple_t

// This function requires the following conf keys:
//   cc.simple.core_count
//   cc.simple.l1.size cc.simple.l1.way_count cc.simple.l1.latency
//   cc.simple.l2.size cc.simple.l2.way_count cc.simple.l2.latency
//   cc.simple.llc.size cc.simple.llc.physical_way_count cc.simple.llc.latency cc.simple.llc.algorithm
//   cc.simple.llc.bcache.line_count cc.simple.llc.bcache.way_count 
//   cc.simple.llc.insert_type ("seg", "sb"), if not specified then just sb
//   cc.simple.default_shape (4_1, 1_4, 2_2, None)
cc_simple_t *cc_simple_init_conf(conf_t *conf) {
  cc_simple_t *cc = cc_simple_init();
  cc->commons.conf = conf;
  // Core count
  int core_count = conf_find_int32_range(conf, "cc.simple.core_count", 1, CONF_INT32_MAX, CONF_RANGE);
  if(core_count != 1) {
    error_exit("cc.simple only supports a single core\n");
  }
  cc->commons.core_count = core_count;
  cc_common_init_stats(&cc->commons, core_count);
  cc->cores = (cc_simple_core_t *)malloc(sizeof(cc_simple_core_t) * core_count);
  SYSEXPECT(cc->cores != NULL);
  memset(cc->cores, 0x00, sizeof(cc_simple_core_t));
  // L1
  int l1_size = conf_find_int32_range(conf, "cc.simple.l1.size", (int)UTIL_CACHE_LINE_SIZE, CONF_INT32_MAX, CONF_RANGE);
  int l1_way_count = conf_find_int32_range(conf, "cc.simple.l1.way_count", 1, CONF_INT32_MAX, CONF_RANGE);
  int l1_latency = conf_find_int32_range(conf, "cc.simple.l1.latency", 0, CONF_INT32_MAX, CONF_RANGE);
  cc->commons.l1_latency = l1_latency;
  cc->commons.l1_size = l1_size;
  cc->commons.l1_way_count = l1_way_count;
  // Same tag and data ways
  cc->cores[0].l1 = ocache_init(l1_size, l1_way_count, l1_way_count); 
  // L2
  int l2_size = conf_find_int32_range(conf, "cc.simple.l2.size", (int)UTIL_CACHE_LINE_SIZE, CONF_INT32_MAX, CONF_RANGE);
  int l2_way_count = conf_find_int32_range(conf, "cc.simple.l2.way_count", 1, CONF_INT32_MAX, CONF_RANGE);
  int l2_latency = conf_find_int32_range(conf, "cc.simple.l2.latency", 0, CONF_INT32_MAX, CONF_RANGE);
  cc->commons.l2_latency = l2_latency;
  cc->commons.l2_size = l2_size;
  cc->commons.l2_way_count = l2_way_count;
  // Same tag and data ways
  cc->cores[0].l2 = ocache_init(l2_size, l2_way_count, l2_way_count);
  // LLC
  int llc_size = \
    conf_find_int32_range(conf, "cc.simple.llc.size", (int)UTIL_CACHE_LINE_SIZE, CONF_INT32_MAX, CONF_RANGE);
  int llc_physical_way_count = \
    conf_find_int32_range(conf, "cc.simple.llc.physical_way_count", 1, CONF_INT32_MAX, CONF_RANGE);
  int llc_tag_way_count = llc_physical_way_count;
  int llc_tag_way_count_found = \
    conf_find_int32(conf, "cc.simple.llc.tag_way_count", &llc_tag_way_count);
  if(llc_tag_way_count_found == 0) {
    llc_tag_way_count = llc_physical_way_count;
  }
  int llc_latency = \
    conf_find_int32_range(conf, "cc.simple.llc.latency", 0, CONF_INT32_MAX, CONF_RANGE);
  const char *llc_algorithm = conf_find_str_mandatory(conf, "cc.simple.llc.algorithm");
  cc->commons.llc_latency = llc_latency;
  cc->commons.llc_size = llc_size;
  cc->commons.llc_physical_way_count = llc_physical_way_count;
  cc->cores[0].llc = ocache_init(llc_size, llc_physical_way_count, llc_tag_way_count);
  cc->shared_llc = cc->cores[0].llc;
  ocache_set_level(cc->shared_llc, MESI_CACHE_LLC);
  ocache_set_name(cc->shared_llc, "Shared LLC (cc_simple)");
  ocache_set_compression_type(cc->shared_llc, llc_algorithm);
  // LLC's MBD optimization type, optional, default to None
  char *MBD_opt_type_str = NULL;
  int MBD_opt_type_found = conf_find_str(conf, "cc.simple.llc.MBD_opt_type", &MBD_opt_type_str);
  if(MBD_opt_type_found == 1) {
    assert(MBD_opt_type_str != NULL);
    ocache_set_MBD_opt_type(cc->shared_llc, MBD_opt_type_str);
  } else {
    ocache_set_MBD_opt_type(cc->shared_llc, "None");
  }
  // LLC evict optimization type, optional, default to None
  char *evict_opt_type_str = NULL;
  int evict_opt_type_found = conf_find_str(conf, "cc.simple.llc.evict_opt_type", &evict_opt_type_str);
  if(evict_opt_type_found == 1) {
    assert(evict_opt_type_str != NULL);
    ocache_set_evict_opt_type(cc->shared_llc, evict_opt_type_str);
  } else {
    ocache_set_evict_opt_type(cc->shared_llc, "None");
  }
  // insert type, optional
  char *insert_type_str;
  int insert_type_found = conf_find_str(conf, "cc.simple.llc.insert_type", &insert_type_str);
  if(insert_type_found == 1) {
    ocache_set_insert_type(cc->shared_llc, insert_type_str);
  }
  // bcache, if given
  int bcache_line_count, bcache_way_count;
  char *bcache_shape_str;
  int bcache_ret = 0;
  bcache_ret += conf_find_int32(conf, "cc.simple.llc.bcache.line_count", &bcache_line_count);
  bcache_ret += conf_find_int32(conf, "cc.simple.llc.bcache.way_count", &bcache_way_count);
  bcache_ret += conf_find_str(conf, "cc.simple.llc.bcache.shape", &bcache_shape_str);
  if(bcache_ret == 3) {
    ocache_init_bcache(cc->shared_llc, bcache_line_count, bcache_way_count, bcache_shape_str);
  } else if(bcache_ret > 0) {
    printf("bcache will not be initialized because some essential configurations are missing\n");
  }
  // Default shape
  const char *default_shape = conf_find_str_mandatory(conf, "cc.simple.default_shape");
  assert(default_shape != NULL);
  if(streq(default_shape, "None") == 1) {
    cc->default_shape = OCACHE_SHAPE_NONE;
  } else if(streq(default_shape, "4_1") == 1) {
    cc->default_shape = OCACHE_SHAPE_4_1;
  } else if(streq(default_shape, "1_4") == 1) {
    cc->default_shape = OCACHE_SHAPE_1_4;
  } else if(streq(default_shape, "2_2") == 1) {
    cc->default_shape = OCACHE_SHAPE_2_2;
  } else {
    error_exit("Unknown default_shape: \"%s\"\n", default_shape);
  }
  return cc;
}

cc_simple_t *cc_simple_init() {
  cc_simple_t *cc = (cc_simple_t *)malloc(sizeof(cc_simple_t));
  SYSEXPECT(cc != NULL);
  memset(cc, 0x00, sizeof(cc_simple_t));
  return cc;
}

void cc_simple_free(cc_simple_t *cc) {
  cc_common_free_stats(&cc->commons);
  ocache_free(cc->cores[0].l1);
  ocache_free(cc->cores[0].l2);
  ocache_free(cc->cores[0].llc);
  free(cc->cores);
  free(cc);
  return;
}

// Sets the dmap for compression
void cc_simple_set_dmap(cc_simple_t *cc, dmap_t *dmap) {
  if(cc->commons.dmap != NULL) {
    error_exit("dmap has already been set\n");
  }
  cc->commons.dmap = dmap;
  // This enables ocache compression
  ocache_set_dmap(cc->shared_llc, dmap);
  assert(ocache_is_use_shape(cc->shared_llc) == 1);
  return;
}

// Set the DRAM object, which will be used for DRAM call back. This is optional if the call back is
// overridden (not the default one)
void cc_simple_set_dram(cc_simple_t *cc, dram_t *dram) {
  if(cc->commons.dram != NULL) {
    error_exit("The DRAM object has already been set in the cc_simple_t\n");
  }
  cc->commons.dram = dram;
  return;
}

void cc_simple_clear_dmap(cc_simple_t *cc) {
  if(cc->commons.dmap == NULL) {
    error_exit("The dmap has not been set\n");
  }
  cc->commons.dmap = NULL;
  return;
}

void cc_simple_clear_dram(cc_simple_t *cc) {
  if(cc->commons.dram == NULL) {
    error_exit("The DRAM has not been set\n");
  }
  cc->commons.dram = NULL;
  return;
}

uint64_t cc_simple_llc_insert_recursive(
  cc_simple_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int shape, int dirty) {
  assert(id == 0); (void)id;
  ocache_op_result_t insert_result;
  ocache_insert(cc->shared_llc, oid, addr, shape, dirty, &insert_result);
  (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_LLC, 0, CC_STAT_INSERT))++;
  if(insert_result.state == OCACHE_EVICT) {
    for(int entry_index = 0;entry_index < insert_result.insert_evict_count;entry_index++) {
      ocache_entry_t *evict_entry = insert_result.insert_evict_entries + entry_index;
      for(int i = 0;i < 4;i++) {
        if(ocache_entry_is_valid(evict_entry, i) == 0) {
          continue;
        }
        uint64_t evict_oid, evict_addr;
        ocache_gen_addr_in_sb(cc->shared_llc, evict_entry->oid, evict_entry->addr, 
          i, evict_entry->shape, &evict_oid, &evict_addr);
        // We only update it when optional dmap is present (for MBD)
        dmap_entry_t *dmap_entry = dmap_find(cc->shared_llc->dmap, evict_oid, evict_addr);
        if(dmap_entry != NULL) {
          dmap_entry->MESI_entry.llc_state = MESI_STATE_I;
        }
        // Whether LLC version is dirty
        int evict_dirty = ocache_entry_is_dirty(evict_entry, i);
        int evict_size = ocache_entry_get_size(evict_entry, i);
        // Whether invalidated version is dirty
        int inv_dirty = 0;
        // Invalidate the line from L1 and L2
        ocache_op_result_t inv_result;
        ocache_inv(cc_simple_get_core_cache(cc, MESI_CACHE_L1, 0), evict_oid, evict_addr, OCACHE_SHAPE_NONE, &inv_result);
        if(inv_result.state == OCACHE_EVICT) {
          assert(inv_result.evict_index == 0);
          inv_dirty |= ocache_entry_is_dirty(&inv_result.evict_entry, 0);
        }
        ocache_inv(cc_simple_get_core_cache(cc, MESI_CACHE_L2, 0), evict_oid, evict_addr, OCACHE_SHAPE_NONE, &inv_result);
        if(inv_result.state == OCACHE_EVICT) {
          assert(inv_result.evict_index == 0);
          inv_dirty |= ocache_entry_is_dirty(&inv_result.evict_entry, 0);
        }
        if(evict_dirty == 1 || inv_dirty == 1) {
          (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_LLC, 0, CC_STAT_EVICT))++;
          int wb_size = (inv_dirty == 1) ? UTIL_CACHE_LINE_SIZE : evict_size;
          dram_write(cc->commons.dram, cycle, evict_oid, evict_addr, wb_size);
          if(cc->commons.dram_debug_cb != NULL) {
            cc->commons.dram_debug_cb(DRAM_WRITE, 0, cycle, evict_oid, evict_addr, wb_size);
          }
          // Update DRAM write stats
          cc->commons.dram_write_count++;
          cc->commons.dram_write_bytes += wb_size;
        }
      }
    }
  }
  return cycle;
}

uint64_t cc_simple_l2_insert_recursive(
  cc_simple_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int shape, int dirty) {
  assert(id == 0 && shape == OCACHE_SHAPE_NONE);
  (void)shape; (void)id;
  ocache_op_result_t insert_result;
  ocache_insert(cc_simple_get_core_cache(cc, MESI_CACHE_L2, 0), oid, addr, OCACHE_SHAPE_NONE, dirty, &insert_result);
  (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L2, 0, CC_STAT_INSERT))++;
  if(insert_result.state == OCACHE_EVICT) {
    assert(insert_result.insert_evict_count == 1);
    assert(ocache_entry_is_valid(&insert_result.insert_evict_entries[0], 0) == 1);
    uint64_t evict_oid = insert_result.insert_evict_entries[0].oid;
    uint64_t evict_addr = insert_result.insert_evict_entries[0].addr;
    pmap_t *pmap = cc->commons.dmap;
    pmap_entry_t *pmap_entry = pmap_find(pmap, addr);
    int evict_shape = (pmap_entry == NULL) ? cc->default_shape : pmap_entry->shape;
    int evict_dirty = ocache_entry_is_dirty(&insert_result.insert_evict_entries[0], 0);
    // Invalidate the line from L1 also
    ocache_op_result_t inv_result;
    ocache_inv(cc_simple_get_core_cache(cc, MESI_CACHE_L1, 0), evict_oid, evict_addr, OCACHE_SHAPE_NONE, &inv_result);
    if(inv_result.state == OCACHE_EVICT) {
      assert(inv_result.evict_index == 0);
      if(ocache_entry_is_dirty(&inv_result.evict_entry, 0) == 1) {
        evict_dirty = 1;
      }
    }
    if(evict_dirty == 1) {
      cc_simple_llc_insert_recursive(cc, id, cycle, evict_oid, evict_addr, evict_shape, 1);
      (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L2, 0, CC_STAT_EVICT))++;
    } else {
      ocache_op_result_t lookup_result;
      _ocache_lookup(cc_simple_get_core_cache(cc, MESI_CACHE_LLC, 0),
        evict_oid, evict_addr, evict_shape, 1, 0, &lookup_result);
    }
  }
  return cycle;
}

uint64_t cc_simple_l1_insert_recursive(
  cc_simple_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int shape, int dirty) {
  assert(id == 0 && shape == OCACHE_SHAPE_NONE);
  (void)shape; (void)id;
  ocache_op_result_t insert_result;
  ocache_insert(cc_simple_get_core_cache(cc, MESI_CACHE_L1, 0), oid, addr, OCACHE_SHAPE_NONE, dirty, &insert_result);
  (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L1, 0, CC_STAT_INSERT))++;
  if(insert_result.state == OCACHE_EVICT) {
    assert(insert_result.insert_evict_count == 1);
    assert(ocache_entry_is_valid(&insert_result.insert_evict_entries[0], 0) == 1);
    uint64_t evict_oid = insert_result.insert_evict_entries[0].oid;
    uint64_t evict_addr = insert_result.insert_evict_entries[0].addr;
    if(ocache_entry_is_dirty(&insert_result.insert_evict_entries[0], 0) == 1) {
      cc_simple_l2_insert_recursive(cc, id, cycle, evict_oid, evict_addr, OCACHE_SHAPE_NONE, 1);
      (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L1, 0, CC_STAT_EVICT))++;
    } else {
      // Just touch it in the L2 to update LRU
      ocache_op_result_t lookup_result;
      _ocache_lookup(cc_simple_get_core_cache(cc, MESI_CACHE_L2, 0),
        evict_oid, evict_addr, OCACHE_SHAPE_NONE, 1, 0, &lookup_result);
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
uint64_t cc_simple_access(cc_simple_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int is_write) {
  assert(id == 0); // Only support single core
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0);
  uint64_t begin_cycle = cycle;
  int end_level = MESI_CACHE_LLC;
  // Use this as the shape for LLC
  int shape = -1;
  int hit_level = MESI_CACHE_END; // This means hit main memory
  for(int level = MESI_CACHE_L1;level <= MESI_CACHE_LLC;level++) {
    // Update stat on reads and writes
    (*cc_common_get_stat_ptr(&cc->commons, level, 0, is_write ? CC_STAT_STORE : CC_STAT_LOAD))++;
    // The latency is always paid as long as the cache is accessed
    cycle += cc->commons.cache_latencies[level];
    ocache_op_result_t lookup_result;
    ocache_t *ocache = cc_simple_get_core_cache(cc, level, 0);
    if(level != MESI_CACHE_LLC) {
      ocache_lookup_read_no_shape(ocache, oid, addr, &lookup_result);
    } else {
      // Only read pmap and get shape when we query LLC
      pmap_entry_t *pmap_entry = pmap_find(cc->commons.dmap, addr);
      //assert(pmap_entry != NULL);
      // If shape is defined, then use it. Otherwise use default shape
      shape = (pmap_entry != NULL) ? pmap_entry->shape : cc->default_shape;
      ocache_lookup_read(ocache, oid, addr, shape, &lookup_result);
      // Update stat for ocache object (for MBD)
      if(lookup_result.state != OCACHE_MISS) {
        uint64_t extra_cycles = ocache->access_hit_cb(ocache, oid, addr, shape);
        cycle += extra_cycles;
      }
    }
    // Note that the state can either be HIT NORMAL or HIT COMPRESSED
    if(lookup_result.state != OCACHE_MISS) {
      assert(lookup_result.state == OCACHE_HIT_NORMAL || 
             lookup_result.state == OCACHE_HIT_COMPRESSED);
      // Update stat, either hits L1 or lower
      (*cc_common_get_stat_ptr(&cc->commons, level, 0, is_write ? CC_STAT_STORE_HIT : CC_STAT_LOAD_HIT))++;
      // Special case: if hit L1 then just mark dirty, if the dirty argument is dirty
      // Also we can return early if hit L1 since there is no insertion
      if(level == MESI_CACHE_L1) {
        assert(lookup_result.hit_entry != NULL);
        if(is_write == 1) {
          ocache_entry_set_dirty(lookup_result.hit_entry, 0);
        }
        end_level = MESI_CACHE_L1;
        cc_common_update_access_cycles(&cc->commons, id, is_write, end_level, begin_cycle, cycle);
        return cycle;
      }
      // If hit lower level or miss hierarchy, update the level, and exit loop
      hit_level = level;
      end_level = level; // This is used to update stat
      break;
    } else {
      // Miss current level, update stat
      (*cc_common_get_stat_ptr(&cc->commons, level, 0, is_write ? CC_STAT_STORE_MISS : CC_STAT_LOAD_MISS))++;
    }
  }
  // Read main memory, if the cache hierarchy misses
  if(hit_level == MESI_CACHE_END) {
    //if(is_write == 1) {
    //  printf("LLC write miss oid %lu addr %lu\n", oid, addr);
    //}
    // Must have already tried LLC, so shape must have been set
    assert(shape != -1);
    uint64_t prev_cycle = cycle;
    cycle = dram_read(cc->commons.dram, cycle, oid, addr);
    // Update stats
    cc->commons.dram_read_count++;
    cc->commons.dram_read_cycle += (cycle - prev_cycle);
    if(cc->commons.dram_debug_cb != NULL) {
      cc->commons.dram_debug_cb(DRAM_READ, 0, cycle, oid, addr, UTIL_CACHE_LINE_SIZE);
    }
    // We only update it when optional dmap is present (for MBD)
    dmap_entry_t *dmap_entry = dmap_find(cc->shared_llc->dmap, oid, addr);
    if(dmap_entry != NULL) {
      dmap_entry->MESI_entry.llc_state = MESI_STATE_S;
    }
  }
  hit_level--;
  assert(hit_level >= MESI_CACHE_L1 && hit_level <= MESI_CACHE_LLC);
  if(hit_level == MESI_CACHE_LLC) {
    cc_simple_llc_insert_recursive(cc, 0, cycle, oid, addr, shape, 0);
    hit_level--;
  }
  if(hit_level == MESI_CACHE_L2) {
    cc_simple_l2_insert_recursive(cc, 0, cycle, oid, addr, OCACHE_SHAPE_NONE, 0);
    hit_level--;
  }
  if(hit_level == MESI_CACHE_L1) {
    cc_simple_l1_insert_recursive(cc, 0, cycle, oid, addr, OCACHE_SHAPE_NONE, is_write); // If write then insert dirty
  }
  // Update access latency
  cc_common_update_access_cycles(&cc->commons, id, is_write, end_level, begin_cycle, cycle);
  return cycle;
}

uint64_t cc_simple_load(cc_simple_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr) {
  assert(id == 0);
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0);
  // Insert with dirty bit set to 0
  cycle = cc_simple_access(cc, id, cycle, oid, addr, 0);
  return cycle;
}

// Simulate store. For single core store, it uses the same procedure as load, except that the 
// cache line is marked as dirty.
// No coherence action is required, since all lines have exclusive permission
// Note:
//   (1) dmap is not updated by this function. It always assume that pmap and dmap entries have been inserted
//   (2) Logical line in the dmap should be updated after this function returns since write back is processed 
//       using dmap data
uint64_t cc_simple_store(cc_simple_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr) {
  assert(id == 0);
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0);
  // Insert with dirty bit set to 1, or if hit, set dirty bit to 1
  cycle = cc_simple_access(cc, id, cycle, oid, addr, 1);
  return cycle;
}

int cc_simple_is_line_invalid(cc_simple_t *cc, int cache, int id, uint64_t oid, uint64_t addr, int shape) {
  assert(id == 0);
  ocache_op_result_t result;
  _ocache_lookup(cc_simple_get_core_cache(cc, cache, id), oid, addr, shape, 0, 0, &result);
  return result.state == OCACHE_MISS;
}

int cc_simple_is_line_dirty(cc_simple_t *cc, int cache, int id, uint64_t oid, uint64_t addr, int shape) {
  assert(id == 0);
  ocache_op_result_t result;
  _ocache_lookup(cc_simple_get_core_cache(cc, cache, id), oid, addr, shape, 0, 0, &result);
  if(result.state == OCACHE_MISS) {
    error_exit("The line oid %lu (0x%lX) addr %lu (%lX) is not in the cache\n", oid, oid, addr, addr);
  }
  assert(result.hit_entry != NULL);
  return ocache_entry_is_dirty(result.hit_entry, result.hit_index);
}

int cc_simple_get_line_shape(cc_simple_t *cc, int cache, int id, uint64_t oid, uint64_t addr, int shape) {
  assert(id == 0);
  ocache_op_result_t result;
  _ocache_lookup(cc_simple_get_core_cache(cc, cache, id), oid, addr, shape, 0, 0, &result);
  if(result.state == OCACHE_MISS) {
    error_exit("The line oid %lu (0x%lX) addr %lu (%lX) is not in the cache\n", oid, oid, addr, addr);
  }
  assert(result.hit_entry != NULL);
  return result.hit_entry->shape;
}

void cc_simple_touch(cc_simple_t *cc, int cache, int id, uint64_t oid, uint64_t addr, int shape) {
  assert(id == 0);
  ocache_op_result_t result;
  // Update LRU
  _ocache_lookup(cc_simple_get_core_cache(cc, cache, id), oid, addr, shape, 1, 0, &result);
  assert(result.state != OCACHE_MISS);
  return;
}

// State_vec is only used to determine whether the line is clean or dirty; S and E state lines are treated equally
// This function also inserts the shape, and reports error if shapes disagree
void cc_simple_insert_levels(
  cc_simple_t *cc, int begin, int id, uint64_t oid, uint64_t addr, int shape, const int *state_vec) {
  assert(id == 0 && begin >= MESI_CACHE_L1 && begin <= MESI_CACHE_LLC);
  (void)id;
  while(begin <= MESI_CACHE_LLC) {
    int state = *state_vec;
    assert(state != MESI_STATE_I);
    int dirty = (state == MESI_STATE_M);
    // This function will report error if shapes disagree
    pmap_insert(cc->commons.dmap, addr, shape);
    int insert_shape = (begin == MESI_CACHE_LLC) ? shape : OCACHE_SHAPE_NONE;
    ocache_op_result_t insert_result;
    ocache_insert(cc_simple_get_core_cache(cc, begin, 0), oid, addr, insert_shape, dirty, &insert_result);
    if(insert_result.state == OCACHE_EVICT) {
      error_exit("Insertion of line oid %lu (0x%lX) addr %lu (0x%lX) results in eviction\n",
        oid, oid, addr, addr);
    }
    begin++;
    state_vec++;
  }
  return;
}

void cc_simple_print_set(cc_simple_t *cc, int cache, int id, int set_index) {
  assert(id == 0);
  ocache_set_print(cc_simple_get_core_cache(cc, cache, id), set_index);
  return;
}

void cc_simple_conf_print(cc_simple_t *cc) {
  printf("---------- cc_simple_t conf ----------\n");
  printf("Cores %d\n", cc->commons.core_count);
  for(int i = MESI_CACHE_BEGIN;i < MESI_CACHE_END;i++) {
    printf("[%s] size %d ways %d latency %d\n", 
      MESI_cache_names[i], cc->commons.cache_sizes[i], cc->commons.cache_way_counts[i], 
      cc->commons.cache_latencies[i]);
  }
  // LLC exclusive property
  printf("Default shape: \"%s\"\n", ocache_shape_names[cc->default_shape]);
  if(cc->commons.dmap == NULL) {
    printf("dmap is not initialized\n");
  }
  if(cc->commons.dram == NULL) {
    printf("DRAM is not initialized\n");
  }
  ocache_conf_print(cc->shared_llc);
  return;
}

// Print coherence-level stat and ocache stat
void cc_simple_stat_print(cc_simple_t *cc) {
  printf("---------- cc_simple_t stat ----------\n");
  cc_common_print_stats(&cc->commons);
  // Also print ocache stat
  ocache_stat_print(cc->shared_llc); 
  return;
}

//* cc_debug_t

cc_debug_t *cc_debug_init() {
  cc_debug_t *cc = (cc_debug_t *)malloc(sizeof(cc_debug_t));
  SYSEXPECT(cc != NULL);
  memset(cc, 0x00, sizeof(cc_debug_t));
  return cc; 
}

// cc.debug.core_count
// cc.debug.mode = {"fix_latency"}
// cc.debug.read_latency/write_latency, if fix latency mode
cc_debug_t *cc_debug_init_conf(conf_t *conf) {
  cc_debug_t *cc = cc_debug_init();
  cc->commons.conf = conf;
  // Core count
  int core_count = conf_find_int32_range(conf, "cc.debug.core_count", 1, CONF_INT32_MAX, CONF_RANGE);
  cc->commons.core_count = core_count;
  cc_common_init_stats(&cc->commons, core_count);
  // Read mode string
  char *mode_str = conf_find_str_mandatory(conf, "cc.debug.mode");
  if(streq(mode_str, "fix_latency") == 1) {
    cc->mode = CC_DEBUG_MODE_FIX_LATENCY;
    cc->load_latency = conf_find_int32_range(conf, "cc.debug.load_latency", 0, CONF_INT32_MAX, CONF_RANGE);
    cc->store_latency = conf_find_int32_range(conf, "cc.debug.store_latency", 0, CONF_INT32_MAX, CONF_RANGE);
  } else {
    error_exit("Unknown mode for cc_debug: \"%s\"\n", mode_str);
  }
  return cc;
}

void cc_debug_free(cc_debug_t *cc) {
  cc_common_free_stats(&cc->commons);
  free(cc);
  return;
}

// Do not check since this is used for debugging
void cc_debug_set_dmap(cc_debug_t *cc, dmap_t *dmap) {
  cc->commons.dmap = dmap;
  return;
}

void cc_debug_set_dram(cc_debug_t *cc, dram_t *dram) {
  cc->commons.dram = dram;
  return;
}

void cc_debug_set_dram_debug_cb(cc_debug_t *cc, cc_dram_debug_cb_t cb) {
  cc->commons.dram_debug_cb = cb;
  return;
}

uint64_t cc_debug_load(cc_debug_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr) {
  if(cc->mode == CC_DEBUG_MODE_FIX_LATENCY) {
    cycle += cc->load_latency;
  }
  (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L1, id, CC_STAT_LOAD))++;
  (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L1, id, CC_STAT_LOAD_HIT))++;
  (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L1, id, CC_STAT_LOAD_CYCLE)) += cc->load_latency;
  (void)id; (void)oid; (void)addr;
  return cycle;
}

uint64_t cc_debug_store(cc_debug_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr) {
  if(cc->mode == CC_DEBUG_MODE_FIX_LATENCY) {
    cycle += cc->store_latency;
  }
  (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L1, id, CC_STAT_STORE))++;
  (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L1, id, CC_STAT_STORE_HIT))++;
  (*cc_common_get_stat_ptr(&cc->commons, MESI_CACHE_L1, id, CC_STAT_STORE_CYCLE)) += cc->store_latency;
  (void)id; (void)oid; (void)addr;
  return cycle;
}

void cc_debug_conf_print(cc_debug_t *cc) {
  printf("---------- cc_debug_t ----------\n");
  printf("Mode: ");
  if(cc->mode == CC_DEBUG_MODE_FIX_LATENCY) {
    printf("fix_latency\n");
    printf("Latency load %d store %d\n", cc->load_latency, cc->store_latency);
  }
  return;
}

void cc_debug_stat_print(cc_debug_t *cc) {
  printf("Mode: ");
  if(cc->mode == CC_DEBUG_MODE_FIX_LATENCY) {
    printf("fix_latency\n");
    cc_common_print_stats(&cc->commons);
  }
  return;
}

//* oc_t

const char *oc_cc_type_names[4] = {"CC_SIMPLE", "CC_SCACHE", "CC_OCACHE", "CC_DEBUG"};

// This function initializes the coherence controller. It sets oc->cc_commons and load/store call back
// functions based on types
// Conf keys: oc.cc_type values {"scache", "ocache", "simple", "debug"}
void oc_init_cc(oc_t *oc, conf_t *conf) {
  const char *cc_type_str = conf_find_str_mandatory(conf, "oc.cc_type");
  assert(cc_type_str != NULL);
  assert(oc->dmap != NULL);
  assert(oc->dram != NULL);
  // Init cc objects using the conf file
  if(streq(cc_type_str, "simple") == 1) {
    oc->cc_type = OC_CC_SIMPLE;
    oc->cc_simple = cc_simple_init_conf(conf);
    oc->cc_commons = &oc->cc_simple->commons;
    oc->load_cb = oc_simple_load;
    oc->store_cb = oc_simple_store;
    cc_simple_set_dram(oc->cc_simple, oc->dram);
    cc_simple_set_dmap(oc->cc_simple, oc->dmap);
  } else if(streq(cc_type_str, "scache") == 1) {
    oc->cc_type = OC_CC_SCACHE;
    oc->cc_scache = cc_scache_init_conf(conf);
    oc->cc_commons = &oc->cc_scache->commons;
    oc->load_cb = oc_scache_load;
    oc->store_cb = oc_scache_store;
    cc_scache_set_dram(oc->cc_scache, oc->dram);
    cc_scache_set_dmap(oc->cc_scache, oc->dmap);
  } else if(streq(cc_type_str, "ocache") == 1) {
    oc->cc_type = OC_CC_OCACHE;
    oc->cc_ocache = cc_ocache_init_conf(conf);
    oc->cc_commons = &oc->cc_ocache->commons;
    oc->load_cb = oc_ocache_load;
    oc->store_cb = oc_ocache_store;
    cc_ocache_set_dram(oc->cc_ocache, oc->dram);
    cc_ocache_set_dmap(oc->cc_ocache, oc->dmap);
  } else if(streq(cc_type_str, "debug") == 1) {
    oc->cc_type = OC_CC_DEBUG;
    oc->cc_debug = cc_debug_init_conf(conf);
    oc->cc_commons = &oc->cc_debug->commons;
    oc->load_cb = oc_debug_load;
    oc->store_cb = oc_debug_store;
    cc_debug_set_dram(oc->cc_debug, oc->dram);
    cc_debug_set_dmap(oc->cc_debug, oc->dmap);
  } else {
    error_exit("Unknown cc type: \"%s\"\n", cc_type_str);
  }
  return;
}

// Keys:
//   dram.bank_count
//   dram.read_latency
//   dram.write_latency
//   ---
//   oc.cc_type
//   ---
//   All arguments for initializing the corresponding cache object
oc_t *oc_init(conf_t *conf) {
  oc_t *oc = (oc_t *)malloc(sizeof(oc_t));
  SYSEXPECT(oc != NULL);
  memset(oc, 0x00, sizeof(oc_t));
  oc->dmap = dmap_init();
  oc->dram = dram_init(conf); // Read bank count, read and write latency from the conf
  // Initialize cc scache/ocache object
  oc_init_cc(oc, conf);
  return oc; 
}

void oc_free(oc_t *oc) {
  dmap_free(oc->dmap);
  dram_free(oc->dram);
  assert(oc->cc_type >= OC_CC_BEGIN && oc->cc_type < OC_CC_END);
  if(oc->cc_type == OC_CC_SIMPLE) {
    cc_simple_free(oc->cc_simple);
  } else if(oc->cc_type == OC_CC_SCACHE) {
    cc_scache_free(oc->cc_scache);
  } else if(oc->cc_type == OC_CC_OCACHE) {
    cc_ocache_free(oc->cc_ocache);
  } else if(oc->cc_type == OC_CC_DEBUG) {
    cc_debug_free(oc->cc_debug);
  }
  free(oc);
  return;
}

uint64_t oc_simple_load(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr) {
  return cc_simple_load(oc->cc_simple, id, cycle, oid, addr);
}

uint64_t oc_simple_store(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr) {
  return cc_simple_store(oc->cc_simple, id, cycle, oid, addr);
}

uint64_t oc_scache_load(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr) {
  (void)oid;
  return cc_scache_load(oc->cc_scache, id, cycle, addr);
}

uint64_t oc_scache_store(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr) {
  (void)oid;
  return cc_scache_store(oc->cc_scache, id, cycle, addr);
}

uint64_t oc_ocache_load(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr) {
  return cc_ocache_load(oc->cc_ocache, id, cycle, oid, addr);
}

uint64_t oc_ocache_store(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr) {
  return cc_ocache_store(oc->cc_ocache, id, cycle, oid, addr);
}

uint64_t oc_debug_load(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr) {
  return cc_debug_load(oc->cc_debug, id, cycle, oid, addr);
}

uint64_t oc_debug_store(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr) {
  return cc_debug_store(oc->cc_debug, id, cycle, oid, addr);
}

// The following two assume that the upper level module will perform data operation on dmap correctly

// It is assumed that dmap is populated with all requested lines and with the current memory image 
// before this operation
uint64_t oc_load(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t aligned_addr, int line_count) {
  assert((aligned_addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0UL);
  // Update stats
  oc->cross_line_access_count += (line_count > 1);
  // Most accesses will not cross cache line boundary, so this has minimum impact on accuracy
  uint64_t max_cycle = 0UL;
  for(int i = 0;i < line_count;i++) {
    // Create data entry if not already exist - this is required for loading data into LLC
    dmap_insert(oc->dmap, oid, aligned_addr);
    cycle = oc->load_cb(oc, id, cycle, oid, aligned_addr);
    // Update max cycle
    if(cycle > max_cycle) {
      max_cycle = cycle;
    }
    aligned_addr += UTIL_CACHE_LINE_SIZE;
  }
  return max_cycle;
}

// Same assumption as load; Also it assumes that the modifications will be applied after store coherence takes place
uint64_t oc_store(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t aligned_addr, int line_count) {
  assert((aligned_addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0UL);
  oc->cross_line_access_count += (line_count > 1);
  uint64_t max_cycle = 0UL;
  for(int i = 0;i < line_count;i++) {
    dmap_insert(oc->dmap, oid, aligned_addr);
    cycle = oc->store_cb(oc, id, cycle, oid, aligned_addr);
    if(cycle > max_cycle) {
      max_cycle = cycle;
    }
    aligned_addr += UTIL_CACHE_LINE_SIZE;
  }  
  return cycle;
}

void oc_append_stat_snapshot(oc_t *oc) {
  switch(oc->cc_type) {
    case OC_CC_SIMPLE: ocache_append_stat_snapshot(oc->cc_simple->shared_llc); break;
    case OC_CC_SCACHE: scache_append_stat_snapshot(oc->cc_scache->shared_llc); break;
    case OC_CC_OCACHE: ocache_append_stat_snapshot(oc->cc_ocache->shared_llc); break;
    case OC_CC_DEBUG: break;
    default: assert(0); break;
  }
  dram_append_stat_snapshot(oc->dram);
  return;
}

// Note that this function does not take file name since the name is generated within
// This function takes a prefix which will be prepended to the generated file name. The prefix can be
// a folder or fire name. If NULL then ignored
void oc_save_stat_snapshot(oc_t *oc, char *prefix) {
  // Generate a file name for dumping
  char filename[1024];
  memset(filename, 0x00, sizeof(filename));
  if(prefix != NULL) {
    strcpy(filename, prefix);
  }
  int prefix_len = strlen(prefix);
  switch(oc->cc_type) {
    case OC_CC_SIMPLE:
      snprintf(filename + prefix_len, sizeof(filename) - prefix_len, "cc-simple-snapshot.csv");
      printf("Saving cc_simple stats to file \"%s\"\n", filename);
      ocache_save_stat_snapshot(oc->cc_simple->shared_llc, filename);
      break;
    case OC_CC_SCACHE:
      snprintf(filename + prefix_len, sizeof(filename) - prefix_len, "cc-scache-snapshot.csv");
      printf("Saving cc_scache stats to file \"%s\"\n", filename);
      scache_save_stat_snapshot(oc->cc_scache->shared_llc, filename);
      break;
    case OC_CC_OCACHE: 
      snprintf(filename + prefix_len, sizeof(filename) - prefix_len, "cc-ocache-snapshot.csv");
      printf("Saving cc_ocache stats to file \"%s\"\n", filename);
      ocache_save_stat_snapshot(oc->cc_ocache->shared_llc, filename);
      break;
    case OC_CC_DEBUG:
      printf("cc_debug will not save any stat file\n");
      break;
    default: assert(0); break;
  }
  // Save DRAM snapshot
  snprintf(filename + prefix_len, sizeof(filename) - prefix_len, "dram-snapshot.csv");
  printf("Saving DRAM stats to file \"%s\"\n", filename);
  dram_save_stat_snapshot(oc->dram, filename);
  return;
}

// Called by debuggers to avoid leaving trash files
void oc_remove_dump_files(oc_t *oc) {
  remove("dram-snapshot.csv");
  remove("cc-simple-snapshot.csv");
  remove("cc-scache-snapshot.csv");
  remove("cc-ocache-snapshot.csv");
  (void)oc;
  return;
}

void oc_print_set(oc_t *oc, int cache, int id, int set_index) {
  switch(oc->cc_type) {
    case OC_CC_SIMPLE: cc_simple_print_set(oc->cc_simple, cache, id, set_index); break;
    case OC_CC_SCACHE: cc_scache_print_set(oc->cc_scache, cache, id, set_index); break;
    case OC_CC_OCACHE: cc_ocache_print_set(oc->cc_ocache, cache, id, set_index); break;
    case OC_CC_DEBUG: printf("cc_debug will not print any set\n"); break;
    default: assert(0); break;
  }
  return;
}

// Print all member object's conf
void oc_conf_print(oc_t *oc) {
  printf("---------- oc_t conf ------------\n");
  printf("dmap:\n");
  dmap_conf_print(oc->dmap);
  printf("DRAM:\n");
  dram_conf_print(oc->dram);
  printf("cc type: \"%s\"\n", oc_cc_type_names[oc->cc_type]);
  if(oc->cc_type == OC_CC_SIMPLE) {
    cc_simple_conf_print(oc->cc_simple);
  } else if(oc->cc_type == OC_CC_SCACHE) {
    cc_scache_conf_print(oc->cc_scache);
  } else if(oc->cc_type == OC_CC_OCACHE) {
    cc_ocache_conf_print(oc->cc_ocache);
  } else if(oc->cc_type == OC_CC_DEBUG) {
    cc_debug_conf_print(oc->cc_debug);
  }
  printf("---------------------------------\n");
  return;
}

void oc_stat_print(oc_t *oc) {
  printf("---------- oc_t stat ----------\n");
  printf("Cross-line access %lu\n", oc->cross_line_access_count);
  printf("dmap:\n");
  dmap_stat_print(oc->dmap);
  printf("DRAM:\n");
  dram_stat_print(oc->dram);
  printf("cc type: \"%s\"\n", oc_cc_type_names[oc->cc_type]);
  if(oc->cc_type == OC_CC_SIMPLE) {
    cc_simple_stat_print(oc->cc_simple);
  } else if(oc->cc_type == OC_CC_SCACHE) {
    cc_scache_stat_print(oc->cc_scache);
  } else if(oc->cc_type == OC_CC_OCACHE) {
    cc_ocache_stat_print(oc->cc_ocache);
  } else if(oc->cc_type == OC_CC_DEBUG) {
    cc_debug_stat_print(oc->cc_debug);
  }
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

// Assumes that addr_1d is aligned
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
  printf("Avg steps %.2lf\n", 
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

// main_zsim_info_t

main_zsim_info_t *main_zsim_info_init(const char *key, const char *value) {
  main_zsim_info_t *info = (main_zsim_info_t *)malloc(sizeof(main_zsim_info_t));
  SYSEXPECT(info != NULL);
  memset(info, 0x00, sizeof(main_zsim_info_t));
  info->key = (char *)malloc(strlen(key) + 1);
  SYSEXPECT(info->key != NULL);
  strcpy(info->key, key);
  info->value = (char *)malloc(strlen(value) + 1);
  SYSEXPECT(info->value != NULL);
  strcpy(info->value, value);
  return info;
}

void main_zsim_info_free(main_zsim_info_t *info) {
  free(info->key);
  free(info->value);
  free(info);
  return;
}

//* main_t

// Read configuration file
// main.max_inst_count - The simulation upper bound
// main.start_inst_count - The simulation starting point
// main.interval_count - Number of simulation intervals (i.e., sim + skip instructions)
// main.interval_skip_inst_count - Number of instructions to skip as the second half of the interval
// main.result_suffix - Result dir suffix for distinction
// main.result_top_level_dir - Chdir to this before writing result into their own dirs
// main.app_name - Will appear in result dir name
// main.addr_1d_to_2d_type - Type of addr mapping
// main.result_dir_has_timestamp - Whether the result dir has a timestamp
// main.ignore_magic_op_start_sim
// main.chdir - Change working dir to this before starting the binary
// main.chdir_is_rel - Whether chdir is relative. If true, and chdir does not begin with '/' then use env WORKLOAD_PATH
main_param_t *main_param_init(conf_t *conf) {
  main_param_t *param = (main_param_t *)malloc(sizeof(main_param_t));
  SYSEXPECT(param != NULL);
  memset(param, 0x00, sizeof(main_param_t));
  param->max_inst_count = conf_find_uint64_range(conf, "main.max_inst_count", 1, UINT64_MAX, CONF_RANGE | CONF_ABBR);
  param->start_inst_count = conf_find_uint64_range(conf, "main.start_inst_count", 0, UINT64_MAX, CONF_RANGE | CONF_ABBR);
  // Read interval related conf, optional
  int interval_count_found = conf_find_int32(conf, "main.interval_count", &param->interval_count);
  if(interval_count_found == 1) {
    if(param->interval_count < 1) {
      error_exit("main.interval_count must be at least 1\n");
    }
    // Read interval skip inst count
    param->interval_skip_inst_count = \
      conf_find_uint64_range(conf, "main.interval_skip_inst_count", 1UL, CONF_UINT64_MAX, CONF_RANGE | CONF_ABBR);  
  } else {
    param->interval_count = 1;
    param->interval_skip_inst_count = 0UL;
  }
  // Derive stat snapshot inst count (0 means never)
  param->stat_snapshot_inst_count = param->interval_count * param->max_inst_count / 100UL;
  // Read result suffix, optional
  char *result_suffix = NULL;
  int result_suffix_found = conf_find_str(conf, "main.result_suffix", &result_suffix);
  if(result_suffix_found == 1) {
    if(strlen(result_suffix) > MAIN_RESULT_SUFFIX_MAX_SIZE) {
      error_exit("Result suffix \"%s\" is too long (max %d)\n", result_suffix, MAIN_RESULT_SUFFIX_MAX_SIZE);
    }
    param->result_suffix = strclone(result_suffix);
  } else {
    param->result_suffix = NULL;
  } 
  // Read result top level dir (optional)
  int result_top_level_dir_found;
  char *result_top_level_dir;
  result_top_level_dir_found = conf_find_str(conf, "main.result_top_level_dir", &result_top_level_dir);
  if(result_top_level_dir_found == 1) {
    param->result_top_level_dir = strclone(result_top_level_dir);
  } else {
    param->result_top_level_dir = NULL;
  }
  // Read app name (optional)
  char *app_name = NULL;
  int app_name_found = conf_find_str(conf, "main.app_name", &app_name);
  if(app_name_found == 1) {
    if(strlen(app_name) > MAIN_APP_NAME_MAX_SIZE) {
      error_exit("App name \"%s\" is too long (max %d)\n", app_name, MAIN_APP_NAME_MAX_SIZE);
    }
    param->app_name = strclone(app_name);
  } else {
    param->app_name = NULL;
  } 
  // Read address translation type string, default to using the map (not performing any auto vertical rotation)
  char *addr_1d_to_2d_type = NULL;
  int addr_1d_to_2d_type_ret = conf_find_str(conf, "main.addr_1d_to_2d_type", &addr_1d_to_2d_type);
  if(addr_1d_to_2d_type_ret == 1) {
    param->addr_1d_to_2d_type = strclone(addr_1d_to_2d_type);
  } else {
    param->addr_1d_to_2d_type = NULL;
  }
  // Whether result dir has timestamp, default true
  int result_ts_found = conf_find_bool(conf, "main.result_dir_has_timestamp", &param->result_dir_has_timestamp);
  if(result_ts_found == 0) {
    param->result_dir_has_timestamp = 1;
  }
  // Whether to ignore zsim / magic op start sim
  int ignore_magic_op_start_sim_found = \
    conf_find_bool(conf, "main.ignore_magic_op_start_sim", &param->ignore_magic_op_start_sim);
  if(ignore_magic_op_start_sim_found == 0) {
    param->ignore_magic_op_start_sim = 0;
  }
  // The directory to change to
  int chdir_found;
  char *chdir_str;
  chdir_found = conf_find_str(conf, "main.chdir", &chdir_str);
  if(chdir_found == 0) {
    param->chdir = NULL;
  } else {
    param->chdir = strclone(chdir_str);
  }
  int chdir_rel_found = conf_find_bool(conf, "main.chdir_is_rel", &param->chdir_is_rel);
  if(chdir_rel_found == 0) {
    param->chdir_is_rel = 0;
  }
  // Whether chdir path is relative or absolute
  if(param->chdir_is_rel == 1) {
    if(param->chdir != NULL && param->chdir[0] == '/') {
      error_exit("Chdir path must not start with '/' while being relative (see \"%s\")\n",
        param->chdir);
    } else if(param->chdir == NULL) {
      printf("[zsim] main.chdir_is_rel is found, but chdir is not specified\n");
    }
    // If NULL then just ignore it when chdir later
    param->env_workload_path = getenv("WORKLOAD_PATH");
    if(param->env_workload_path == NULL) {
      printf("[main] Warn: main.chdir_is_rel is set, but env WORKLOAD_PATH is not found\n");
    }
  } else {
    param->env_workload_path = NULL;
  }
  return param;
}

void main_param_free(main_param_t *param) {
  if(param->result_suffix != NULL) free(param->result_suffix);
  if(param->result_top_level_dir != NULL) free(param->result_top_level_dir);
  if(param->app_name != NULL) free(param->app_name);
  if(param->addr_1d_to_2d_type != NULL) free(param->addr_1d_to_2d_type);
  if(param->chdir != NULL) free(param->chdir);
  free(param);
  return;
}

void main_param_conf_print(main_param_t *param) {
  printf("Inst count max %lu start %lu\n", param->max_inst_count, param->start_inst_count);
  printf("Intrvals %d skip count %lu (stat snapshot inst %lu)\n", 
    param->interval_count, param->interval_skip_inst_count,
    param->stat_snapshot_inst_count);
  //
  if(param->result_suffix != NULL) printf("Result suffix: \"%s\"\n", param->result_suffix);
  if(param->result_top_level_dir != NULL) printf("Top-level result dir: \"%s\"\n", param->result_top_level_dir);
  if(param->app_name != NULL) printf("App name: \"%s\"\n", param->app_name);
  // Addr xtat type
  printf("Addr 1d-to-2d type: ");
  if(param->addr_1d_to_2d_type != NULL) {
    printf("\"%s\"\n", param->addr_1d_to_2d_type);
  } else {
    printf("Not using 1d-to-2d address mapping\n");
  }
  // Result saving directory
  printf("Results will be saved in a directory %s timestamp\n", 
    param->result_dir_has_timestamp == 1 ? "WITH" : "WITHOUT");
  // Ignore sim start singal
  printf("Start sim magic op will be %s\n",
    param->ignore_magic_op_start_sim == 1 ? "IGNORED" : "FOLLOWED");
  if(param->chdir != NULL) {
    printf("Change to directory: \"%s\"\n", param->chdir);
    if(param->env_workload_path != NULL) {
      printf("  Rel path prefix: \"%s\"\n", param->env_workload_path);
    }
  } else {
    printf("Not using main.chdir\n");
  }
  return;
}

// Keys:
//   cc.type
//   (cc-related cache hierarchy confs)
//   cc.default_shape
//   ---
//   dram.bank_count
//   dram.read_latency
//   dram.write_latency
//   ---
//   param-related parameters (see main_param_init)
main_t *main_init_conf(conf_t *conf) {
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
  // Just for safety, check alignment of this field
  if((uint64_t)&main->zsim_write_buffer % 64 != 0) {
    error_exit("Field main->zsim_write_buffer is not properly aligned (alignment = %lu)\n",
      (uint64_t)&main->zsim_write_buffer % 64);
  }
  // Initialize address translation call back using the type string. The string is read by main's param init
  if(main->param->addr_1d_to_2d_type == NULL || streq(main->param->addr_1d_to_2d_type, "None") == 1) {
    main->addr_1d_to_2d_cb = main_1d_to_2d_None_cb;
  } else if(streq(main->param->addr_1d_to_2d_type, "addr_map") == 1) {
    main->addr_1d_to_2d_cb = main_1d_to_2d_cb;
  } else if(streq(main->param->addr_1d_to_2d_type, "4_1") == 1) {
    main->addr_1d_to_2d_cb = main_1d_to_2d_auto_vertical_4_1_cb;
  } else {
    error_exit("Unknown 1d-to-2d addr translation type: \"%s\"\n", main->param->addr_1d_to_2d_type);
  }
  // Initialize global MBD variables as argument - note that this is independent of the algo of LLC
  MBD_init(conf);
  return main;
}

main_t *main_init(const char *conf_filename) {
  conf_t *conf = conf_init(conf_filename);
  main_t *main = main_init_conf(conf);
  // This has been allocated in main_init_conf() 
  free(main->conf_filename);
  // Copy file name to the name buffer
  main->conf_filename = strclone(conf_filename);
  if(main->param->chdir != NULL) {
    main_chdir(main);
  }
  // Must be the last one to call
  main_sim_begin(main);
  return main;
}

void main_add_slave(main_t *main, main_t *slave) {
  if(slave->slave_count != 0) {
    error_exit("A main object must not be master and slave at the same time\n");
  }
  slave->next = NULL;
  main_t *curr = main;
  while(curr->next != NULL) {
    curr = curr->next;
  }
  curr->next = slave;
  main->slave_count++;
  return;
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
  // Free info list
  main_zsim_info_t *curr = main->zsim_info_list;
  while(curr != NULL) {
    main_zsim_info_t *next = curr->next;
    main_zsim_info_free(curr);
    curr = next;
  }
  if(main->saved_cwd != NULL) {
    free(main->saved_cwd);
  }
  free(main);
  return;
}

// Change working dir to chdir, and save previous working dir in saved_cwd
void main_chdir(main_t *main) {
  assert(main->param->chdir != NULL);
  main->saved_cwd = get_current_dir_name();
  SYSEXPECT(main->saved_cwd != NULL);
  int chdir_ret;
  if(main->param->env_workload_path != NULL) {
    printf("[main] Chdir using rel path\n");
    char *full_chdir_path = \
      (char *)malloc(strlen(main->param->chdir) + strlen(main->param->env_workload_path) + 16);
    SYSEXPECT(full_chdir_path != NULL);
    strcpy(full_chdir_path, main->param->env_workload_path);
    strcat(full_chdir_path, "/");
    strcat(full_chdir_path, main->param->chdir);
    chdir_ret = chdir(full_chdir_path);
    free(full_chdir_path);
  } else {
    printf("[main] Chdir using abs path\n");
    chdir_ret = chdir(main->param->chdir);
  }
  SYSEXPECT(chdir_ret == 0);
  char *test = get_current_dir_name();
  printf("[main] Switched working dir to: \"%s\"\n", test);
  printf("[main]   ...from \"%s\"\n", main->saved_cwd);
  free(test);
  return;
}

void main_sim_begin(main_t *main) {
  // Print conf first
  main_conf_print(main);
  printf("\n\n========== simulation begin ==========\n");
  main->init_time = time(NULL);
  main->progress = 0;
  // Delayed start in report_progress() - This may be a little inaccurate if bbs are only reported sparsingly
  main->current_interval = -1; // Indicating that we have not started
  main->started = 0;
  main->next_toggle_inst_count = main->param->start_inst_count;
  return;
}

// This can be called when report progress function finds out that the simulation has come to an end
// This function does not return
void main_sim_end(main_t *main) {
  printf("\n\n========== simulation end ==========\n");
  main->end_time = time(NULL);
  // Call this before printing confs and stats
  if(main->sim_end_cb != NULL) {
    main->sim_end_cb(main, main->sim_end_cb_arg);
  }
  main_stat_print(main);
  int chdir_ret;
  // Switch back to previous working dir
  if(main->saved_cwd != NULL) {
    chdir_ret = chdir(main->saved_cwd);
    SYSEXPECT(chdir_ret == 0);
  }
  // If there is a different top level result dir than the current one, then switch to it
  char *saved_cwd = NULL;
  if(main->param->result_top_level_dir != NULL) {
    saved_cwd = get_current_dir_name(); // This is malloc'ed by the library
    // First check if the dir is there, if not then mkdir
    struct stat st;
    if(stat(main->param->result_top_level_dir, &st) == -1) {
      int mkdir_ret = mkdir(main->param->result_top_level_dir, 0700);
      SYSEXPECT(mkdir_ret == 0);
    }
    chdir_ret = chdir(main->param->result_top_level_dir);
    SYSEXPECT(chdir_ret == 0);
  }
  // Generate save directory name
  char save_dir[MAIN_RESULT_DIR_SIZE];
  main_gen_result_dir_name(main, save_dir);
  // Check whether the buf is overflown
  if(strlen(save_dir) > (MAIN_RESULT_DIR_SIZE - 2)) {
    error_exit("Dir name buffer too large; Stack overflows\n");
  }
  // First create the directory for saving
  struct stat st;
  if(stat(save_dir, &st) == -1) {
    int ret = mkdir(save_dir, 0700);
    SYSEXPECT(ret == 0);
  }
  // Then build the suffix
  strcat(save_dir, "/");
  oc_save_stat_snapshot(main->oc, save_dir);
  main_save_conf_stat(main, save_dir);
  // Switch back to the working dir
  if(saved_cwd != NULL) {
    chdir_ret = chdir(saved_cwd);
    SYSEXPECT(chdir_ret == 0);
    free(saved_cwd);
  }
  // Just terminate here under non-debug mode
#ifndef UNIT_TEST
  exit(0);
#endif
  return;
}

void main_register_sim_end_cb(main_t *main, void (*cb)(main_t *main, void *arg), void *arg) {
  if(main->sim_end_cb != NULL) {
    error_exit("End-of-sim call back has already been registered\n");
  }
  main->sim_end_cb = cb;
  main->sim_end_cb_arg = arg;
  return;
}

// Generates the directory name used to save result;
// Format is: result_[time]_[app name]_[custom suffix], without the trailing "/"
// This function does not check buffer size. Always assume it is large enough
// The [time] will not be added if param->result_dir_has_timestamp == 0
void main_gen_result_dir_name(main_t *main, char *buf) {
  snprintf(buf, MAIN_RESULT_DIR_SIZE, "result");
  if(main->param->result_dir_has_timestamp == 1) {
    snprintf(buf, MAIN_RESULT_DIR_SIZE, "_%lu", main->begin_time);
  }
  if(main->param->app_name != NULL) {
    strcat(buf, "_");
    strcat(buf, main->param->app_name);
  }
  if(main->param->result_suffix != NULL) {
    strcat(buf, "_");
    strcat(buf, main->param->result_suffix);
  }
  return;
}

// Called by the simulator to report current inst and cycle count to the main class, such that it
// determines whether the simulation should come to an end
void main_report_progress(main_t *main, uint64_t inst, uint64_t cycle) {
  assert(inst >= main->last_inst_count);
  assert(cycle >= main->last_cycle_count);
  // Stat snapshot
  if(main->started == 1) {
    main->since_last_stat_snapshot_inst_count += (inst - main->last_inst_count);
    if(main->since_last_stat_snapshot_inst_count >= main->param->stat_snapshot_inst_count) {
      main->since_last_stat_snapshot_inst_count = 0;
      oc_append_stat_snapshot(main->oc);
    }
  }
  main->last_inst_count = inst;
  main->last_cycle_count = cycle;
  if(inst < main->next_toggle_inst_count) {
    uint64_t passed_inst_count = inst - main->last_toggle_inst_count;
    uint64_t total_inst_count = main->next_toggle_inst_count - main->last_toggle_inst_count;
    int progress = (int)(100.0 * (double)passed_inst_count / (double)total_inst_count);
    if(progress != main->progress) {
      printf("\rProgress: %d%% (invl %d start %d)", progress, main->current_interval, main->started);
      fflush(stdout); // Make sure we can see it
      main->progress = progress;
    }
  } else {
    // Toggle
    if(main->started == 1) {
      // Toggle to waiting
      main->started = 0;
      // Update inst and cycle stat
      main->sim_inst_count += (inst - main->last_toggle_inst_count);
      main->sim_cycle_count += (cycle - main->last_toggle_cycle_count);
      main->next_toggle_inst_count = inst + main->param->interval_skip_inst_count;
      // End the sim if we have run out of intervals
      if(main->current_interval == main->param->interval_count - 1) {
        main_sim_end(main);
      }
    } else {
      // Toggle to sim
      main->started = 1;
      if(main->current_interval == -1) {
        main->begin_time = time(NULL);
      }
      main->current_interval++;
      main->next_toggle_inst_count = inst + main->param->max_inst_count;
    }
    main->last_toggle_inst_count = inst;
    main->last_toggle_cycle_count = cycle;
  }
  return;
}

// Called by simulator for end of BB simulation
// This function is called after zsim has finished simulating the pipeline for the BB just finished
void main_bb_sim_finish(main_t *main) {
  int list_count = main_latency_list_get_count(main->latency_list);
  // These two must be identical otherwise the sim'ed core missed or over-used mem uops
  if(list_count != main->mem_op_index) {
    error_exit("Latency list size and mem op index mismatch: %d and %d\n", list_count, main->mem_op_index);
  }
  main->mem_op_index = 0;
  main_latency_list_reset(main->latency_list);
  return;
}

// Translating from 1D to 2D. This is the main function called by every memory operation
// If the 1D address is not registered in the addr map, then just return without translation with OID = 0
void main_1d_to_2d_cb(main_t *main, uint64_t addr_1d, uint64_t *oid_2d, uint64_t *addr_2d) {
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

// Does nothing
void main_1d_to_2d_None_cb(main_t *main, uint64_t addr_1d, uint64_t *oid_2d, uint64_t *addr_2d) {
  *oid_2d = 0;
  *addr_2d = addr_1d;
  (void)main;
  return;
}

// Static mapping, each 4-line super block address will be rotated to the vertical shape, i.e., 
// Addr 4k + 0, 4k + 1, 4k + 2, 4k + 3 will be transformed to 4k OID 0, 1, 2, 3 respectively
void main_1d_to_2d_auto_vertical_4_1_cb(main_t *main, uint64_t addr_1d, uint64_t *oid_2d, uint64_t *addr_2d) {
  *oid_2d = (addr_1d & (0x3UL << UTIL_CACHE_LINE_BITS)) >> UTIL_CACHE_LINE_BITS;
  *addr_2d = addr_1d & (~0x3UL << UTIL_CACHE_LINE_BITS);
  (void)main;
  return;
}

// Populate dmap before mem operations takes place
// Insert entry into dmap, if these entries are first inserted, then copy data from aligned_addr
// Returns the dmap entry of the 2D address
void main_mem_op_before(main_t *main, 
  uint64_t addr_1d_aligned, uint64_t oid_2d, uint64_t addr_2d_aligned, int line_count) {
  dmap_t *dmap = oc_get_dmap(main->oc);
  for(int i = 0;i < line_count;i++) {
    int new_entry;
    // Returns whether the entry is newly inserted in the last argument
    dmap_entry_t *entry = _dmap_insert(dmap, oid_2d, addr_2d_aligned, &new_entry);
    if(new_entry == 1) {
      memcpy(entry->data, (void *)addr_1d_aligned, UTIL_CACHE_LINE_SIZE);
    }
    addr_2d_aligned += UTIL_CACHE_LINE_SIZE;
    addr_1d_aligned += UTIL_CACHE_LINE_SIZE;
  }
  return;
}

// Update dmap with data from the latency list
// Note that the 2D address is unaligned, and buf only contains data that gets written (i.e., also unaligned)
void main_mem_op_after(main_t *main, int op, uint64_t oid_2d, uint64_t addr_2d_unaligned, int size, void *buf) {
  if(op == MAIN_READ) {
    return;
  }
  dmap_t *dmap = oc_get_dmap(main->oc);
  dmap_write(dmap, oid_2d, addr_2d_unaligned, size, buf);
  return;
}

// Calls main_read and main_write() after preparing for memory operations
// Return finish cycle of the operation
// This function is called from zsim pipeline simulation phase after a basic block has been finished
// Note that this is the master-slave version: Input cycle and output cycles are an array
void _main_mem_op(main_t *main, uint64_t *in_cycles, uint64_t *out_cycles) {
  // If sim has not started, just return in the same cycle (fast-forward)
  if(main->started == 0) {
    //*out_cycles = *in_cycles;
    error_exit("This function should not be called when started is zero\n");
    return;
  }
  int op_index = main->mem_op_index++;
  // This function also checks bound
  main_latency_list_entry_t *entry = main_latency_list_get(main->latency_list, op_index);
  // This is potentially unaligned, which is directly taken from the application's trace
  uint64_t addr_1d = entry->addr;
  int op = entry->op;
  assert(op == MAIN_READ || op == MAIN_WRITE);
  int size = entry->size;
  void *data = entry->data; // Only meaningful for write
  // Align the addr on 1D space
  uint64_t addr_1d_aligned;
  int line_count;
  main_gen_aligned_line_addr(main, addr_1d, size, &addr_1d_aligned, &line_count);
  // Use this to generate the exact 2D address to be accessed
  int addr_1d_offset = addr_1d - addr_1d_aligned;
  assert(addr_1d_offset >= 0 && addr_1d_offset < (int)UTIL_CACHE_LINE_SIZE);
  uint64_t oid_2d, addr_2d_aligned;
  // First translate address - this is shared by all slaves
  main->addr_1d_to_2d_cb(main, addr_1d_aligned, &oid_2d, &addr_2d_aligned);
  uint64_t addr_2d_unaligned = addr_2d_aligned + addr_1d_offset;
  // Calling before function to populate data map
  main_mem_op_before(main, addr_1d_aligned, oid_2d, addr_2d_aligned, line_count);
  int index = 0;
  // Then call coherence controller
  if(op == MAIN_READ) {
    main_t *curr = main;
    do {
      out_cycles[index] = oc_load(curr->oc, 0, in_cycles[index], oid_2d, addr_2d_aligned, line_count);
      curr = curr->next;
      index++;
    } while(curr != NULL);
  } else {
    main_t *curr = main;
    do {
      out_cycles[index] = oc_store(curr->oc, 0, in_cycles[index], oid_2d, addr_2d_aligned, line_count);
      curr = curr->next;
      index++;
    } while(curr != NULL);
    // Only update dmap with the newest data after calling all slaves
    main_mem_op_after(main, op, oid_2d, addr_2d_unaligned, size, data);
  }
  assert(index == main->slave_count);
  return;
}

void main_add_info(main_t *main, const char *key, const char *value) {
  main_zsim_info_t *info = main_zsim_info_init(key, value);
  // Add it to the linked list
  info->next = main->zsim_info_list;
  main->zsim_info_list = info;
  return;
}

// The return value is malloc'ed from the heap and should be free'd manually
char *main_get_cwd(main_t *main) {
  // buf is NULL and size is zero, meaning this call will allocate a large buffer
  char *ret = getcwd(NULL, 0);
  SYSEXPECT(ret != NULL);
  (void)main;
  return ret;
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

// Update dmap, bypassing cache hierarchy
void main_update_data(main_t *main, uint64_t addr_1d, int size, void *data) {
  uint64_t addr_2d, oid_2d;
  main->addr_1d_to_2d_cb(main, addr_1d, &oid_2d, &addr_2d);
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

// This function starts simulation immediately. It works by updating start_inst_count to the current value
// such that the sim will begin on next report progress call
void main_zsim_start_sim(main_t *main) {
  if(main->started == 1) {
    printf("[2DOC] Received magic op, but simulation has already started at inst %lu cycle %lu\n",
      main->param->start_inst_count, main->start_cycle_count);
  } else {
    if(main->param->ignore_magic_op_start_sim == 1) {
      printf("[2DOC] Received magic op; Ignored due to main.ignore_magic_op_start_sim\n");
    } else {
      printf("[2DOC] Received magic op; Simulation starts at inst %lu cycle %lu (wait progress %d)\n",
        main->last_inst_count, main->last_cycle_count, main->progress);
      main->param->start_inst_count = main->last_inst_count;
    }
  }
  return;
}

void main_save_conf_stat(main_t *main, char *prefix) {
  char filename[1024];
  memset(filename, 0x00, sizeof(filename));
  if(prefix != NULL) {
    strcpy(filename, prefix);
  }
  int prefix_len = strlen(prefix);
  snprintf(filename + prefix_len, sizeof(filename) - prefix_len, "conf.txt");
  int saved_stdout = dup(STDOUT_FILENO);
  // Create the file, and truncate it if already exists
  int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  SYSEXPECT(fd != -1);
  dup2(fd, STDOUT_FILENO); // Check `man stdin` for more info
  main_conf_print(main);
  int close_ret = close(fd);
  SYSEXPECT(close_ret == 0);
  // This will close fd since the last ref has been closed
  dup2(saved_stdout, STDOUT_FILENO);
  //
  // Then do the same with stat
  snprintf(filename + prefix_len, sizeof(filename) - prefix_len, "stat.txt");
  saved_stdout = dup(STDOUT_FILENO);
  fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  SYSEXPECT(fd != -1);
  dup2(fd, STDOUT_FILENO);
  main_stat_print(main);
  close_ret = close(fd);
  SYSEXPECT(close_ret == 0);
  // This will close fd since the last ref has been closed
  dup2(saved_stdout, STDOUT_FILENO);
  return;
}

void main_conf_print(main_t *main) {
  printf("---------- main_t conf ----------\n");
  // Print current time
  time_t curr_time = time(NULL);
  printf("Current time: %s", ctime(&curr_time));
  // Prints out all variables read from conf file
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
  //uint64_t sim_inst_count = main->last_inst_count - main->param->start_inst_count;
  //uint64_t sim_cycle_count = main->last_cycle_count - main->start_cycle_count;
  printf("Completed simulation with %lu instructions and %lu cycles (conf max %lu start %lu intvls %d skip %lu)\n", 
      main->sim_inst_count, main->sim_cycle_count, 
      main->param->max_inst_count, main->param->start_inst_count,
      main->param->interval_count, main->param->interval_skip_inst_count);
  printf("Last reported inst %lu cycle %lu start cycle %lu\n", 
    main->last_inst_count, main->last_cycle_count, main->start_cycle_count);
  // Note: Do not make it .2lf because we want IPC to be as precise as possible
  printf("  IPC = %lf\n", (double)main->sim_inst_count / (double)main->sim_cycle_count);
  printf("Total time: %lu seconds (wait+sim throughput %.2lf inst/sec)\n", 
    main->end_time - main->init_time,
    (double)main->last_inst_count / (double)(main->end_time - main->init_time));
  printf("  Wait time: %lu seconds (throughput %.2lf inst/sec)\n",
    main->begin_time - main->init_time,
    (double)main->param->start_inst_count / (double)(main->begin_time - main->init_time));
  printf("  Sim time: %lu seconds (throughput %.2lf inst/sec)\n",
    main->end_time - main->begin_time,
    (double)main->sim_inst_count / (double)(main->end_time - main->begin_time));
  // Print current time to avoid confusion
  time_t curr_time = time(NULL);
  printf("Current time: %s", ctime(&curr_time));
  printf("Slave count %d (this is %s)\n", main->slave_count, main->slave_count ? "slave" : "master");
  printf("oc:\n");
  oc_stat_print(main->oc);
  printf("addr map:\n");
  main_addr_map_stat_print(main->addr_map);
  printf("latency list:\n");
  main_latency_list_stat_print(main->latency_list);
  if(main->sim_end_cb != NULL) {
    printf("Sim end cb is registered\n");
  } else {
    printf("Sim end cb is NOT registered\n");
  }
  printf("zsim info:\n");
  printf("---------- zsim info ----------\n");
  main_zsim_info_t *curr = main->zsim_info_list;
  while(curr != NULL) {
    printf("  Key: \"%s\"; Value: \"%s\"\n", curr->key, curr->value);
    curr = curr->next;
  }
  return;
}

