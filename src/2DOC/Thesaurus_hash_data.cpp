// Use 8-bit words to hash the fingerprint vector
#define THESAURUS_INPUT_WORD_BITS 8
// 12 bits in the final fingerprint
#define THESAURUS_FINGERPRINT_BITS 12
static uint16_t Thesaurus_hash_data(void *_data) {
  int8_t *data = (int8_t *)_data;
  uint16_t ret = 0U;
  int32_t curr;
  // row 0
  curr = 0 - data[0] - data[2] + data[3] - data[4] + data[6] - data[11] - data[12] - data[14] - data[15] + data[19] + data[20] - data[22] + data[23] - data[24] + data[31] + data[32] + data[33] + data[36] + data[40] - data[44] + data[45] + data[47] + data[48] + data[50] + data[53] - data[54] - data[61];
  // Row 0 Rands: -1 0 -1 1 -1 0 1 0 0 0 0 -1 -1 0 -1 -1 0 0 0 1 1 0 -1 1 -1 0 0 0 0 0 0 1 1 1 0 0 1 0 0 0 1 0 0 0 -1 1 0 1 1 0 1 0 0 1 -1 0 0 0 0 0 0 -1 0 0
  ret = (ret << 1) | (curr >= 0 ? 0x0U : 0x1U);
  // row 1
  curr = 0 - data[0] + data[7] + data[11] + data[12] - data[13] + data[15] + data[16] - data[18] - data[22] - data[24] + data[27] + data[28] - data[30] - data[36] - data[38] + data[39] - data[47] - data[56] - data[57] - data[58] - data[61];
  // Row 1 Rands: -1 0 0 0 0 0 0 1 0 0 0 1 1 -1 0 1 1 0 -1 0 0 0 -1 0 -1 0 0 1 1 0 -1 0 0 0 0 0 -1 0 -1 1 0 0 0 0 0 0 0 -1 0 0 0 0 0 0 0 0 -1 -1 -1 0 0 -1 0 0
  ret = (ret << 1) | (curr >= 0 ? 0x0U : 0x1U);
  // row 2
  curr = 0 + data[0] + data[1] + data[2] - data[9] - data[18] - data[19] - data[33] - data[37] - data[39] - data[41] - data[45] - data[46] - data[52] + data[53] - data[54] - data[57] - data[59] + data[60] - data[61];
  // Row 2 Rands: 1 1 1 0 0 0 0 0 0 -1 0 0 0 0 0 0 0 0 -1 -1 0 0 0 0 0 0 0 0 0 0 0 0 0 -1 0 0 0 -1 0 -1 0 -1 0 0 0 -1 -1 0 0 0 0 0 -1 1 -1 0 0 -1 0 -1 1 -1 0 0
  ret = (ret << 1) | (curr >= 0 ? 0x0U : 0x1U);
  // row 3
  curr = 0 - data[0] - data[2] - data[3] + data[7] + data[15] - data[17] - data[18] - data[23] - data[25] - data[30] + data[32] + data[39] - data[42] + data[48] + data[50] + data[54] + data[56] + data[60];
  // Row 3 Rands: -1 0 -1 -1 0 0 0 1 0 0 0 0 0 0 0 1 0 -1 -1 0 0 0 0 -1 0 -1 0 0 0 0 -1 0 1 0 0 0 0 0 0 1 0 0 -1 0 0 0 0 0 1 0 1 0 0 0 1 0 1 0 0 0 1 0 0 0
  ret = (ret << 1) | (curr >= 0 ? 0x0U : 0x1U);
  // row 4
  curr = 0 - data[3] - data[6] + data[8] - data[12] + data[13] - data[15] - data[16] + data[18] - data[22] + data[24] - data[25] - data[29] + data[37] - data[39] + data[44] - data[47] + data[49] + data[50] + data[51] - data[53] + data[54] - data[55] + data[60] - data[61] + data[62];
  // Row 4 Rands: 0 0 0 -1 0 0 -1 0 1 0 0 0 -1 1 0 -1 -1 0 1 0 0 0 -1 0 1 -1 0 0 0 -1 0 0 0 0 0 0 0 1 0 -1 0 0 0 0 1 0 0 -1 0 1 1 1 0 -1 1 -1 0 0 0 0 1 -1 1 0
  ret = (ret << 1) | (curr >= 0 ? 0x0U : 0x1U);
  // row 5
  curr = 0 + data[3] - data[5] - data[8] - data[10] - data[11] - data[12] - data[13] - data[16] - data[18] + data[25] - data[27] + data[28] + data[30] - data[32] - data[36] + data[40] - data[43] + data[44] - data[45] - data[46] - data[53] + data[54] + data[55] - data[56] - data[58] - data[62];
  // Row 5 Rands: 0 0 0 1 0 -1 0 0 -1 0 -1 -1 -1 -1 0 0 -1 0 -1 0 0 0 0 0 0 1 0 -1 1 0 1 0 -1 0 0 0 -1 0 0 0 1 0 0 -1 1 -1 -1 0 0 0 0 0 0 -1 1 1 -1 0 -1 0 0 0 -1 0
  ret = (ret << 1) | (curr >= 0 ? 0x0U : 0x1U);
  // row 6
  curr = 0 + data[1] - data[2] - data[6] + data[9] + data[11] - data[12] + data[18] - data[19] + data[20] - data[21] + data[23] - data[28] - data[30] - data[32] - data[33] + data[36] - data[37] - data[38] - data[40] - data[41] + data[42] + data[46] + data[48] - data[49] - data[53] - data[54] - data[56] + data[57] - data[58] - data[61] + data[62];
  // Row 6 Rands: 0 1 -1 0 0 0 -1 0 0 1 0 1 -1 0 0 0 0 0 1 -1 1 -1 0 1 0 0 0 0 -1 0 -1 0 -1 -1 0 0 1 -1 -1 0 -1 -1 1 0 0 0 1 0 1 -1 0 0 0 -1 -1 0 -1 1 -1 0 0 -1 1 0
  ret = (ret << 1) | (curr >= 0 ? 0x0U : 0x1U);
  // row 7
  curr = 0 + data[1] - data[2] + data[4] + data[10] + data[20] - data[22] - data[23] + data[24] - data[27] - data[29] + data[32] - data[34] + data[36] - data[41] - data[47] + data[49] + data[52] + data[53] + data[55] + data[56] + data[57] - data[62];
  // Row 7 Rands: 0 1 -1 0 1 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 1 0 -1 -1 1 0 0 -1 0 -1 0 0 1 0 -1 0 1 0 0 0 0 -1 0 0 0 0 0 -1 0 1 0 0 1 1 0 1 1 1 0 0 0 0 -1 0
  ret = (ret << 1) | (curr >= 0 ? 0x0U : 0x1U);
  // row 8
  curr = 0 - data[0] - data[2] + data[4] + data[8] + data[10] - data[12] - data[22] - data[23] - data[29] + data[30] + data[35] - data[40] - data[42] - data[43] - data[45] + data[46] + data[48] - data[51] + data[53] - data[54] - data[58] - data[61];
  // Row 8 Rands: -1 0 -1 0 1 0 0 0 1 0 1 0 -1 0 0 0 0 0 0 0 0 0 -1 -1 0 0 0 0 0 -1 1 0 0 0 0 1 0 0 0 0 -1 0 -1 -1 0 -1 1 0 1 0 0 -1 0 1 -1 0 0 0 -1 0 0 -1 0 0
  ret = (ret << 1) | (curr >= 0 ? 0x0U : 0x1U);
  // row 9
  curr = 0 + data[0] + data[1] - data[3] - data[4] + data[6] - data[9] - data[11] + data[13] + data[21] + data[22] - data[25] + data[27] + data[34] + data[36] + data[37] + data[43] - data[47] - data[54];
  // Row 9 Rands: 1 1 0 -1 -1 0 1 0 0 -1 0 -1 0 1 0 0 0 0 0 0 0 1 1 0 0 -1 0 1 0 0 0 0 0 0 1 0 1 1 0 0 0 0 0 1 0 0 0 -1 0 0 0 0 0 0 -1 0 0 0 0 0 0 0 0 0
  ret = (ret << 1) | (curr >= 0 ? 0x0U : 0x1U);
  // row 10
  curr = 0 + data[5] + data[7] + data[9] + data[10] - data[11] + data[13] - data[17] + data[18] - data[26] - data[31] + data[32] - data[33] - data[39] + data[43] + data[45] - data[52] - data[53] + data[55] - data[61];
  // Row 10 Rands: 0 0 0 0 0 1 0 1 0 1 1 -1 0 1 0 0 0 -1 1 0 0 0 0 0 0 0 -1 0 0 0 0 -1 1 -1 0 0 0 0 0 -1 0 0 0 1 0 1 0 0 0 0 0 0 -1 -1 0 1 0 0 0 0 0 -1 0 0
  ret = (ret << 1) | (curr >= 0 ? 0x0U : 0x1U);
  // row 11
  curr = 0 - data[3] - data[4] - data[5] - data[6] + data[8] - data[11] + data[12] + data[13] + data[17] - data[19] - data[22] - data[25] - data[27] - data[31] + data[33] - data[34] - data[36] - data[38] - data[46] + data[49] + data[50] - data[52] - data[60] + data[61];
  // Row 11 Rands: 0 0 0 -1 -1 -1 -1 0 1 0 0 -1 1 1 0 0 0 1 0 -1 0 0 -1 0 0 -1 0 -1 0 0 0 -1 0 1 -1 0 -1 0 -1 0 0 0 0 0 0 0 -1 0 0 1 1 0 -1 0 0 0 0 0 0 0 -1 1 0 0
  ret = (ret << 1) | (curr >= 0 ? 0x0U : 0x1U);
  // Total 768 zeros 496 (0.65%) ones 119 (0.15%) minus 153 (0.20%)
  // Ops 272
  return ret;
}
