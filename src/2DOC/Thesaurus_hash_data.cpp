static uint16_t Thesaurus_hash_data(int32_t *data) {
  uint16_t ret = 0U;
  int32_t curr;
  // row 0
  curr = 0 - data[0] + data[1] - data[5] + data[6] + data[8] - data[9] + data[14];
  // Rands: -1 1 0 0 0 -1 1 0 1 -1 0 0 0 0 1 0
  ret = (ret << 1) | (curr > 0 ? 0x1 : 0x0);
  // row 1
  curr = 0 - data[9] + data[10] + data[13] - data[14];
  // Rands: 0 0 0 0 0 0 0 0 0 -1 1 0 0 1 -1 0
  ret = (ret << 1) | (curr > 0 ? 0x1 : 0x0);
  // row 2
  curr = 0 + data[0] - data[7] - data[12] + data[13];
  // Rands: 1 0 0 0 0 0 0 -1 0 0 0 0 -1 1 0 0
  ret = (ret << 1) | (curr > 0 ? 0x1 : 0x0);
  // row 3
  curr = 0 + data[1] - data[4] - data[11] + data[13] + data[14];
  // Rands: 0 1 0 0 -1 0 0 0 0 0 0 -1 0 1 1 0
  ret = (ret << 1) | (curr > 0 ? 0x1 : 0x0);
  // row 4
  curr = 0 + data[5] + data[10] - data[11] - data[15];
  // Rands: 0 0 0 0 0 1 0 0 0 0 1 -1 0 0 0 -1
  ret = (ret << 1) | (curr > 0 ? 0x1 : 0x0);
  // row 5
  curr = 0 - data[0] + data[1] + data[5] - data[7] - data[11] - data[14];
  // Rands: -1 1 0 0 0 1 0 -1 0 0 0 -1 0 0 -1 0
  ret = (ret << 1) | (curr > 0 ? 0x1 : 0x0);
  // row 6
  curr = 0 - data[2] - data[4] + data[6] - data[7] + data[9] - data[10] + data[11] + data[14] - data[15];
  // Rands: 0 0 -1 0 -1 0 1 -1 0 1 -1 1 0 0 1 -1
  ret = (ret << 1) | (curr > 0 ? 0x1 : 0x0);
  // row 7
  curr = 0 - data[6] + data[15];
  // Rands: 0 0 0 0 0 0 -1 0 0 0 0 0 0 0 0 1
  ret = (ret << 1) | (curr > 0 ? 0x1 : 0x0);
  // row 8
  curr = 0 + data[6] + data[7] - data[9] - data[11] + data[12];
  // Rands: 0 0 0 0 0 0 1 1 0 -1 0 -1 1 0 0 0
  ret = (ret << 1) | (curr > 0 ? 0x1 : 0x0);
  // row 9
  curr = 0 - data[0] + data[3] + data[4] - data[7];
  // Rands: -1 0 0 1 1 0 0 -1 0 0 0 0 0 0 0 0
  ret = (ret << 1) | (curr > 0 ? 0x1 : 0x0);
  // row 10
  curr = 0 - data[1] - data[2] - data[3] - data[5];
  // Rands: 0 -1 -1 -1 0 -1 0 0 0 0 0 0 0 0 0 0
  ret = (ret << 1) | (curr > 0 ? 0x1 : 0x0);
  // row 11
  curr = 0 + data[3] - data[4] - data[5] - data[6] + data[7];
  // Rands: 0 0 0 1 -1 -1 -1 1 0 0 0 0 0 0 0 0
  ret = (ret << 1) | (curr > 0 ? 0x1 : 0x0);
  return ret;
}
