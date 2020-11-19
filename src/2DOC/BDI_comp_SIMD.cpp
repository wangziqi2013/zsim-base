

#include "util.h"
#include "2DOC.h"

void SIMD_print_m256i(const char *title, __m256i value, int granularity, int base) {
  uint64_t temp[4];
  _mm256_storeu_si256((__m256i *)temp, value);
  uint64_t *qword_p = (uint64_t *)temp;
  uint32_t *dword_p = (uint32_t *)temp;
  uint16_t *word_p = (uint16_t *)temp;
  uint8_t *byte_p = (uint8_t *)temp;
  const char *fmt = NULL;
  switch(base) {
    case 10: fmt = (granularity == 8) ? "%ld " : "%d "; break;
    case 16: fmt = (granularity == 8) ? "%lX " : "%X "; break;
    default: error_exit("Unknown base: %d\n", base);
  }
  printf("%s: ", title);
  switch(granularity) {
    case 8: for(int i = 0;i < 4;i++) printf(fmt, *qword_p++); break;
    case 4: for(int i = 0;i < 8;i++) printf(fmt, *dword_p++); break;
    case 2: for(int i = 0;i < 16;i++) printf(fmt, *word_p++); break;
    case 1: for(int i = 0;i < 32;i++) printf(fmt, *byte_p++); break;
    default: error_exit("Unknown granularity: %d\n", granularity);
  }
  putchar('\n');
  return;
}

// Returns type (positive number) if succeeds; BDI_TYPE_INVALID (-1) if fails
int BDI_comp_8_AVX2(void *out_buf, void *in_buf, int target_size, int dry_run) {
  assert(target_size == 1 || target_size == 2 || target_size == 4);
  // Load 256 bits (32 bytes) data into the var, not aligned
  __m256i in1 = _mm256_loadu_si256((__m256i *)in_buf);
  __m256i in2 = _mm256_loadu_si256((__m256i *)((uint8_t *)in_buf + 32));
  //SIMD_print_m256i("in1", in1, 8, 10);
  //SIMD_print_m256i("in2", in2, 8, 10);
  // Broadcast the mask to all elements
  // Set to all zero
  __m256i all_zero = _mm256_setzero_si256();
  __m256i mask;
  switch(target_size) {
    case 1: mask = _mm256_set1_epi64x(0xFFFFFFFFFFFFFF80); break;
    case 2: mask = _mm256_set1_epi64x(0xFFFFFFFFFFFF8000); break;
    case 4: mask = _mm256_set1_epi64x(0xFFFFFFFF80000000); break;
    default: mask = all_zero; break; // This is impossible but we add it anyway to avoid warnings
  }
  // Compute small value mask
  __m256i masked_result1 = _mm256_and_si256(in1, mask);
  __m256i masked_result2 = _mm256_and_si256(in2, mask);
  // Compare for equality with mask; If equal set to -1UL, otherwise set to 0UL
  __m256i cmp_pos1 = _mm256_cmpeq_epi64(masked_result1, all_zero);
  __m256i cmp_neg1 = _mm256_cmpeq_epi64(masked_result1, mask);
  __m256i cmp_pos2 = _mm256_cmpeq_epi64(masked_result2, all_zero);
  __m256i cmp_neg2 = _mm256_cmpeq_epi64(masked_result2, mask);
  // OR the previous comparison together, -1UL means small value, 0UL means large value
  __m256i cmp_result1 = _mm256_or_si256(cmp_pos1, cmp_neg1);
  __m256i cmp_result2 = _mm256_or_si256(cmp_pos2, cmp_neg2);
  //SIMD_print_m256i("cmp_result1", cmp_result1, 8, 10);
  //SIMD_print_m256i("cmp_result2", cmp_result2, 8, 10);
  uint64_t base = -1UL;
  // Set to -1 such that by default all values are small
  uint64_t small_value_bitmap = -1UL;
  // 0xFF bytes means small 64 bit integer, 0x00 bytes means large
  // Also use these two as blend mask
  uint32_t cmp_mask1 = (uint32_t)_mm256_movemask_epi8(cmp_result1);
  uint32_t cmp_mask2 = (uint32_t)_mm256_movemask_epi8(cmp_result2);
  uint64_t cmp_mask_int = ((uint64_t)cmp_mask2 << 32) | (uint64_t)cmp_mask1;
  //printf("cmp_mask_int: 0x%lX\n", cmp_mask_int); // Expect 0x000000FF0000FFFF (lower bits are for lower input values)
  __m256i delta1, delta2;
  // Only perform delta compression when not all values are small
  if(cmp_mask_int != -1UL) {
    int index = (__builtin_ffsl(~cmp_mask_int) - 1);
    assert(index >= 0 && (index % 8 == 0));
    // 8 bit per 64-bit value, so we divide it by eight
    base = ((uint64_t *)in_buf)[index / 8];
    // Parallel bit extraction, part of BMI2 support (CPUs newer than Haswell should support it)
    small_value_bitmap = _pext_u64(cmp_mask_int, 0x0101010101010101UL);
    //small_value_bitmap = (
    //  ((0x1UL & cmp_mask_int) << 7) | ((0x100UL & cmp_mask_int) >> 2) | ((0x10000UL & cmp_mask_int) >> 11) | 
    //  ((0x1000000UL & cmp_mask_int) >> 20) | ((0x100000000UL & cmp_mask_int) >> 29) | ((0x10000000000UL & cmp_mask_int) >> 38) | 
    //  ((0x1000000000000UL & cmp_mask_int) >> 47) | ((0x100000000000000UL & cmp_mask_int) >> 56)
    //);
    //printf("cmp_mask_int 0x%016lX small value bitmap: %lX base %ld\n", cmp_mask_int, small_value_bitmap, base);
    __m256i base_vec = _mm256_set1_epi64x(base);
    delta1 = _mm256_sub_epi64(in1, base_vec);
    delta2 = _mm256_sub_epi64(in2, base_vec);
    //SIMD_print_m256i("delta1", delta1, 8, 10);
    //SIMD_print_m256i("delta2", delta2, 8, 10);
    __m256i masked_delta1 = _mm256_and_si256(delta1, mask);
    __m256i masked_delta2 = _mm256_and_si256(delta2, mask);
    //SIMD_print_m256i("masked_delta1", masked_delta1, 8, 10);
    //SIMD_print_m256i("masked_delta2", masked_delta2, 8, 10);
    cmp_pos1 = _mm256_cmpeq_epi64(masked_delta1, all_zero);
    cmp_neg1 = _mm256_cmpeq_epi64(masked_delta1, mask);
    cmp_pos2 = _mm256_cmpeq_epi64(masked_delta2, all_zero);
    cmp_neg2 = _mm256_cmpeq_epi64(masked_delta2, mask);
    // -1UL means delta is small, 0 means they are large
    __m256i delta_small1 = _mm256_or_si256(cmp_pos1, cmp_neg1);
    __m256i delta_small2 = _mm256_or_si256(cmp_pos2, cmp_neg2);
    //SIMD_print_m256i("delta_small1", delta_small1, 8, 10);
    //SIMD_print_m256i("delta_small2", delta_small2, 8, 10);
    // All 1s means compressable
    __m256i compressable1 = _mm256_or_si256(delta_small1, cmp_result1);
    __m256i compressable2 = _mm256_or_si256(delta_small2, cmp_result2);
    // AND them together, all 1s means compressable
    __m256i compressable = _mm256_and_si256(compressable1, compressable2);
    int compressable_mask_int = _mm256_movemask_epi8(compressable);
    if(compressable_mask_int != -1) {
      return BDI_TYPE_INVALID;
    }
  }
  if(dry_run == 0) {
    // This will blend 8-bit values, based on every 8-th bit of the mask which is the last arg
    // If mask bit is 1, then select from the second (i.e., small values have them set as 1,
    // so we should place input as the second param)
    __m256i body1 = _mm256_blendv_epi8(delta1, in1, cmp_result1);
    __m256i body2 = _mm256_blendv_epi8(delta2, in2, cmp_result2);
    // Write one byte small value bitmap
    *(uint8_t *)out_buf = (uint8_t)small_value_bitmap;
    out_buf = (uint8_t *)out_buf + 1;
    // Write 8 byte base value
    *(uint64_t *)out_buf = (uint64_t)base;
    out_buf = (uint64_t *)out_buf + 1;
    //SIMD_print_m256i("body1", body1, 8, 10);
    //SIMD_print_m256i("body2", body2, 8, 10);
    switch(target_size) {
      case 1: {
        // Shift lower 128 [0][8], 0, 0, ... higher 128 0, 0, [0][8], 0, 0, ...
        body1 = _mm256_shuffle_epi8(body1, 
          _mm256_set_epi64x(0x8080808080808080UL, 0x8080808008008080UL, 0x8080808080808080UL, 0x8080808080800800UL));
        // Shift lower 128 0, 0, 0, 0, [0][8], 0, ... higher 128 0, 0, 0, 0, 0, 0, [0][8], 0, ...
        body2 = _mm256_shuffle_epi8(body2, 
          _mm256_set_epi64x(0x8080808080808080UL, 0x0800808080808080UL, 0x8080808080808080UL, 0x8080080080808080UL));
      } break;
      case 2: {
        // [] means 16-bit target word
        // body1 [ ][ ][ ][ ] [3][2][ ][ ] [ ][ ][ ][ ] [ ][ ][1][0]
        // body2 [7][6][ ][ ] [ ][ ][ ][ ] [ ][ ][5][4] [ ][ ][ ][ ]
        // OR'ed [7][6][ ][ ] [3][2][ ][ ] [ ][ ][5][4] [ ][ ][1][0]
        body1 = _mm256_shuffle_epi8(body1, 
          _mm256_set_epi64x(0x8080808080808080UL, 0x0908010080808080UL, 0x8080808080808080UL, 0x8080808009080100UL));
        body2 = _mm256_shuffle_epi8(body2, 
          _mm256_set_epi64x(0x0908010080808080UL, 0x8080808080808080UL, 0x8080808009080100UL, 0x8080808080808080UL));
      } break;
      case 4: {
        // [] means 32-bit target word
        // body1 [ ][ ] [3][2] [ ][ ] [1][0]
        // body2 [7][6] [ ][ ] [5][4] [ ][ ]
        // OR'ed [7][6] [3][2] [5][4] [1][0]
        body1 = _mm256_shuffle_epi8(body1, 
          _mm256_set_epi64x(0x8080808080808080UL, 0x0B0A090803020100UL, 0x8080808080808080UL, 0x0B0A090803020100UL));
        body2 = _mm256_shuffle_epi8(body2, 
          _mm256_set_epi64x(0x0B0A090803020100UL, 0x8080808080808080UL, 0x0B0A090803020100UL, 0x8080808080808080UL));
      } break;
      // Do not add default to preserve YMM registers
    }
    __m256i body = _mm256_or_si256(body1, body2);
    switch(target_size) {
      case 1: {
        uint64_t result = _mm256_extract_epi64(body, 0) | _mm256_extract_epi64(body, 2);
        *(uint64_t *)out_buf = result;
      } break;
      case 2: {
        uint64_t result = _mm256_extract_epi64(body, 0) | _mm256_extract_epi64(body, 2);
        *(uint64_t *)out_buf = result;
        result = _mm256_extract_epi64(body, 1) | _mm256_extract_epi64(body, 3);
        *((uint64_t *)out_buf + 1) = result;
      } break;
      case 4: {
        // Swap 64 bit word index 1 and 2; Control integer is (MSB to LSB): 11 01 10 00
        body = _mm256_permute4x64_epi64(body, 0xD8);
        // Just 256B store
        _mm256_storeu_si256((__m256i *)out_buf, body);
      } break;
    }
  }
  // Return type on success
  switch(target_size) {
    case 1: return 2; break;
    case 2: return 3; break;
    case 4: return 4; break;
  }
  assert(0);
  return 0;
}

int BDI_comp_4_AVX2(void *out_buf, void *in_buf, int target_size, int dry_run) {
  assert(target_size == 1 || target_size == 2);
  __m256i in1 = _mm256_loadu_si256((__m256i *)in_buf);
  __m256i in2 = _mm256_loadu_si256((__m256i *)((uint8_t *)in_buf + 32));
  //SIMD_print_m256i("in1", in1, 4, 10);
  //SIMD_print_m256i("in2", in2, 4, 10);
  __m256i all_zero = _mm256_setzero_si256();
  __m256i mask;
  switch(target_size) {
    case 1: mask = _mm256_set1_epi32(0xFFFFFF80); break;
    case 2: mask = _mm256_set1_epi32(0xFFFF8000); break;
    default: mask = all_zero; break; 
  }
  __m256i masked_result1 = _mm256_and_si256(in1, mask);
  __m256i masked_result2 = _mm256_and_si256(in2, mask);
  __m256i cmp_pos1 = _mm256_cmpeq_epi32(masked_result1, all_zero);
  __m256i cmp_neg1 = _mm256_cmpeq_epi32(masked_result1, mask);
  __m256i cmp_pos2 = _mm256_cmpeq_epi32(masked_result2, all_zero);
  __m256i cmp_neg2 = _mm256_cmpeq_epi32(masked_result2, mask);
  __m256i cmp_result1 = _mm256_or_si256(cmp_pos1, cmp_neg1);
  __m256i cmp_result2 = _mm256_or_si256(cmp_pos2, cmp_neg2);
  //SIMD_print_m256i("cmp_result1", cmp_result1, 4, 10);
  //SIMD_print_m256i("cmp_result2", cmp_result2, 4, 10);
  uint64_t base = -1UL;
  uint64_t small_value_bitmap = -1UL;
  uint32_t cmp_mask1 = (uint32_t)_mm256_movemask_epi8(cmp_result1);
  uint32_t cmp_mask2 = (uint32_t)_mm256_movemask_epi8(cmp_result2);
  //printf("cmp mask1 0x%X mask2 0x%X\n", cmp_mask1, cmp_mask2);
  uint64_t cmp_mask_int = ((uint64_t)cmp_mask2 << 32) | (uint64_t)cmp_mask1;
  //printf("cmp_mask_int: 0x%lX\n", cmp_mask_int); // Expect 0x000000FF0000FFFF (lower bits are for lower input values)
  __m256i delta1, delta2;
  // Only perform delta compression when not all values are small
  if(cmp_mask_int != -1UL) {
    int index = (__builtin_ffsl(~cmp_mask_int) - 1);
    assert(index >= 0 && (index % 4 == 0));
    base = ((uint32_t *)in_buf)[index / 4];
    small_value_bitmap = _pext_u64(cmp_mask_int, 0x1111111111111111UL);
    //printf("cmp_mask_int 0x%016lX small value bitmap: %lX base %ld\n", cmp_mask_int, small_value_bitmap, base);
    __m256i base_vec = _mm256_set1_epi32((uint32_t)base);
    delta1 = _mm256_sub_epi32(in1, base_vec);
    delta2 = _mm256_sub_epi32(in2, base_vec);
    //SIMD_print_m256i("delta1", delta1, 8, 10);
    //SIMD_print_m256i("delta2", delta2, 8, 10);
    __m256i masked_delta1 = _mm256_and_si256(delta1, mask);
    __m256i masked_delta2 = _mm256_and_si256(delta2, mask);
    //SIMD_print_m256i("masked_delta1", masked_delta1, 8, 10);
    //SIMD_print_m256i("masked_delta2", masked_delta2, 8, 10);
    cmp_pos1 = _mm256_cmpeq_epi32(masked_delta1, all_zero);
    cmp_neg1 = _mm256_cmpeq_epi32(masked_delta1, mask);
    cmp_pos2 = _mm256_cmpeq_epi32(masked_delta2, all_zero);
    cmp_neg2 = _mm256_cmpeq_epi32(masked_delta2, mask);
    __m256i delta_small1 = _mm256_or_si256(cmp_pos1, cmp_neg1);
    __m256i delta_small2 = _mm256_or_si256(cmp_pos2, cmp_neg2);
    //SIMD_print_m256i("delta_small1", delta_small1, 8, 10);
    //SIMD_print_m256i("delta_small2", delta_small2, 8, 10);
    __m256i compressable1 = _mm256_or_si256(delta_small1, cmp_result1);
    __m256i compressable2 = _mm256_or_si256(delta_small2, cmp_result2);
    __m256i compressable = _mm256_and_si256(compressable1, compressable2);
    int compressable_mask_int = _mm256_movemask_epi8(compressable);
    if(compressable_mask_int != -1) {
      return BDI_TYPE_INVALID;
    }
  }
  if(dry_run == 0) {
    // This will blend 8-bit values, based on every 8-th bit of the mask which is the last arg
    // If mask bit is 1, then select from the second (i.e., small values have them set as 1,
    // so we should place input as the second param)
    __m256i body1 = _mm256_blendv_epi8(delta1, in1, cmp_result1);
    __m256i body2 = _mm256_blendv_epi8(delta2, in2, cmp_result2);
    *(uint16_t *)out_buf = (uint16_t)small_value_bitmap;
    out_buf = (uint16_t *)out_buf + 1;
    *(uint32_t *)out_buf = (uint32_t)base;
    out_buf = (uint32_t *)out_buf + 1;
    //SIMD_print_m256i("body1", body1, 8, 10);
    //SIMD_print_m256i("body2", body2, 8, 10);
    switch(target_size) {
      case 1: {
        // [] means 8-bit target word
        // body1 [ ][ ][ ][ ][ ][ ][ ][ ] [7][6][5][4][ ][ ][ ][ ] [ ][ ][ ][ ][ ][ ][ ][ ] [ ][ ][ ][ ][3][2][1][0]
        // body2 [F][E][D][C][ ][ ][ ][ ] [ ][ ][ ][ ][ ][ ][ ][ ] [ ][ ][ ][ ][B][A][9][8] [ ][ ][ ][ ][ ][ ][ ][ ]
        // OR'ed [F][E][D][C][ ][ ][ ][ ] [7][6][5][4][ ][ ][ ][ ] [ ][ ][ ][ ][B][A][9][8] [ ][ ][ ][ ][3][2][1][0]
        body1 = _mm256_shuffle_epi8(body1, 
          _mm256_set_epi64x(0x8080808080808080UL, 0x0C08040080808080UL, 0x8080808080808080UL, 0x808080800C080400UL));
        body2 = _mm256_shuffle_epi8(body2, 
          _mm256_set_epi64x(0x0C08040080808080UL, 0x8080808080808080UL, 0x808080800C080400UL, 0x8080808080808080UL));
      } break;
      case 2: {
        // [] means 16-bit target word
        // body1 [ ][ ][ ][ ] [7][6][5][4] [ ][ ][ ][ ] [3][2][1][0]
        // body2 [F][E][D][C] [ ][ ][ ][ ] [B][A][9][8] [ ][ ][ ][ ]
        // OR'ed [F][E][D][C] [7][6][5][4] [B][A][9][8] [3][2][1][0]
        body1 = _mm256_shuffle_epi8(body1, 
          _mm256_set_epi64x(0x8080808080808080UL, 0x0D0C090805040100UL, 0x8080808080808080UL, 0x0D0C090805040100UL));
        body2 = _mm256_shuffle_epi8(body2, 
          _mm256_set_epi64x(0x0D0C090805040100UL, 0x8080808080808080UL, 0x0D0C090805040100UL, 0x8080808080808080UL));
      } break;
      // Do not add default to preserve YMM registers
    }
    __m256i body = _mm256_or_si256(body1, body2);
    switch(target_size) {
      case 1: {
        uint64_t result = _mm256_extract_epi64(body, 0) | _mm256_extract_epi64(body, 2);
        *(uint64_t *)out_buf = result;
        result = _mm256_extract_epi64(body, 1) | _mm256_extract_epi64(body, 3);
        *((uint64_t *)out_buf + 1) = result;
      } break;
      case 2: {
        body = _mm256_permute4x64_epi64(body, 0xD8);
        _mm256_storeu_si256((__m256i *)out_buf, body);
      } break;
    }
  }
  switch(target_size) {
    case 1: return 5; break;
    case 2: return 6; break;
  }
  assert(0);
  return 0;
}

int BDI_comp_2_AVX2(void *out_buf, void *in_buf, int target_size, int dry_run) {
  assert(target_size == 1);
  __m256i in1 = _mm256_loadu_si256((__m256i *)in_buf);
  __m256i in2 = _mm256_loadu_si256((__m256i *)((uint8_t *)in_buf + 32));
  //SIMD_print_m256i("in1", in1, 4, 10);
  //SIMD_print_m256i("in2", in2, 4, 10);
  __m256i all_zero = _mm256_setzero_si256();
  __m256i mask = _mm256_set1_epi16(0xFF80); 
  __m256i masked_result1 = _mm256_and_si256(in1, mask);
  __m256i masked_result2 = _mm256_and_si256(in2, mask);
  __m256i cmp_pos1 = _mm256_cmpeq_epi16(masked_result1, all_zero);
  __m256i cmp_neg1 = _mm256_cmpeq_epi16(masked_result1, mask);
  __m256i cmp_pos2 = _mm256_cmpeq_epi16(masked_result2, all_zero);
  __m256i cmp_neg2 = _mm256_cmpeq_epi16(masked_result2, mask);
  __m256i cmp_result1 = _mm256_or_si256(cmp_pos1, cmp_neg1);
  __m256i cmp_result2 = _mm256_or_si256(cmp_pos2, cmp_neg2);
  //SIMD_print_m256i("cmp_result1", cmp_result1, 4, 10);
  //SIMD_print_m256i("cmp_result2", cmp_result2, 4, 10);
  uint64_t base = -1UL;
  uint64_t small_value_bitmap = -1UL;
  uint32_t cmp_mask1 = (uint32_t)_mm256_movemask_epi8(cmp_result1);
  uint32_t cmp_mask2 = (uint32_t)_mm256_movemask_epi8(cmp_result2);
  //printf("cmp mask1 0x%X mask2 0x%X\n", cmp_mask1, cmp_mask2);
  uint64_t cmp_mask_int = ((uint64_t)cmp_mask2 << 32) | (uint64_t)cmp_mask1;
  //printf("cmp_mask_int: 0x%lX\n", cmp_mask_int); // Expect 0x000000FF0000FFFF (lower bits are for lower input values)
  __m256i delta1, delta2;
  // Only perform delta compression when not all values are small
  if(cmp_mask_int != -1UL) {
    int index = (__builtin_ffsl(~cmp_mask_int) - 1);
    assert(index >= 0 && (index % 2 == 0));
    base = ((uint16_t *)in_buf)[index / 2];
    small_value_bitmap = _pext_u64(cmp_mask_int, 0x5555555555555555UL);
    //printf("cmp_mask_int 0x%016lX small value bitmap: %lX base %ld\n", cmp_mask_int, small_value_bitmap, base);
    __m256i base_vec = _mm256_set1_epi16((uint16_t)base);
    delta1 = _mm256_sub_epi16(in1, base_vec);
    delta2 = _mm256_sub_epi16(in2, base_vec);
    //SIMD_print_m256i("delta1", delta1, 8, 10);
    //SIMD_print_m256i("delta2", delta2, 8, 10);
    __m256i masked_delta1 = _mm256_and_si256(delta1, mask);
    __m256i masked_delta2 = _mm256_and_si256(delta2, mask);
    //SIMD_print_m256i("masked_delta1", masked_delta1, 8, 10);
    //SIMD_print_m256i("masked_delta2", masked_delta2, 8, 10);
    cmp_pos1 = _mm256_cmpeq_epi16(masked_delta1, all_zero);
    cmp_neg1 = _mm256_cmpeq_epi16(masked_delta1, mask);
    cmp_pos2 = _mm256_cmpeq_epi16(masked_delta2, all_zero);
    cmp_neg2 = _mm256_cmpeq_epi16(masked_delta2, mask);
    __m256i delta_small1 = _mm256_or_si256(cmp_pos1, cmp_neg1);
    __m256i delta_small2 = _mm256_or_si256(cmp_pos2, cmp_neg2);
    //SIMD_print_m256i("delta_small1", delta_small1, 8, 10);
    //SIMD_print_m256i("delta_small2", delta_small2, 8, 10);
    __m256i compressable1 = _mm256_or_si256(delta_small1, cmp_result1);
    __m256i compressable2 = _mm256_or_si256(delta_small2, cmp_result2);
    __m256i compressable = _mm256_and_si256(compressable1, compressable2);
    int compressable_mask_int = _mm256_movemask_epi8(compressable);
    if(compressable_mask_int != -1) {
      return BDI_TYPE_INVALID;
    }
  }
  if(dry_run == 0) {
    // This will blend 8-bit values, based on every 8-th bit of the mask which is the last arg
    // If mask bit is 1, then select from the second (i.e., small values have them set as 1,
    // so we should place input as the second param)
    __m256i body1 = _mm256_blendv_epi8(delta1, in1, cmp_result1);
    __m256i body2 = _mm256_blendv_epi8(delta2, in2, cmp_result2);
    *(uint32_t *)out_buf = (uint32_t)small_value_bitmap;
    out_buf = (uint32_t *)out_buf + 1;
    *(uint16_t *)out_buf = (uint16_t)base;
    out_buf = (uint16_t *)out_buf + 1;
    //SIMD_print_m256i("body1", body1, 8, 10);
    //SIMD_print_m256i("body2", body2, 8, 10);

    // [] means 8-bit target word
    // body1 [ ][ ][ ][ ][ ][ ][ ][ ] [F][E][D][C][B][A][9][8] [ ][ ][ ][ ][ ][ ][ ][ ] [7][6][5][4][3][2][1][0]
    // body2 [V][U][T][S][R][Q][P][O] [ ][ ][ ][ ][ ][ ][ ][ ] [N][M][L][K][J][I][H][G] [ ][ ][ ][ ][ ][ ][ ][ ]
    // OR'ed [V][U][T][S][R][Q][P][O] [F][E][D][C][B][A][9][8] [N][M][L][K][J][I][H][G] [7][6][5][4][3][2][1][0]
    body1 = _mm256_shuffle_epi8(body1, 
      _mm256_set_epi64x(0x8080808080808080UL, 0x0E0C0A0806040200UL, 0x8080808080808080UL, 0x0E0C0A0806040200UL));
    body2 = _mm256_shuffle_epi8(body2, 
      _mm256_set_epi64x(0x0E0C0A0806040200UL, 0x8080808080808080UL, 0x0E0C0A0806040200UL, 0x8080808080808080UL));
    __m256i body = _mm256_or_si256(body1, body2);
    body = _mm256_permute4x64_epi64(body, 0xD8);
    _mm256_storeu_si256((__m256i *)out_buf, body);
  }
  (void)target_size;
  return 7;
}

// Wrapper that makes the function have the same interface as BDI_comp_scalar
int BDI_comp_AVX2(void *out_buf, void *in_buf, BDI_param_t *param, int dry_run) {
  int ret = 0;
  switch(param->word_size) {
    case 2: ret = BDI_comp_2_AVX2(out_buf, in_buf, param->target_size, dry_run); break;
    case 4: ret = BDI_comp_4_AVX2(out_buf, in_buf, param->target_size, dry_run); break;
    case 8: ret = BDI_comp_8_AVX2(out_buf, in_buf, param->target_size, dry_run); break;
  }
  return ret;
}

void BDI_decomp_8_AVX2(void *out_buf, void *in_buf, int target_size) {
  assert(target_size == 1 || target_size == 2 || target_size == 4);
  uint64_t bitmap = *(uint8_t *)in_buf;
  in_buf = ((uint8_t *)in_buf) + 1;
  uint64_t base = *(uint64_t *)in_buf;
  in_buf = ((uint64_t *)in_buf) + 1;
  //printf("Base %lu bitmap 0x%lX\n", base, bitmap);
  __m128i _body1, _body2;
  __m256i body1, body2;
  switch(target_size) {
    case 1: {
      // 8 byte body, 4 byte each register
      //printf("word 1 0x%X word 2 0x%X\n", ((int32_t *)in_buf)[0], ((int32_t *)in_buf)[1]);
      _body1 = _mm_insert_epi32(_mm_setzero_si128(), ((int32_t *)in_buf)[0], 0);
      _body2 = _mm_insert_epi32(_mm_setzero_si128(), ((int32_t *)in_buf)[1], 0);
      body1 = _mm256_cvtepi8_epi64(_body1); // It is sign extension, we do not need to do it ourselves
      body2 = _mm256_cvtepi8_epi64(_body2);
      //SIMD_print_m256i("body1", body1, 8, 10);
      //SIMD_print_m256i("body2", body2, 8, 10);
    } break;
    case 2: {
      // 16 byte body, 8 byte each register
      _body1 = _mm_set_epi64x(0, ((int64_t *)in_buf)[0]);
      _body2 = _mm_set_epi64x(0, ((int64_t *)in_buf)[1]);
      body1 = _mm256_cvtepi16_epi64(_body1);
      body2 = _mm256_cvtepi16_epi64(_body2);
    } break;
    case 4: {
      // 32 byte body, 16 byte each register
      _body1 = _mm_loadu_si128((__m128i *)in_buf);
      _body2 = _mm_loadu_si128((__m128i *)(((uint8_t *)in_buf) + 16));
      body1 = _mm256_cvtepi32_epi64(_body1);
      body2 = _mm256_cvtepi32_epi64(_body2);
    } break;
    default: body1 = _mm256_setzero_si256(); body2 = _mm256_setzero_si256(); break; // Avoid warning
  }
  // Common base value
  __m256i base_vec = _mm256_set1_epi64x(base);
  __m256i body1_large = _mm256_add_epi64(body1, base_vec);
  __m256i body2_large = _mm256_add_epi64(body2, base_vec);
  // Then generate the blend vector by duplicating bits
  // First make it byte and then cast to 64 bit integers
  uint64_t blend1 = _pdep_u64(bitmap, 0x0000000001010101UL);
  uint64_t blend2 = _pdep_u64(bitmap >> 4, 0x0000000001010101UL);
  __m256i blend_vec1 = _mm256_cvtepi8_epi64(_mm_set_epi64x(0, blend1));
  __m256i blend_vec2 = _mm256_cvtepi8_epi64(_mm_set_epi64x(0, blend2));
  // all means large value, clear means small value
  // Note that this is the opposite of compression
  blend_vec1 = _mm256_sub_epi64(blend_vec1, _mm256_set1_epi64x(1));
  blend_vec2 = _mm256_sub_epi64(blend_vec2, _mm256_set1_epi64x(1));
  //SIMD_print_m256i("body1", body1, 8, 10);
  //SIMD_print_m256i("body2", body2, 8, 10);
  body1 = _mm256_blendv_epi8(body1, body1_large, blend_vec1);
  body2 = _mm256_blendv_epi8(body2, body2_large, blend_vec2);
  //SIMD_print_m256i("body1_large", body1_large, 8, 10);
  //SIMD_print_m256i("body2_large", body2_large, 8, 10);
  _mm256_storeu_si256((__m256i *)out_buf, body1);
  _mm256_storeu_si256((__m256i *)((uint8_t *)out_buf + 32), body2);
  return;
}

void BDI_decomp_4_AVX2(void *out_buf, void *in_buf, int target_size) {
  assert(target_size == 1 || target_size == 2);
  uint64_t bitmap = *(uint16_t *)in_buf;
  in_buf = ((uint16_t *)in_buf) + 1;
  uint64_t base = *(uint32_t *)in_buf;
  in_buf = ((uint32_t *)in_buf) + 1;
  //printf("Base %lu bitmap 0x%lX\n", base, bitmap);
  __m128i _body1, _body2;
  __m256i body1, body2;
  switch(target_size) {
    case 1: {
      //printf("word 1 0x%X word 2 0x%X\n", ((int32_t *)in_buf)[0], ((int32_t *)in_buf)[1]);
      _body1 = _mm_set_epi64x(0UL, ((int64_t *)in_buf)[0]);
      _body2 = _mm_set_epi64x(0UL, ((int64_t *)in_buf)[1]);
      body1 = _mm256_cvtepi8_epi32(_body1);
      body2 = _mm256_cvtepi8_epi32(_body2);
    } break;
    case 2: {
      _body1 = _mm_loadu_si128((__m128i *)in_buf);
      _body2 = _mm_loadu_si128((__m128i *)((uint8_t *)in_buf + 16));
      body1 = _mm256_cvtepi16_epi32(_body1);
      body2 = _mm256_cvtepi16_epi32(_body2);
    } break;
    default: body1 = _mm256_setzero_si256(); body2 = _mm256_setzero_si256(); break; // Avoid warning
  }
  __m256i base_vec = _mm256_set1_epi32((int)base);
  __m256i body1_large = _mm256_add_epi32(body1, base_vec);
  __m256i body2_large = _mm256_add_epi32(body2, base_vec);
  uint64_t blend1 = _pdep_u64(bitmap, 0x0101010101010101UL);
  uint64_t blend2 = _pdep_u64(bitmap >> 8, 0x0101010101010101UL);
  __m256i blend_vec1 = _mm256_cvtepi8_epi32(_mm_set_epi64x(0, blend1));
  __m256i blend_vec2 = _mm256_cvtepi8_epi32(_mm_set_epi64x(0, blend2));
  // all means large value, clear means small value
  // Note that this is the opposite of compression
  blend_vec1 = _mm256_sub_epi32(blend_vec1, _mm256_set1_epi32(1));
  blend_vec2 = _mm256_sub_epi32(blend_vec2, _mm256_set1_epi32(1));
  //SIMD_print_m256i("body1", body1, 8, 10);
  //SIMD_print_m256i("body2", body2, 8, 10);
  body1 = _mm256_blendv_epi8(body1, body1_large, blend_vec1);
  body2 = _mm256_blendv_epi8(body2, body2_large, blend_vec2);
  //SIMD_print_m256i("body1_large", body1_large, 8, 10);
  //SIMD_print_m256i("body2_large", body2_large, 8, 10);
  _mm256_storeu_si256((__m256i *)out_buf, body1);
  _mm256_storeu_si256((__m256i *)((uint8_t *)out_buf + 32), body2);
  return;
}

void BDI_decomp_2_AVX2(void *out_buf, void *in_buf, int target_size) {
  assert(target_size == 1);
  uint64_t bitmap = *(uint32_t *)in_buf;
  in_buf = ((uint32_t *)in_buf) + 1;
  uint64_t base = *(uint16_t *)in_buf;
  in_buf = ((uint16_t *)in_buf) + 1;

  __m128i _body1 = _mm_loadu_si128((__m128i *)in_buf);
  __m128i _body2 = _mm_loadu_si128((__m128i *)((uint8_t *)in_buf + 16));
  __m256i body1 = _mm256_cvtepi8_epi16(_body1);
  __m256i body2 = _mm256_cvtepi8_epi16(_body2);
    
  __m256i base_vec = _mm256_set1_epi16((int16_t)base);
  __m256i body1_large = _mm256_add_epi16(body1, base_vec);
  __m256i body2_large = _mm256_add_epi16(body2, base_vec);
  uint64_t blend1 = _pdep_u64(bitmap, 0x0101010101010101UL);
  uint64_t blend2 = _pdep_u64(bitmap >> 8, 0x0101010101010101UL);
  uint64_t blend3 = _pdep_u64(bitmap >> 16, 0x0101010101010101UL);
  uint64_t blend4 = _pdep_u64(bitmap >> 24, 0x0101010101010101UL);
  __m256i blend_vec1 = _mm256_cvtepi8_epi16(_mm_set_epi64x(blend2, blend1));
  __m256i blend_vec2 = _mm256_cvtepi8_epi16(_mm_set_epi64x(blend4, blend3));
  // all means large value, clear means small value
  // Note that this is the opposite of compression
  blend_vec1 = _mm256_sub_epi16(blend_vec1, _mm256_set1_epi16(1));
  blend_vec2 = _mm256_sub_epi16(blend_vec2, _mm256_set1_epi16(1));
  //SIMD_print_m256i("body1", body1, 8, 10);
  //SIMD_print_m256i("body2", body2, 8, 10);
  body1 = _mm256_blendv_epi8(body1, body1_large, blend_vec1);
  body2 = _mm256_blendv_epi8(body2, body2_large, blend_vec2);
  //SIMD_print_m256i("body1_large", body1_large, 8, 10);
  //SIMD_print_m256i("body2_large", body2_large, 8, 10);
  _mm256_storeu_si256((__m256i *)out_buf, body1);
  _mm256_storeu_si256((__m256i *)((uint8_t *)out_buf + 32), body2);
  (void)target_size;
  return;
}

void BDI_decomp_AVX2(void *out_buf, void *in_buf, BDI_param_t *param) {
  switch(param->word_size) {
    case 2: BDI_decomp_2_AVX2(out_buf, in_buf, param->target_size); break;
    case 4: BDI_decomp_4_AVX2(out_buf, in_buf, param->target_size); break;
    case 8: BDI_decomp_8_AVX2(out_buf, in_buf, param->target_size); break;
  }
  return;
}

/*
// TODO: THE SMALL VALUE MASK IS WRONG
int BDI_comp_8_1_AVX512(void *out_buf, void *in_buf) {
  // Load 512 bits (64 bytes) data into the var, not aligned
  __m512i in = _mm512_loadu_si512(in_buf);
  // Broadcast the mask to all elements
  __m512i mask = _mm512_set1_epi64(0xFFFFFFFFFFFFFF80);
  // Set to all zero
  __m512i all_zero = _mm512_setzero_si512();
  // Compute small value mask
  __m512i masked_result = _mm512_and_epi64(in, mask);
  __mmask8 pos = _mm512_cmpeq_epu64_mask(masked_result, all_zero);
  __mmask8 neg = _mm512_cmpeq_epu64_mask(masked_result, mask);
  // Note: if we use normal operators it will be casted back and fourth
  __mmask8 small_value_mask = _mm512_kor(pos, neg);
  __mmask8 large_value_mask = _mm512_knot(small_value_mask);
  // Set to 1 for large values (bit 0 - 7 only)
  unsigned int large_value_mask_int = (unsigned int)large_value_mask;
  __m512i delta;
  uint64_t base_int = 0;
  if(large_value_mask_int != 0) {
    int base_index = __builtin_clz(large_value_mask_int) - 24;
    assert(base_index >= 0 && base_index < 8);
    // All base values
    base_int = ((uint64_t *)in_buf)[base_index];
    __m512i base = _mm512_set1_epi64(base_int);
    delta = _mm512_sub_epi64(in, base);
    masked_result = _mm512_and_epi64(delta, mask);
    pos = _mm512_cmpeq_epu64_mask(masked_result, all_zero);
    neg = _mm512_cmpeq_epu64_mask(masked_result, mask);
    __mmask8 small_delta_mask = _mm512_kor(pos, neg);
    // If there are large deltas then return
    if(small_delta_mask != large_value_mask) {
      return 0;
    }
  }
  // Blend the original values and deltas using small value mask
  __m512i result = _mm512_mask_blend_epi64(small_value_mask, in, delta);
  // Down-cast to 64 bits
  __m128i body = _mm512_cvtepi64_epi8(result);
  unsigned int small_value_mask_int = (unsigned int)small_value_mask;
  out_buf = BDI_comp_write_bitmap(out_buf, small_value_mask_int, 8);
  out_buf = BDI_comp_write_base(out_buf, base_int, 8);
  // Stores lower 64 bits of the MMX register
  // *(uint64_t *)out_buf = (uint64_t)body;
  //_mm_storeu_si64(out_buf, body);
  // Hopefully this generates a single mov instruction
  memcpy(out_buf, &body, 64);
  return 1;
}
*/