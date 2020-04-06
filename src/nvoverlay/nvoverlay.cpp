
#include "nvoverlay.h"

//* fifo_t

fifo_node_t *fifo_node_init() {
  fifo_node_t *fifo_node = (fifo_node_t *)malloc(sizeof(fifo_node_t));
  SYSEXPECT(fifo_node != NULL);
  memset(fifo_node, 0x00, sizeof(fifo_node_t));
  return fifo_node;
}
void fifo_node_free(fifo_node_t *fifo_node) { free(fifo_node); }

fifo_t *fifo_init() {
  fifo_t *fifo = (fifo_t *)malloc(sizeof(fifo_t));
  SYSEXPECT(fifo != NULL);
  memset(fifo, 0x00, sizeof(fifo_t));
  return fifo;
}

void fifo_free(fifo_t *fifo) {
  fifo_node_t *curr = fifo->head;
  uint64_t count = 0UL;
  while(curr) {
    fifo_node_t *next = curr->next;
    fifo_node_free(curr);
    count++;
    curr = next;
  }
  assert(count == fifo->item_count);
  free(fifo);
  return;
}

// This function creates a new node and linked it to the tail
void fifo_insert(fifo_t *fifo, uint64_t value) {
  fifo_node_t *node = fifo_node_init();
  node->value = value;
  if(fifo->head == NULL) {
    assert(fifo->tail == NULL);
    node->next = NULL;
    fifo->head = fifo->tail = node;
  } else {
    assert(fifo->tail != NULL);
    // Insert after tail, read from head
    fifo->tail->next = node;
    fifo->tail = node;
  }
  fifo->item_count++;
  return;
}

//* ht64_t

ht64_node_t *ht64_node_init(uint64_t key, uint64_t value, ht64_node_t *next) {
  ht64_node_t *node = (ht64_node_t *)malloc(sizeof(ht64_node_t));
  SYSEXPECT(node != NULL);
  node->key = key;
  node->value = value;
  node->next = next;
  return node;
}

void ht64_node_free(ht64_node_t *node) { free(node); }

ht64_t *ht64_init() {
  return ht64_init_size(HT64_DEFAULT_INIT_BUCKETS);
}

ht64_t *ht64_init_size(int bucket_count) {
  ht64_t *ht64 = (ht64_t *)malloc(sizeof(ht64_t));
  SYSEXPECT(ht64 != NULL);
  memset(ht64, 0x00, sizeof(ht64_t));
  ht64->bucket_count = bucket_count;
  assert_uint64_power2(ht64->bucket_count, "ht64_t's bucket_mask arg");
  ht64->bucket_mask = ht64->bucket_count - 1;
  ht64->buckets = (ht64_node_t **)malloc(sizeof(ht64_node_t *) * ht64->bucket_count);
  SYSEXPECT(ht64->buckets != NULL);
  memset(ht64->buckets, 0x00, sizeof(ht64_node_t *) * ht64->bucket_count);
  return ht64;
}

void ht64_free_cb(ht64_t *ht64, ht64_free_cb_t cb) {
  uint64_t freed_count = 0;
  for(uint64_t i = 0;i < ht64->bucket_count;i++) {
    ht64_node_t *node = ht64->buckets[i];
    while(node != NULL) {
      ht64_node_t *next = node->next;
      if(cb) cb((void *)node->value); // Cast the uint64_t value to void *
      ht64_node_free(node);
      freed_count++;
      node = next;
    }
  }
  if(freed_count != ht64->item_count) {
    error_exit("Freed item count (%lu) does not equal table size %lu\n", freed_count, ht64->item_count);
  }
  free(ht64->buckets);
  free(ht64);
  return;
}

// This is only used for rehashing
static void _ht64_insert_node(ht64_t *ht64, ht64_node_t *old_node) {
  uint64_t key = old_node->key;
  uint64_t index = ht64_hash_func(key) & ht64->bucket_mask;
  assert(index < ht64->bucket_count);
  ht64_node_t *node = ht64->buckets[index];
  if(node == NULL) {
    ht64->buckets[index] = old_node;
    old_node->next = NULL;
    return;
  }
  while(1) {
    //printf("See %lu\n", node->key);
    assert(node->next == NULL || node->next->key > node->key);
    assert(node->key != key);
    if(node->next == NULL || node->next->key > key) {
      old_node->next = node->next;
      node->next = old_node;
      return;
    }
    node = node->next;
  }
  assert(0);
  return;
}

// Doubles the number of buckets
void ht64_resize(ht64_t *ht64) {
  uint64_t old_bucket_count = ht64->bucket_count;
  ht64->bucket_count <<= 1;
  ht64->bucket_mask = ht64->bucket_count - 1UL;
  ht64_node_t **old_buckets = ht64->buckets;
  // Allocate a new bucket array
  ht64->buckets = (ht64_node_t **)malloc(sizeof(ht64_node_t *) * ht64->bucket_count);
  SYSEXPECT(ht64->buckets != NULL);
  memset(ht64->buckets, 0x00, sizeof(ht64_node_t *) * ht64->bucket_count);
  // Rehash
  for(uint64_t i = 0;i < old_bucket_count;i++) {
    ht64_node_t *node = old_buckets[i];
    while(node) {
      ht64_node_t *next = node->next;
      //printf("rehashing %lu\n", node->key);
      _ht64_insert_node(ht64, node);
      // node->next has been changed
      node = next;
    }
  }
  free(old_buckets);
  return;
}

// This function does not consider table expansion. Internally called to simply add a key-value pair
// into the buckets array
static int _ht64_insert(ht64_t *ht64, uint64_t key, uint64_t value) {
  uint64_t index = ht64_hash_func(key) & ht64->bucket_mask;
  assert(index < ht64->bucket_count);
  ht64_node_t *node = ht64->buckets[index];
  // Corner case: No element or only one element
  if(node == NULL || node->key > key) {
    ht64->buckets[index] = ht64_node_init(key, value, node);
    return 1;
  } 
  // General case: More than 1 elements
  while(1) {
    // Ordering property
    assert(node->next == NULL || node->next->key > node->key);
    if(node->key == key) {
      return 0;
    } else if(node->next == NULL || node->next->key > key) {
      // Evaluate the func arg first, and then assignment
      node->next = ht64_node_init(key, value, node->next);
      return 1;
    }
    node = node->next;
  }
  // This should never happen
  assert(0);
  return 0;
}

// Do not insert if the key already exists; return 0 in this case.
int ht64_insert(ht64_t *ht64, uint64_t key, uint64_t value) {
  if(ht64->item_count / ht64->bucket_count >= HT64_LOAD_FACTOR) {
    ht64_resize(ht64);
  }
  assert(ht64->item_count / ht64->bucket_count <= HT64_LOAD_FACTOR);
  // Return 1 means a new item was created, 0 means not created
  int ret = _ht64_insert(ht64, key, value);
  ht64->item_count += ret; 
  return ret;
}

uint64_t ht64_find(ht64_t *ht64, uint64_t key, uint64_t default_val) {
  uint64_t index = ht64_hash_func(key) & ht64->bucket_mask;
  assert(index < ht64->bucket_count);
  ht64_node_t *node = ht64->buckets[index];
  if(node == NULL) return default_val;
  while(node != NULL) {
    assert(node->next == NULL || node->next->key > node->key);
    //printf("node key %lu\n", node->key);
    if(node->key == key) {
      return node->value;
    }
    node = node->next;
  }
  return default_val;
}

uint64_t ht64_remove(ht64_t *ht64, uint64_t key, uint64_t default_val) {
  uint64_t index = ht64_hash_func(key) & ht64->bucket_mask;
  assert(index < ht64->bucket_count);
  ht64_node_t *node = ht64->buckets[index];
  if(node == NULL) {
    // Corner case: Empty chain
    return default_val;
  } else if(node->key == key) {
    // Corner case: First element is the one to be removed
    ht64->buckets[index] = node->next;
    uint64_t ret = node->value;
    ht64_node_free(node);
    assert(ht64->item_count != 0UL);
    ht64->item_count--;
    return ret;
  }
  // We know the first node must be good, so start iteration with the second node in the chain
  while(node->next != NULL) {
    assert(node->next->key > node->key);
    if(node->next->key == key) {
      ht64_node_t *victim = node->next;
      //printf("key %lu val %lu\n", node->next->key, node->next->value);
      // Note: This must be read before we change node->next
      uint64_t ret = victim->value;
      node->next = node->next->next;
      ht64_node_free(victim);
      assert(ht64->item_count != 0UL);
      ht64->item_count--;
      return ret;
    }
    node = node->next;
  }
  return default_val;
}

void ht64_clear(ht64_t *ht64) {
  for(uint64_t i = 0UL;i < ht64->bucket_count;i++) {
    ht64_node_t *node = ht64->buckets[i];
    ht64->buckets[i] = NULL;
    while(node != NULL) {
      ht64_node_t *next = node->next;
      ht64_node_free(node);
      node = next;
    }
  }
  ht64->item_count = 0UL;
}

void ht64_begin(ht64_t *ht64, ht64_it_t *it) {
  if(ht64->item_count == 0UL) {
    // Corner case: If the table is empty we directly assign end iterator
    it->bucket = ht64->bucket_count;
    it->node = NULL;
  } else {
    it->bucket = 0UL;
    // There must be at least one non-empty bucket
    while(ht64->buckets[it->bucket] == NULL) {
      assert(it->bucket < ht64->bucket_count);
      it->bucket++;
    }
    it->node = ht64->buckets[it->bucket];
    //printf("bucket %lu node key %lu\n", it->bucket, it->node->key);
  }
}

// Does nothing if the iter is already at end
// Invariant: curr always points a valid node, and bucket always less than bucket_count before iter ends
void ht64_next(ht64_t *ht64, ht64_it_t *it) {
  assert(it->bucket <= ht64->bucket_count);
  if(ht64_is_end(ht64, it)) return;
  if(it->node->next == NULL) {
    // Find next bucket that has nodes
    //printf("find next\n");
    do {
      it->bucket++;
      if(it->bucket == ht64->bucket_count) {
        it->node = NULL;
        return; 
      }
    } while(ht64->buckets[it->bucket] == NULL);
    it->node = ht64->buckets[it->bucket];
  } else {
    it->node = it->node->next;
  }
  return;
}

// Remove the current node, and go to the next node
void ht64_rm_next(ht64_t *ht64, ht64_it_t *it) {
  assert(it->bucket <= ht64->bucket_count);
  if(ht64_is_end(ht64, it)) return;
  assert(it->node == ht64->buckets[it->bucket]);
  assert(it->node != NULL);
  ht64_node_t *curr = ht64->buckets[it->bucket];
  // Also update it->node for ht64_it_key()/value()
  ht64->buckets[it->bucket] = it->node = curr->next;
  ht64_node_free(curr);
  assert(ht64->item_count != 0UL);
  ht64->item_count--;
  if(ht64->buckets[it->bucket] == NULL) {
    do {
      it->bucket++;
      if(it->bucket == ht64->bucket_count) {
        it->node = NULL;
        return; 
      }
    } while(ht64->buckets[it->bucket] == NULL);
    it->node = ht64->buckets[it->bucket];
  }
  return;
}

//* mtable_t

static uint8_t mtable_jit_preamble[] = {
  0xEB, 0x08,                   // jmp (8 bit)  -> Offset 1 is the offset
  0x48, 0x89, 0xF8,             // mov rax, rdi
  0xC3,                         // ret
  0x48, 0x31, 0xC0,             // xor rax, rax
  //0x48, 0xFF, 0xC8,             // dec rax -> Just return NULL pointer
  0xC3,                         // ret
  // The following is for checking whether the root is NULL
  0x48, 0x85, 0xFF,             // test rdi, rdi
  0x74, 0x00,                   // jz (8 bit)
};

// This code snippet extracts the index from RSI, using RDI as base, and puts the next
// level pointer into RDI
static uint8_t mtable_jit_next_level[] = {
  0x48, 0x89, 0xF0,       // mov rax, rsi
  0x48, 0xC1, 0xE8, 0xFF, // shr rax, 0xff -> offset 6 shr argument, 8 bits
  0x48, 0x25, 0x34, 0x12, 0x00, 0x00, // and rax, 0x1234 -> offset 9, and constant (sign extension) 4 bytes
  0x48, 0x8B, 0x3C, 0xC7, // mov rdi, [rdi + rax * 8]
  // 0x48, 0x8D, 0x3C, 0xC7 // lea rdi, [rdi + rax * 8] -> For last level we change the offset[-3] into 0x8D
};

static uint8_t mtable_jit_null_check[] = {
  0x48, 0x85, 0xFF,                   // test rdi, rdi
  0x0F, 0x84, 0x00, 0x00, 0x00, 0x00, // jz
};

static uint8_t mtable_jit_last_level[] = {
  0xE9, 0x00, 0x00, 0x00, 0x00,       // Jmp
};

mtable_idx_t *mtable_idx_init(int start_bit, int bits) {
  int start = start_bit;
  int end = start + bits - 1;
  if(start < 0 || start > 63) error_exit("mtable index start must be [0, 63] (see %d)\n", start);
  if(end < 0 || end > 63) error_exit("mtable index end must be [0, 63] (see %d)\n", end);
  if(bits <= 0) error_exit("mtable index bits must be positive (see %d)\n", bits);
  mtable_idx_t *idx = (mtable_idx_t *)malloc(sizeof(mtable_idx_t));
  SYSEXPECT(idx != NULL);
  memset(idx, 0x00, sizeof(mtable_idx_t));
  idx->bits = bits;
  idx->pg_size = sizeof(uint64_t *) * (1 << bits); // Byte size of the current level of index
  idx->rshift = start_bit;
  idx->mask = (0x1UL << bits) - 1; // Mask off higher bits and leave low "size" bits
  return idx;
}
void mtable_idx_free(mtable_idx_t *idx) { free(idx); }

mtable_t *mtable_init() {
  mtable_t *mtable = (mtable_t *)malloc(sizeof(mtable_t));
  SYSEXPECT(mtable != NULL);
  memset(mtable, 0x00, sizeof(mtable_t));
  return mtable;
}

// This function frees the radix tree recursively
static void mtable_tree_free(uint64_t **tree, mtable_idx_t *idx, mtable_free_cb_t cb) {
  assert(idx);
  if(tree == NULL) return;
  int num_entry = 0x1 << idx->bits; // Numer of entries
  mtable_idx_t *next_idx = idx->next;
  if(next_idx != NULL) { // If still has child
    for(int i = 0;i < num_entry;i++) {
      mtable_tree_free((uint64_t **)tree[i], next_idx, cb); // Free next level page, if there is one
    }
  } else {
    // Terminal level - may need to do extra cleanup by calling the cb
    if(cb) {
      for(int i = 0;i < num_entry;i++) {
        if(tree[i]) cb(tree[i]);
      }
    }
  }
  free(tree);
}

void mtable_free_cb(mtable_t *mtable, mtable_free_cb_t cb) {
  if(mtable->idx) mtable_tree_free(mtable->root, mtable->idx, cb); // First free the radix tree
  mtable_idx_t *curr = mtable->idx;
  while(curr) {
    mtable_idx_t *next = curr->next;
    free(curr);
    curr = next;
  }
  if(mtable->lookup_cb) free((void *)mtable->lookup_cb);
  free(mtable);
}

void mtable_free(mtable_t *mtable) { mtable_free_cb(mtable, NULL); }

void mtable_idx_add(mtable_t *mtable, int start_bit, int bits) {
  mtable_idx_t *idx = mtable_idx_init(start_bit, bits);
  idx->next = NULL;
  idx->level = 0;
  mtable_idx_t *curr = mtable->idx;
  if(curr == NULL) {
    mtable->idx = idx;
  } else {
    while(curr) {
      idx->level++;
      int start = idx->rshift;
      int end = start + idx->bits - 1;
      int curr_start = curr->rshift;
      int curr_end = curr_start + curr->bits - 1;
      if(!(start > curr_end || end < curr_start)) {
        error_exit("Index range overlaps: new [%d, %d]; curr [%d, %d]\n", start, end, curr_start, curr_end);
      }
      if(curr->next == NULL) {
        curr->next = idx;
        break;
      }
      curr = curr->next;
    }
  }
  mtable->idx_size++;
  return;
}

void mtable_idx_print(mtable_t *mtable) {
  mtable_idx_t *curr = mtable->idx;
  if(!curr) printf("[EMPTY INDEX]\n");
  while(curr) {
    printf("Level %d; mask 0x%lX; rshift %d; bits %d; pg size %d (%d entries)\n", 
      curr->level, curr->mask, curr->rshift, curr->bits, curr->pg_size, curr->pg_size / (int)sizeof(void **));
    curr = curr->next;
  }
  if(mtable->lookup_cb) {
    printf("JIT lookup ENABLED; JIT size %d bytes\n", mtable->jit_lookup_size);
  } else {
    printf("JIT lookup DISABLED\n");
  }
}

void mtable_conf_print(mtable_t *mtable) {
  printf("---------- mtable_t ----------\n");
  mtable_idx_print(mtable);
}

// Returns the pointer to the value slot
// Always insert pages down the path even if the key does not exist
static void **_mtable_insert(mtable_t *mtable, uint64_t key) {
  mtable_idx_t *idx = mtable->idx;
  uint64_t ***prev = &mtable->root;
  while(idx) {
    int slice = (int)((key >> idx->rshift) & idx->mask);
    assert(slice >= 0 && slice < (0x1 << idx->bits));
    uint64_t **curr = *prev;
    if(curr == NULL) {
      *prev = curr = (uint64_t **)malloc(idx->pg_size);
      SYSEXPECT(curr != NULL);
      memset(curr, 0x00, idx->pg_size);   // By default all pointers are NULL in a new leaf
      mtable->page_count++;
      mtable->size += (uint64_t)idx->pg_size; // Do not count the last 8 bytes
    }
    prev = (uint64_t ***)(curr + slice);
    idx = idx->next;
  }
  // At this point prev prints to the last level pointer
  return (void **)prev;
}

// Wrapper on JIT version and C version of insert
// If JIT is not enabled (func pointer being NULL) we just call into the regular insert
void **mtable_insert(mtable_t *mtable, uint64_t key) {
  if(mtable->lookup_cb == NULL) return _mtable_insert(mtable, key);
  void **ret = mtable->lookup_cb((void **)mtable->root, key);
  if(ret == NULL) return _mtable_insert(mtable, key);
  return ret;
}

// This function does not use JIT function, since it does not return the pointer
// Returns NULL if the key does not exist, or if the value is NULL
// The caller should make sure that NULL value can be distinguished properly
void *mtable_find(mtable_t *mtable, uint64_t key) {
  mtable_idx_t *idx = mtable->idx;
  uint64_t **prev = mtable->root;
  while(idx) {
    if(prev == NULL) return NULL;
    int slice = (int)((key >> idx->rshift) & idx->mask);
    assert(slice >= 0 && slice < (0x1 << idx->bits));
    prev = (uint64_t **)(prev[slice]);
    idx = idx->next;
  }
  // At this point prev is the last level pointer
  return (void *)prev;
}

#ifdef MTABLE_ENABLE_JIT
// JIT compiles the index structure into a function to avoid accessing the index every time
// Call this any time after building the index
void mtable_jit_lookup(mtable_t *mtable) {
  // First deal with repeated calls to this function
  if(mtable->lookup_cb != NULL) {
    printf("WARNING: Already having a JIT version of the lookup function\n");
    free((void *)mtable->lookup_cb);
    mtable->lookup_cb = NULL;
  }
  // The 5 bytes are jmp
  int jit_size = (int)\
    (sizeof(mtable_jit_preamble) + \
    (sizeof(mtable_jit_next_level) + sizeof(mtable_jit_null_check)) * (mtable->idx_size - 1) + \
    sizeof(mtable_jit_next_level) + sizeof(mtable_jit_last_level));
  uint8_t *jit_mem = (uint8_t *)malloc(jit_size);
  SYSEXPECT(jit_mem != NULL);
  void *base = page_align_down(jit_mem);           // Aligned page address of the jit region
  int pages = num_aligned_page(jit_mem, jit_size); // Number of pages in round figures
  // Call the OS to make the heap executable
  int vmm_ret = mprotect(base, pages * UTIL_PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC);
  SYSEXPECT(vmm_ret == 0);
  int offset = 0;
  mtable_idx_t *idx = mtable->idx;
  // The following are relative to buffer pointer
  const int ret_normal_offset = 2;
  const int ret_retry_offset = 6;
  // First, copy the jump and return code to the beginning
  memcpy(jit_mem + offset, mtable_jit_preamble, sizeof(mtable_jit_preamble));
  offset += sizeof(mtable_jit_preamble);
  //int32_t jmp_offset = -(offset - ret_retry_offset);
  //memcpy(jit_mem + offset - 4, &jmp_offset, sizeof(int32_t)); // Overwrite retry jump address
  int32_t jmp_offset = -(offset - ret_retry_offset);
  memcpy(jit_mem + offset - 1, &jmp_offset, sizeof(int8_t)); // Overwrite retry jump address
  while(idx) {
    // x86-64 only supports 32 bit sign extended imm on AND instruction with 64 bit register
    // So we limit the mask to be 32 bit, and the highest bit must be 1
    if((idx->mask & 0x80000000UL) != 0UL) {
      error_exit("Only support mask < 0x80000000 (see 0x%lX)\n", idx->mask);
    }
    memcpy(jit_mem + offset, mtable_jit_next_level, sizeof(mtable_jit_next_level));
    memcpy(jit_mem + offset + 6, &idx->rshift, 1);
    memcpy(jit_mem + offset + 9, &idx->mask, 4);
    offset += sizeof(mtable_jit_next_level);
    if(idx->next) {
      memcpy(jit_mem + offset, mtable_jit_null_check, sizeof(mtable_jit_null_check));
      offset += sizeof(mtable_jit_null_check);
      jmp_offset = -(offset - ret_retry_offset);          // 2's complement
      memcpy(jit_mem + offset - 4, &jmp_offset, sizeof(int32_t)); // Overwrite retry jump address
    } else {
      jit_mem[offset - 3] = 0x8D; // Change MOV to LEA, since we return address of the slot, not the value
      memcpy(jit_mem + offset, mtable_jit_last_level, sizeof(mtable_jit_last_level));
      offset += sizeof(mtable_jit_last_level);
      jmp_offset = -(offset - ret_normal_offset);          // 2's complement
      memcpy(jit_mem + offset - 4, &jmp_offset, sizeof(int32_t)); // Overwrite success return jump address
    }
    idx = idx->next;
  }
  mtable->jit_lookup_size = jit_size;
  mtable->lookup_cb = (mtable_jit_lookup_t)jit_mem;
  return;
}
#else 
void mtable_jit_lookup(mtable_t *mtable) {
  (void)mtable;
  return;
}
#endif

void mtable_jit_lookup_print(mtable_t *mtable) {
  if(mtable->lookup_cb == NULL) {
    printf("There is not JIT function to print\n");
    return;
  }
  assert(mtable->jit_lookup_size > 0);
  uint8_t *p = (uint8_t *)mtable->lookup_cb;
  printf("jit size: %d\n", mtable->jit_lookup_size);
  for(int i = 0;i < mtable->jit_lookup_size;i++) {
    printf("%02X ", (int)p[i]);
  }
  putchar('\n');
  return;
}

void _mtable_print(mtable_t *mtable, uint64_t **tree, mtable_idx_t *idx) {
  int num_entry = 0x1 << idx->bits;
  //printf("tree = %p\n", tree);
  if(tree == NULL) return; // Do not print empty tree
  //printf("idx->next: %p; num entry = %d\n", idx->next, num_entry);
  if(idx->next) {   // If not last level
    for(int i = 0;i < num_entry;i++) {
      //printf("tree[i] = %p\n", tree[i]);
      _mtable_print(mtable, (uint64_t **)tree[i], idx->next);
    }
  } else {
    printf("Level %d:", idx->level);
    for(int i = 0;i < num_entry;i++) {
      if(tree[i]) printf(" 0x%lX", (uint64_t)tree[i]);
    }
    putchar('\n');
  }
}

// Only prints in-place data as HEX strings
void mtable_print(mtable_t *mtable) {
  printf("----- mtable_print -----\n");
  _mtable_print(mtable, mtable->root, mtable->idx);
  printf("Pages: %lu; Total size: %lu\n", mtable_get_page_count(mtable), mtable_get_size(mtable));
}

// For each valid entry, apply the call back function. NULL entries will not be called since they
// carry no information.
static void _mtable_traverse(void **tree, mtable_idx_t *idx, matble_traverse_cb_t cb, uint64_t key, void *arg) {
  if(tree == NULL) return;
  int num_entry = 0x1 << idx->bits;
  if(idx->next) {
    for(int i = 0;i < num_entry;i++) {
      _mtable_traverse((void **)tree[i], idx->next, cb, key | ((uint64_t)i) << idx->rshift, arg);
    }
  } else {
    for(int i = 0;i < num_entry;i++) {
      if(tree[i] != NULL) cb(key | ((uint64_t)i) << idx->rshift, tree[i], arg);
    }
  }
}

void mtable_traverse(mtable_t *mtable, matble_traverse_cb_t cb, void *arg) {
  _mtable_traverse((void **)mtable->root, mtable->idx, cb, 0UL, arg);
}

//* bitmap64_t

void bitmap64_print_bitstr(bitmap64_t *bitmap) {
  uint64_t x = bitmap->x;
  for(int i = 0;i < 64;i++) {
    if(x & 0x8000000000000000UL) putchar('1');
    else putchar('0');
    x <<= 1;
    if((i + 1) % 8 == 0 && i != 63) putchar(' ');
  }
  return;
}

// omt_t

omt_t *omt_init() {
  omt_t *omt = (omt_t *)malloc(sizeof(omt_t));
  SYSEXPECT(omt != NULL);
  memset(omt, 0x00, sizeof(omt_t));
  omt->mtable = mtable_init();
  // 5 level mapping table
  mtable_idx_add(omt->mtable, 39, 9);
  mtable_idx_add(omt->mtable, 30, 9);
  mtable_idx_add(omt->mtable, 21, 9);
  mtable_idx_add(omt->mtable, 12, 9);
  mtable_idx_add(omt->mtable, 6, 6);
  return omt;
}

void omt_free(omt_t *omt) {
  mtable_free(omt->mtable);
  free(omt);
}

// Migrate one line from the overlay page; called by overlay
// Returns the original overlay epoch in the mapping table (could be NULL meaning there is no prev data)
overlay_epoch_t *omt_merge_line(omt_t *omt, overlay_epoch_t *overlay_epoch, uint64_t line_addr) {
  uint64_t before_pages = mtable_get_page_count(omt->mtable);
  // Previously stored value is the overlay epoch the cache line comes from
  overlay_epoch_t **overlay_epoch_p = (overlay_epoch_t **)mtable_insert(omt->mtable, line_addr);
  uint64_t after_pages = mtable_get_page_count(omt->mtable);
  // Update writes for this merge operation - The minimum number of writes is one, since 
  // we always update the leaf node. If new pages are inserted we also add writes for 
  // updating new pages
  omt->write_count += (after_pages - before_pages + 1);
  omt->merge_line_count++; // Incremented for every merge line
  // Swap out old and write new
  overlay_epoch_t *old_overlay_epoch = *overlay_epoch_p;
  *overlay_epoch_p = overlay_epoch;
  // There is no previous data, and now we merge one line, so working set size + 64 bytes
  if(old_overlay_epoch == NULL) {
    omt->working_set_size += UTIL_CACHE_LINE_SIZE;
  }
  return old_overlay_epoch;
}

// Call back function used for tracersal
void omt_selfcheck_cb(uint64_t line_addr, void *node, void *arg) {
  (*(uint64_t *)arg)++; // Each call back instance represents a valid entry
  (void)line_addr; (void)node;
}

void omt_selfcheck(omt_t *omt) {
  // Check write count
  if(omt->write_count != omt->merge_line_count + mtable_get_page_count(omt->mtable)) {
    error_exit("Inconsistent write count and merge/page count: %lu %lu\n",
      omt->write_count, omt->merge_line_count + mtable_get_page_count(omt->mtable));
  }
  // Check working set size
  uint64_t leaf_count = 0UL;
  mtable_traverse(omt->mtable, omt_selfcheck_cb, &leaf_count);
  if(leaf_count * UTIL_CACHE_LINE_SIZE != omt->working_set_size) {
    error_exit("Inconsistent working set size between mtable and recorded size: %lu %lu\n",
      leaf_count * UTIL_CACHE_LINE_SIZE, omt->working_set_size);
  }
  return;
}

void omt_conf_print(omt_t *omt) {
  printf("---------- omt_t ----------\n");
  mtable_idx_print(omt->mtable);
  return;
}

void omt_stat_print(omt_t *omt) {
  printf("---------- omt_t ----------\n");
  printf("Table pages %lu table size %lu bytes merge lines %lu working set size %lu bytes\n", 
    mtable_get_page_count(omt->mtable), mtable_get_size(omt->mtable),
    omt->merge_line_count, omt->working_set_size);
  printf("Writes %lu (table merging)\n", omt->write_count);
  return;
}

//* cpu_t

const char *core_cap_status_names[2] = {
  "ACTIVE", "HALTED",
};

core_t *core_init() {
  core_t *core = (core_t *)malloc(sizeof(core_t));
  SYSEXPECT(core != NULL);
  memset(core, 0x00, sizeof(core_t));
  core->cap_status = CORE_CAP_STATUS_ACTIVE;
  return core;
}

void core_free(core_t *core) { 
  free(core); 
}

cpu_t *cpu_init(int core_count, cpu_tag_walk_cb_t cb) {
  cpu_t *cpu = (cpu_t *)malloc(sizeof(cpu_t));
  SYSEXPECT(cpu != NULL);
  memset(cpu, 0x00, sizeof(cpu_t));
  cpu->core_count = core_count;
  cpu->cores = (core_t *)malloc(sizeof(core_t) * core_count);
  SYSEXPECT(cpu->cores);
  memset(cpu->cores, 0x00, sizeof(core_t) * core_count);
  cpu->tag_walk_cb = cb;
  return cpu;
}

void cpu_free(cpu_t *cpu) {
  // Free tag arrays (one dimension)
  for(int i = 0;i < CPU_TAG_MAX;i++) {
    if(cpu->tag_arrays[i].tags != NULL) free(cpu->tag_arrays[i].tags);
  }
  free(cpu->cores);
  free(cpu);
}

// Called when the core receives a version, not necessarily a dirty one
void cpu_core_recv(cpu_t *cpu, int id, uint64_t version) {
  assert(id >= 0 && id < cpu->core_count);
  core_t *core = cpu->cores + id;
  if(version > core->epoch) {
    // Update statistics
    cpu->skip_epoch_count += ((version == (core->epoch + 1)) ? 0 : 1);
    cpu->coherence_advance_count++;
    cpu->total_advance_count++;
    core->epoch = version;
    core->epoch_store_count = 0UL;
  }
  // TODO: May add other behavior (register dump?)
  return;
}

// External events other than coherence (coherence does not call this)
// Unconditionally advance the epoch
void cpu_advance_epoch(cpu_t *cpu, int id) {
  assert(id >= 0 && id < cpu->core_count);
  core_t *core = cpu->cores + id;
  core->epoch++;
  core->epoch_store_count = 0UL;
  cpu->total_advance_count++;
  return;
}

// Computes and returns the minimum local epoch
uint64_t cpu_min_epoch(cpu_t *cpu) {
  uint64_t min_epoch = -1UL;
  for(int i = 0;i < cpu->core_count;i++) {
    if(cpu->cores[i].epoch < min_epoch) {
      min_epoch = cpu->cores[i].epoch;
    }
  }
  return min_epoch;
}

// Returns the max epoch. This is used for computing skew
uint64_t cpu_max_epoch(cpu_t *cpu) {
  uint64_t max_epoch = 0UL;
  for(int i = 0;i < cpu->core_count;i++) {
    if(cpu->cores[i].epoch > max_epoch) {
      max_epoch = cpu->cores[i].epoch;
    }
  }
  return max_epoch;
}

void cpu_tag_init(cpu_t *cpu, int level, int sets, int ways) {
  assert(popcount_int32(sets) == 1); // Input function should guarantee this
  if(level < 0 || level >= CPU_TAG_MAX) {
    error_exit("Level exceeds number of tag arrays: %d (see %d)\n", CPU_TAG_MAX, level);
  }
  cpu_tag_t *tag_array = &cpu->tag_arrays[level];
  uint64_t sz = sets * ways * cpu->core_count * sizeof(ver_t *); // It is indiced in this order: core, set, way
  tag_array->tags = (ver_t **)malloc(sz);
  SYSEXPECT(tag_array->tags != NULL);
  memset(tag_array->tags, 0x00, sz);
  tag_array->sets = sets;
  tag_array->ways = ways;
  tag_array->mask = (uint64_t)sets - 1;
  tag_array->core_ver_count = sets * ways;
  tag_array->set_bits = util_log2_int32(sets, "sets");
  return;
}

// This function inserts the ver_t pointer into tag table; It does not use the sharer list
void cpu_tag_insert(cpu_t *cpu, int level, int id, ver_t *ver) {
  uint64_t addr = ver->addr;
  ver_t **begin = cpu_addr_tag_begin(cpu, level, id, addr);
  ver_t **end = cpu_addr_tag_end(cpu, level, begin);
  assert(begin < end);
  while(begin != end) {
    if(*begin == ver) {
      error_exit("Address inserting into the cache already exists (0x%lX level %d core %d)\n", addr, level, id);
    } else if(*begin == NULL) {
      *begin = ver;
      return;
    }
    begin++;
  }
  error_exit("Did not find empty slot - missing evictions? (0x%lX level %d core %d)\n", addr, level, id);
  return;
}

void cpu_tag_remove(cpu_t *cpu, int level, int id, ver_t *ver) {
  uint64_t addr = ver->addr;
  ver_t **begin = cpu_addr_tag_begin(cpu, level, id, addr);
  ver_t **end = cpu_addr_tag_end(cpu, level, begin);
  assert(begin < end);
  while(begin != end) {
    if(*begin == ver) {
      *begin = NULL;
      return;
    }
    begin++;
  }
  // If control flow reachs here we did not find the tag
  error_exit("Did not find the tag to be removed (0x%lX level %d core %d)\n", addr, level, id);
  return;
}

// Return NULL if the addr is not found in the given core and level tag array
ver_t **cpu_tag_find(cpu_t *cpu, int level, int core, uint64_t line_addr) {
  ver_t **begin = cpu_addr_tag_begin(cpu, level, core, line_addr);
  ver_t **end = cpu_addr_tag_end(cpu, level, begin);
  assert(begin < end);
  while(begin != end) {
    ver_t *ver = *begin;
    if(ver != NULL && ver->addr == line_addr) {
      return begin;
    }
    begin++;
  }
  return NULL;
}

// Operations on the tag array; This will be called in the runtime by the vtable
// When the op is CPU_TAG_OP_SET, the version object should be before the bit is actually set on the version bitmap
void cpu_tag_op(cpu_t *cpu, int op, int level, int core, ver_t *ver) {
  switch(op) {
    case CPU_TAG_OP_ADD: {
      cpu_tag_insert(cpu, level, core, ver);
    } break;
    case CPU_TAG_OP_REMOVE: {
      cpu_tag_remove(cpu, level, core, ver);
    } break;
    case CPU_TAG_OP_CLEAR:
    case CPU_TAG_OP_SET: {
      bitmap64_t *bitmap;
      if(level == CPU_TAG_L1) {
        bitmap = &ver->l1_bitmap;
      } else {
        assert(level == CPU_TAG_L2);
        bitmap = &ver->l2_bitmap;
      }
      // Iterate over every core that has the version, remove them
      int pos = -1;
      while((pos = bitmap64_iter(bitmap, pos)) != -1) {
        cpu_tag_remove(cpu, level, pos, ver);
      }
      // Only insert the tag if it is SET operation
      if(op == CPU_TAG_OP_SET) cpu_tag_insert(cpu, level, core, ver);
    } break;
    default: {
      error_exit("Unknown op code: %d\n", op);
    } break;
  }
}

// Evict all dirty versions < target epoch (but not equal to) to the NVM
// This may result in ownership transfer, but sharer's list is not modified
void cpu_tag_walk(cpu_t *cpu, int id, uint64_t cycle, uint64_t target_epoch) {
  ver_t **begin = cpu_core_tag_begin(cpu, CPU_TAG_L2, id);
  ver_t **end = cpu_core_tag_end(cpu, CPU_TAG_L2, begin);
  core_t *core = cpu_get_core(cpu, id);
  //printf("%p %p diff %lu\n", begin, end, end - begin);
  while(begin != end) {
    ver_t *ver = *begin;
    if(ver == NULL) { 
      begin++; 
      continue; 
    }
    //printf("Found ver\n");
    if(ver->owner == OWNER_L1) {
      // Case 1: Ownership L1
      assert(ver->l1_state == STATE_M);
      assert(vtable_l1_sharer(ver) == id);
      if(ver->l1_ver < target_epoch) {
        // Case 1.1: L1 is dirty and version < target epoch
        // Both L1 and L2 (if dirty) are written back, ownership transfers to LLC
        ver->owner = OWNER_OTHER;
        ver->other_ver = ver->l1_ver;
        ver->l1_state = STATE_S;
        cpu->tag_walk_cb(cpu->nvoverlay, ver->addr, id, ver->l1_ver, cycle);
        core->tag_walk_evict_count++;
        // If l2 ver equals l1 ver just discard it
        if(ver->l2_state == STATE_M && ver->l2_ver != ver->l1_ver) {
          ver->l2_state = STATE_S;
          cpu->tag_walk_cb(cpu->nvoverlay, ver->addr, id, ver->l2_ver, cycle);
          core->tag_walk_evict_count++;
        }
      } else if(ver->l2_ver < target_epoch && ver->l2_state == STATE_M) {
        // Case 1.2 L2 has a dirty version < target epoch
        // Only evict L2 version, L1 is still the owner -> only case we do not xfter ownership
        assert(ver->l2_ver < ver->l1_ver);
        ver->l2_state = STATE_S;
        cpu->tag_walk_cb(cpu->nvoverlay, ver->addr, id, ver->l2_ver, cycle);
        core->tag_walk_evict_count++;
      }
    } else if(ver->owner == OWNER_L2) {
      // Case 2: Ownership L2
      assert(ver->l2_state == STATE_M);
      assert(ver->l1_state != STATE_M);
      assert(vtable_l2_sharer(ver) == id);
      if(ver->l2_ver < target_epoch) {
        ver->owner = OWNER_OTHER;
        ver->other_ver = ver->l2_ver;
        ver->l2_state = STATE_S;
        cpu->tag_walk_cb(cpu->nvoverlay, ver->addr, id, ver->l2_ver, cycle);
        core->tag_walk_evict_count++;
      }
    }
    begin++;
  }
  return;
}

// Returns min_epoch or 0UL if the cache is empty
// The caller will only merge epoch less than the return value
uint64_t cpu_tag_walk_passive(cpu_t *cpu, int id) {
  ver_t **begin = cpu_core_tag_begin(cpu, CPU_TAG_L2, id);
  ver_t **end = cpu_core_tag_end(cpu, CPU_TAG_L2, begin);
  uint64_t min_epoch = -1UL; // This is the maximum value
  while(begin != end) {
    ver_t *ver = *begin;
    if(ver == NULL) { 
      begin++; 
      continue; 
    }
    // Find min dirty version in L1 and L2
    uint64_t ver_min = -1UL; // By default it will not change ver_min, so setting it to largest
    if(ver->owner != OWNER_OTHER) {
      assert(ver->l2_state != STATE_I); // The version must be in L2
      // L1 ver and L2 ver and states are available
      if(ver->l2_state == STATE_M) {
        // L1-L2 state being M-M or S-M or I-M
        ver_min = ver->l2_ver;
      } else if(ver->l1_state == STATE_M) {
        // L1-L2 state being M-S
        ver_min = ver->l1_ver;
      }
    }
    // Update min epoch using current address's min
    if(ver_min < min_epoch) min_epoch = ver_min;
    begin++;
  }
  if(min_epoch == -1UL) min_epoch = 0UL;
  return min_epoch;
}

// Report error and exit if inconsistency is found
void cpu_tag_selfcheck(cpu_t *cpu, vtable_t *vtable, int level, int core) {
  if(level < 0 || level >= CPU_TAG_MAX) {
    error_exit("Invalid tag array level (see %d)\n", level);
  } else if(core < 0 || core >= cpu->core_count) {
    error_exit("Invalid core ID (see %d)\n", core);
  }
  ht64_t *ht = ht64_init();
  ver_t **begin = cpu_core_tag_begin(cpu, level, core);
  ver_t **end = cpu_core_tag_end(cpu, level, begin);
  uint64_t ver_count = 0UL;
  while(begin != end) {
    ver_t *ver = *begin;
    if(ver == NULL) {
      begin++;
      continue;
    }
    ver_count++;
    // Check whether the ver object is mapped by vtable
    ver_t *ret_ver = vtable_find(vtable, ver->addr);
    if(ret_ver == NULL) {
      error_exit("Version 0x%lX is not mapped by vtable\n", ver->addr);
    } else if(ret_ver != ver) {
      error_exit("Version 0x%lX is mapped by vtable using a different object\n", ver->addr);
    }
    // Basic check: If ver is in the tag array, it must also be in the bitmap
    bitmap64_t *bitmap;
    if(level == CPU_TAG_L1) bitmap = &ver->l1_bitmap;
    else bitmap = &ver->l2_bitmap;
    if(bitmap64_is_set(bitmap, core) == 0) {
      error_exit("Version 0x%lX is not set in core %d level %d\n", ver->addr, core, level);
    }
    // Ownership and bitmap consistency test
    if(ver->owner == OWNER_L1) {
      // If owner is L1, and we see them, then popcount must be 1, and both levels current core
      if(bitmap64_popcount(&ver->l1_bitmap) != 1 || bitmap64_popcount(&ver->l2_bitmap) != 1) {
        error_exit("Version 0x%lX is shared by more than one L1 or L2 (core %d level %d)\n",
          ver->addr, core, level);
      } else if(bitmap64_pos(&ver->l1_bitmap) != core || bitmap64_pos(&ver->l2_bitmap) != core) {
        error_exit("Version 0x%lX is shared by wrong L1 or L2 (core %d level %d)\n",
          ver->addr, core, level);
      }
    } else if(ver->owner == OWNER_L2) {
      if(bitmap64_popcount(&ver->l2_bitmap) != 1) {
        error_exit("Version 0x%lX is shared by more than one L2 (core %d level %d)\n",
          ver->addr, core, level);
      } else if(bitmap64_pos(&ver->l2_bitmap) != core) {
        error_exit("Version 0x%lX is shared by wrong L2 (core %d level %d)\n",
          ver->addr, core, level);
      }
    } 
    // Note: We insert address of the ver object here
    int insert_ret = ht64_insert(ht, (uint64_t)ver, 1UL);
    if(insert_ret == 0) {
      error_exit("Version 0x%lX already exists in core %d level %d\n", ver->addr, core, level);
    }
    begin++;
  }
  assert(ver_count == ht64_get_item_count(ht));
  //printf("Scan tag version count %lu\n", ver_count);
  // Then iterate over all versions in the vtable, and for each each ver in the current cache
  // according to the bitmap, make sure they are all in the hash table
  // Otherwise, we have versions missing from the tag array
  ht64_it_t it;
  uint64_t total_count = 0; // Total number of versions we iterate through
  uint64_t count = 0; // Total number of versions in the current level and core
  ht64_begin(vtable_get_vers(vtable), &it);
  while(ht64_is_end(vtable_get_vers(vtable), &it) == 0) {
    total_count++;
    uint64_t addr = ht64_it_key(&it);
    ver_t *ver = (ver_t *)ht64_it_value(&it);
    assert(ver->addr == addr);
    bitmap64_t *bitmap;
    if(level == CPU_TAG_L1) bitmap = &ver->l1_bitmap;
    else bitmap = &ver->l2_bitmap;
    // Does not matter - this version does not exist in the core
    if(bitmap64_is_set(bitmap, core) == 0) {
      ht64_next(vtable_get_vers(vtable), &it);
      continue;
    }
    count++;
    // If the version object exists in the core, then it must be inserted into the ht
    // Otherwise we are missing a version
    uint64_t find_ret = ht64_find(ht, (uint64_t)ver, 0UL);
    if(find_ret == 0UL) {
      error_exit("Version 0x%lX is in vtable, but missing in core %d level %d\n", ver->addr, core, level);
    }
    ht64_next(vtable_get_vers(vtable), &it);
  }
  //printf("vtable versions %lu; bitmap versions %lu; core %d level %d tag scan versions %lu\n", 
  //  total_count, count, core, level, ver_count);
  if(ver_count != count) {
    error_exit("Version counts are inconsistent (tag scan count %lu vtable count %lu)\n",
      count, ver_count);
  }
  ht64_free(ht);
  (void)ver_count;
  return;
}

void cpu_tag_selfcheck_core(cpu_t *cpu, struct vtable_struct_t *vtable, int core) {
  for(int level = CPU_TAG_L1; level < CPU_TAG_MAX;level++) {
    cpu_tag_selfcheck(cpu, vtable, level, core);
  }
  // Check consistency between cores - Those in L1 must also be in L2
  ver_t **begin, **end;
  begin = cpu_core_tag_begin(cpu, CPU_TAG_L1, core);
  end = cpu_core_tag_end(cpu, CPU_TAG_L1, begin);
  while(begin != end) {
    ver_t *ver = *begin;
    if(ver == NULL) {
      begin++;
      continue;
    }
    ver_t **find_ret = cpu_tag_find(cpu, CPU_TAG_L2, core, ver->addr);
    if(find_ret == NULL) {
      error_exit("Inclusion property broken: Version 0x%lX found in L1 but not in L2\n", ver->addr);
    }
    begin++;
  }
  return;
}

void cpu_tag_selfcheck_all(cpu_t *cpu, vtable_t *vtable) {
  for(int core = 0;core < cpu->core_count;core++) {
    cpu_tag_selfcheck_core(cpu, vtable, core);
  }
  return;
}

void cpu_tag_print(cpu_t *cpu, int level) {
  cpu_tag_t *tag_array = &cpu->tag_arrays[level];
  int num = tag_array->sets * tag_array->ways * cpu->core_count;
  for(int i = 0;i < num;i++) {
    if(tag_array->tags[i] != NULL) {
      int core = i / tag_array->core_ver_count;
      int offset = i % tag_array->core_ver_count;
      int set = offset / tag_array->ways;
      int way = offset % tag_array->ways;
      printf("Core %d set %d way %d addr 0x%lX\n", core, set, way, tag_array->tags[i]->addr);
    }
  }
  return;
}

int cpu_tag_get_ways(cpu_t *cpu, int level) {
  assert(level >= 0 && level < CPU_TAG_MAX);
  cpu_tag_t *tag_array = &cpu->tag_arrays[level];
  assert(tag_array->tags != NULL);
  return tag_array->ways;
}

int cpu_tag_get_sets(cpu_t *cpu, int level) {
  assert(level >= 0 && level < CPU_TAG_MAX);
  cpu_tag_t *tag_array = &cpu->tag_arrays[level];
  assert(tag_array->tags != NULL);
  return tag_array->sets;
}

void cpu_selfcheck(cpu_t *cpu) {
  uint64_t total_inst_count = 0UL, total_cycle_count = 0UL;
  for(int i = 0;i < cpu->core_count;i++) {
    core_t *core = cpu_get_core(cpu, i);
    total_inst_count += core->last_inst_count;
    total_cycle_count += core->last_cycle_count;
  }
  if(total_inst_count != cpu->total_inst_count) {
    error_exit("Inconsistent inst count from cores and cpu (%lu %lu)\n", total_inst_count, cpu->total_inst_count);
  } else if(total_inst_count == 0UL) {
    printf("Inst count == 0 - Did you instrument the workload source with NVOVERLAYY_SIM_END()?\n");
  }
  if(total_cycle_count != cpu->total_cycle_count) {
    error_exit("Inconsistent cycle count from cores and cpu (%lu %lu)\n", total_cycle_count, cpu->total_cycle_count);
  } else if(total_cycle_count == 0UL) {
    printf("Cycle count == 0 - Did you instrument the workload source with NVOVERLAYY_SIM_END()?\n");
  }
  return;
}

void cpu_conf_print(cpu_t *cpu) {
  printf("---------- cpu_t ----------\n");
  printf("Cores: %d\n", cpu->core_count);
  for(int i = 0;i < CPU_TAG_MAX;i++) {
    cpu_tag_t *tag_array = &cpu->tag_arrays[i];
    printf("Array level %d sets %d ways %d (total size %lu bytes) mask 0x%lX set bits %d\n",
      i, tag_array->sets, tag_array->ways, tag_array->core_ver_count * UTIL_CACHE_LINE_SIZE,
      tag_array->mask, tag_array->set_bits);
  }
  return;
}

void cpu_stat_print(cpu_t *cpu, int verbose) {
  printf("---------- cpu_t ----------\n");
  printf("total advance %lu coherence %lu skip %lu\n", 
    cpu->total_advance_count, cpu->coherence_advance_count, cpu->skip_epoch_count);
  printf("epoch min %lu max %lu\n", cpu_min_epoch(cpu), cpu_max_epoch(cpu));
  uint64_t total_epoch_store_count = 0UL, total_store_count = 0UL;
  uint64_t total_tag_walk_evict_count = 0UL, total_tag_walk_count = 0UL;
  uint64_t total_inst_count = 0UL, total_cycle_count = 0UL;
  uint64_t max_cycle_count = 0UL;
  int active_count = 0;
  for(int i = 0;i < cpu->core_count;i++) {
    core_t *core = cpu_get_core(cpu, i);
    if(verbose) {
      printf("  Core %d: epoch st %lu total st %lu last walk %lu walk evict %lu walks %lu inst %lu cap_status %s\n", 
        i, core->epoch_store_count, core->total_store_count, core->last_walk_epoch, core->tag_walk_evict_count,
        core->tag_walk_count, core->last_inst_count,
        core_cap_status_names[core->cap_status]);
    }
    // Aggregate to global stat
    total_epoch_store_count += core->epoch_store_count;
    total_store_count += core->total_store_count;
    total_tag_walk_evict_count += core->tag_walk_evict_count;
    total_tag_walk_count += core->tag_walk_count;
    total_inst_count += core->last_inst_count;
    total_cycle_count += core->last_cycle_count;
    if(core->last_cycle_count > max_cycle_count) {
      max_cycle_count = core->last_cycle_count;
    }
    if(core->cap_status == CORE_CAP_STATUS_ACTIVE) active_count++;
  }
  printf("Total: epoch st %lu st %lu walk evict %lu walks %lu inst %lu cycle %lu max cycle %lu cap_active %d\n",
    total_epoch_store_count, total_store_count, total_tag_walk_evict_count, total_tag_walk_count,
    total_inst_count, total_cycle_count, max_cycle_count, active_count);
  return;
}

//* vtable_t

vtable_t *vtable_init(vtable_evict_cb_t evict_cb, vtable_core_recv_cb_t core_recv_cb, vtable_core_tag_cb_t core_tag_cb) {
  vtable_t *vtable = (vtable_t *)malloc(sizeof(vtable_t));
  SYSEXPECT(vtable != NULL);
  memset(vtable, 0x00, sizeof(vtable_t));
  vtable->evict_cb = evict_cb;
  vtable->core_recv_cb = core_recv_cb;
  vtable->core_tag_cb = core_tag_cb;
  vtable->vers = ht64_init_size(VTABLE_HT_INIT_BUCKET_COUNT);
  return vtable;
}

void vtable_free(vtable_t *vtable) {
  // Passing ver_t object free function as call back 
  ht64_free_cb(vtable->vers, ver_free); 
  free(vtable);
}

void vtable_set_parent(vtable_t *vtable, struct nvoverlay_struct_t *nvoverlay) {
  vtable->nvoverlay = nvoverlay;
}

ver_t *ver_init() {
  ver_t *ver = (ver_t *)malloc(sizeof(ver_t));
  SYSEXPECT(ver != NULL);
  memset(ver, 0x00, sizeof(ver_t));
  bitmap64_clear(&ver->l1_bitmap);
  bitmap64_clear(&ver->l2_bitmap);
  return ver;
}
void ver_free(void *ver) { free(ver); }

// Either insert a new value or return existing value
// New addresses always originate from LLC + DRAM and has version zero
ver_t *vtable_insert(vtable_t *vtable, uint64_t addr) {
  ASSERT_CACHE_ALIGNED(addr);
  // Passing NULL (0UL) as default value for not found
  ver_t *ver = (ver_t *)ht64_find(vtable->vers, addr, 0UL);
  if(ver == NULL) {
    ver = ver_init();
    ver->addr = addr;
    ver->owner = OWNER_OTHER;
    ht64_insert(vtable->vers, addr, (uint64_t)ver);
  }
  return ver;
}

void ver_sharer_print(bitmap64_t *bitmap) {
  int pos = -1;
  while((pos = bitmap64_iter(bitmap, pos)) != -1) {
    printf("%d", pos);
    if(bitmap64_iter_is_last(bitmap, pos) == 0) {
      printf(", ");
    } 
  }
  return;
}

// When owner is other, we do not care about L1/L2 state and version
void ver_print(ver_t *ver) {
  static const char *state_names[] = {"I", "S", "E", "M"};
  if(ver->owner == OWNER_OTHER) {
    printf("[OTHER] ver %lu; L1 sharer [", ver->other_ver);
    ver_sharer_print(&ver->l1_bitmap);
    printf("]; L2 sharer [");
    ver_sharer_print(&ver->l2_bitmap);
  } else {
    printf("[%s];", ver->owner == OWNER_L1 ? "L1" : "L2");
    printf(" L1 state: %s @ %lu; L2 state: %s @ %lu; ", 
      state_names[ver->l1_state], ver->l1_ver,
      state_names[ver->l2_state], ver->l2_ver);
    printf("L1 sharer [");
    ver_sharer_print(&ver->l1_bitmap);
    printf("]; L2 sharer [");
    ver_sharer_print(&ver->l2_bitmap);
  }
  printf("] addr 0x%lX\n", ver->addr);
  return;
}

// This function simulates L1 load operation
//   1. If hits L1 then no state change - regardless of ownership
//   2. If misses L1 but hits L2, then load the line into L1 - regardless of ownership
//   3. Otherwise we need coherence actions - check for ownership.
//      3.1 If ownership in LLC + DRAM then just load their versions into BOTH L1 and L2 and set both state to shared.
//          Ownership does not change
//      3.2 If ownership in another L1, then both L1 and L2 of the same level must be the only one that cache the line
//          We transfer ownership to LLC + DRAM and set current L1 + L2 to the shared version
//      3.3 If ownership is in another L2, then L1 can be in shared or invalid state. We do the same except that there 
//          is no dirty WB from L1
void vtable_l1_load(vtable_t *vtable, uint64_t addr, int id, uint64_t epoch, uint64_t cycle) {
  (void)epoch;
  ver_t *ver = vtable_insert(vtable, addr);
  assert(ver);
  // Case 1: Hit L1
  if(vtable_l1_has_ver(ver, id)) return; // L1 hit, nothing changed
  // Case 2: Miss L1, hit L2
  if(vtable_l2_has_ver(ver, id)) {       // L1 miss L2 hit - ownership must be either L2 or other
    assert(ver->owner == OWNER_L2 || ver->owner == OWNER_OTHER);
    if(ver->owner == OWNER_L2) { // State is only defined when owner is not OWNER_OTHER
      assert(ver->l1_state == STATE_I && ver->l2_state != STATE_I); // Read miss can only be caused by I state line
    }
    vtable_l1_add_ver(vtable, ver, id);       // The version is also cached in L1
    ver->l1_state = STATE_S;          // L1 state is STATE_I before this
    ver->l1_ver = ver->l2_ver;        // L1 also copies L2's version
    return;
  }
  // Case 3: Miss L1 and L2
  if(ver->owner == OWNER_OTHER) {
    // Case 3.1: Hit lower level
    ver->l1_ver = ver->l2_ver = ver->other_ver; // Both L1 and L2 caches the version from lower level
    ver->l1_state = ver->l2_state = STATE_S;    // Load the line is S state
    vtable_l1_add_ver(vtable, ver, id);
    vtable_l2_add_ver(vtable, ver, id);
    vtable->core_recv_cb(vtable->nvoverlay, id, ver->other_ver); // Received other_ver from LLC
    return;
  } else {
    // Case 3.2: Downgrade and set all to shared; Ownership transfers to LLC + DRAM
    if(ver->owner == OWNER_L1) {
      assert(vtable_l1_num_sharer(ver) == 1 && vtable_l2_num_sharer(ver) == 1);
      assert(vtable_l1_sharer(ver) == vtable_l2_sharer(ver));
      assert(ver->l1_state == STATE_M);
      // First, L1 write back to L2, and force eviction if L2 is dirty and vers diff
      if(ver->l2_state == STATE_M && ver->l2_ver != ver->l1_ver) {
        assert(ver->l2_ver < ver->l1_ver);
        // Only evict to OMC, not LLC
        vtable_evict_wrapper(vtable, ver->addr, vtable_l2_sharer(ver), ver->l2_ver, cycle, EVICT_OMC);
        vtable->ld_inv_evict_count++;
      }
      // Then the dirty version in L1 is also written back
      vtable_evict_wrapper(vtable, ver->addr, vtable_l1_sharer(ver), ver->l1_ver, cycle, EVICT_BOTH);
      vtable->ld_inv_evict_count++;
      ver->other_ver = ver->l1_ver; // Global version is L1 version
    } else {
      assert(ver->owner == OWNER_L2);
      assert(ver->l2_state == STATE_M);
      assert(vtable_l1_num_sharer(ver) <= 1 && vtable_l2_num_sharer(ver) == 1);
      if(vtable_l1_num_sharer(ver) == 1) { // If L1 also caches the line, it must be S state and on the same core
        assert(vtable_l1_sharer(ver) == vtable_l2_sharer(ver));
        assert(ver->l1_state == STATE_S);
      } else {
        assert(ver->l1_state == STATE_I);
      }
      vtable_evict_wrapper(vtable, ver->addr, vtable_l2_sharer(ver), ver->l2_ver, cycle, EVICT_BOTH);
      vtable->ld_inv_evict_count++;
      ver->other_ver = ver->l2_ver; // Global version is L2 version
    }
    // In either case, the current L1 and L2 are loaded with the global version
    ver->owner = OWNER_OTHER;
    vtable_l1_add_ver(vtable, ver, id);
    vtable_l2_add_ver(vtable, ver, id);
    vtable->core_recv_cb(vtable->nvoverlay, id, ver->other_ver); // Received version from LLC after downgrade
  }
  return;
}

// Five big cases: 
//   Owner current L1; Owner current L2; Owner LLC + DRAM; Owner other L1; Owner other L2
// 1. Owner current L1 (must be dirty):
//    1.1 If ver == epoch then store hit
//    1.2 If ver != epoch, write back ver to L2, possibly incurring eviction from L2 to OMC
// 2. Owner current L2 (must be dirty): Just fetch to L1 and let L1 store directly (see trick below) 
// 3. Owner LLC + DRAM: Just fetch the block to L2 as clean and L1 as dirty in the same version
//    There can be potentially shared cached version in other caches; Just invalidate them
// 4. Owner other L1: Write back L2 if dirty and version smaller; Ownership transfers to current L1 + L2
// 5. Owner other L2: Transfer ownership to current L1 + L2; Both have same version number
void vtable_l1_store(vtable_t *vtable, uint64_t addr, int id, uint64_t epoch, uint64_t cycle) {
  ver_t *ver = vtable_insert(vtable, addr);
  assert(ver);
  if(ver->owner == OWNER_L1 && vtable_l1_sharer(ver) == id) {
    // Case 1
    assert(vtable_l1_num_sharer(ver) == 1 && vtable_l2_num_sharer(ver) == 1);
    assert(vtable_l2_sharer(ver) == id);
    assert(ver->l1_state == STATE_M && ver->l2_state != STATE_I);
    // Case 1.1: Store hit
    if(ver->l1_ver == epoch) {
      return;
    } else {
      // Case 1.2
      if(ver->l2_state == STATE_M && ver->l2_ver != ver->l1_ver) {
        assert(ver->l2_ver < ver->l1_ver);
        // If L2 is dirty different version then write back L2 first
        vtable_evict_wrapper(vtable, ver->addr, id, ver->l2_ver, cycle, EVICT_OMC);
        vtable->st_evict_count++; // Store evict to OMC
      }
      // Evict from L1 to L2
      ver->l2_state = STATE_M;
      ver->l2_ver = ver->l1_ver;
      ver->l1_ver = epoch;
    }
  } else if(ver->owner == OWNER_L2 && vtable_l2_sharer(ver) == id) {
    // Case 2
    assert(vtable_l1_num_sharer(ver) <= 1 && vtable_l2_num_sharer(ver) == 1);
    assert(ver->l2_state == STATE_M);
    if(vtable_l1_num_sharer(ver) == 1) {
      assert(vtable_l1_sharer(ver) == id);
      assert(ver->l1_state == STATE_S);
    } else {
      assert(ver->l1_state == STATE_I);
    }
    // Trick: Since L2 has M copy, L1 could just write without evicting, since L2 will see the same version even if 
    // eviction were triggered
    ver->owner = OWNER_L1;
    ver->l1_state = STATE_M;
    ver->l1_ver = epoch;
    vtable_l1_set_ver(vtable, ver, id); // Always set L1 as the only sharer also
  } else if(ver->owner == OWNER_OTHER) {
    // Case 3
    ver->owner = OWNER_L1;
    ver->l1_state = STATE_M;
    ver->l1_ver = epoch;
    ver->l2_state = STATE_S;
    ver->l2_ver = ver->other_ver; // L2 version is the LLC version before ownership xfer
    // Here we use "set" to invalidate others
    vtable_l1_set_ver(vtable, ver, id); 
    vtable_l2_set_ver(vtable, ver, id); 
    vtable->core_recv_cb(vtable->nvoverlay, id, ver->other_ver);
  } else if(ver->owner == OWNER_L1) {
    // Case 4
    assert(vtable_l1_sharer(ver) != id);
    assert(ver->l1_state == STATE_M && ver->l2_state != STATE_I);
    assert(vtable_l1_num_sharer(ver) == 1 && vtable_l2_num_sharer(ver) == 1);
    assert(vtable_l1_sharer(ver) == vtable_l2_sharer(ver));
    // If the L2 also contains a different dirty version we evict it to OMC only
    if(ver->l2_state == STATE_M && ver->l2_ver != ver->l1_ver) {
      assert(ver->l2_ver < ver->l1_ver);
      vtable_evict_wrapper(vtable, ver->addr, vtable_l1_sharer(ver), ver->l2_ver, cycle, EVICT_OMC);
      vtable->st_inv_evict_count++; // Store invalidate (not including ownership xfer on L1)
    }
    uint64_t recv_version;
    recv_version = ver->l2_ver = ver->l1_ver; // Receive the transferred version
    ver->owner = OWNER_L1;
    ver->l1_state = STATE_M;
    ver->l1_ver = epoch;       // Same trick above: We load L2 state as M, so any possible store-evict will be discarded
    ver->l2_state = STATE_M;   // L2 is dirty since we did not write back the version transferred from the other cache
    vtable_l1_set_ver(vtable, ver, id); 
    vtable_l2_set_ver(vtable, ver, id); 
    vtable->core_recv_cb(vtable->nvoverlay, id, recv_version);
  } else {
    // Case 5
    assert(ver->owner == OWNER_L2);
    assert(vtable_l1_num_sharer(ver) <= 1 && vtable_l2_num_sharer(ver) == 1);
    assert(ver->l2_state == STATE_M);
    if(vtable_l1_num_sharer(ver) == 1) {
      assert(vtable_l1_sharer(ver) == vtable_l2_sharer(ver));
      assert(vtable_l1_sharer(ver) != id);
      assert(ver->l1_state == STATE_S);
    } else {
      assert(ver->l1_state == STATE_I);
    }
    // Just transfer ownership without writing back
    //ver->l2_ver = ....;         <- The L2 receives from remote L2 and keeps its version and dirtiness
    //ver->l2_state = STATE_M;    <- Already is
    uint64_t recv_version = ver->l2_ver; // Received version is remote L2's version
    ver->owner = OWNER_L1;
    ver->l1_state = STATE_M;
    ver->l1_ver = epoch;     
    vtable_l1_set_ver(vtable, ver, id); 
    vtable_l2_set_ver(vtable, ver, id); 
    vtable->core_recv_cb(vtable->nvoverlay, id, recv_version);
  }
  return;
}

// This is called when the simulator evicts a block from L1
//   Case 1: Owner is current L1: Transfer ownership to L2, possibly write back current L2 version
//   Case 2: Owner is current L2: No action
//   Case 3: Owner is LLC + DRAM: No action
// In all cases we clear the L1 sharer bit
// Note that owner cannot be other caches, since otherwise there will not be a copy on current core
void vtable_l1_eviction(vtable_t *vtable, uint64_t addr, int id, uint64_t epoch, uint64_t cycle) {
  (void)epoch;
  ver_t *ver = vtable_insert(vtable, addr);
  assert(ver);
  // At least the version should exist in L1 and L2
  assert(vtable_l1_has_ver(ver, id) && vtable_l2_has_ver(ver, id));
  if(ver->owner == OWNER_L1) {
    // Case 1: Dirty eviction
    assert(ver->l1_state == STATE_M);
    assert(vtable_l1_num_sharer(ver) == 1 && vtable_l2_num_sharer(ver) == 1);
    assert(vtable_l1_sharer(ver) == id && vtable_l2_sharer(ver) == id);
    if(ver->l2_state == STATE_M && ver->l2_ver != ver->l1_ver) {
      assert(ver->l2_ver < ver->l1_ver);
      vtable_evict_wrapper(vtable, ver->addr, id, ver->l2_ver, cycle, EVICT_OMC);
      vtable->l1_cap_evict_count++;
    }
    ver->owner = OWNER_L2;
    ver->l1_state = STATE_I;
    ver->l2_state = STATE_M;
    ver->l2_ver = ver->l1_ver;
    vtable_l1_rm_ver(vtable, ver, id);
  } else if(ver->owner == OWNER_L2) {
    // Case 2: Clean eviction but L2 is dirty
    assert(ver->l2_state == STATE_M);
    assert(vtable_l1_num_sharer(ver) <= 1 && vtable_l2_num_sharer(ver) == 1);
    if(vtable_l1_num_sharer(ver) == 1) {
      assert(vtable_l1_sharer(ver) == id);
      assert(ver->l1_state == STATE_S);
    } else {
      assert(ver->l1_state == STATE_I);
    }
    ver->l1_state = STATE_I;
    vtable_l1_rm_ver(vtable, ver, id);
  } else {
    // Case 3: Clean eviction and all copies are also clean
    assert(ver->owner == OWNER_OTHER);
    vtable_l1_rm_ver(vtable, ver, id);
  }
  return;
}

// This is called when the simulator evicts a block from inclusive L2; Both L1 and L2 will be inv'ed
//   Case 1: Owner is current L1: Transfer ownership to LLC + DRAM, possibly write back current L1 and L2 version
//   Case 2: Owner is current L2: Transfer ownership to LLC + DRAM, write back current L2 version
//   Case 3: Owner is LLC + DRAM: No action
// In all cases we clear the L1 and L2 sharer bit
// Note that owner cannot be other caches, since otherwise there will not be a copy on current core
void _vtable_l2_eviction(vtable_t *vtable, uint64_t addr, int id, uint64_t epoch, uint64_t cycle, int from_l3) {
  (void)epoch;
  ver_t *ver = vtable_insert(vtable, addr);
  assert(ver);
  // At least the version should exist in L2 (maybe not in L1)
  assert(vtable_l2_has_ver(ver, id));
  if(ver->owner == OWNER_L1) {
    // Case 1: Dirty eviction
    assert(ver->l1_state == STATE_M);
    assert(vtable_l1_num_sharer(ver) == 1 && vtable_l2_num_sharer(ver) == 1);
    assert(vtable_l1_sharer(ver) == id && vtable_l2_sharer(ver) == id);
    if(ver->l2_state == STATE_M && ver->l2_ver != ver->l1_ver) {
      assert(ver->l2_ver < ver->l1_ver);
      vtable_evict_wrapper(vtable, ver->addr, id, ver->l2_ver, cycle, EVICT_OMC);
      if(from_l3) {
        vtable->l3_cap_evict_count++;
      } else {
        vtable->l2_cap_evict_count++;
      }
    }
    vtable_evict_wrapper(vtable, ver->addr, id, ver->l1_ver, cycle, EVICT_BOTH);
    if(from_l3) {
      vtable->l3_cap_evict_count++;
    } else {
      vtable->l2_cap_evict_count++;
    }
    ver->owner = OWNER_OTHER;
    ver->other_ver = ver->l1_ver;
    vtable_l1_rm_ver(vtable, ver, id);
    vtable_l2_rm_ver(vtable, ver, id);
  } else if(ver->owner == OWNER_L2) {
    // Case 2: L1 clean/invalid but L2 is dirty
    assert(ver->l2_state == STATE_M);
    assert(vtable_l1_num_sharer(ver) <= 1 && vtable_l2_num_sharer(ver) == 1);
    int l1_rm = 0;
    if(vtable_l1_num_sharer(ver) == 1) {
      l1_rm = 1;
      assert(vtable_l1_sharer(ver) == id);
      assert(ver->l1_state == STATE_S);
    } else {
      assert(ver->l1_state == STATE_I);
    }
    vtable_evict_wrapper(vtable, ver->addr, id, ver->l2_ver, cycle, EVICT_BOTH);
    if(from_l3) {
      vtable->l3_cap_evict_count++;
    } else {
      vtable->l2_cap_evict_count++;
    }
    ver->owner = OWNER_OTHER;
    ver->other_ver = ver->l2_ver;
    if(l1_rm) vtable_l1_rm_ver(vtable, ver, id);
    vtable_l2_rm_ver(vtable, ver, id);
  } else {
    // Case 3: Clean eviction and all copies in this core are also clean
    assert(ver->owner == OWNER_OTHER);
    // If L1 has the address then we also remove it
    if(vtable_l1_has_ver(ver, id)) vtable_l1_rm_ver(vtable, ver, id);
    vtable_l2_rm_ver(vtable, ver, id);
  }
  return;
}

// This is called when the inclusive LLC invalidates a line. All copies in L1 and L2
// are also invalidated. 
// ID and epoch are not used.
// Case 1: Owner is OTHER; Just clear the sharers list in both L1 and L2
// Case 2: Owner is any cache; Transfer ownership to LLC (w/ possible WB) and clear the sharer list
// We call higher level eviction function to avoid duplicating the logic here
// ID is not used, although we do pass it around as parameter
void vtable_l3_eviction(vtable_t *vtable, uint64_t addr, int id, uint64_t epoch, uint64_t cycle) {
  ver_t *ver = vtable_insert(vtable, addr);
  if(ver->owner == OWNER_OTHER) {
    // Case 1. In this case there can be multiple upper level caches that have the address
    vtable_l1_clear_sharer(vtable, ver, id);
    vtable_l2_clear_sharer(vtable, ver, id);
  } else {
    // Case 2. Only one core could have the address. L2 must have it. Not sure whether L1 also has it
    assert(vtable_l2_num_sharer(ver) == 1 && vtable_l1_num_sharer(ver) <= 1);
    if(vtable_l1_num_sharer(ver) == 1) {
      assert(vtable_l1_sharer(ver) == vtable_l2_sharer(ver));
    }
    _vtable_l2_eviction(vtable, addr, id, epoch, cycle, 1); // It is equivalent to L2 eviction
  }
  return;
}

void vtable_conf_print(vtable_t *vtable) {
  printf("---------- vtable_t ----------\n");
  printf("HT init buckets %d\n", HT64_DEFAULT_INIT_BUCKETS);
  printf("NVOverlay object: %p\n", vtable->nvoverlay);
  return;
} 

void vtable_stat_print(vtable_t *vtable) {
  printf("---------- vtable_t ----------\n");
  printf("HT size %lu buckets %lu\n", ht64_get_item_count(vtable->vers), ht64_get_bucket_count(vtable->vers));
  printf("Evict OMC %lu LLC %lu\n", vtable->omc_evict_count, vtable->llc_evict_count);
  uint64_t sum = (vtable->st_evict_count + vtable->ld_inv_evict_count + vtable->st_inv_evict_count + \
    vtable->l1_cap_evict_count + vtable->l2_cap_evict_count + vtable->l3_cap_evict_count);
  printf("Evict reason: st %lu ld-inv %lu st-inv %lu l1-cap %lu l2-cap %lu l3-cap %lu (sum %lu)\n",
    vtable->st_evict_count, vtable->ld_inv_evict_count, vtable->st_inv_evict_count,
    vtable->l1_cap_evict_count, vtable->l2_cap_evict_count, vtable->l3_cap_evict_count, sum);
  return;
}

//* omcbuf_t

omcbuf_t *omcbuf_init(int sets, int ways, omcbuf_evict_cb_t evict_cb) {
  omcbuf_t *omcbuf = (omcbuf_t *)malloc(sizeof(omcbuf_t));
  SYSEXPECT(omcbuf != NULL);
  memset(omcbuf, 0x00, sizeof(omcbuf_t));
  omcbuf->sets = sets;
  omcbuf->ways = ways;
  omcbuf->evict_cb = evict_cb;
  omcbuf->set_idx_bits = util_log2_int32(sets, "OMCBUF sets");
  omcbuf->set_mask = (0x1UL << omcbuf->set_idx_bits) - 1;
  omcbuf->array = (omcbuf_entry_t *)malloc(sizeof(omcbuf_entry_t) * sets * ways);
  memset(omcbuf->array, 0x00, sizeof(omcbuf_entry_t) * sets * ways);
  for(int i = 0;i < ways * sets;i++) omcbuf->array[i].epoch = -1UL;
  SYSEXPECT(omcbuf->array);
  return omcbuf;
}

void omcbuf_free(omcbuf_t *omcbuf) {
  free(omcbuf->array);
  free(omcbuf);
}

void omcbuf_set_parent(omcbuf_t *omcbuf, struct nvoverlay_struct_t *nvoverlay) {
  omcbuf->nvoverlay = nvoverlay;
}

// Cycle is needed for writing to the NVM (omcbuf eivction)
void omcbuf_insert(omcbuf_t *omcbuf, uint64_t addr, uint64_t epoch, uint64_t cycle) {
  assert(epoch != -1UL); // -1UL means invalid slot
  ASSERT_CACHE_ALIGNED(addr);
  omcbuf->access_count++;
  int set_index = (int)((addr >> UTIL_CACHE_LINE_BITS) & omcbuf->set_mask);
  uint64_t tag = addr >> (UTIL_CACHE_LINE_BITS + omcbuf->set_idx_bits);
  omcbuf_entry_t *start = omcbuf->array + set_index * omcbuf->ways;
  omcbuf_entry_t *curr = start;
  for(int i = 0;i < omcbuf->ways;i++) {
    // Only hit if both addr and epoch are the same; Addr from different epochs cannot 
    // overwrite each other
    if(curr->tag == tag && curr->epoch == epoch) {
      omcbuf->hit_count++;
      return; 
    } else if(curr->epoch == -1UL) { // Compulsory misses
      curr->tag = tag;
      curr->epoch = epoch;
      curr->lru = ++omcbuf->lru_counter;
      omcbuf->miss_count++;
      omcbuf->item_count++;
      assert(omcbuf->item_count <= (uint64_t)omcbuf->ways * (uint64_t)omcbuf->sets);
      return;
    }
    curr++;
  }
  omcbuf->miss_count++;
  // Next compute the smallest LRU counter in the current set and evict it; No tie!
  uint64_t lru = -1UL;
  int lru_index = -1;
  curr = start; // Reset curr
  for(int i = 0;i < omcbuf->ways;i++) {
    assert(curr->epoch != -1UL);
    if(curr->lru < lru) {
      lru = curr->lru;
      lru_index = i;
    }
    curr++;
  }
  curr = start + lru_index; // Points to the entry to be evicted
  uint64_t evict_addr = ((curr->tag << omcbuf->set_idx_bits) | (uint64_t)set_index) << UTIL_CACHE_LINE_BITS;
  omcbuf->evict_cb(omcbuf->nvoverlay, evict_addr, curr->epoch, cycle);
  omcbuf->evict_count++;
  curr->tag = tag;
  curr->epoch = epoch;
  curr->lru = ++omcbuf->lru_counter;
  return;
}

// Evict all versions < target epoch
void omcbuf_tag_walk(omcbuf_t *omcbuf, uint64_t target_epoch, uint64_t cycle) {
  int total = omcbuf->sets * omcbuf->ways;
  int set_index = -1; // Since this will ++ on first iteration, we set it to -1 before loop
  for(int i = 0;i < total;i++) {
    omcbuf_entry_t *entry = omcbuf->array + i;
    if(i % omcbuf->ways == 0) set_index++;
    if(entry->epoch == -1UL) continue; // -1UL means invalid entry
    if(entry->epoch < target_epoch) {
      uint64_t evict_addr = ((entry->tag << omcbuf->set_idx_bits) | (uint64_t)set_index) << UTIL_CACHE_LINE_BITS;
      omcbuf->evict_cb(omcbuf->nvoverlay, evict_addr, entry->epoch, cycle);
      omcbuf->evict_count++;
      omcbuf->tag_walk_evict_count++;
      assert(omcbuf->item_count != 0UL);
      omcbuf->item_count--;
      entry->epoch = -1UL;
    }
  }
  return;
}

void omcbuf_conf_print(omcbuf_t *omcbuf) {
  printf("---------- omcbuf_t ----------\n");
  printf("sets %d ways %d (size %lu bytes) mask 0x%lX set bits %d\n", omcbuf->sets, omcbuf->ways, 
    omcbuf->sets * omcbuf->ways * UTIL_CACHE_LINE_SIZE, omcbuf->set_mask, omcbuf->set_idx_bits);
  return;
}

void omcbuf_stat_print(omcbuf_t *omcbuf) {
  printf("---------- omcbuf_t ----------\n");
  printf("Access %lu hit %lu miss %lu evict %lu tag walk evict %lu\n", 
    omcbuf->access_count, omcbuf->hit_count, omcbuf->miss_count, omcbuf->evict_count,
    omcbuf->tag_walk_evict_count);
  return;
}

//* overlay_t

overlay_page_t *overlay_page_init() {
  overlay_page_t *overlay_page = (overlay_page_t *)malloc(sizeof(overlay_page_t));
  SYSEXPECT(overlay_page != NULL);
  memset(overlay_page, 0x00, sizeof(overlay_page_t));
  bitmap64_clear(&overlay_page->bitmap);
  return overlay_page;
}

// This function is also the call back to mtable_free()
void overlay_page_free(void *overlay_page) {
  free((overlay_page_t *)overlay_page);
}

overlay_epoch_t *overlay_epoch_init() {
  overlay_epoch_t *overlay_epoch = (overlay_epoch_t *)malloc(sizeof(overlay_epoch_t));
  SYSEXPECT(overlay_epoch != NULL);
  memset(overlay_epoch, 0x00, sizeof(overlay_epoch_t));
  overlay_epoch->mtable = mtable_init();
  mtable_idx_add(overlay_epoch->mtable, 39, 9);
  mtable_idx_add(overlay_epoch->mtable, 30, 9);
  mtable_idx_add(overlay_epoch->mtable, 21, 9);
  mtable_idx_add(overlay_epoch->mtable, 12, 9);
  mtable_jit_lookup(overlay_epoch->mtable);
  return overlay_epoch;
}

// This function is also the call back to mtable_free()
void overlay_epoch_free(void *overlay_epoch) {
  // Also free overlay entries when freeing the table
  mtable_free_cb(((overlay_epoch_t *)overlay_epoch)->mtable, overlay_page_free);
  free(overlay_epoch);
}

// Empty page has 0 as size class
uint64_t overlay_page_size_class(int line_count) {
  assert(line_count >= 0 && line_count <= (int)(UTIL_LINE_PER_PAGE));
  if(line_count == 0) return 0UL;
  else if(line_count <= 3) return 256UL;
  else if(line_count <= 7) return 512UL;
  else if(line_count <= 15) return 1024UL;
  else if(line_count <= 31) return 2048UL;
  else return 4096UL;
}

void overlay_page_print(overlay_t *overlay, uint64_t epoch, uint64_t page_addr) {
  overlay_page_t *page = overlay_find_page(overlay, epoch, page_addr);
  printf("Addr 0x%lX @ %lu ref count %d\n", page_addr, epoch, page->ref_count);
  printf("Bitmap64 ");
  bitmap64_print_bitstr(&page->bitmap); // Print in 10101010 form
  putchar('\n');
  return;
}

// This function determines overlay size of the page
// Return value is the size that should be added onto epoch size, if any.
// The following is the size class and their definitions:
// 1. 0 - 3 cache lines: 256B page
// 2. 4 - 7 cache lines: 512B page
// 3. 8 - 15 cache lines: 1KB page
// 4. 16 - 31 cache lines: 2KB page
// 5. 32 - 64 cache lines: 4KB page
uint64_t overlay_epoch_insert(overlay_epoch_t *overlay_epoch, uint64_t addr) {
  overlay_page_t **overlay_page_p = (overlay_page_t **)mtable_insert(overlay_epoch->mtable, addr);
  // Create a new page
  if(*overlay_page_p == NULL) {
    *overlay_page_p = overlay_page_init();
    overlay_epoch->overlay_page_count++;
  }
  overlay_page_t *overlay_page = *overlay_page_p;
  int offset = page_line_offset(addr);
  assert(offset >= 0 && offset < 64); 
  int before_num = bitmap64_popcount(&overlay_page->bitmap);
  // OR the bit and inc the ref counter
  bitmap64_add(&overlay_page->bitmap, offset);
  int after_num = bitmap64_popcount(&overlay_page->bitmap);
  //printf("before %d after %d offset %d addr 0x%lX\n", before_num, after_num, offset, addr);
  if(before_num == after_num) return 0UL; // If we overwrite the same line, overlay size does not change
  assert(after_num == before_num + 1);
  // Only increment ref count if we inserted a new line, not overwriting an existing line
  overlay_page->ref_count++;
  assert(overlay_page->ref_count > 0 && overlay_page->ref_count <= (int)UTIL_LINE_PER_PAGE);
  uint64_t ret = 0UL;
  switch(before_num) {
    case 0: ret = 256UL; break; // 256 base
    case 3: ret = 256UL; break; // 256 more
    case 7: ret = 512UL; break; // 512 more
    case 15: ret = 1024UL; break; 
    case 31: ret = 2048UL; break; 
    default: ret = 0UL; break;  // The page size does not change
  }
  overlay_epoch->size += ret;
  return ret;
}

// Report error if not found
overlay_page_t *overlay_epoch_find(overlay_epoch_t *overlay_epoch, uint64_t addr) {
  ASSERT_PAGE_ALIGNED(addr);
  overlay_page_t *overlay_page = (overlay_page_t *)mtable_find(overlay_epoch->mtable, addr);
  if(overlay_page == NULL) {
    error_exit("Could not find overlay page 0x%lX in overlay epoch %lu\n", addr, overlay_epoch->epoch);
  }
  return overlay_page;
}

// Note the "node" value is only the value of the leaf slot, not pointer to it (i.e. different
// from what is returned by )
static void overlay_epoch_line_count_cb(uint64_t key, void *node, void *counter) {
  assert(node != NULL);
  (*(uint64_t *)counter) += bitmap64_popcount(&(((overlay_page_t *)node)->bitmap));
  (void)key;
}

// Returns the number of cache lines in the current overlay, all addresses
uint64_t overlay_epoch_line_count(overlay_epoch_t *overlay_epoch) {
  uint64_t count = 0UL;
  overlay_epoch_traverse(overlay_epoch, overlay_epoch_line_count_cb, &count);
  return count;
}

static void overlay_epoch_size_cb(uint64_t key, void *node, void *size) {
  assert(node != NULL);
  // If this is zero then we add zero
  overlay_page_t *page = (overlay_page_t *)node;
  // If ref count == 0 the page has been GC'ed (although we do not remove the page)
  if(page->ref_count != 0) {
    int line_count = bitmap64_popcount(&page->bitmap);
    (*(uint64_t *)size) += overlay_page_size_class(line_count);
  }
  (void)key;
}

// This computes size without page shrinking, i.e. when a line is removed from the 
// page, the page size is not adjusted
uint64_t overlay_epoch_size(overlay_epoch_t *epoch) {
  uint64_t ret = 0UL;
  overlay_epoch_traverse(epoch, overlay_epoch_size_cb, &ret);
  return ret;
}

static void overlay_epoch_min_size_cb(uint64_t key, void *node, void *size) {
  assert(node != NULL);
  // If this is zero then we add zero
  int line_count = ((overlay_page_t *)node)->ref_count;
  (*(uint64_t *)size) += overlay_page_size_class(line_count);
  (void)key;
}

// Compute the minimum size required to store the epoch, i.e. always use the small page size class
// for each page recorded in the epoch. 
// Note: Use ref_count to determine line count in each page
uint64_t overlay_epoch_min_size(overlay_epoch_t *epoch) {
  uint64_t ret = 0UL;
  overlay_epoch_traverse(epoch, overlay_epoch_min_size_cb, &ret);
  return ret;
}

uint64_t overlay_mapping_size(overlay_t *overlay) {
  uint64_t ret = 0UL;
  ht64_t *epochs = overlay->epochs;
  ht64_it_t it;
  ht64_begin(epochs, &it);
  while(ht64_is_end(epochs, &it) == 0) {
    overlay_epoch_t *epoch = (overlay_epoch_t *)ht64_it_value(&it);
    ret += mtable_get_size(epoch->mtable);
    ht64_next(epochs, &it);
  }
  return ret;
}

uint64_t overlay_line_count(overlay_t *overlay) {
  uint64_t ret = 0UL;
  ht64_t *epochs = overlay->epochs;
  ht64_it_t it;
  ht64_begin(epochs, &it);
  while(ht64_is_end(epochs, &it) == 0) {
    overlay_epoch_t *epoch = (overlay_epoch_t *)ht64_it_value(&it);
    ret += overlay_epoch_line_count(epoch);
    ht64_next(epochs, &it);
  }
  return ret;
}

void overlay_epoch_print(overlay_epoch_t *overlay_epoch) {
  printf("Overlay Epoch %lu pages %lu size %lu\n", 
    overlay_epoch->epoch, overlay_epoch->overlay_page_count, overlay_epoch->size);
}

overlay_t *overlay_init() {
  overlay_t *overlay = (overlay_t *)malloc(sizeof(overlay_t));
  SYSEXPECT(overlay != NULL);
  memset(overlay, 0x00, sizeof(overlay_t));
  overlay->epochs = ht64_init();
  return overlay;
}

void overlay_free(overlay_t *overlay) {
  ht64_free_cb(overlay->epochs, overlay_epoch_free);
  free(overlay);
}

void overlay_insert(overlay_t *overlay, uint64_t addr, uint64_t epoch) {
  ASSERT_CACHE_ALIGNED(addr);
  overlay_epoch_t *overlay_epoch = (overlay_epoch_t *)ht64_find(overlay->epochs, epoch, 0UL);
  if(overlay_epoch == NULL) {
    overlay->epoch_init_count++;
    overlay->epoch_count++;
    overlay_epoch = overlay_epoch_init();
    overlay_epoch->epoch = epoch;
    int ret = ht64_insert(overlay->epochs, epoch, (uint64_t)overlay_epoch);
    assert(ret == 1);
  }
  assert(overlay_epoch->epoch == epoch);
  if(overlay_epoch->merged != 0) {
    error_exit("Overlay epoch %lu addr 0x%lX has been merged; Insert is disabled\n", epoch, addr);
  }
  uint64_t delta = overlay_epoch_insert(overlay_epoch, addr);
  overlay->size += delta;
  overlay->min_size += delta;
  return;
}

void overlay_remove(overlay_t *overlay, uint64_t epoch) {
  // Remove from the hash table (returns NULL if key not found)
  overlay_epoch_t *overlay_epoch = (overlay_epoch_t *)ht64_remove(overlay->epochs, epoch, 0UL);
  if(overlay_epoch == NULL) {
    error_exit("Could not find epoch %lu to remove from overlay\n", epoch);
  }
  // Then update statistics
  assert(overlay->epoch_count != 0);
  overlay->epoch_count--;
  assert(overlay->size >= overlay_epoch->size);
  overlay->size -= overlay_epoch->size; // In practice this should always be zero, but let's make it general purpose
  uint64_t min_size = overlay_epoch_min_size(overlay_epoch);
  assert(overlay->min_size >= min_size);
  overlay->min_size -= min_size; // Only deduce min_size, since shirnked
  // Finally free the epoch object itself
  overlay_epoch_free(overlay_epoch);
  return;
}

// Return error if page not found
overlay_page_t *overlay_find_page(overlay_t *overlay, uint64_t epoch, uint64_t page_addr) {
  ASSERT_PAGE_ALIGNED(page_addr);
  overlay_epoch_t *overlay_epoch = overlay_find(overlay, epoch);
  if(overlay_epoch == NULL) error_exit("Could not find epoch %lu\n", epoch);
  overlay_page_t *page = overlay_epoch_find(overlay_epoch, page_addr); 
  if(page == NULL) error_exit("Page 0x%lX does not exist @ %lu\n", page_addr, epoch);
  return page;
}

// Arg is of type overlay_merge_arg_t, declared locally on the stack (no need to free)
static void overlay_merge_epoch_cb(uint64_t page_addr, void *node, void *_arg) {
  assert(node != NULL);
  overlay_merge_cb_arg_t *arg = (overlay_merge_cb_arg_t *)_arg;
  overlay_t *overlay = arg->overlay;
  overlay_epoch_t *overlay_epoch = arg->overlay_epoch;
  omt_t *omt = arg->omt;
  // Entry contains cache lines within the overlay page
  overlay_page_t *overlay_page = (overlay_page_t *)node;
  int line_offset = -1;
  while((line_offset = bitmap64_iter(&overlay_page->bitmap, line_offset)) != -1) {
    // This is the cache line address
    uint64_t line_addr = page_addr | ((uint64_t)line_offset << UTIL_CACHE_LINE_BITS); 
    overlay_epoch_t *old_overlay_epoch = omt_merge_line(omt, overlay_epoch, line_addr);
    // If NULL then there is no previous version on this line addr
    if(old_overlay_epoch != NULL) {
      // Only need page level address, since ref count is maintained at page level
      overlay_line_unlink(overlay, old_overlay_epoch, page_addr); 
    }
  }
  return;
}

// This is the entry point for epoch merge operation. It calls into OMT's insertion function
// and may also optionally call into overlay_line_unlink() and overlay_gc_epoch() for GC
// If the epoch does not exist, the function simply returns (so the caller could just loop
// through a range of epochs without checking whether they exist)
void overlay_epoch_merge(overlay_t *overlay, uint64_t epoch, omt_t *omt) {
  // This will report error if epoch does not exist
  overlay_epoch_t *overlay_epoch = overlay_find(overlay, epoch);
  // Trying to merge a non-existing epoch
  if(overlay_epoch == NULL) {
    return;
  }
  //assert(overlay_epoch != NULL);
  assert(overlay_epoch->merged == 0);
  overlay_epoch->merged = 1;
  overlay->merge_count++; // Increment merge count
  //printf("Merge epoch %lu\n", epoch);
  overlay_merge_cb_arg_t arg = {overlay, overlay_epoch, omt};
  overlay_epoch_traverse(overlay_epoch, overlay_merge_epoch_cb, &arg);
  return;
}

// Unlink addr from the given epoch node
void overlay_line_unlink(overlay_t *overlay, overlay_epoch_t *overlay_epoch, uint64_t addr) {
  // Find the overlay page object using key (the page aligned address)
  ASSERT_PAGE_ALIGNED(addr);
  overlay->line_gc_count++;
  overlay_page_t *old_overlay_page = overlay_epoch_find(overlay_epoch, addr);
  assert(old_overlay_page != NULL);
  assert(old_overlay_page->ref_count > 0);
  old_overlay_page->ref_count--;
  // If unlinking the line causes page size to change, we deduce it from overlay total size
  uint64_t before_size = overlay_page_size_class(old_overlay_page->ref_count + 1);
  uint64_t after_size = overlay_page_size_class(old_overlay_page->ref_count);
  assert(after_size <= before_size);
  // This is valid even if ref count drops to zero
  overlay->min_size -= (before_size - after_size);
  // Overlay page GC - adjust page count and size for BOTH overlay epoch and overlay
  // NOTE: EVEN IF THE PAGE IS EMPTY AND GC'ED, WE DO NOT REMOVE THE PAGE POINTER FROM THE MAPPING
  // So traversal function should check whether the page's ref count is zero to determine whether the
  // page is still active
  if(old_overlay_page->ref_count == 0) {
    assert(overlay_epoch->overlay_page_count != 0);
    overlay_epoch->overlay_page_count--;
    // Size of the old page - we keep the bitmap unchanged to compute the old size
    uint64_t page_size = overlay_page_size_class(bitmap64_popcount(&old_overlay_page->bitmap));
    assert(page_size != 0UL);
    assert(overlay_epoch->size >= page_size);
    assert(overlay->size >= page_size);
    overlay_epoch->size -= page_size;
    overlay->size -= page_size;
    overlay->page_gc_count++;
    overlay->page_gc_size += page_size;
    if(overlay_epoch->overlay_page_count == 0) {
      overlay_gc_epoch(overlay, overlay_epoch);
    }
  }
  return;
}

void overlay_gc_epoch(overlay_t *overlay, overlay_epoch_t *overlay_epoch) {
   overlay->epoch_gc_count++;
   // Only GC when all pages have been GC'ed
   assert(overlay_epoch->overlay_page_count == 0UL);
   assert(overlay_epoch->size == 0UL);
   // This function will also dec overlay size, but since it is already zero in epoch
   overlay_remove(overlay, overlay_epoch->epoch);
   return;
}

// Checks consistency of the data structure
void overlay_selfcheck(overlay_t *overlay) {
  ht64_it_t it;
  ht64_t *epochs = overlay->epochs;
  ht64_begin(epochs, &it);
  uint64_t total_size = 0UL, total_min_size = 0UL;
  while(ht64_is_end(epochs, &it) == 0) {
    overlay_epoch_t *epoch = (overlay_epoch_t *)ht64_it_value(&it);
    assert(epoch != NULL);
    total_size += overlay_epoch_size(epoch);
    total_min_size += overlay_epoch_min_size(epoch);
    ht64_next(epochs, &it);
  }
  if(total_size != overlay->size) {
    error_exit("Size inconsistency: walk size %lu size %lu\n", total_size, overlay->size);
  } else if(total_min_size != overlay->min_size) {
    error_exit("Min-size inconsistency: walk min-size %lu min-size %lu\n", total_min_size, overlay->min_size);
  }
  return;
}

void overlay_conf_print(overlay_t *overlay) {
  printf("---------- overlay_t ----------\n");
  printf("HT init buckets %d\n", HT64_DEFAULT_INIT_BUCKETS);
  return;
  (void)overlay;
}

void overlay_stat_print(overlay_t *overlay) {
  printf("---------- overlay_t ----------\n");
  printf("HT size %lu buckets %lu\n", ht64_get_item_count(overlay->epochs), ht64_get_bucket_count(overlay->epochs));
  printf("Epochs mtable size %lu bytes (sum of all epochs)\n", overlay_mapping_size(overlay));
  printf("Epochs active %lu init %lu size %lu bytes min-size %lu bytes merge-calls %lu\n",
    overlay->epoch_count, overlay->epoch_init_count, overlay->size, overlay->min_size, overlay->merge_count);
  printf("GC lines %lu pages %lu epochs %lu page size %lu\n",
    overlay->line_gc_count, overlay->page_gc_count, overlay->epoch_gc_count, overlay->page_gc_size);
  return;
}

//* nvm_t

nvm_t *nvm_init(int bank_count, uint64_t rlat, uint64_t wlat) {
  nvm_t *nvm = (nvm_t *)malloc(sizeof(nvm_t));
  SYSEXPECT(nvm != NULL);
  memset(nvm, 0x00, sizeof(nvm_t));
  nvm->rlat = rlat;
  nvm->wlat = wlat;
  nvm->bank_count = bank_count;
  if(popcount_int32(bank_count) != 1) {
    error_exit("[nvm_init] bank_count must be a power of two (see %d)\n", bank_count);
  }
  nvm->mask = (uint64_t)bank_count - 1;
  nvm->bank_bit = popcount_uint64(nvm->mask);
  nvm->banks = (uint64_t *)malloc(bank_count * sizeof(uint64_t));
  SYSEXPECT(nvm->banks != NULL);
  memset(nvm->banks, 0x00, bank_count * sizeof(uint64_t));
  return nvm;
}

void nvm_free(nvm_t *nvm) {
  free(nvm->banks);
  free(nvm);
}

uint64_t nvm_access(nvm_t *nvm, uint64_t addr, uint64_t cycle, uint64_t lat, uint64_t *uncontended_counter) {
  int index = (int)((addr >> UTIL_CACHE_LINE_BITS) & nvm->mask); // Lower bits as index
  assert(index >= 0 && index < nvm->bank_count);
  uint64_t finish_cycle;
  if(cycle >= nvm->banks[index]) {
    (*uncontended_counter)++;      // The op is uncontended
    finish_cycle = cycle + lat; // The bank has been idle for a while
  } else { 
    finish_cycle = nvm->banks[index] + lat; // The bank is busy now, so we need to wait until it becomes idle
  }
  nvm->banks[index] = finish_cycle;
  return finish_cycle;
}

uint64_t nvm_sync(nvm_t *nvm) {
  uint64_t max = 0UL;
  for(int i = 0;i < nvm->bank_count;i++) {
    if(nvm->banks[i] > max) max = nvm->banks[i];
  }
  return max;
}

uint64_t nvm_min(nvm_t *nvm) {
  uint64_t min = -1UL;
  for(int i = 0;i < nvm->bank_count;i++) {
    if(nvm->banks[i] < min) min = nvm->banks[i];
  }
  return min;
}

// The bank # should not be larger than bank count, otherwise we throw error
uint64_t nvm_addr_gen(nvm_t *nvm, uint64_t tag, uint64_t bank) {
  if(bank & ~nvm->mask) error_exit("Bank id %lu larger than bank count %d\n", bank, nvm->bank_count);
  return (tag << (UTIL_CACHE_LINE_BITS + nvm->bank_bit)) + (bank << UTIL_CACHE_LINE_BITS);
}

void nvm_conf_print(nvm_t *nvm) {
  printf("---------- nvm_t ----------\n");
  printf("banks %d rlat %lu wlat %lu bit %d mask 0x%lX\n", nvm->bank_count, nvm->rlat, nvm->wlat, nvm->bank_bit, nvm->mask);
  return;
}

void nvm_stat_print(nvm_t *nvm) {
  printf("---------- nvm_t ----------\n");
  printf("reads %lu (uncontended %lu) writes %lu (uncontended %lu)\n", 
    nvm->read_count, nvm->uncontended_read_count, nvm->write_count, nvm->uncontended_write_count);
  printf("Sync @ %lu min @ %lu\n", nvm_sync(nvm), nvm_min(nvm));
  return;
}

//* picl_t

picl_t *picl_init(picl_evict_cb_t evict_cb) {
  picl_t *picl = (picl_t *)malloc(sizeof(picl_t));
  SYSEXPECT(picl != NULL);
  memset(picl, 0x00, sizeof(picl_t));
  picl->ht64 = ht64_init();
  picl->evict_cb = evict_cb;
  picl->log_sizes = fifo_init();
  return picl;
}

void picl_free(picl_t *picl) {
  fifo_free(picl->log_sizes);
  ht64_free(picl->ht64);
  free(picl);
  return;
}

void picl_store(picl_t *picl, uint64_t line_addr, uint64_t cycle) {
  ASSERT_CACHE_ALIGNED(line_addr);
  // Return 1 if the insert succeeds (addr does not exist) - evict
  int ret = ht64_insert(picl->ht64, line_addr, PICL_ADDR_PRESENT);
  if(ret == 1) {
    // This is log write, use log address
    picl->evict_cb(picl->nvoverlay, picl->log_ptr, cycle);
    picl->line_count++;
    // Update maximum line count
    if(picl->line_count > picl->max_line_count) {
      picl->max_line_count = picl->line_count;
    }
    picl->log_ptr += UTIL_CACHE_LINE_SIZE;
    picl->log_write_count++;
    picl->epoch_log_write_count++;
  }
  picl->epoch_store_count++;
  picl->total_store_count++;
  return;
}

// We do not assert the address being in the cache, since it can be a non-dirty eviction
// Remember the ht64 only tracks dirty working set
void picl_l3_eviction(picl_t *picl, uint64_t line_addr, uint64_t cycle) {
  ASSERT_CACHE_ALIGNED(line_addr);
  // Returns PICL_ADDR_PRESENT if address exists
  int ret = ht64_remove(picl->ht64, line_addr, PICL_ADDR_MISSING);
  // Write the evicted line into NVM
  if(ret == PICL_ADDR_PRESENT) {
    picl->evict_cb(picl->nvoverlay, line_addr, cycle);
    picl->line_count--;
    picl->l3_evict_count++;
  }
  return;
}

void picl_advance_epoch(picl_t *picl, uint64_t cycle) {
  ht64_rm_it_t it;
  ht64_rm_begin(picl->ht64, &it);
  uint64_t count = 0;
  // Performs tag walk to downgrade dirty lines
  while(ht64_is_rm_end(picl->ht64, &it) == 0) {
    picl->evict_cb(picl->nvoverlay, ht64_it_key(&it), cycle); // Write back the cache line into NVM
    picl->tag_walk_evict_count++;
    // Removes the current item and moves to next
    ht64_rm_next(picl->ht64, &it);
    count++;
  }
  assert(count == picl->line_count);
  picl->line_count = 0UL;
  picl->epoch_count++;
  // We align the log pointer after advancing new epoch
  picl->log_ptr = 0UL;
  picl->epoch_store_count = 0UL;
  // Clear per-epoch log write count after saving it to the fifo
  fifo_insert(picl->log_sizes, picl->epoch_log_write_count);
  picl->epoch_log_write_count = 0UL;
  return;
}

void picl_conf_print(picl_t *picl) {
  printf("---------- picl_t ----------\n");
  printf("HT init buckets %d epoch size %lu\n", HT64_DEFAULT_INIT_BUCKETS, picl->epoch_size);
  return;
}

void picl_stat_print(picl_t *picl) {
  printf("---------- picl_t ----------\n");
  printf("HT size %lu buckets %lu\n", ht64_get_item_count(picl->ht64), ht64_get_bucket_count(picl->ht64));
  printf("Lines curr %lu (max %lu) epochs %lu log ptr %lu epoch stores %lu total stores %lu\n", 
    picl->line_count, picl->max_line_count, picl->epoch_count, picl->log_ptr, 
    picl->epoch_store_count, picl->total_store_count);
  // Print evict counters (should equal total writes to NVM)
  uint64_t log_size = (picl->log_write_count * PICL_LOG_ENTRY_SIZE + 
    (picl->tag_walk_evict_count + picl->l3_evict_count) * UTIL_CACHE_LINE_SIZE);
  printf("Writes log %lu walk %lu LLC evict %lu (sum %lu) total size %lu bytes\n", 
    picl->log_write_count, picl->tag_walk_evict_count, picl->l3_evict_count, 
    picl_get_total_evict_count(picl), log_size);
  printf("  Alternate writes %lu (assuming one log entry needs two writes)\n",
    picl->log_write_count * 2 + picl->tag_walk_evict_count + picl->l3_evict_count);
  // Log sizes (average of all epochs)
  fifo_it_t it;
  fifo_begin(picl->log_sizes, &it);
  uint64_t total_log_writes = 0UL;
  while(fifo_is_end(picl->log_sizes, &it) == 0) {
    total_log_writes += fifo_it_value(&it);
    fifo_next(picl->log_sizes, &it);
  }
  double avg_writes = (double)total_log_writes / (double)fifo_get_item_count(picl->log_sizes);
  printf("Avg epoch log writes %f (%f bytes, %lu data pts) epoch size %lu\n", 
    avg_writes, avg_writes * PICL_LOG_ENTRY_SIZE, fifo_get_item_count(picl->log_sizes),
    picl->epoch_size);
  return;
}

//* nvoverlay

const char *nvoverlay_mode_names[] = {
  "MODE_FULL", "MODE_PICL", "MODE_TRACER",
};

// This interface is used to disable nvoverlay, e.g. when the simulation ends but there
// are still clean up instructions in the workload before program exits
nvoverlay_intf_t nvoverlay_intf_noop = {
  nvoverlay_noop_noop, nvoverlay_noop_noop,
  nvoverlay_noop_noop, nvoverlay_noop_noop, nvoverlay_noop_noop,
  nvoverlay_noop_noop, 0UL,
};

nvoverlay_intf_t nvoverlay_intf_full = {
  nvoverlay_full_load, nvoverlay_full_store,
  nvoverlay_full_l1_evict, nvoverlay_full_l2_evict, nvoverlay_full_l3_evict,
  nvoverlay_other, 0UL,
};

nvoverlay_intf_t nvoverlay_intf_tracer = {
  nvoverlay_tracer_load, nvoverlay_tracer_store,
  nvoverlay_tracer_l1_evict, nvoverlay_tracer_l2_evict, nvoverlay_tracer_l3_evict,
  nvoverlay_other, 0UL,
};

nvoverlay_intf_t nvoverlay_intf_picl = {
  nvoverlay_picl_load, nvoverlay_picl_store,
  nvoverlay_picl_l1_evict, nvoverlay_picl_l2_evict, nvoverlay_picl_l3_evict,
  nvoverlay_other, 0UL,
};

// This is the public interface object
nvoverlay_intf_t nvoverlay_intf;

// Forward declaration of static functions - they will not be included in the header file
static void nvoverlay_vtable_evict_cb(nvoverlay_t *nvoverlay, uint64_t addr, int id, uint64_t version, uint64_t cycle, int evict_type);
static void nvoverlay_vtable_core_recv_cb(nvoverlay_t *nvoverlay, int id, uint64_t version);
static void nvoverlay_vtable_core_tag_cb(nvoverlay_t *nvoverlay, int op, int level, int id, ver_t *ver);
static void nvoverlay_omcbuf_evict_cb(nvoverlay_t *nvoverlay, uint64_t line_addr, uint64_t version, uint64_t cycle);
static void nvoverlay_cpu_tag_walk_cb(nvoverlay_t *nvoverlay, uint64_t line_addr, int id, uint64_t version, uint64_t cycle);
static void nvoverlay_picl_evict_cb(nvoverlay_t *nvoverlay, uint64_t line_addr, uint64_t cycle);

nvoverlay_t *nvoverlay_init(const char *conf_file) {
  nvoverlay_t *nvoverlay = (nvoverlay_t *)malloc(sizeof(nvoverlay_t));
  SYSEXPECT(nvoverlay != NULL);
  memset(nvoverlay, 0x00, sizeof(nvoverlay_t));
  // Configuration file
  conf_t *conf = nvoverlay->conf = conf_init(conf_file);

  // Read mode - depending on the mode, we assign different interfaces to the main intf object
  char *mode_str = conf_find_str_mandatory(conf, "nvoverlay.mode");
  if(streq(mode_str, "debug")) {
    error_exit("Please set nvoverlay.mode in the conf file before running the test\n");
  } else if(streq(mode_str, "full")) {
    nvoverlay->mode = NVOVERLAY_MODE_FULL;
    nvoverlay_intf = nvoverlay_intf_full;
    nvoverlay_init_full(nvoverlay);
  } else if(streq(mode_str, "tracer")) {
    nvoverlay->mode = NVOVERLAY_MODE_TRACER;
    nvoverlay_intf = nvoverlay_intf_tracer;
    nvoverlay_init_tracer(nvoverlay);
  } else if(streq(mode_str, "picl")) {
    nvoverlay->mode = NVOVERLAY_MODE_PICL;
    nvoverlay_intf = nvoverlay_intf_picl;
    nvoverlay_init_picl(nvoverlay);
  } else {
    error_exit("Unknown mode in configuration: \"%s\"\n", mode_str);
  }
  printf("NVOverlay now operating in mode: %s (%d)\n", 
    nvoverlay_mode_names[nvoverlay->mode], nvoverlay->mode);
  return nvoverlay;
}

void nvoverlay_free(nvoverlay_t *nvoverlay) {
  switch(nvoverlay->mode) {
    case NVOVERLAY_MODE_FULL: {
      nvoverlay_free_full(nvoverlay);
    } break;
    case NVOVERLAY_MODE_TRACER: {
      nvoverlay_free_tracer(nvoverlay);
    } break;
    case NVOVERLAY_MODE_PICL: {
      nvoverlay_free_picl(nvoverlay);
    } break;
    default: {
      error_exit("Unknown operating mode: %d\n", nvoverlay->mode);
    }
  }
  conf_free(nvoverlay->conf);
  free(nvoverlay);
  return;
}

// Check whether CPU mask matches active cores in tracer
void nvoverlay_trace_driven_check_core_mask(nvoverlay_t *nvoverlay) {
  assert(nvoverlay->mode == NVOVERLAY_MODE_FULL);
  int tracer_core_count = tracer_get_core_count(nvoverlay->tracer);
  int cpu_core_count = cpu_get_core_count(nvoverlay->cpu);
  if(tracer_core_count != cpu_core_count) {
    printf("WARNING: Tracer core count (%d) does not equal CPU core count (%d)\n",
      tracer_core_count, cpu_core_count);
  }
  int core_count = tracer_core_count < cpu_core_count ? tracer_core_count : cpu_core_count;
  for(int i = 0;i < core_count;i++) {
    if(nvoverlay_core_mask_is_set(nvoverlay, i) == 0 && 
       tracer_get_core_record_count(nvoverlay->tracer, i) != 0) {
      printf("WARNING: Core %d has record but is not set in the mask\n", i);
      printf("Core mask: ");
      bitmap64_print_bitstr(&nvoverlay->core_mask);
      putchar('\n');
      error_exit("Please see above error message\n");
    } 
  }
  return;
}

void nvoverlay_trace_driven_start(nvoverlay_t *nvoverlay) {
  if(nvoverlay->trace_driven_enabled == 0) {
    error_exit("Trace-driven simulation is disabled (set \"nvoverlaay.trace_driven\" to \"true\" to enable it)\n");
  }
  tracer_t *tracer = nvoverlay->tracer;
  assert(tracer);
  printf("*** Starting trace-driven simulation\n");
  printf("0%%"); // Starting of the progress bar
  tracer_record_t *rec;
  tracer_begin(tracer);
  nvoverlay_trace_driven_cb_t cb = nvoverlay->trace_driven_cb;
  // Only check this for full mode
  if(nvoverlay->mode == NVOVERLAY_MODE_FULL) {
    nvoverlay_trace_driven_check_core_mask(nvoverlay);
  }
  uint64_t count = 0UL;
  int percent = 0;
  const uint64_t quantum = tracer_get_record_count(nvoverlay->tracer) / 50;
  while((rec = tracer_next(tracer)) != NULL) {
    if(cb) cb(nvoverlay, rec); 
    // Take sample if serial is large enough
    nvoverlay_stat_sample(nvoverlay, rec->serial);
    switch(rec->type) {
      case TRACER_LOAD: {
        nvoverlay_intf.load_cb(nvoverlay, rec->id, rec->line_addr, rec->cycle, rec->serial);
      } break;
      case TRACER_STORE: {
        nvoverlay_intf.store_cb(nvoverlay, rec->id, rec->line_addr, rec->cycle, rec->serial);
      } break;
      case TRACER_L1_EVICT: {
        nvoverlay_intf.l1_evict_cb(nvoverlay, rec->id, rec->line_addr, rec->cycle, rec->serial);
      } break;
      case TRACER_L2_EVICT: {
        nvoverlay_intf.l2_evict_cb(nvoverlay, rec->id, rec->line_addr, rec->cycle, rec->serial);
      } break;
      case TRACER_L3_EVICT: {
        nvoverlay_intf.l3_evict_cb(nvoverlay, rec->id, rec->line_addr, rec->cycle, rec->serial);
      } break;
      case TRACER_INST: { // Non-functional, only reports instruction count
        nvoverlay_full_inst(nvoverlay, rec->id, rec->cycle);
      } break;
      case TRACER_CYCLE: {
        nvoverlay_full_cycle(nvoverlay, rec->id, rec->cycle);
      } break;
      default: {
        error_exit("Unknown record type: %d\n", rec->type);
      } break;
    }
    // Update progress bar
    count++;
    if(count == quantum) {
      count = 0UL;
      putchar('.');
      // Percentage
      percent += 2;
      if(percent % 10 == 0) printf("%d%%", percent);
      fflush(stdout); // Force display
    }
  }
  putchar('\n');
  printf("*** Finished trace-driven simulation. Total records: %lu\n", tracer_get_record_count(nvoverlay->tracer));
  // Self-check on data structures
  nvoverlay_stat_print(nvoverlay);
  nvoverlay_selfcheck(nvoverlay);
  // Destroy the object
  nvoverlay_free(nvoverlay);
  exit(0);
}

// Check whether trace driven is enabled, and initialize the tracer if true
void nvoverlay_trace_driven_init(nvoverlay_t *nvoverlay) {
  if(nvoverlay->mode == NVOVERLAY_MODE_TRACER) {
    error_exit("Trace driven mode is not supported for tracer mode\n");
  }
  conf_t *conf = nvoverlay->conf;
  int td;
  int ret = conf_find_bool(conf, "nvoverlay.trace_driven", &td);
  if(ret == 0 || td == 0) return; // Did not find trace driven, or it is set to false, just exit
  int tracer_core_count = conf_find_int32_range(conf, "tracer.cores", 1, CORE_COUNT_MAX, CONF_RANGE);
  char *filename = conf_find_str_mandatory(conf, "tracer.filename");
  assert(nvoverlay->tracer == NULL);
  nvoverlay->tracer = tracer_init(filename, tracer_core_count, TRACER_MODE_READ);
  nvoverlay->trace_driven_enabled = 1;
  printf("*** Trace driven enabled (file %s cores %d)\n", filename, tracer_core_count);
  //nvoverlay_trace_driven_engine(nvoverlay);
  return;
}

// Mode and conf are set
void nvoverlay_init_full(nvoverlay_t *nvoverlay) {
  conf_t *conf = nvoverlay->conf;
  assert(nvoverlay->mode == NVOVERLAY_MODE_FULL);
  // NVM device 
  int nvm_rlat = conf_find_int32_range(conf, "nvm.rlat", 0, CONF_INT32_MAX, CONF_RANGE);
  int nvm_wlat = conf_find_int32_range(conf, "nvm.wlat", 0, CONF_INT32_MAX, CONF_RANGE);
  int nvm_banks = conf_find_int32_range(conf, "nvm.banks", 1, CONF_INT32_MAX, CONF_RANGE | CONF_POWER2);
  nvoverlay->nvm = nvm_init(nvm_banks, nvm_rlat, nvm_wlat);
  // OMC buffer
  int omcbuf_sets = conf_find_int32_range(conf, "omcbuf.sets", 1, CONF_INT32_MAX, CONF_RANGE | CONF_POWER2);
  int omcbuf_ways = conf_find_int32_range(conf, "omcbuf.ways", 1, CONF_INT32_MAX, CONF_RANGE | CONF_POWER2);
  nvoverlay->omcbuf = omcbuf_init(omcbuf_sets, omcbuf_ways, nvoverlay_omcbuf_evict_cb);
  omcbuf_set_parent(nvoverlay->omcbuf, nvoverlay);
  // Overlay
  nvoverlay->overlay = overlay_init();
  // Master Mapping Table
  nvoverlay->omt = omt_init();
  // Version table (vtable)
  nvoverlay->vtable = \
    vtable_init(nvoverlay_vtable_evict_cb, nvoverlay_vtable_core_recv_cb, nvoverlay_vtable_core_tag_cb);
  vtable_set_parent(nvoverlay->vtable, nvoverlay);
  // CPU
  int cpu_core_count = conf_find_int32_range(conf, "cpu.cores", 1, CORE_COUNT_MAX, CONF_RANGE);
  nvoverlay->cpu = cpu_init(cpu_core_count, nvoverlay_cpu_tag_walk_cb);
  cpu_set_parent(nvoverlay->cpu, nvoverlay);
  // Read L1 and L2 cache configuration
  int l1_ways = conf_find_int32_range(conf, "cpu.l1.ways", 1, CONF_INT32_MAX, CONF_RANGE);
  uint64_t l1_size = conf_find_uint64_range(conf, "cpu.l1.size", 0, 0, CONF_SIZE); // Size suffix
  if(l1_size % UTIL_CACHE_LINE_SIZE != 0) {
    error_exit("L1 size is not a multiple of cache lines (see %lu)\n", l1_size);
  } else if((l1_size / UTIL_CACHE_LINE_SIZE) % l1_ways != 0) {
    error_exit("L1 size is not a multiple of L1 ways (size %lu ways %d)\n", l1_size, l1_ways);
  }
  int l1_sets = (l1_size / UTIL_CACHE_LINE_SIZE) / l1_ways;
  int l2_ways = conf_find_int32_range(conf, "cpu.l2.ways", 1, CONF_INT32_MAX, CONF_RANGE);
  uint64_t l2_size = conf_find_uint64_range(conf, "cpu.l2.size", 0, 0, CONF_SIZE); // Size suffix
  if(l2_size % UTIL_CACHE_LINE_SIZE != 0) {
    error_exit("L2 size is not a multiple of cache lines (see %lu)\n", l2_size);
  } else if((l2_size / UTIL_CACHE_LINE_SIZE) % l2_ways != 0) {
    error_exit("L2 size is not a multiple of L2 ways (size %lu ways %d)\n", l2_size, l2_ways);
  }
  int l2_sets = (l2_size / UTIL_CACHE_LINE_SIZE) / l2_ways;
  cpu_tag_init(nvoverlay->cpu, CPU_TAG_L1, l1_sets, l1_ways);
  cpu_tag_init(nvoverlay->cpu, CPU_TAG_L2, l2_sets, l2_ways);
  // Read epoch size
  nvoverlay->epoch_size = conf_find_uint64_range(conf, "nvoverlay.epoch_size", 1, CONF_UINT64_MAX, CONF_RANGE | CONF_ABBR);
  // Read tag walk freq
  nvoverlay->tag_walk_freq = conf_find_uint64_range(conf, "nvoverlay.tag_walk_freq", 1, CONF_UINT64_MAX, CONF_RANGE | CONF_ABBR);
  // Epochs 
  nvoverlay->last_stable_epoch = 0UL;
  nvoverlay->stable_epochs = (uint64_t *)malloc(sizeof(uint64_t) * cpu_get_core_count(nvoverlay->cpu));
  SYSEXPECT(nvoverlay->stable_epochs != NULL);
  memset(nvoverlay->stable_epochs, 0x00, sizeof(uint64_t) * cpu_get_core_count(nvoverlay->cpu));
  // Must be after all options are read
  conf_print_unused(conf);
  // Initialize the stable epoch mask to all-one, i.e. we use all cores
  bitmap64_set_all_one(&nvoverlay->core_mask);
  int passive_ret = conf_find_bool(conf, "nvoverlay.tag_walk_passive", &nvoverlay->tag_walk_passive);
  if(passive_ret == 0) nvoverlay->tag_walk_passive = 0; // Proactive tag walk by default
  // Sample frequency - if not then by default set to zero
  if(conf_exists(conf, "nvoverlay.sample_freq") == 1) {
    nvoverlay->sample_freq = conf_find_uint64_range(conf, "nvoverlay.sample_freq", 0, 0, CONF_ABBR);
    nvoverlay->samples = fifo_init();
    // Report error if not found, since in this branch we must take samples
    const char *filename = conf_find_str_mandatory(conf, "nvoverlay.sample_filename");
    // NVOverlay owns the file name string, should free it later
    nvoverlay->sample_filename = (char *)malloc(strlen(filename) + 1);
    SYSEXPECT(nvoverlay->sample_filename != NULL);
    strcpy(nvoverlay->sample_filename, filename);
    assert(nvoverlay_sample_is_enabled(nvoverlay) == 1);
  } else {
    nvoverlay->sample_freq = 0UL; // 0 means no sample
    nvoverlay->samples = NULL;
    nvoverlay->sample_filename = NULL;
    assert(nvoverlay_sample_is_enabled(nvoverlay) == 0);
  }
  // Verbose print flag
  if(conf_exists(conf, "nvoverlay.stat_verbose") == 1) {
    nvoverlay->stat_verbose = conf_find_bool_mandatory(conf, "nvoverlay.stat_verbose");
  } else {
    nvoverlay->stat_verbose = 0;
  }
  // Caps
  if(conf_exists(conf, "nvoverlay.inst_cap") == 1) {
    nvoverlay->inst_cap = conf_find_uint64_range(conf, "nvoverlay.inst_cap", 
      0UL, CONF_UINT64_MAX, CONF_ABBR | CONF_RANGE);
  } else {
    nvoverlay->inst_cap = 0UL; // 0 means no cap
  }
  if(conf_exists(conf, "nvoverlay.cycle_cap") == 1) {
    nvoverlay->cycle_cap = conf_find_uint64_range(conf, "nvoverlay.cycle_cap", 
      0UL, CONF_UINT64_MAX, CONF_ABBR | CONF_RANGE);
  } else {
    nvoverlay->cycle_cap = 0UL; // 0 means no cap
  }
  if(nvoverlay->cycle_cap != 0UL && nvoverlay->inst_cap != 0UL) {
    error_exit("At most one of cycle_cap and inst_cap can be set for NVOverlay\n");
  }
  // Set to core count, and drop to zero
  nvoverlay->cap_core_count = cpu_get_core_count(nvoverlay->cpu);
  // Trace-driven - This must be at the very end of the function
  nvoverlay_trace_driven_init(nvoverlay);
  return;
}

void nvoverlay_free_full(nvoverlay_t *nvoverlay) {
  if(nvoverlay_sample_is_enabled(nvoverlay)) {
    // Dump stat first if sampling is enabled
    printf("Dumping statistics to %d files...\n", SAMPLE_COUNT);
    nvoverlay_stat_dump(nvoverlay);
    printf("Done\n");
    assert(nvoverlay->samples != NULL);
    assert(nvoverlay->sample_filename != NULL);
    free(nvoverlay->sample_filename);
    fifo_free(nvoverlay->samples);
  }
  free(nvoverlay->stable_epochs);
  cpu_free(nvoverlay->cpu);
  vtable_free(nvoverlay->vtable);
  omt_free(nvoverlay->omt);
  overlay_free(nvoverlay->overlay);
  omcbuf_free(nvoverlay->omcbuf);
  nvm_free(nvoverlay->nvm);
  // When trace driven is enabled we should also free the tracer
  if(nvoverlay->tracer) {
    assert(nvoverlay->trace_driven_enabled == 1);
    tracer_free(nvoverlay->tracer);
  }
  return;
}

void nvoverlay_init_tracer(nvoverlay_t *nvoverlay) {
  conf_t *conf = nvoverlay->conf;
  char *filename = conf_find_str_mandatory(conf, "tracer.filename");
  int tracer_core_count = conf_find_int32_range(conf, "tracer.cores", 1, CORE_COUNT_MAX, CONF_RANGE);
  char *tracer_mode_str = conf_find_str_mandatory(conf, "tracer.mode");
  // Set traver mode (mandatory)
  int tracer_mode;
  if(streq(tracer_mode_str, "read")) {
    tracer_mode = TRACER_MODE_READ;
  } else if(streq(tracer_mode_str, "write")) {
    tracer_mode = TRACER_MODE_WRITE;
  } else {
    error_exit("Unknown tracer mode in configuration: \"%s\"\n", tracer_mode_str);
  }
  nvoverlay->tracer = tracer_init(filename, tracer_core_count, tracer_mode);
  // Set clean up
  int cleanup;
  int cleanup_ret = conf_find_bool(conf, "tracer.cleanup", &cleanup);
  if(cleanup_ret == 1) {
    if(cleanup == 1) tracer_set_cleanup(nvoverlay->tracer, TRACER_REMOVE_FILE);
    else tracer_set_cleanup(nvoverlay->tracer, TRACER_KEEP_FILE);
  }
  // Set cap mode (optional)
  char *tracer_cap_mode_str;
  int cap_mode_ret = conf_find_str(conf, "tracer.cap_mode", &tracer_cap_mode_str);
  if(cap_mode_ret == 1) {
    // Allows using abbreviation
    uint64_t tracer_cap = conf_find_uint64_range(conf, "tracer.cap", 0, 0, CONF_ABBR);
    int tracer_cap_mode;
    if(streq(tracer_cap_mode_str, "load")) {
      tracer_cap_mode = TRACER_CAP_MODE_LOAD;
    } else if(streq(tracer_cap_mode_str, "store")) {
      tracer_cap_mode = TRACER_CAP_MODE_STORE;
    } else if(streq(tracer_cap_mode_str, "inst")) {
      tracer_cap_mode = TRACER_CAP_MODE_INST;
    } else if(streq(tracer_cap_mode_str, "memop")) {
      tracer_cap_mode = TRACER_CAP_MODE_MEMOP;
    } else if(streq(tracer_cap_mode_str, "none")) {
      tracer_cap_mode = TRACER_CAP_MODE_NONE;
    } else {
      error_exit("Unknown tracer cap mode: \"%s\"\n", tracer_cap_mode_str);
    }
    tracer_set_cap_mode(nvoverlay->tracer, tracer_cap_mode, tracer_cap);
  }
  return;
}

void nvoverlay_free_tracer(nvoverlay_t *nvoverlay) {
  tracer_free(nvoverlay->tracer);
}

// Picl also uses NVM to compute timing
void nvoverlay_init_picl(nvoverlay_t *nvoverlay) {
  conf_t *conf = nvoverlay->conf;
  int nvm_rlat = conf_find_int32_range(conf, "nvm.rlat", 0, CONF_INT32_MAX, CONF_RANGE);
  int nvm_wlat = conf_find_int32_range(conf, "nvm.wlat", 0, CONF_INT32_MAX, CONF_RANGE);
  int nvm_banks = conf_find_int32_range(conf, "nvm.banks", 1, CONF_INT32_MAX, CONF_RANGE | CONF_POWER2);
  nvoverlay->nvm = nvm_init(nvm_banks, nvm_rlat, nvm_wlat);
  picl_t *picl = nvoverlay->picl = picl_init(nvoverlay_picl_evict_cb); // Pass the evict cb
  picl_set_parent(picl, nvoverlay);
  // CPU
  int cpu_core_count = conf_find_int32_range(conf, "cpu.cores", 1, CORE_COUNT_MAX, CONF_RANGE);
  nvoverlay->cpu = cpu_init(cpu_core_count, NULL);
  cpu_set_parent(nvoverlay->cpu, NULL); // We do not use any call back, just set to NULL
  // Allows using K or M suffix, range [1, +inf)
  uint64_t epoch_size = conf_find_uint64_range(conf, "picl.epoch_size", 
    1, CONF_UINT64_MAX, CONF_RANGE | CONF_ABBR);
  picl_set_epoch_size(picl, epoch_size);
  // Verbose print flag
  if(conf_exists(conf, "nvoverlay.stat_verbose") == 1) {
    nvoverlay->stat_verbose = conf_find_bool_mandatory(conf, "nvoverlay.stat_verbose");
  } else {
    nvoverlay->stat_verbose = 0;
  }
  // Caps
  if(conf_exists(conf, "nvoverlay.inst_cap") == 1) {
    nvoverlay->inst_cap = conf_find_uint64_range(conf, "nvoverlay.inst_cap", 
      0UL, CONF_UINT64_MAX, CONF_ABBR | CONF_RANGE);
  } else {
    nvoverlay->inst_cap = 0UL; // 0 means no cap
  }
  if(conf_exists(conf, "nvoverlay.cycle_cap") == 1) {
    nvoverlay->cycle_cap = conf_find_uint64_range(conf, "nvoverlay.cycle_cap", 
      0UL, CONF_UINT64_MAX, CONF_ABBR | CONF_RANGE);
  } else {
    nvoverlay->cycle_cap = 0UL; // 0 means no cap
  }
  if(nvoverlay->cycle_cap != 0UL && nvoverlay->inst_cap != 0UL) {
    error_exit("At most one of cycle_cap and inst_cap can be set for NVOverlay\n");
  }
  // Set to core count, and drop to zero
  nvoverlay->cap_core_count = cpu_get_core_count(nvoverlay->cpu);
  // Trace-driven
  nvoverlay_trace_driven_init(nvoverlay);
  return;
}

void nvoverlay_free_picl(nvoverlay_t *nvoverlay) {
  picl_free(nvoverlay->picl);
  nvm_free(nvoverlay->nvm);
  if(nvoverlay->tracer) {
    assert(nvoverlay->trace_driven_enabled == 1);
    tracer_free(nvoverlay->tracer);
  }
  return;
}

void nvoverlay_core_mask_add(nvoverlay_t *nvoverlay, int pos) {
  if(pos < 0 || pos > BITMAP64_MAX_POS) {
    error_exit("Illegal core ID (valid [0, %d], see %d)\n", pos, BITMAP64_MAX_POS);
  }
  bitmap64_add(&nvoverlay->core_mask, pos);
  return;
}

void nvoverlay_core_mask_remove(nvoverlay_t *nvoverlay, int pos) {
  if(pos < 0 || pos > BITMAP64_MAX_POS) {
    error_exit("Illegal core ID (valid [0, %d], see %d)\n", pos, BITMAP64_MAX_POS);
  }
  bitmap64_remove(&nvoverlay->core_mask, pos);
  return;
}

void nvoverlay_core_mask_set(nvoverlay_t *nvoverlay, int pos) { 
  if(pos < 0 || pos > BITMAP64_MAX_POS) {
    error_exit("Illegal core ID (valid [0, %d], see %d)\n", pos, BITMAP64_MAX_POS);
  }
  nvoverlay_core_mask_clear(nvoverlay); 
  nvoverlay_core_mask_add(nvoverlay, pos);
  return;
}

uint64_t nvoverlay_get_min_epoch(nvoverlay_t *nvoverlay) {
  cpu_t *cpu = nvoverlay->cpu;
  uint64_t min_epoch = -1UL;
  for(int i = 0;i < nvoverlay->cpu->core_count;i++) {
    if(nvoverlay_core_mask_is_set(nvoverlay, i) == 0) {
      continue; // If the core is not in active core list just skip
    } else if(cpu->cores[i].epoch < min_epoch) {
      min_epoch = cpu->cores[i].epoch;
    }
  }
  return min_epoch;
}

uint64_t nvoverlay_get_max_epoch(nvoverlay_t *nvoverlay) {
  cpu_t *cpu = nvoverlay->cpu;
  uint64_t max_epoch = 0UL;
  for(int i = 0;i < nvoverlay->cpu->core_count;i++) {
    if(nvoverlay_core_mask_is_set(nvoverlay, i) == 0) {
      continue; // If the core is not in active core list just skip
    } else if(cpu->cores[i].epoch > max_epoch) {
      max_epoch = cpu->cores[i].epoch;
    }
  }
  return max_epoch;
}

void nvoverlay_selfcheck(nvoverlay_t *nvoverlay) {
  printf("Performing self check.. (mode = %s)\n", nvoverlay_mode_names[nvoverlay->mode]);
  switch(nvoverlay->mode) {
    case NVOVERLAY_MODE_FULL: nvoverlay_selfcheck_full(nvoverlay); break;
    case NVOVERLAY_MODE_PICL: nvoverlay_selfcheck_picl(nvoverlay); break;
    default: break;
  }
  printf("Pass!\n");
  return;
}

// NVOverlay consistency check
void nvoverlay_selfcheck_full(nvoverlay_t *nvoverlay) {
  assert(nvoverlay->mode == NVOVERLAY_MODE_FULL);
  omt_selfcheck(nvoverlay->omt);
  overlay_selfcheck(nvoverlay->overlay);
  // This checks tag array and vtable consistency
  cpu_tag_selfcheck_all(nvoverlay->cpu, nvoverlay->vtable);
  // This only checks CPU itself
  cpu_selfcheck(nvoverlay->cpu);
  // Cross check
  // Core evict + vtable evict = omcbuf access
  uint64_t core_tw_evict_count = 0UL;
  for(int i = 0;i < cpu_get_core_count(nvoverlay->cpu);i++) {
    core_t *core = cpu_get_core(nvoverlay->cpu, i);
    core_tw_evict_count += core->tag_walk_evict_count;
  }
  if(core_tw_evict_count + vtable_get_omc_evict_count(nvoverlay->vtable) != 
     omcbuf_get_access_count(nvoverlay->omcbuf)) {
    error_exit("Inconsistent evict count and omcbuf access count: core %lu + vtable %lu != omcbuf %lu\n",
      core_tw_evict_count, vtable_get_omc_evict_count(nvoverlay->vtable), 
      omcbuf_get_access_count(nvoverlay->omcbuf));
  }
  // omcbuf evict = nvm write
  if(omcbuf_get_evict_count(nvoverlay->omcbuf) != nvm_get_write_count(nvoverlay->nvm)) {
    error_exit("Inconsistent omcbuf evict and NVM write: %lu != %lu\n",
      omcbuf_get_evict_count(nvoverlay->omcbuf), nvm_get_write_count(nvoverlay->nvm));
  }
  return;
}

// Performs picl consistency check
void nvoverlay_selfcheck_picl(nvoverlay_t *nvoverlay) {
  assert(nvoverlay->mode == NVOVERLAY_MODE_PICL);
  picl_t *picl = nvoverlay->picl;
  nvm_t *nvm = nvoverlay->nvm;
  // Cross-check: Eviction count must equal NVM write count
  if(picl_get_total_evict_count(picl) != nvm_get_write_count(nvm)) {
    error_exit("Inconsistent evict and NVM write: %lu and %lu\n",
      picl_get_total_evict_count(picl), nvm_get_write_count(nvm));
  }
  return;
}

void nvoverlay_conf_print(nvoverlay_t *nvoverlay) {
  printf("++++++++++ nvoverlay_t conf ++++++++++\n");
  switch(nvoverlay->mode) {
    case NVOVERLAY_MODE_FULL: {
      conf_conf_print(nvoverlay->conf);
      omt_conf_print(nvoverlay->omt);
      cpu_conf_print(nvoverlay->cpu);
      vtable_conf_print(nvoverlay->vtable);
      omcbuf_conf_print(nvoverlay->omcbuf);
      overlay_conf_print(nvoverlay->overlay);
      nvm_conf_print(nvoverlay->nvm);
      printf("---------- nvoverlay_t ----------\n");
      printf("Epoch size %lu tag walk freq %lu passive walk %d sample freq %lu\n", 
        nvoverlay->epoch_size, nvoverlay->tag_walk_freq, nvoverlay->tag_walk_passive, nvoverlay->sample_freq);
      printf("Core mask ");
      bitmap64_print_bitstr(&nvoverlay->core_mask);
      putchar('\n');
      printf("Stat verbose %d\n", nvoverlay->stat_verbose);
      printf("Cap inst %lu cycle %lu\n", nvoverlay->inst_cap, nvoverlay->cycle_cap);
    } break;
    case NVOVERLAY_MODE_PICL: {
      nvm_conf_print(nvoverlay->nvm);
      picl_conf_print(nvoverlay->picl);
    } break;
    case NVOVERLAY_MODE_TRACER: {
      tracer_conf_print(nvoverlay->tracer);
    } break;
    default: {
      error_exit("Unknown NVOverlay mode: %d\n", nvoverlay->mode);
    } break;
  }
  return;
}

void nvoverlay_stat_print(nvoverlay_t *nvoverlay) {
  printf("++++++++++ nvoverlay_t stat ++++++++++\n");
  switch(nvoverlay->mode) {
    case NVOVERLAY_MODE_FULL: {
      // Print tracer stat at the beginning if trace driven is enabled
      if(nvoverlay->trace_driven_enabled == 1) {
        tracer_stat_print(nvoverlay->tracer, nvoverlay->stat_verbose);
      }
      omt_stat_print(nvoverlay->omt);
      cpu_stat_print(nvoverlay->cpu, nvoverlay->stat_verbose);
      vtable_stat_print(nvoverlay->vtable);
      omcbuf_stat_print(nvoverlay->omcbuf);
      overlay_stat_print(nvoverlay->overlay);
      nvm_stat_print(nvoverlay->nvm);
      printf("---------- nvoverlay_t ----------\n");
      printf("Last stable epoch %lu; Total walks %lu\n", 
        nvoverlay->last_stable_epoch, nvoverlay->tag_walk_count);
      printf("Masked cores: curr epoch min %lu max %lu\n", 
        nvoverlay_get_min_epoch(nvoverlay), nvoverlay_get_max_epoch(nvoverlay));
      printf("Samples %lu (freq %lu %s) last sampled %lu file name %s\n", 
        nvoverlay->sample_count, nvoverlay->sample_freq, nvoverlay->sample_freq == 0UL ? "OFF" : "ON",
        nvoverlay->last_sample_serial, 
        nvoverlay->sample_filename ? nvoverlay->sample_filename : "None");
      // Although it is not stat, print here to help interpret data
      printf("Tag walk mode %s epoch size %lu\n", 
        nvoverlay->tag_walk_passive ? "Passive" : "Normal", nvoverlay->epoch_size);
      printf("Total NVM writes %lu size %lu bytes\n",
        nvm_get_write_count(nvoverlay->nvm) + omt_get_write_count(nvoverlay->omt),
        nvm_get_write_count(nvoverlay->nvm) * UTIL_CACHE_LINE_SIZE + omt_get_write_count(nvoverlay->omt) * 8UL);
      printf("Cap core count %d\n", nvoverlay->cap_core_count);
    } break;
    case NVOVERLAY_MODE_PICL: {
      if(nvoverlay->trace_driven_enabled == 1) {
        tracer_stat_print(nvoverlay->tracer, nvoverlay->stat_verbose);
      }
      cpu_stat_print(nvoverlay->cpu, nvoverlay->stat_verbose);
      nvm_stat_print(nvoverlay->nvm);
      picl_stat_print(nvoverlay->picl);
    } break;
    case NVOVERLAY_MODE_TRACER: {
      tracer_stat_print(nvoverlay->tracer, nvoverlay->stat_verbose);
    } break;
    default: {
      error_exit("Unknown NVOverlay mode: %d\n", nvoverlay->mode);
    } break;
  }
  return;
}

// Following are sampling functions

// These are used for both print and file name construction
const char *overlay_sample_names[SAMPLE_COUNT] = {
  "overlay_size", "omt_size", "epoch_skew",
};

// Note: All fields except serial are initialized. Caller should set serial
nvoverlay_sample_t *nvoverlay_sample_init(nvoverlay_t *nvoverlay) {
  nvoverlay_sample_t *sample = (nvoverlay_sample_t *)malloc(sizeof(nvoverlay_sample_t));
  SYSEXPECT(sample != NULL);
  memset(sample, 0x00, sizeof(nvoverlay_sample_t));
  // Initialize fields
  sample->data[SAMPLE_OMT_SIZE] = omt_get_size(nvoverlay->omt);
  sample->data[SAMPLE_OVERLAY_SIZE] = overlay_get_size(nvoverlay->overlay);
  uint64_t max_epoch = 0UL, min_epoch = -1UL;
  if(nvoverlay_core_mask_popcount(nvoverlay) == 0) {
    error_exit("There should be at least one active core\n");
  }
  for(int i = 0;i < cpu_get_core_count(nvoverlay->cpu);i++) {
    if(nvoverlay_core_mask_is_set(nvoverlay, i) == 0) continue; 
    core_t *core = cpu_get_core(nvoverlay->cpu, i);
    if(core->epoch > max_epoch) max_epoch = core->epoch;
    if(core->epoch < min_epoch) min_epoch = core->epoch;
  }
  assert(max_epoch >= min_epoch);
  sample->data[SAMPLE_EPOCH_SKEW] = max_epoch - min_epoch;
  return sample;
}

void nvoverlay_sample_free(nvoverlay_sample_t *sample) { 
  free(sample); 
}

// Sample is a global thing. We use serial number as sample
// This function can be called for any serial; We determine whether to take
// the sample inside the function
void nvoverlay_stat_sample(nvoverlay_t *nvoverlay, uint64_t serial) {
  // If sampling is turned off, do not do anything
  if(nvoverlay_sample_is_enabled(nvoverlay) == 0) {
    return;
  }
  assert(serial > nvoverlay->last_sample_serial);
  //printf("%lu\n", serial - nvoverlay->last_sample_serial);
  if(serial - nvoverlay->last_sample_serial >= nvoverlay->sample_freq) {
    nvoverlay->last_sample_serial = serial;
    nvoverlay->sample_count++;
    nvoverlay_sample_t *sample = nvoverlay_sample_init(nvoverlay);
    sample->serial = serial;
    fifo_insert(nvoverlay->samples, (uint64_t)sample);
  }
  return;
}

// Caller should free the returned file name buffer
char *nvoverlay_stat_dump_filename(nvoverlay_t *nvoverlay, int index) {
  if(index < 0 || index >= SAMPLE_COUNT) {
    error_exit("Illegal index for stat dump: %d (should be [0, %d))\n",
      index, SAMPLE_COUNT);
  }
  const char *prefix = nvoverlay->sample_filename;
  // prefix + "_" + name + ".txt" + '\0'
  int len = strlen(prefix) + 1 + strlen(overlay_sample_names[index]) + 4 + 1;
  char *ret = (char *)malloc(len);
  SYSEXPECT(ret != NULL);
  strcpy(ret, prefix);
  strcat(ret, "_");
  strcat(ret, overlay_sample_names[index]);
  strcat(ret, ".txt");
  return ret;
}

// Dumps all stats into seperate files, using sample_filename as prefix
// Printing format: [serial number] [data point]
void nvoverlay_stat_dump(nvoverlay_t *nvoverlay) {
  if(nvoverlay_sample_is_enabled(nvoverlay) == 0) {
    return;
  }
  fifo_t *samples = nvoverlay->samples;
  for(int i = 0;i < SAMPLE_COUNT;i++) {
    // First, compute the file name, and open the file
    char *filename = nvoverlay_stat_dump_filename(nvoverlay, i);
    FILE *fp = fopen(filename, "w");
    fifo_it_t it;
    fifo_begin(samples, &it);
    uint64_t count = 0UL;
    while(fifo_is_end(samples, &it) == 0) {
      // Get the i-th sample point
      uint64_t data = ((nvoverlay_sample_t *)fifo_it_value(&it))->data[i];
      uint64_t serial = ((nvoverlay_sample_t *)fifo_it_value(&it))->serial;
      // Text output, with a new line for each data point
      fprintf(fp, "%lu %lu\n", serial, data);
      count++;
      fifo_next(samples, &it);
    }
    assert(count == fifo_get_item_count(samples));
    (void)count;
    fclose(fp);
    free(filename);
  }
  return;
}

// Following are component call backs

static void nvoverlay_vtable_evict_cb(nvoverlay_t *nvoverlay, 
  uint64_t addr, int id, uint64_t version, uint64_t cycle, int evict_type) {
  assert(nvoverlay); // NULL means we forgot to call vtable_set_parent
  assert(evict_type == EVICT_BOTH || evict_type == EVICT_OMC);
  if(evict_type == EVICT_BOTH) {
    omcbuf_insert(nvoverlay->omcbuf, addr, version, cycle); // Evict to both (we do not model LLC here)
  } else {
    omcbuf_insert(nvoverlay->omcbuf, addr, version, cycle); // Evict to OMC only
  }
  (void)id;
  return;
}

static void nvoverlay_vtable_core_recv_cb(nvoverlay_t *nvoverlay, int id, uint64_t version) {
  assert(nvoverlay != NULL);
  cpu_core_recv(nvoverlay->cpu, id, version);
  return;
}

static void nvoverlay_vtable_core_tag_cb(nvoverlay_t *nvoverlay, int op, int level, int id, ver_t *ver) {
  assert(nvoverlay != NULL);
  // Comment out the following to disable tag simulation
  cpu_tag_op(nvoverlay->cpu, op, level, id, ver);
  return;
}

static void nvoverlay_omcbuf_evict_cb(nvoverlay_t *nvoverlay, uint64_t line_addr, uint64_t version, uint64_t cycle) {
  assert(nvoverlay != NULL);
  // When the omcbuf writes back a line, we: (1) Insert the line into the current overlay in DRAM; (2) Write the line 
  // into the overlay data page on NVM
  overlay_insert(nvoverlay->overlay, line_addr, version);
  nvm_write(nvoverlay->nvm, line_addr, cycle);
  return;
} 

static void nvoverlay_cpu_tag_walk_cb(nvoverlay_t *nvoverlay, uint64_t line_addr, int id, uint64_t version, uint64_t cycle) {
  assert(nvoverlay != NULL);
  // Tag walker also evict cache lines into omcbuf
  omcbuf_insert(nvoverlay->omcbuf, line_addr, version, cycle); 
  (void)id; 
  return;
} 

static void nvoverlay_picl_evict_cb(nvoverlay_t *nvoverlay, uint64_t line_addr, uint64_t cycle) {
  assert(nvoverlay != NULL);
  nvm_write(nvoverlay->nvm, line_addr, cycle);
  return;
}

// No-op call back functions; Does nothing
void nvoverlay_noop_noop(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial) {
  (void)nvoverlay; (void)id; (void)line_addr; (void)cycle; (void)serial;
}

void nvoverlay_full_load(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial) {
  core_t *core = cpu_get_core(nvoverlay->cpu, id);
  // VTABLE drives tag array, OMCBUF, overlay and core epochs
  vtable_l1_load(nvoverlay->vtable, line_addr, id, core_get_epoch(core), cycle);
  (void)serial;
  return;
}
void nvoverlay_full_store(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial) {
  cpu_t *cpu = nvoverlay->cpu;
  core_t *core = cpu_get_core(cpu, id);
  // This may trigger eviction, OMC write, overlay insert. Overlay GC is not included.
  vtable_l1_store(nvoverlay->vtable, line_addr, id, core_get_epoch(core), cycle);
  core->epoch_store_count++;
  core->total_store_count++;
  if(core->epoch_store_count > nvoverlay->epoch_size) {
    cpu_advance_epoch(cpu, id);
    assert(core->epoch_store_count == 0UL);
  }
  assert(core->epoch >= core->last_walk_epoch);
  // Determine whether we need tag walk on the core (tag walks are per-core)
  if(core->epoch - core->last_walk_epoch >= nvoverlay->tag_walk_freq) {
    // Perform tag walk
    if(nvoverlay->tag_walk_passive == 0) {
      // Proactive tag walk: Evict cache lines older than current epoch of the core
      cpu_tag_walk(cpu, id, cycle, core->epoch);  // Make sure no version < epoch exists
      nvoverlay->stable_epochs[id] = core->epoch; // Update stable epoch (all ver < current epoch are evicted)
    } else {
      // Passive tag walk: Only scan the tag array and return min version in L2;
      // It is guaranteed that no older version will be evicted in the future
      uint64_t stable_epoch = cpu_tag_walk_passive(cpu, id);
      if(stable_epoch > nvoverlay->stable_epochs[id]) {
        nvoverlay->stable_epochs[id] = stable_epoch;
      }
    }
    // Increment both local and global tag walk counter
    nvoverlay->tag_walk_count++;
    core->tag_walk_count++;
    core->last_walk_epoch = core->epoch;
    // Compute global stable epoch
    uint64_t min_epoch = -1UL; // Largest value
    for(int i = 0;i < cpu_get_core_count(nvoverlay->cpu);i++) {
      if(nvoverlay->stable_epochs[i] < min_epoch) {
        // Only use the core to compute min stable epoch if the bit is set
        if(nvoverlay_core_mask_is_set(nvoverlay, i)) {
          min_epoch = nvoverlay->stable_epochs[i];
        }
      }
    }
    // Merge overlay to the master table in range [stable, min_epoch)
    // -1UL means no core is compared in the previous loop
    if(min_epoch != -1UL && min_epoch > nvoverlay->last_stable_epoch) {
      // Also flush omcbuf to avoid writing cache lines back to overlay after the tag walk
      omcbuf_tag_walk(nvoverlay->omcbuf, min_epoch, cycle); 
      for(uint64_t epoch = nvoverlay->last_stable_epoch; epoch < min_epoch;epoch++) {
        // This may trigger page GC and overlay GC; overlay size may change
        overlay_epoch_merge(nvoverlay->overlay, epoch, nvoverlay->omt);
      }
      nvoverlay->last_stable_epoch = min_epoch;
    }
  }
  // TODO: TRACK OVERLAY SIZE IN TIME SERIES GRAPH
  (void)serial;
  return;
} 
void nvoverlay_full_l1_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial) {
  core_t *core = cpu_get_core(nvoverlay->cpu, id);
  vtable_l1_eviction(nvoverlay->vtable, line_addr, id, core_get_epoch(core), cycle);
  (void)serial;
  return;
}
void nvoverlay_full_l2_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial) {
  core_t *core = cpu_get_core(nvoverlay->cpu, id);
  vtable_l2_eviction(nvoverlay->vtable, line_addr, id, core_get_epoch(core), cycle);
  (void)serial;
  return;
}
void nvoverlay_full_l3_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial) {
  core_t *core = cpu_get_core(nvoverlay->cpu, id);
  vtable_l3_eviction(nvoverlay->vtable, line_addr, id, core_get_epoch(core), cycle);
  (void)serial;
  return;
}

void nvoverlay_tracer_load(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial) {
  tracer_insert(nvoverlay->tracer, TRACER_LOAD, id, line_addr, cycle, serial);
  return;
}
void nvoverlay_tracer_store(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial) {
  tracer_insert(nvoverlay->tracer, TRACER_STORE, id, line_addr, cycle, serial);
  return;
} 
void nvoverlay_tracer_l1_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial) {
  tracer_insert(nvoverlay->tracer, TRACER_L1_EVICT, id, line_addr, cycle, serial);
  return;
}
void nvoverlay_tracer_l2_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial) {
  tracer_insert(nvoverlay->tracer, TRACER_L2_EVICT, id, line_addr, cycle, serial);
  return;
}
void nvoverlay_tracer_l3_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial) {
  tracer_insert(nvoverlay->tracer, TRACER_L3_EVICT, id, line_addr, cycle, serial);
  return;
}

void nvoverlay_picl_load(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial) {
  // Nothing to do
  (void)nvoverlay; (void)line_addr; (void)id; (void)cycle; (void)serial;
  return;
}
void nvoverlay_picl_store(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial) {
  (void)id; (void)serial;
  picl_t *picl = nvoverlay->picl;
  picl_store(picl, line_addr, cycle);
  if(picl_get_epoch_store_count(picl) == picl_get_epoch_size(picl)) {
    picl_advance_epoch(picl, cycle);
  }
  return;
}
void nvoverlay_picl_l1_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial) {
  // Nothing to do
  (void)nvoverlay; (void)line_addr; (void)id; (void)cycle; (void)serial;
  return;
}
void nvoverlay_picl_l2_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial) {
  // Nothing to do
  (void)nvoverlay; (void)line_addr; (void)id; (void)cycle; (void)serial;
  return;
}
void nvoverlay_picl_l3_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial) {
  (void)id; (void)serial;
  picl_l3_eviction(nvoverlay->picl, line_addr, cycle);
  return;
}

// This is the call back function shared by all modes; This function accepts the same arguments plus an extra
// 64 bit integer in nvoverlay_intf object. It is called to fulfill all other purposes including some control tasks.
void nvoverlay_other(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial) {
  uint64_t arg = nvoverlay_intf.other_arg;
  switch(arg) {
    case NVOVERLAY_OTHER_HELLO: {
      nvoverlay_printf("Interface successfully attached. mode = %s\n", nvoverlay_mode_names[nvoverlay->mode]);
      break;
    } case NVOVERLAY_OTHER_INST: {
      // Under trace mode insert a special record, which will be executed later in other modes
      if(nvoverlay->mode == NVOVERLAY_MODE_TRACER) {
        tracer_insert(nvoverlay->tracer, TRACER_INST, id, 0UL, cycle, serial);
      } else {
        // Called for all other modes
        nvoverlay_full_inst(nvoverlay, id, cycle);
      }
      break;
    } case NVOVERLAY_OTHER_CONF: {
      nvoverlay_conf_print(nvoverlay);
      break;
    } case NVOVERLAY_OTHER_STAT: {
      nvoverlay_stat_print(nvoverlay);
      break;
    } case NVOVERLAY_OTHER_CYCLE: {
      if(nvoverlay->mode == NVOVERLAY_MODE_TRACER) {
        tracer_insert(nvoverlay->tracer, TRACER_CYCLE, id, 0UL, cycle, serial);
      } else {
        // Called for all other modes
        nvoverlay_full_cycle(nvoverlay, id, cycle);
      }
      break;
    } default: {
      nvoverlay_error("Unknown arg: %lu\n", arg);
      break;
    }
  }
  (void)nvoverlay; (void)id; (void)line_addr; (void)cycle; (void)serial;
  return;
}

// Report inst count on a core
void nvoverlay_full_inst(nvoverlay_t *nvoverlay, int id, uint64_t inst_count) {
  assert(id >= 0 && id < nvoverlay->cpu->core_count);
  core_t *core = cpu_get_core(nvoverlay->cpu, id);
  // Inst must be monotonically increasing
  assert(inst_count >= core->last_inst_count);
  nvoverlay->cpu->total_inst_count += (inst_count - core->last_inst_count);
  core->last_inst_count = inst_count;
  if(nvoverlay->inst_cap != 0UL && inst_count >= nvoverlay->inst_cap) {
    if(core->cap_status == CORE_CAP_STATUS_ACTIVE) {
      core->cap_status = CORE_CAP_STATUS_HALTED;
      nvoverlay->cap_core_count--;
      assert(nvoverlay->cap_core_count >= 0);
    }
  }
  return;
}

// Report cycle count on a core
void nvoverlay_full_cycle(nvoverlay_t *nvoverlay, int id, uint64_t cycle_count) {
  assert(id >= 0 && id < nvoverlay->cpu->core_count);
  core_t *core = cpu_get_core(nvoverlay->cpu, id);
  // Cycle count may decrease in OOO exec
  if(cycle_count > core->last_cycle_count) {
    nvoverlay->cpu->total_cycle_count += (cycle_count - core->last_cycle_count);
    core->last_cycle_count = cycle_count;
    // Check whether we need to exit
    if(nvoverlay->cycle_cap != 0UL && cycle_count >= nvoverlay->cycle_cap) {
      if(core->cap_status == CORE_CAP_STATUS_ACTIVE) {
        core->cap_status = CORE_CAP_STATUS_HALTED;
        nvoverlay->cap_core_count--;
        assert(nvoverlay->cap_core_count >= 0);
      }
    }
  }
  return;
}

//* zsim

// Make sure that the file is actually linked
void nvoverlay_hello_world() {
  nvoverlay_printf("zsim Hello World!\n");
  return;
}

// Check cache parameters
static void nvoverlay_check_conf_cache(nvoverlay_t *nvoverlay, const char *ways_key, 
  const char *size_key, int level, const char *name, int *ways_p, int *sets_p, int *size_p) {
  conf_t *conf = nvoverlay->conf;
  int mode = nvoverlay->mode;
  int ways = conf_find_int32_mandatory(conf, ways_key);
  if(mode == NVOVERLAY_MODE_FULL) {
    int cpu_ways = cpu_tag_get_ways(nvoverlay->cpu, level);
    if(cpu_ways !=ways) {
      error_exit("%s = %d; cpu_t %s ways = %d\n", ways_key, ways, name, cpu_ways);
    }
  }
  
  int size = conf_find_int32_mandatory(conf, size_key);
  int sets = 0;
  if(mode == NVOVERLAY_MODE_FULL) {
    int cpu_sets = cpu_tag_get_sets(nvoverlay->cpu, level);
    int lines = size / (int)UTIL_CACHE_LINE_SIZE;
    if(lines % ways != 0) {
      error_exit("%s lines %d must be a multiple of ways %d (size %d)\n", name, lines, ways, size);
    }
    sets = lines / ways;
    if(cpu_sets != sets) {
      error_exit("%s sets = %d; cpu_t L1 sets = %d\n", name, sets, cpu_sets);
    }
  }
  *ways_p = ways;
  *sets_p = sets;
  *size_p = size;
  return;
}

// Make sure that zsim and nvoverlay uses the same argument
void nvoverlay_check_conf(nvoverlay_t *nvoverlay) {
  conf_t *conf = nvoverlay->conf;
  int mode = nvoverlay->mode;
  int line_size = conf_find_int32_mandatory(conf, "zsim.sys.lineSize");
  if(line_size != UTIL_CACHE_LINE_SIZE) {
    error_exit("zsim.sys.lineSize = %d; UTIL_CACHE_LINE_SIZE = %lu\n", line_size, UTIL_CACHE_LINE_SIZE);
  }
  const char *core_count_key = "zsim.sys.cores.beefy.cores";
  int core_count = conf_find_int32_mandatory(conf, core_count_key);
  // Note: PiCL does not care about core count, since all cores run the same epoch
  if(mode == NVOVERLAY_MODE_TRACER) {
    assert(nvoverlay->tracer);
    int tracer_cc = tracer_get_core_count(nvoverlay->tracer);
    if(core_count != tracer_cc) {
      error_exit("%s = %d; tracer_t core count = %d\n", core_count_key, core_count, tracer_cc);
    }
  } else if(mode == NVOVERLAY_MODE_FULL) {
    assert(nvoverlay->cpu);
    int cpu_cc = cpu_get_core_count(nvoverlay->cpu);
    if(core_count != cpu_cc) {
      error_exit("%s = %d; cpu_t core count = %d\n", core_count_key, core_count, cpu_cc);
    }
  }
  // L1 and L2 parameters
  const char *l1_ways_key = "zsim.sys.caches.l1d.array.ways";
  const char *l1_size_key = "zsim.sys.caches.l1d.size";
  int l1_ways, l1_sets, l1_size;
  nvoverlay_check_conf_cache(nvoverlay, l1_ways_key, l1_size_key, CPU_TAG_L1, "L1", &l1_ways, &l1_sets, &l1_size);
  const char *l2_ways_key = "zsim.sys.caches.l2.array.ways";
  const char *l2_size_key = "zsim.sys.caches.l2.size";
  int l2_ways, l2_sets, l2_size;
  nvoverlay_check_conf_cache(nvoverlay, l2_ways_key, l2_size_key, CPU_TAG_L2, "L2", &l2_ways, &l2_sets, &l2_size);

  nvoverlay_printf("Finished cross-checking zsim/nvoverlay parameters. Here is a summary:\n");
  nvoverlay_printf("- Cache line size: %d bytes\n", line_size);
  nvoverlay_printf("- Core count: %d\n", core_count);
  nvoverlay_printf("- L1 ways %d sets %d (size %d)\n", l1_ways, l1_sets, l1_size);
  nvoverlay_printf("- L2 ways %d sets %d (size %d)\n", l2_ways, l2_sets, l2_size);
  if(l1_sets == 0) {
    assert(l2_sets == 0);
    nvoverlay_printf("  Sets = 0 is normal for non-nvoverlay designs\n");
  }
  return;
}
