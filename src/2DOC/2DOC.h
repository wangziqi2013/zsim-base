
#include "util.h"
#include <xmmintrin.h>
#include <emmintrin.h>
#include <immintrin.h>

// Forward declaration
struct cc_struct_t;

// Compression algorithm interface
typedef int (*comp_algo_t)(void *input, void *output, void *metadata);

// void * advance functions
inline static void *ptr_add_64(void *p) { return (void *)((uint64_t *)p + 1); }
inline static void *ptr_add_32(void *p) { return (void *)((uint32_t *)p + 1); }
inline static void *ptr_add_16(void *p) { return (void *)((uint16_t *)p + 1); }
inline static void *ptr_add_8(void *p) { return (void *)((uint8_t *)p + 1); }
inline static void *ptr_add(void *p, int offset) { return (void*)((uint8_t *)p + offset); }

// void * read functions
inline static uint64_t ptr_load_64(void *p) { return *(uint64_t *)p; }
inline static uint32_t ptr_load_32(void *p) { return *(uint32_t *)p; }
inline static uint16_t ptr_load_16(void *p) { return *(uint16_t *)p; }
inline static uint8_t ptr_load_8(void *p) { return *(uint8_t *)p; }

// void * pop functions - will read memory and move pointer (need pointer to pointers)
inline static uint64_t ptr_pop_64(void **p) { *p = ((uint64_t *)*p + 1); return ((uint64_t *)*p)[-1]; }
inline static uint32_t ptr_pop_32(void **p) { *p = ((uint32_t *)*p + 1); return ((uint32_t *)*p)[-1]; }
inline static uint16_t ptr_pop_16(void **p) { *p = ((uint16_t *)*p + 1); return ((uint16_t *)*p)[-1]; }
inline static uint8_t ptr_pop_8(void **p) { *p = ((uint8_t *)*p + 1); return ((uint8_t *)*p)[-1]; }

// void * write functions
inline static void ptr_store_64(void *p, uint64_t value) { *(uint64_t *)p = value; }
inline static void ptr_store_32(void *p, uint32_t value) { *(uint32_t *)p = value; }
inline static void ptr_store_16(void *p, uint16_t value) { *(uint16_t *)p = value; }
inline static void ptr_store_8(void *p, uint8_t value) { *(uint8_t *)p = value; }

// void * append functions - append will move the pointer
inline static void *ptr_append_64(void *p, uint64_t value) { ptr_store_64(p, value); return ptr_add_64(p); }
inline static void *ptr_append_32(void *p, uint32_t value) { ptr_store_32(p, value); return ptr_add_32(p); }
inline static void *ptr_append_16(void *p, uint16_t value) { ptr_store_16(p, value); return ptr_add_16(p); }
inline static void *ptr_append_8(void *p, uint8_t value) { ptr_store_8(p, value); return ptr_add_8(p); }

// Advance an address by certain number of cache lines 
inline static uint64_t cache_addr_add(uint64_t addr, int offset) { return addr + offset * UTIL_CACHE_LINE_SIZE; }

//* BDI

typedef struct {
  int id;
  int word_size;            // In bytes
  int target_size;          // In bytes
  int compressed_size;      // In bytes
  int mask_size;            // In bytes, this is the number of bytes in all per-word masks
  int type;                 // 4-bit type for the compression parameter; type 0, 1, 15 are for other purposes
  int body_offset;          // Offset to compressed code words, in bytes
  // Note that the high mask should only cover bits that are in word_size; bits not in word_size are set to 0
  uint64_t high_mask;       // High-1 masks for test whether high bits + sign bit are zeros or ones; Always 64 bytes
} BDI_param_t;

extern BDI_param_t BDI_types[8]; // Selected using "type" field; Invalid entries that are not BDI will be NULL
extern const char *BDI_names[8];  // String names of BDI types; indexed using runtime type
extern int BDI_comp_order[6];     // Order of types we try for compression

// BDI type in the run time
#define BDI_TYPE_INVALID   -1  // Could not compress; Returned by BDI_comp() and BDI_decomp()
#define BDI_TYPE_NOT_FOUND -2  // Could not be found; Returned by dmap_find_compressed()   

// These two are currently not used
#define BDI_ZERO        0
#define BDI_DUP         1
// Index the BDI_types array
#define BDI_TYPE_BEGIN  2
#define BDI_8_1         2
#define BDI_8_2         3
#define BDI_8_4         4
#define BDI_4_1         5
#define BDI_4_2         6
#define BDI_2_1         7
#define BDI_TYPE_END    8

// Helper function that writes/reads the base of the given size into the output buffer
void *BDI_comp_write_base(void *out_buf, uint64_t base, int word_size);
uint64_t BDI_decomp_read_base(void **in_buf, int word_size);
// Helper function that writes/reads the small value bitmap to the buffer
void *BDI_comp_write_bitmap(void *out_buf, uint64_t bitmap, int word_size);
uint64_t BDI_decomp_read_bitmap(void **in_buf, int word_size);

// Packs uint64_t integers to compact representation of word_size
void BDI_pack(void *out_buf, uint64_t *in_buf, int word_size, int iter);
// Unpacks word_size integers as uint64_t for easier coding
void BDI_unpack(uint64_t *out_buf, void *in_buf, int word_size, int iter);

// Just for fun
//int BDI_comp_8_1_AVX512(void *out_buf, void *in_buf);

// AVX2 implementation of BDI, fully compatible with the serial version
int BDI_comp_8_AVX2(void *out_buf, void *in_buf, int target_size, int dry_run);
int BDI_comp_4_AVX2(void *out_buf, void *in_buf, int target_size, int dry_run);
int BDI_comp_2_AVX2(void *out_buf, void *in_buf, int target_size, int dry_run);
void BDI_decomp_8_AVX2(void *out_buf, void *in_buf, int target_size);
void BDI_decomp_4_AVX2(void *out_buf, void *in_buf, int target_size);
void BDI_decomp_2_AVX2(void *out_buf, void *in_buf, int target_size);

// BDI_param_t stores the compression parameters
// dry_run = 1 means we only check whether the given scheme is feasible, and will not write out_buf
int BDI_comp_scalar(void *out_buf, void *in_buf, BDI_param_t *param, int dry_run);
void BDI_decomp_scalar(void *out_buf, void *in_buf, BDI_param_t *param);

int BDI_comp_AVX2(void *out_buf, void *in_buf, BDI_param_t *param, int dry_run);
void BDI_decomp_AVX2(void *out_buf, void *in_buf, BDI_param_t *param);

// Returns type of compression used; BDI_TYPE_INVALID (-1) if uncompressable
int BDI_comp(void *out_buf, void *in_buf);
int BDI_get_comp_size(void *in_buf);
void BDI_decomp(void *out_buf, void *in_buf, int type);

// Print compressed line in text format for debugging
// This is irrelevant to the compression function used. Always valid.
void BDI_print_compressed(void *in_buf, BDI_param_t *param);

// This function compares the body of the two buffers, and computes the size of compressed data
// Assume in_buf has already been compressed
int BDI_vertical_comp(void *_base_buf, void *_in_buf, int type);

// Returns 1 if all bits are zero
int zero_comp(void *in_buf);

//* FPC

#define FPC_STATE_NORMAL         0
#define FPC_STATE_ZERO_RUN       1
#define FPC_BIT_COPY_GRANULARITY 64

// Number of bits of data payload, indexed by type
extern int FPC_data_bits[8];

int FPC_pack_bits(void *dest, int dest_offset, uint64_t *src, int bits);
int FPC_pack_bits_bmi(void *_dest, int dest_offset, uint64_t *src, int bits);
int FPC_unpack_bits(uint64_t *dest, void *src, int src_offset, int bits);
int FPC_unpack_bits_bmi(uint64_t *dest, void *_src, int src_offset, int bits);
// Returns the number of bits of a successful compression
int FPC_comp(void *out_buf, void *_in_buf); // Generate output data, return comp'ed size in bits
int FPC_get_comp_size_bits(void *_in_buf);  // Dry run only; Return comp size in bits
void FPC_decomp(void *_out_buf, void *in_buf);
void FPC_print_compressed(void *in_buf);
void FPC_print_packed_bits(uint64_t *buf, int begin_offset, int bits);

//* CPACK

#define CPACK_DICT_COUNT  16    // Number of entries in the dictionary
#define CPACK_WORD_COUNT 16    // Number of 32-bit words in a block to be compressed

typedef struct {
  int count;                      // Number of valid entries
  int next_slot_index;            // Index of the next usable slot
  uint32_t data[CPACK_DICT_COUNT]; // First entry is always MRU, last LRU
} CPACK_dict_t;

inline static void CPACK_dict_invalidate(CPACK_dict_t *dict) {
  dict->count = 0;
  dict->next_slot_index = 0;
  return;
}

// Get a free entry, possibly evicting the LRU entry
inline static int CPACK_dict_get_free_entry_index(CPACK_dict_t *dict) {
  if(dict->next_slot_index == CPACK_DICT_COUNT) {
    dict->next_slot_index = 0;
  }
  return dict->next_slot_index++;
}

// This function also updates LRU
inline static void CPACK_dict_insert(CPACK_dict_t *dict, uint32_t data) {
  int index = CPACK_dict_get_free_entry_index(dict);
  dict->data[index] = data;
  if(dict->count != CPACK_DICT_COUNT) {
    dict->count++;
  }
  return;
}

// Search pattern match of the word; output stores the output, return value is # of bits in the output
int CPACK_search(CPACK_dict_t *dict, uint32_t word, uint64_t *output);

int CPACK_comp(void *out_buf, void *_in_buf);
void CPACK_decomp(void *out_buf, void *in_buf);
int CPACK_get_comp_size_bits(void *_in_buf); // Dry run only, no output buffer

void CPACK_print_compressed(void *in_buf);

//* MBD - Experimental Multi-Based Delta compression

#define MBD_DICT_COUNT        16
#define MBD_DICT_INDEX_BITS   4

#define MBD_RESERVED_COUNT    1

#define MBD_COLLECT_STAT      1

typedef struct {
  int count;
  int next_slot_index;
  uint32_t data[MBD_DICT_COUNT];
#ifdef MBD_COLLECT_STAT 
  // Statistics about a compression instance
  uint8_t stat_begin[0];
  uint32_t delta_size_dist[4]; // 4, 8, 12, 16 bits
  uint32_t zero_count;
  uint32_t uncomp_count;
  uint32_t match_count;
  uint8_t stat_end[0];
#endif
} MBD_dict_t;

// Initially there is one entry, which is zero
inline static void MBD_dict_invalidate(MBD_dict_t *dict) {
  dict->count = 1;
  dict->next_slot_index = 1;
  dict->data[0] = 0;
#ifdef MBD_COLLECT_STAT 
  memset(dict->stat_begin, 0x00, dict->stat_end - dict->stat_begin);
#endif
  return;
}

inline static int MBD_dict_get_free_entry_index(MBD_dict_t *dict) {
  if(dict->next_slot_index == MBD_DICT_COUNT) {
    dict->next_slot_index = 1;
  }
  return dict->next_slot_index++;
}

// This function also updates LRU
inline static void MBD_dict_insert(MBD_dict_t *dict, uint32_t data) {
  int index = MBD_dict_get_free_entry_index(dict);
  dict->data[index] = data;
  if(dict->count != MBD_DICT_COUNT) {
    dict->count++;
  }
  return;
}

// Same intf as CPACK, but performs min-delta search
int MBD_search(MBD_dict_t *dict, uint32_t word, uint64_t *output);

// Size is the number of bytes in the input stream, which must be a multiple of 4 (32 bit)
int __MBD_comp(void *out_buf, void *_in_buf, int size, MBD_dict_t *dict);
inline static int _MBD_comp(void *out_buf, void *_in_buf, int size) {
  MBD_dict_t dict;
  MBD_dict_invalidate(&dict);
  return __MBD_comp(out_buf, _in_buf, size, &dict);
}
void _MBD_decomp(void *_out_buf, void *in_buf, int size);
void _MBD_print_compressed(void *in_buf, int size);
int __MBD_get_comp_size_bits(void *_in_buf, int size, MBD_dict_t *dict);
inline static int _MBD_get_comp_size_bits(void *_in_buf, int size) {
  MBD_dict_t dict;
  MBD_dict_invalidate(&dict);
  return __MBD_get_comp_size_bits(_in_buf, size, &dict);
}

inline static int MBD_comp(void *out_buf, void *_in_buf) { 
  return _MBD_comp(out_buf, _in_buf, UTIL_CACHE_LINE_SIZE); 
}
inline static void MBD_decomp(void *out_buf, void *in_buf) { 
  _MBD_decomp(out_buf, in_buf, UTIL_CACHE_LINE_SIZE); 
}
inline static void MBD_print_compressed(void *in_buf) {
  _MBD_print_compressed(in_buf, UTIL_CACHE_LINE_SIZE);
} 
inline static int MBD_get_comp_size_bits(void *_in_buf) {
  return _MBD_get_comp_size_bits(_in_buf, UTIL_CACHE_LINE_SIZE);
}

//* MESI_t - Abstract coherence controller, decoupled from cache implementation

// Maximum number of cores supported by this stucture
#define MESI_MAX_SHARER   64

// MESI stable states
#define MESI_STATE_BEGIN  0
#define MESI_STATE_I      0
#define MESI_STATE_S      1
#define MESI_STATE_E      2
#define MESI_STATE_M      3
#define MESI_STATE_END    4

#define MESI_CACHE_BEGIN  0
#define MESI_CACHE_L1     0
#define MESI_CACHE_L2     1
#define MESI_CACHE_LLC    2
#define MESI_CACHE_END    3

extern const char *MESI_cache_names[MESI_CACHE_END];
extern const char *MESI_state_names[MESI_STATE_END];

typedef struct {
  // This can be accessed either way
  union {
    struct {
      int8_t l1_state;
      int8_t l2_state;
      int8_t llc_state; // This represents the ownership of the block; Must not be E; M means LLC is owner
    };
    int8_t states[3];
  };
  union {
    struct {
      uint64_t l1_sharer;
      uint64_t l2_sharer;
    };
    uint64_t sharers[2];
  };
} MESI_entry_t;

// Reset the entry, i.e., in-place initialization; Set all states to STATE_I and clears the sharer list
inline static void MESI_entry_invalidate(MESI_entry_t *entry) {
  memset(entry, 0x00, sizeof(MESI_entry_t));
  return;
}

inline static void MESI_entry_set_state(MESI_entry_t *entry, int cache, int state) {
  assert(cache >= MESI_CACHE_BEGIN && cache < MESI_CACHE_END);
  assert(state >= MESI_STATE_BEGIN && state < MESI_STATE_END);
  entry->states[cache] = state;
  return;
}

inline static int MESI_entry_get_state(MESI_entry_t *entry, int cache) {
  assert(cache >= MESI_CACHE_BEGIN && cache < MESI_CACHE_END);
  return entry->states[cache];
}

// Sets the bitmap of the sharer list; This function does not care whether the sharer has been set already
inline static void MESI_entry_set_sharer(MESI_entry_t *entry, int cache, int sharer) {
  assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2);
  assert(sharer >= 0 && sharer < MESI_MAX_SHARER);
  entry->sharers[cache] |= (0x1UL << sharer);
  return;
}

// Does not care whether the sharer is clear already
inline static void MESI_entry_clear_sharer(MESI_entry_t *entry, int cache, int sharer) {
  assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2);
  assert(sharer >= 0 && sharer < MESI_MAX_SHARER);
  entry->sharers[cache] &= ~(0x1UL << sharer);
  return;
}

inline static void MESI_entry_clear_all_sharer(MESI_entry_t *entry, int cache) {
  assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2);
  entry->sharers[cache] = 0UL;
  return;
}

// Returns 1 if the given sharer is set in the cache's bitmap
inline static int MESI_entry_is_sharer(MESI_entry_t *entry, int cache, int sharer) {
  assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2);
  assert(sharer >= 0 && sharer < MESI_MAX_SHARER);
  return !!(entry->sharers[cache] & (0x1UL << sharer));
}

// Returns the next sharer, starting from location curr
// If curr is out of bound, then result is undefined
// Return value is the index of the sharer; -1 if no sharer is available
inline static int MESI_entry_get_next_sharer(MESI_entry_t *entry, int cache, int curr) {
  assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2);
  assert(curr >= 0 && curr <= MESI_MAX_SHARER);
  // First mask off bits below curr, and then get the index of the first bit that is set
  return (curr >= MESI_MAX_SHARER) ? -1 : ffs_uint64(entry->sharers[cache] & ~((0x1UL << curr) - 1)) - 1;
}

// Count the number of sharers
inline static int MESI_entry_count_sharer(MESI_entry_t *entry, int cache) {
  assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2);
  return popcount_uint64(entry->sharers[cache]);
}

// Return the only sharer (undefined when there is more than one sharers)
inline static int MESI_entry_get_exclusive_sharer(MESI_entry_t *entry, int cache) {
  assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2);
  assert(MESI_entry_count_sharer(entry, cache) == 1);
  // FFS instruction returns first set bit index plus one, so we minus one here
  return ffs_uint64(entry->sharers[cache]) - 1;
}

// An invalid entry must be all-zero
inline static int MESI_entry_is_invalid(MESI_entry_t *entry) {
  if(MESI_entry_count_sharer(entry, MESI_CACHE_L1) != 0) return 0;
  if(MESI_entry_count_sharer(entry, MESI_CACHE_L2) != 0) return 0;
  if(entry->l1_state != MESI_STATE_I || entry->l2_state != MESI_STATE_I || entry->llc_state != MESI_STATE_I) return 0;
  return 1;
}

// Consistency check; Assertion would fail if inconsistency occurs
// This function is disabled in no debug mode
void MESI_entry_verify(MESI_entry_t *entry);
void MESI_entry_print_sharer_list(MESI_entry_t *entry, int cache);
void MESI_entry_print(MESI_entry_t *entry);

//* dmap_t - Cache line level data map
//* pmap_t - Page information

// 256 MB working set, 4M lines, avg chain length 4, 1M slots, 8M memory for the directory
#define DMAP_INIT_COUNT                  (1 * 1024 * 1024)
#define DMAP_MASK                        (DMAP_INIT_COUNT - 1)
// Comment this out to disable profiling
#define DMAP_PROFILING

#define DMAP_READ                        0
#define DMAP_WRITE                       1

#define PMAP_OID                         (-1UL)

// Hash table entry - do not store hash value since key comparison is just 2 comparison, and
// we know that conflicts are rare
typedef struct dmap_entry_struct_t {
  // Common fields
  uint64_t oid;   // Stores -1 for page shapes
  uint64_t addr;
  // Doubly linked list
  dmap_entry_struct_t *next;
  dmap_entry_struct_t *prev;
  union {
    // This is for per-cache line information
    struct {
      // This will will init'ed to zero
      uint8_t data[UTIL_CACHE_LINE_SIZE]; // Data, always uncompressed, and updated from the simulator writes
      MESI_entry_t MESI_entry; // MESI state machine
    };
    // This is for per-page information
    struct {
      int shape;
    };
  };
} dmap_entry_t; 

dmap_entry_t *dmap_entry_init();
void dmap_entry_free(dmap_entry_t *entry);

// This struct has no resize operation; Note that this should not be allocated on the stack
typedef struct {
  // Common fields
  dmap_entry_t *entries[DMAP_INIT_COUNT];  // Array of hash entries
  int count;                               // Number of lines currently mapped
  int pmap_count;                          // Only pmap count
  // Statistics fields
  uint64_t query_count;                    // Total query count
  uint64_t iter_count;
} dmap_t;

// pmap and dmap share the same function; pmap does not need separated init and free
typedef dmap_entry_t pmap_entry_t;
typedef dmap_t pmap_t;

dmap_t *dmap_init();
void dmap_free(dmap_t *dmap);

inline static uint64_t dmap_gen_index(dmap_t *dmap, uint64_t oid, uint64_t addr) {
  (void)dmap;
  return (hash_64(oid) ^ hash_64(addr)) & (uint64_t)DMAP_MASK;
}

inline static int dmap_get_count(dmap_t *dmap) { return dmap->count - dmap->pmap_count; }

// Unlink an entry from the internal list
void dmap_entry_unlink(dmap_entry_t *entry);
// Insert a new record, or return an existing one. Always return not-NULL
dmap_entry_t *dmap_insert(dmap_t *dmap, uint64_t oid, uint64_t addr);
// Can return NULL if the combination does not exist
dmap_entry_t *dmap_find(dmap_t *dmap, uint64_t oid, uint64_t addr);
// Returns NULL if the dmap entry is not found
inline static MESI_entry_t *dmap_find_MESI_entry(dmap_t *dmap, uint64_t oid, uint64_t addr) {
  dmap_entry_t *dmap_entry = dmap_find(dmap, oid, addr);
  if(dmap_entry == NULL) {
    return NULL;
  }
  return &dmap_entry->MESI_entry;
}

// The optional argument all_zero (can be NULL) returns whether the line is all zero
int dmap_find_compressed(dmap_t *dmap, uint64_t oid, uint64_t addr, void *out_buf, int *all_zero);

// Read / Write data to / from buf to the given address. 
// Size can be larger than a single cache line. Addr can be unaligned
// buf should be at least the requested size, and will be modified.
void dmap_data_op(dmap_t *dmap, uint64_t oid, uint64_t _addr, int size, void *buf, int op);
inline static void dmap_read(dmap_t *dmap, uint64_t oid, uint64_t _addr, int size, void *buf) {
  dmap_data_op(dmap, oid, _addr, size, buf, DMAP_READ);
}
inline static void dmap_write(dmap_t *dmap, uint64_t oid, uint64_t _addr, int size, void *buf) {
  dmap_data_op(dmap, oid, _addr, size, buf, DMAP_WRITE);
}

inline static pmap_t *pmap_init() { return dmap_init(); }
inline static void pmap_free(pmap_t *pmap) { dmap_free(pmap); }

inline static int pmap_get_count(pmap_t *pmap) { return pmap->pmap_count; }

pmap_entry_t *pmap_insert(pmap_t *pmap, uint64_t addr, int shape);
// Insert a range of pages, all have the same shape
void pmap_insert_range(pmap_t *pmap, uint64_t addr, int size, int shape);
// addr should be cache line aligned
inline static pmap_entry_t *pmap_find(pmap_t *pmap, uint64_t addr) {
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0);
  //printf("Find   0x%lX\n", addr & UTIL_PAGE_MSB_MASK);
  return dmap_find(pmap, PMAP_OID, UTIL_GET_PAGE_ADDR(addr));
} 

// Print content
void dmap_print(dmap_t *dmap);
void pmap_print(pmap_t *pmap);

void dmap_conf_print(dmap_t *dmap);
void dmap_stat_print(dmap_t *dmap);

//* ocache_t - Overlay tagged cache

// Number of logical lines per super block
#define OCACHE_SB_SIZE                   4

// Define shapes
#define OCACHE_SHAPE_BEGIN               0
#define OCACHE_SHAPE_NONE                0
#define OCACHE_SHAPE_4_1                 1
#define OCACHE_SHAPE_1_4                 2
#define OCACHE_SHAPE_2_2                 3
#define OCACHE_SHAPE_COUNT               4
#define OCACHE_SHAPE_END                 4

extern const char *ocache_shape_names[4];

#define OCACHE_LEVEL_NONE                -1
#define OCACHE_LEVEL_L1                  0
#define OCACHE_LEVEL_L2                  1
#define OCACHE_LEVEL_L3                  2

// Used for segmented ocache design
#define OCACHE_DATA_ALIGNMENT            8

// Physical slot definition
typedef struct ocache_entry_struct_t {
  // Note that multiple compressed blocks share one OID and addr tag
  uint64_t oid;      // Make it large, avoid overflow
  uint64_t addr;     // Not shifted, but lower bits are zero
  // LRU is 0 for invalid slot such that they will always be used for insertion when present
  uint64_t lru;      // LRU counter value; Smallest is LRU line
  // 4 * 1: Ordered by OIDs
  // 1 * 4: Ordered by addresses
  // 2 * 2: First ordered by OID, then by addresses
  // None: Not compressed, act like normal cache
  int shape;
  uint8_t states;    // Valid/dirty bits, lower 4 valid, higher 4 dirty
  int8_t sizes[4];   // Compressed size of block 0, 1, 2, 3; Must be cleared if not valid
  int total_aligned_size; // Sum of aligned sizes of this entry
} ocache_entry_t;

// Align a size to the given boundary (must be power of 2)
inline static int ocache_entry_align_size(ocache_entry_t *entry, int size) {
  (void)entry;
  return (size + OCACHE_DATA_ALIGNMENT - 1) & ~(OCACHE_DATA_ALIGNMENT - 1);
} 

// Returns 1 if the index is dirty, 0 otherwise
inline static int ocache_entry_is_dirty(ocache_entry_t *entry, int index) {
  assert(index >= 0 && index < 4);
  return !!(entry->states & (0x1 << (4 + index)));
}

// Assertion fail if the line is alrady dirty
inline static void ocache_entry_set_dirty(ocache_entry_t *entry, int index) {
  assert(index >= 0 && index < 4);
  entry->states |= (0x1 << (4 + index));
  return;
}

inline static void ocache_entry_clear_dirty(ocache_entry_t *entry, int index) {
  assert(index >= 0 && index < 4);
  entry->states &= ~(0x1 << (4 + index));
  return;
}

// Returns 1 if the index is valid, 0 otherwise
inline static int ocache_entry_is_valid(ocache_entry_t *entry, int index) {
  assert(index >= 0 && index < 4);
  return !!(entry->states & (0x1 << index));
}

// Whether all entries are invalid
inline static int ocache_entry_is_all_invalid(ocache_entry_t *entry) {
  return !(entry->states & 0x0F); // If any of the bits are 1 should return 0
}

// Whether all entries are clean
inline static int ocache_entry_is_all_clean(ocache_entry_t *entry) {
  return !(entry->states & 0xF0);
}

// Assertion fail if the line is alrady valid
inline static void ocache_entry_set_valid(ocache_entry_t *entry, int index) {
  assert(index >= 0 && index < 4);
  entry->states |= (0x1 << index);
  return;
}

inline static void ocache_entry_clear_valid(ocache_entry_t *entry, int index) {
  assert(index >= 0 && index < 4);
  entry->states &= ~(0x1 << index);
  return;
}

// Clear all valid and dirty bits
inline static void ocache_entry_clear_all_state(ocache_entry_t *entry) {
  entry->states = 0;
  return;
}

inline static void ocache_entry_set_size(ocache_entry_t *entry, int index, int size) {
  assert(index >= 0 && index < 4);
  assert(size >= 0 && size <= (int)UTIL_CACHE_LINE_SIZE);
  entry->total_aligned_size -= ocache_entry_align_size(entry, entry->sizes[index]);
  assert(entry->total_aligned_size >= 0);
  entry->sizes[index] = (int8_t)size;
  entry->total_aligned_size += ocache_entry_align_size(entry, size);
  return;
}

inline static int ocache_entry_get_size(ocache_entry_t *entry, int index) {
  assert(index >= 0 && index < 4);
  assert(entry->sizes[index] >= 0 && entry->sizes[index] <= (int)UTIL_CACHE_LINE_SIZE);
  return (int)entry->sizes[index];
}

// Return the sum of all sizes, not aligned
inline static int ocache_entry_get_all_size(ocache_entry_t *entry) {
  int ret = entry->sizes[0] + entry->sizes[1] + entry->sizes[2] + entry->sizes[3];
  assert(ret >= 0 && ret <= (int)UTIL_CACHE_LINE_SIZE);
  return ret;
}

// Whether an incoming line of given size will fit into the cache
inline static int ocache_entry_is_fit(ocache_entry_t *entry, int size) {
  return ocache_entry_get_all_size(entry) + size <= (int)UTIL_CACHE_LINE_SIZE;
}

// Clear the size array and aligned size
inline static void ocache_entry_clear_all_size(ocache_entry_t *entry) {
  memset(entry->sizes, 0x00, sizeof(entry->sizes));
  entry->total_aligned_size = 0;
  return;
}

// Invalidate all lines in the entry. Also used as the initialization routine
// Reset LRU to 0, clear dirty/valid bits, and clear all size fields
inline static void ocache_entry_inv(ocache_entry_t *entry) {
  entry->lru = 0UL; // Such that they will be selected as LRU when write misses
  ocache_entry_clear_all_state(entry);
  ocache_entry_clear_all_size(entry);
  // Under debug mode, make them invalid values for easier debugging
#ifndef NDEBUG
  entry->oid = entry->addr = -1UL;
  entry->shape = -1;
#endif
  return;
}

// Clear a single index
// Clears valid, dirty and size field
inline static void ocache_entry_inv_index(ocache_entry_t *entry, int index) {
  assert(index >= 0 && index < 4);
  ocache_entry_clear_valid(entry, index);
  ocache_entry_clear_dirty(entry, index);
  ocache_entry_set_size(entry, index, 0);
  return;
}

inline static void ocache_entry_copy(ocache_entry_t *dest, ocache_entry_t *src) {
  memcpy(dest, src, sizeof(ocache_entry_t));
  return;
}

// The following are used as return status of ocache lookup
// Miss completely, not even invalid in a super block
#define OCACHE_MISS                      0
// Line is hit and the slot is uncompressed
#define OCACHE_HIT_NORMAL                1
// Line is hit and the slot is compressed
#define OCACHE_HIT_COMPRESSED            2
// The following two are used for eviction
// Insert or evict succeeds without evicting anything
#define OCACHE_SUCCESS                   3
// The entry to be evicted is copied to the evict_entry of the result
#define OCACHE_EVICT                     4

// MBD compression vertical types
#define OCACHE_MBD_V0                    0
#define OCACHE_MBD_V2                    1
#define OCACHE_MBD_V4                    2

extern const char *ocache_MBD_type_names[3];

// This represents a line in the ocache
typedef struct {
  int state;                    // Result of the lookup
  ocache_entry_t *candidates[OCACHE_SB_SIZE]; // Lookup for write stores same SB entry pointers in this array
  int indices[OCACHE_SB_SIZE];  // Stores the index that the requested address should be in the super block
  int cand_count;               // Number of candidates (i.e., slots in the same set for the same SB)
  ocache_entry_t *hit_entry;    // For lookup, stores the entry that is hit, if state indicates hit
  int hit_index;                // Index within the SB for the hit block
  ocache_entry_t *lru_entry;    // Lookup write will always store LRU entry in this
  // Following used by insertion and eviction
  ocache_entry_t *insert_entry; // Entry just inserted
  int insert_index;             // Index of the entry just inserted
  ocache_entry_t evict_entry;   // The entry that will be evicted (consider this as an eviction buffer entry or MSHR)
  int evict_index;              // Only used for inv() related function call
  ocache_entry_t *downgrade_entry; // Entry and index that get downgraded, if downgrade returns success; NULL if miss
  int downgrade_index;
  // This is used for segmented ocache
  int total_aligned_size;       // Total aligned size of valid lines in the set; Only valid if lookup scans all
} ocache_op_result_t;

// Index computation parameters; We do some masking and shifting of addr. and OID,
// and XOR them together to derive the index
typedef struct {
  uint64_t addr_mask;         // The mask applied to line aligned address
  int addr_shift;             // Right shift amount after masking
  uint64_t oid_mask;          // The mask applied to OID
  int oid_shift;              // Right shift amount after masking
} ocache_index_t;

// This stores the snapshot node which is organized as a linked list
typedef struct ocache_stat_snapshot_struct_t {
  uint64_t logical_line_count;       // Valid logical lines
  uint64_t sb_slot_count;            // Valid super blocks (i.e. slots dedicated to super blocks)
  uint64_t sb_logical_line_count;    // Valid lines in valid super blocks
  uint64_t sb_logical_line_size_sum; // Sum of shaped line sizes
  uint64_t sb_4_1_count;             // 4 * 1 sb count
  uint64_t sb_1_4_count;             // 1 * 4 sb count
  uint64_t sb_2_2_count;             // 2 * 2 sb count
  uint64_t no_shape_line_count;      // no shape line count
  uint64_t size_histogram[8];        // Same as the histogram in ocache
  uint64_t sb_logical_line_count_dist[4]; // index i is number of SBs that have i logical lines (i = [1, 4])
  struct ocache_stat_snapshot_struct_t *next;
} ocache_stat_snapshot_t;

ocache_stat_snapshot_t *ocache_stat_snapshot_init();
void ocache_stat_snapshot_free(ocache_stat_snapshot_t *snapshot);

typedef struct ocache_struct_t {
  char *name;                // Optional name of the cache object, NULL by default
  int level;                 // Level of the cache (L1 - 0, L2 - 1, L3 - 2)
  int set_count;             // Number of sets, must be power of two
  int way_count;             // Number of ways, can be arbitrary
  int line_count;            // Total number of cache lines (physical slots)
  int size;                  // total_count * UTIL_CACHE_LINE_SIZE
  int set_bits;              // Number of bits to represent the set
  // Shape related
  int use_shape;             // Use shape argumnent, otherwise always use OCACHE_SHAPE_NONE
  ocache_index_t indices[OCACHE_SHAPE_COUNT]; // Index masking and shift
  uint64_t lru_counter;      // Use this to implement LRU
  // dmap_t object for compression
  dmap_t *dmap;              // Used if the cache is compressed, otherwise NULL
  // Call back for compression; Can be hooked with a different function
  int (*get_compressed_size_cb)(struct ocache_struct_t *ocache, uint64_t oid, uint64_t addr, int shape);
  ocache_stat_snapshot_t *stat_snapshot_head; // Head of the snapshot linked list
  ocache_stat_snapshot_t *stat_snapshot_tail; // Tail of the snapshot linked list
  int MBD_type;              // OCACHE_MBD_V0, _V2, _V4
  // Statistics
  uint8_t stat_begin[0];             // Makes the begin address of stat region
  // General stat
  uint64_t comp_attempt_count;       // Number of compression attempts
  uint64_t comp_attempt_size_sum;         // All attempted lines (corresponds to comp_attempt_count)
  // Vertical compression & BDI stats
  uint64_t vertical_in_compressed_count; // This is equivalent to BDI_success_count
  uint64_t vertical_not_base_count;
  uint64_t vertical_base_found_count;
  uint64_t vertical_same_type_count;
  uint64_t vertical_success_count;
  // Only count successes; Lines not using vertical comp are not considered
  uint64_t vertical_before_size_sum; // Size of lines before vertical compression
  uint64_t vertical_after_size_sum;  // Size of lines after vertical compression 
  // BDI stats
  uint64_t BDI_success_count;
  uint64_t BDI_failed_count;         // Lines that cannot be compressed by any of the BDI type
  uint64_t BDI_before_size_sum;
  uint64_t BDI_after_size_sum;
  // Size and counts at each BDI and vertical stage
  uint64_t BDI_uncomp_size_sum;      // BDI uncompressable lines (corresponds to BDI_failed_count)
  uint64_t vertical_uncomp_size_sum; // BDI compressable but vertical uncompressable lines
  uint64_t vertical_uncomp_count;    // Number of lines not compressable to vertical
  // BDI type count, updated by get_compressed_size function of LLC
  union {
    struct {
      uint64_t BDI_invalid_count[2];
      uint64_t BDI_8_1_count;
      uint64_t BDI_8_2_count;
      uint64_t BDI_8_4_count;
      uint64_t BDI_4_1_count;
      uint64_t BDI_4_2_count;
      uint64_t BDI_2_1_count;
    };
    // Indexed using run-time type
    uint64_t BDI_compress_type_counts[8];
  };
  // FPC-specific stats
  uint64_t FPC_success_count;        // Number of requests that are smaller after FPC
  uint64_t FPC_fail_count;           // Number of requests that failed FPC
  uint64_t FPC_uncomp_size_sum;      // Size sum of lines that failed FPC
  uint64_t FPC_before_size_sum;      // Size sum before compression, only for successful lines
  uint64_t FPC_after_size_sum;       // Size sum after compression, only for successful lines
  uint64_t FPC_size_counts[8];       // Size distribution of FPC
  // CPACK-specific stats. All fields have the same meaning as FPC
  uint64_t CPACK_success_count;        
  uint64_t CPACK_fail_count;
  uint64_t CPACK_uncomp_size_sum;
  uint64_t CPACK_before_size_sum;
  uint64_t CPACK_after_size_sum;
  uint64_t CPACK_size_counts[8];
  // MBD-specific stats
  uint64_t MBD_success_count;
  uint64_t MBD_fail_count;
  uint64_t MBD_uncomp_size_sum;
  uint64_t MBD_before_size_sum;
  uint64_t MBD_after_size_sum;
  uint64_t MBD_size_counts[8];
  uint64_t MBD_delta_size_dist[4]; // 4, 8, 12, 16 bits
  uint64_t MBD_zero_count;
  uint64_t MBD_uncomp_count;
  uint64_t MBD_match_count;
  uint64_t MBD_insert_base_is_present;  // Number of times base is present
  uint64_t MBD_insert_base_is_missing;  // Number of times base is missing
  uint64_t MBD_read_base_is_present;    // This is set by cc_simple
  uint64_t MBD_read_base_is_missing;    // This is set by cc_simple
  // Statistics that will be refreshed by ocache_refresh_stat()
  uint64_t logical_line_count;       // Valid logical lines
  uint64_t sb_slot_count;            // Valid super blocks (i.e. slots dedicated to super blocks)
  uint64_t sb_logical_line_count;    // Valid lines in valid super blocks
  uint64_t sb_logical_line_size_sum; // Sum of shaped line sizes
  uint64_t sb_4_1_count;             // 4 * 1 sb count
  uint64_t sb_1_4_count;             // 1 * 4 sb count
  uint64_t sb_2_2_count;             // 2 * 2 sb count
  uint64_t no_shape_line_count;      // no shape line count
  uint64_t sb_logical_line_count_dist[4];  // Number of SBs that have particular number of lines
  uint64_t size_histogram[8];        // 8-byte interval; Only valid lines; First interval is [1, 8], not [0, 7]
  uint8_t stat_end[0];          // Marks end address of stat region
  // Always inline this at the end to save one pointer redirection
  ocache_entry_t data[0];
} ocache_t;

ocache_t *ocache_init(int size, int way_count);
void ocache_free(ocache_t *ocache);
void ocache_init_param(ocache_t *ocache, int size, int way_count); // Not called by programmer
void ocache_set_name(ocache_t *ocache, const char *name);
void ocache_set_level(ocache_t *ocache, int level);
void ocache_set_dmap(ocache_t *ocache, dmap_t *dmap);
inline static int ocache_is_use_shape(ocache_t *ocache) { return ocache->use_shape; }
inline static void ocache_set_get_compressed_size_cb(
  ocache_t *ocache, int (*cb)(ocache_t *, uint64_t, uint64_t, int)) {
  ocache->get_compressed_size_cb = cb;
  return;
}
// "BDI", "None"
void ocache_set_compression_type(ocache_t *ocache, const char *name);

uint64_t ocache_gen_addr(ocache_t *ocache, uint64_t tag, uint64_t set_id, int shape);
uint64_t ocache_gen_addr_with_oid(ocache_t *ocache, uint64_t oid, uint64_t tag, uint64_t set_id, int shape);
void ocache_gen_addr_in_sb(ocache_t *ocache, uint64_t base_oid, uint64_t base_addr, int index, int shape,
                           uint64_t *oid_p, uint64_t *addr_p);
uint64_t ocache_gen_addr_no_shape(ocache_t *ocache, uint64_t tag, uint64_t set_id);
uint64_t ocache_gen_addr_page(ocache_t *ocache, uint64_t page, uint64_t line);

// Returns a random set index which is always valid for the ocache
inline static int ocache_get_random_set_index(ocache_t *ocache) {
  return rand() % ocache->set_count;
}

// Aligns the address to a certain cache line boundary - only called for debugging
uint64_t ocache_align_addr(uint64_t addr, int alignment);
// Aligns the OID to a certain alignment
uint64_t ocache_align_oid(uint64_t oid, int alignment);

// Generate the set selection index using either simple bit masking or OID
// We also need the shape of the address's page
int ocache_get_set_index(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape);
ocache_entry_t *ocache_get_set_begin(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape);
// Given OID, addr, return the index in the given super block entry
int ocache_get_sb_index(ocache_t *ocache, ocache_entry_t *entry, uint64_t oid, uint64_t addr);
// Given OID, addr, shape, return the super block tag that should be used in entry and 
// the index of the line in the sb
void ocache_get_sb_tag(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape,
  uint64_t *oid_base, uint64_t *addr_base, int *index);
// Given index and entry (shape), return the vertical base's index in that sb
int ocache_get_vertical_base_index(ocache_t *ocache, ocache_entry_t *entry, int index);
// Given OID, addr, shape, return the OID, addr of the vertical base
uint64_t ocache_get_vertical_base_oid(ocache_t *ocache, uint64_t oid, int shape);

void _ocache_lookup(
  ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, int update_lru, int scan_all, ocache_op_result_t *result);
inline static void ocache_lookup_read(
  ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, ocache_op_result_t *result) {
  _ocache_lookup(ocache, oid, addr, shape, 1, 0, result);
}
// This function is read-only to the ocache set
inline static void ocache_lookup_write(
  ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, ocache_op_result_t *result) {
  _ocache_lookup(ocache, oid, addr, shape, 0, 1, result);
}
// Just probe an entry without changing its LRU and do not update candidate
inline static void ocache_lookup_probe(
  ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, ocache_op_result_t *result) {
  _ocache_lookup(ocache, oid, addr, shape, 0, 0, result);
}

// This is to update stats when read hits a MBD line
void ocache_llc_read_hit_MBD(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape);

// Get compressed size call backs
int ocache_get_compressed_size_BDI_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape);
int ocache_get_compressed_size_BDI_third_party_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape);
int ocache_get_compressed_size_FPC_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape);
int ocache_get_compressed_size_FPC_third_party_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape);
int ocache_get_compressed_size_CPACK_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape);
int ocache_get_compressed_size_MBD_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape);
int ocache_get_compressed_size_None_cb(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape);

void ocache_insert(ocache_t *ocache, uint64_t oid, uint64_t addr, 
                   int shape, int dirty, ocache_op_result_t *insert_result);
void ocache_after_insert_adjust(
  ocache_t *ocache, ocache_op_result_t *insert_result, ocache_op_result_t *lookup_result);
void ocache_inv(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, ocache_op_result_t *result);
void ocache_downgrade(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, ocache_op_result_t *result);

// The following functions are obsolete and should no longer be used; Always use the shaped version

// Either HIT_NORMAL or MISS; Updates hit entry and state
inline static void ocache_lookup_read_no_shape(
  ocache_t *ocache, uint64_t oid, uint64_t addr, ocache_op_result_t *result) {
  ocache_lookup_read(ocache, oid, addr, OCACHE_SHAPE_NONE, result);
}
// Do not push candidate, but set lru entry if miss; Also update hit entry and state
// This function is read-only to the ocache set
inline static void ocache_lookup_write_no_shape(
  ocache_t *ocache, uint64_t oid, uint64_t addr, ocache_op_result_t *result) {
  ocache_lookup_write(ocache, oid, addr, OCACHE_SHAPE_NONE, result);
}
// Insert a full sized line, possibly hitting an existing line, an invalid line, or evict a line
inline static void ocache_insert_no_shape(
  ocache_t *ocache, uint64_t oid, uint64_t addr, int dirty, ocache_op_result_t *result) {
  ocache_insert(ocache, oid, addr, OCACHE_SHAPE_NONE, dirty, result);
}
// Invalidate the exact address, if it is found. Eviction is needed if the line is dirty
inline static void ocache_inv_no_shape(ocache_t *ocache, uint64_t oid, uint64_t addr, ocache_op_result_t *result) {
  ocache_inv(ocache, oid, addr, OCACHE_SHAPE_NONE, result);
}

// Returns the MRU entry of the given set; Report error if set invalid; Only used for debugging
ocache_entry_t *ocache_get_mru(ocache_t *ocache, int set_index);
// Returns the MRU entry of the entire cache
ocache_entry_t *ocache_get_mru_global(ocache_t *ocache);

// Reset the object's content and stats and LRU counter
void ocache_reset(ocache_t *ocache);

// Update statistics that are not updated during normal operation
// See ocache_t object body for more details
void ocache_refresh_stat(ocache_t *ocache);
// Creates a snapshot of the current cache content by running ocache_refresh_stat() first and then
// saving the current stats to a linked list
void ocache_append_stat_snapshot(ocache_t *ocache);
// Saves the snapshots to a file
void ocache_save_stat_snapshot(ocache_t *ocache, const char *filename);

inline static uint64_t ocache_get_logical_line_count(ocache_t *ocache) { return ocache->logical_line_count; }
inline static uint64_t ocache_get_sb_slot_count(ocache_t *ocache) { return ocache->sb_slot_count; }
inline static uint64_t ocache_get_sb_logical_line_count(ocache_t *ocache) { return ocache->sb_logical_line_count; }
inline static uint64_t ocache_get_no_shape_line_count(ocache_t *ocache) { return ocache->no_shape_line_count; }

// Invalidates all OIDs >= addr_lo and < addr_hi; Both lo and hi must be cache aligned
// Returns the number of lines invalidated
int ocache_inv_range(ocache_t *ocache, uint64_t addr_lo, uint64_t addr_hi);
// Invalidates the entire cache
int ocache_inv_all(ocache_t *ocache);

inline static void ocache_update_entry_lru(ocache_t *ocache, ocache_entry_t *entry) {
  entry->lru = ocache->lru_counter++;
  return;
}

inline static int ocache_get_entry_index(ocache_t *ocache, ocache_entry_t *entry) {
  return (int)(entry - ocache->data);
}

inline static int ocache_get_entry_set_index(ocache_t *ocache, ocache_entry_t *entry) {
  return ocache_get_entry_index(ocache, entry) / ocache->way_count;
}

inline static int ocache_get_entry_way_index(ocache_t *ocache, ocache_entry_t *entry) {
  return ocache_get_entry_index(ocache, entry) % ocache->way_count;
}

void ocache_set_print(ocache_t *ocache, int set);
void ocache_conf_print(ocache_t *ocache);
void ocache_stat_print(ocache_t *ocache);

//* scache_t - Segmented cache for compression

// Maximum compression ratio - change this to change the compression ratio
#define SCACHE_MAX_RATIO      4

// This is a single tag in the compressed segmented cache
typedef struct {
  uint64_t addr;                    // Cache line aligned, but not shifted
  uint64_t lru;                     // Current LRU position
  int8_t valid;
  int8_t dirty;
  int8_t size;                      // Unaligned size of the current line
} scache_entry_t;

// This operation sets LRU, size and valid to zero which is required as invariant for invalid entry
inline static void scache_entry_invalidate(scache_entry_t *entry) {
  memset(entry, 0x00, sizeof(scache_entry_t));
  return;
}

inline static void scache_entry_dup(scache_entry_t *dest, scache_entry_t *src) {
  memcpy(dest, src, sizeof(scache_entry_t));
  return;
}

#define SCACHE_DATA_ALIGNMENT  4    // Compressed block size is aligned to 4 bytes

// States of op_result_t
// Used for lookup, inv
#define SCACHE_HIT             1
#define SCACHE_MISS            2
// Used for insert, inv
#define SCACHE_SUCCESS         3
#define SCACHE_EVICT           4

// Maximum number of possible evicts per operation (this can be higher than the actual possible number)
#define SCACHE_MAX_EVICT_ENTRY 16

// Unified structure for returning operation result
typedef struct {
  int state;
  union {
    scache_entry_t *lru_entry;       // LRU in the current set
    scache_entry_t *hit_entry;       // hit entry in the current set
    scache_entry_t *insert_entry;    // Inserted entry for insert operation; Always not NULL for insertion
    scache_entry_t *inv_entry;       // Invalidated entry, NULL if inv misses
    scache_entry_t *downgrade_entry; // Downgraded entry, NULL if downgrade misses
  };
  int total_aligned_size;           // Aligned size of compressed lines in the set
  int evict_count;                  // Number of evictions
  // Copied entry (used for eviction); Insert may need multiple evictions
  scache_entry_t evict_entries[SCACHE_MAX_EVICT_ENTRY]; 
} scache_op_result_t;

typedef struct scache_stat_snapshot_struct_t {
  uint64_t logical_line_count;
  uint64_t uncomp_logical_line_count;
  uint64_t comp_line_size;
  uint64_t aligned_comp_line_size;
  struct scache_stat_snapshot_struct_t *next;
} scache_stat_snapshot_t;

scache_stat_snapshot_t *scache_stat_snapshot_init();
void scache_stat_snapshot_free(scache_stat_snapshot_t *snapshot);

// Everything without "physical" prefix is logical
typedef struct scache_struct_t {
  int size;               // Total size, in bytes
  int ratio;              // Only stored for conf printing
  int physical_way_count; // Only stored for conf printing; Normal operation does not need this
  int way_count;          // Logical way count, i.e., number of tags per set
  int physical_line_count;
  int line_count;         // Number of tags, i.e., logical line count
  int set_count;
  int set_size;           // Number of bytes storage available in a set
  int set_bits;           // Number of bits in the set bit mask
  uint64_t addr_mask;     // Before shift
  int addr_shift;
  uint64_t lru_counter;   // Global LRU counter
  dmap_t *dmap;           // Data map object storing data
  int (*get_compressed_size_cb)(struct scache_struct_t *scache, uint64_t addr); // Call back for the compressed size
  uint8_t stat_begin[0];
  // Statistics - BDI
  uint64_t BDI_attempt_count;   // Number of BDI compression attempts, including succeesses and failures
  uint64_t BDI_success_count;   // BDI success, i.e., compressable by BDI
  uint64_t BDI_fail_count;
  uint64_t BDI_type_counts[8];  // BDI types
  uint64_t all_zero_count;      // All-zero lines
  uint64_t BDI_attempt_size;    // Attempted sizes before compression
  uint64_t BDI_uncomp_size;     // Lines that are not compressed by BDI
  uint64_t BDI_before_size;     // Successful lines only
  uint64_t BDI_after_size;      // Total size of lines compressed by BDI, only successful ones, including zero lines
  // Statistics - FPC
  uint64_t FPC_attempt_count;
  uint64_t FPC_success_count;
  uint64_t FPC_fail_count;
  uint64_t FPC_attempt_size;    // Attempted size, before compression
  uint64_t FPC_uncomp_size;     // Failed sizes
  uint64_t FPC_before_size;     // Only successful lines
  uint64_t FPC_after_size;      // Only successful lines
  uint64_t FPC_size_counts[8];  // FPC size counts, index i counts size [8i, 8i + 8)
  // Statistics - CPACK
  uint64_t CPACK_attempt_count;
  uint64_t CPACK_success_count;
  uint64_t CPACK_fail_count;
  uint64_t CPACK_attempt_size;
  uint64_t CPACK_uncomp_size;
  uint64_t CPACK_before_size;
  uint64_t CPACK_after_size;
  uint64_t CPACK_size_counts[8];
  // Statistics - MBD
  uint64_t MBD_attempt_count;
  uint64_t MBD_success_count;
  uint64_t MBD_fail_count;
  uint64_t MBD_attempt_size;
  uint64_t MBD_uncomp_size;
  uint64_t MBD_before_size;
  uint64_t MBD_after_size;
  uint64_t MBD_size_counts[8];
  // Statistics - General
  uint64_t uncompressed_count;  // Not compressed
  uint64_t read_count;          // Reads, not including internal lookups
  uint64_t insert_count;        // Number of insertion operations
  uint64_t inv_count;           // Number of evictions
  uint64_t inv_success_count;   // Number of successful evictions
  uint64_t downgrade_count;     // Number of downgrades
  uint64_t downgrade_success_count; // Number of successful downgrades (line is hit)
  uint64_t evict_line_count;    // Number of lines evicted (can be insertion or eviction)
  // Statistics after tag walk - comp line size and uncomp line count can be derived from these three
  uint64_t logical_line_count;        // Number of valid logical lines
  uint64_t uncomp_logical_line_count; // Number of uncompressed logical lines
  uint64_t comp_line_size;            // Compressed line size sum, unaligned (value stored in scache_entry_t)
  uint64_t aligned_comp_line_size;    // Aligned compressed line size sum
  uint8_t stat_end[0];
  // Snapshot linked list head and tail
  scache_stat_snapshot_t *stat_snapshot_head;
  scache_stat_snapshot_t *stat_snapshot_tail;
  // Entries
  scache_entry_t data[0]; // Must be allocated based on actual number of tags in the cache
} scache_t;

void scache_init_param(scache_t *scache, int size, int physical_way_count, int ratio);
scache_t *scache_init(int size, int physical_way_count, int ratio);
scache_t *scache_init_by_set_count(int set_count, int physical_way_count, int ratio);
void scache_free(scache_t *scache);

inline static int scache_gen_index(scache_t *scache, uint64_t addr) {
  return (int)((addr & scache->addr_mask) >> scache->addr_shift);
}

inline static scache_entry_t *scache_get_set_begin(scache_t *scache, uint64_t addr) {
  int index = scache_gen_index(scache, addr);
  return scache->data + scache->way_count * index;
}

inline static void scache_set_dmap(scache_t *scache, dmap_t *dmap) {
  scache->dmap = dmap;
  return;
}

inline static void scache_set_get_compressed_size_cb(scache_t *scache, int (*cb)(scache_t *, uint64_t)) {
  scache->get_compressed_size_cb = cb;
  return;
}

// Align a size to the given boundary (must be power of 2)
inline static int scache_align_size(scache_t *scache, int size) {
  (void)scache;
  return (size + SCACHE_DATA_ALIGNMENT - 1) & ~(SCACHE_DATA_ALIGNMENT - 1);
} 

inline static void scache_update_entry_lru(scache_t *scache, scache_entry_t *entry) {
  entry->lru = scache->lru_counter++;
  return;
}

// Returns a random set index used for testing. The returned value is guaranteed to be a valid index
inline static int scache_get_random_set_index(scache_t *scache) {
  return rand() % scache->set_count;
}

// Generate an address given the tag and set index
inline static uint64_t scache_gen_addr(scache_t *scache, int set_index, uint64_t tag) {
  assert(set_index >= 0 && set_index < scache->set_count);
  return (((uint64_t)set_index) << UTIL_CACHE_LINE_BITS) + (tag << (UTIL_CACHE_LINE_BITS + scache->set_bits));
}

// Sets a compression call back using a name
// Valid values are "BDI", "FPC", "None"
void scache_set_compression_type(scache_t *scache, const char *name);
void _scache_lookup(scache_t *scache, uint64_t addr, int update_lru, int scan_all, scache_op_result_t *result);
// Performs lookup with an address; Return lookup result in the result parameter
inline static void scache_lookup(scache_t *scache, uint64_t addr, scache_op_result_t *result) {
  scache->read_count++;
  _scache_lookup(scache, addr, 1, 0, result); // Update LRU, do not update total aligned size
  return;
}
void scache_insert(scache_t *scache, uint64_t addr, int _dirty, scache_op_result_t *result);

void scache_inv(scache_t *scache, uint64_t addr, scache_op_result_t *result);
void scache_downgrade(scache_t *scache, uint64_t addr, scache_op_result_t *result);

// Reset everything, including lines and stat
void scache_reset(scache_t *scache);

// Stat refresh and snapshot
void scache_refresh_stat(scache_t *scache);
void scache_append_stat_snapshot(scache_t *scache);
void scache_save_stat_snapshot(scache_t *scache, const char *name);

// For debugging; print the content of a set; Valid entries only
void scache_print_set(scache_t *scache, int set_index);

void scache_conf_print(scache_t *scache);
void scache_stat_print(scache_t *scache);

//* dram_t - Simple DRAM timing model

// Each sample span 10K cycle
#define DRAM_STAT_WINDOW_SIZE   10000

// May be used by other modules
#define DRAM_READ               0
#define DRAM_WRITE              1

typedef struct dram_stat_snapshot_struct_t {
  uint64_t cycle_count;        // Number of cycles in this window
  uint64_t read_count;
  uint64_t write_count;
  uint64_t write_size;
  uint64_t read_cycle;         // Total cycles spent on read in this window
  uint64_t write_cycle;        // Total cycles spent on write in this window
  struct dram_stat_snapshot_struct_t *next;
} dram_stat_snapshot_t;

dram_stat_snapshot_t *dram_stat_snapshot_init();
void dram_stat_snapshot_free(dram_stat_snapshot_t *stat);

typedef struct {
  // Read from the conf
  int read_latency;
  int write_latency;
  int bank_count;       // Must be a power of two
  // First mask then shift to generate the bank index
  uint64_t oid_mask;    // Mask off higher bits, range 0 -- bank_count - 1
  uint64_t addr_mask;
  int addr_shift;
  int bank_count_bits;  // Number of bits in the address mask
  // Statistics
  uint64_t stat_read_count;  // Number of reads since "last_cycle"
  uint64_t stat_write_count; // Number of writes since "last_cycle"
  uint64_t stat_write_size;  // Number of bytes written since "last_cycle"
  uint64_t stat_read_cycle;  // Number of cycles spent on read since "last_cycle"
  uint64_t stat_write_cycle; // Number of cycles spent on write since "last_cycle"
  // Total aggregations
  uint64_t total_read_count;
  uint64_t total_write_count;
  uint64_t total_write_size;
  uint64_t total_read_cycle;
  uint64_t total_write_cycle;
  // These two are updated by its caller
  uint64_t total_sb_count;      // Number of super blocks written back
  uint64_t total_sb_line_count; // Number of logical lines per write
  // Whether the access is queued when the bank is still busy
  uint64_t contended_access_count;
  uint64_t read_contended_access_count;
  uint64_t write_contended_access_count;
  uint64_t uncontended_access_count;
  dram_stat_snapshot_t *stat_snapshot_head;    // Head of linked list
  dram_stat_snapshot_t *stat_snapshot_tail;    // Head of linked list
  // Bank timing
  uint64_t banks[0];
} dram_t;

dram_t *dram_init(conf_t *conf);
void dram_free(dram_t *dram);

inline static int dram_get_bank_count(dram_t *dram) { return dram->bank_count; }
inline static uint64_t dram_get_bank_cycle(dram_t *dram, int index) {
  assert(index >= 0 && index < dram->bank_count);
  return dram->banks[index];
}

inline static int dram_gen_bank_index(dram_t *dram, uint64_t oid, uint64_t addr) {
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0);
  int index = (oid & dram->oid_mask) ^ ((addr & dram->addr_mask) >> dram->addr_shift);
  assert(index >= 0 && index < dram->bank_count);
  return index;
}

// Generate address given tag and the bank index
// Index can be larger than bank count, in which case it just wraps back
inline static uint64_t dram_gen_addr(dram_t *dram, uint64_t tag, int index) {
  return ((tag << dram->bank_count_bits) + (index % dram->bank_count)) << dram->addr_shift;
}

inline static uint64_t dram_gen_addr_with_oid(dram_t *dram, uint64_t tag, uint64_t oid, int index) {
  return dram_gen_addr(dram, tag, (oid ^ (uint64_t)index) & dram->oid_mask);
}

uint64_t dram_access(dram_t *dram, uint64_t cycle, uint64_t oid, uint64_t addr, int latency);

inline static uint64_t dram_read(dram_t *dram, uint64_t cycle, uint64_t oid, uint64_t addr) {
  dram->stat_read_count++;
  dram->total_read_count++;
  uint64_t ret = dram_access(dram, cycle, oid, addr, dram->read_latency);
  dram->total_read_cycle += (ret - cycle);
  dram->stat_read_cycle += (ret - cycle);
  if(ret - cycle > (uint64_t)dram->read_latency) {
    dram->read_contended_access_count++;
  }
  return ret;
}

inline static uint64_t dram_write(dram_t *dram, uint64_t cycle, uint64_t oid, uint64_t addr, uint64_t size) {
  // Update stat window variables
  dram->stat_write_count++;
  dram->stat_write_size += size;
  // Update total aggregated variables
  dram->total_write_count++;
  dram->total_write_size += size;
  uint64_t ret = dram_access(dram, cycle, oid, addr, dram->write_latency);
  dram->total_write_cycle += (ret - cycle);
  dram->stat_write_cycle += (ret - cycle);
  if(ret - cycle > (uint64_t)dram->write_latency) {
    dram->write_contended_access_count++;
  }
  return ret;
}

// Returns the maximum bank cycle, i.e., the cycle when all banks are free
uint64_t dram_get_max_cycle(dram_t *dram);

void dram_append_stat_snapshot(dram_t *dram);
// Dump stats into a file
void dram_save_stat_snapshot(dram_t *dram, const char *filename);

void dram_conf_print(dram_t *dram);
void dram_stat_print(dram_t *dram);

//* cc_common_t - Common components for all cc objects

#define CC_STAT_BEGIN             0
#define CC_STAT_LOOKUP            0       // Number of lookups, both read and write
#define CC_STAT_INSERT            1       // Number of inserts
#define CC_STAT_EVICT             2       // Number of insert evictions
#define CC_STAT_INV               3       // Number of coherence invs and inclusiveness-induced invs
#define CC_STAT_DOWNGRADE         4       // Number of coherence downgrades
// Functional stats
#define CC_STAT_LOAD              5
#define CC_STAT_STORE             6
#define CC_STAT_LOAD_HIT          7
#define CC_STAT_STORE_HIT         8
#define CC_STAT_LOAD_MISS         9
#define CC_STAT_STORE_MISS        10
// Cycle stats
#define CC_STAT_LOAD_CYCLE        11
#define CC_STAT_STORE_CYCLE       12
#define CC_STAT_END               13

typedef void (*cc_dram_debug_cb_t)(int op, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int size);

// String names of cc stats
extern const char *cc_common_stat_names[CC_STAT_END];

// Common data structures of coherence controller
typedef struct {
  dmap_t *dmap;                           // No ownership
  dram_t *dram;                           // No ownership
  conf_t *conf;                           // No ownership; NULL if not init'ed from a conf object
  int core_count;                         // Number of L1s and L2s
  uint64_t *stats[3];                     // First dimension cache level, second core ID * stat type, core ID first
  // DRAM stats
  uint64_t dram_read_count;
  uint64_t dram_write_count;
  uint64_t dram_read_cycle;
  uint64_t dram_write_bytes;
  // Cache conf
  union {
    struct {
      int l1_size;
      int l2_size;
      int llc_size;
    };
    int cache_sizes[3];
  };
  union {
    struct {
      int l1_way_count;
      int l2_way_count;
      int llc_physical_way_count;
    };
    int cache_way_counts[3];
  };
  union {
    struct {
      int l1_latency;
      int l2_latency;
      int llc_latency;
    };
    int cache_latencies[3];
  };
  // Debugging call back - Only called if set; All DRAM requests will be duplicated to this function
  cc_dram_debug_cb_t dram_debug_cb;
} cc_common_t;

void cc_common_init_stats(cc_common_t *commons, int core_count);
void cc_common_free_stats(cc_common_t *commons);

// Resets all stats to zero
inline static void cc_common_reset_stats(cc_common_t *commons) {
  cc_common_free_stats(commons);
  cc_common_init_stats(commons, commons->core_count);
  return;
}

// Returns a pointer to the stat variable
// Note that the first dimension is cache level, then core ID, the last dimension is type
inline static uint64_t *cc_common_get_stat_ptr(cc_common_t *commons, int cache, int id, int type) {
  assert(type >= CC_STAT_BEGIN && type < CC_STAT_END);
  assert(cache >= MESI_CACHE_BEGIN && cache < MESI_CACHE_END);
  assert(id >= 0 && id < commons->core_count);
  return commons->stats[cache] + id * CC_STAT_END + type;
}

// Update access cycle for each component in the hierarchy
void cc_common_update_access_cycles(
  cc_common_t *commons, int id, int is_store, int end_level, uint64_t begin_cycle, uint64_t end_cycle);

void cc_common_print_stats(cc_common_t *commons);

//* cc_scache_t - Multicore coherence controller for scache

// Per-core cache reference; LLC is shared, so the pointer refers to a shared object; L1 and L2 are private
typedef struct {
  int id;                                 // ID of the core
  union {
    struct {
      scache_t *l1;
      scache_t *l2;
      scache_t *llc;                      // Does not own (points to shared_llc)
    };
    scache_t *scaches[3];                 // Use MESI_CACHE_L1/L2/LLC to address
  };
} cc_scache_core_t;

typedef struct {
  cc_common_t commons;                    // Common fields that are essential to all types of CCs
  cc_scache_core_t *cores;                // Array of private cache and shared cache references
  scache_t *shared_llc;                   // Has ownership
  // This one is unique to LLC
  int llc_ratio;
} cc_scache_t;

// Initialize caches structure, without filling the cache objects
void cc_scache_init_cores(cc_scache_t *cc, int core_count);
void cc_scache_init_llc(
  cc_scache_t *cc, int size, int physical_way_count, int ratio, int latency, const char *algorithm);
void cc_scache_init_l1_l2(cc_scache_t *cc, int cache, int size, int way_count, int latency);
cc_scache_t *cc_scache_init_conf(conf_t *conf); // Initialize with configuration files (rather than ad-hoc)
cc_scache_t *cc_scache_init(); // This does not init anything
void cc_scache_free(cc_scache_t *cc);

// Returns the private cache; LLC is not supported
inline static scache_t *cc_scache_get_core_cache(cc_scache_t *cc, int cache, int id) {
  assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2 || cache == MESI_CACHE_LLC);
  assert(id >= 0 && id < cc->commons.core_count);
  return cc->cores[id].scaches[cache];
}

// Returns the latency of a given cache; No id is required since all caches are identical on the same level
inline static int cc_scache_get_latency(cc_scache_t *cc, int cache) {
  assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2 || cache == MESI_CACHE_LLC);
  return cc->commons.cache_latencies[cache];
}

void cc_scache_set_dmap(cc_scache_t *cc, dmap_t *dmap);
inline static void cc_scache_clear_dmap(cc_scache_t *cc) { cc->commons.dmap = NULL; }
void cc_scache_set_dram(cc_scache_t *cc, dram_t *dram);
inline static void cc_scache_clear_dram(cc_scache_t *cc) { cc->commons.dram = NULL; }

inline static void cc_scache_set_dram_debug_cb(cc_scache_t *cc, cc_dram_debug_cb_t cb) {
  cc->commons.dram_debug_cb = cb;
  return;
}

int cc_scache_is_line_invalid(cc_scache_t *cc, int cache, int id, uint64_t addr);
int cc_scache_is_line_dirty(cc_scache_t *cc, int cache, int id, uint64_t addr);

uint64_t cc_scache_llc_insert_recursive(cc_scache_t *cc, int id, uint64_t cycle, uint64_t addr, int dirty);
uint64_t cc_scache_l2_insert_recursive(cc_scache_t *cc, int id, uint64_t cycle, uint64_t addr, int dirty);
uint64_t cc_scache_l1_insert_recursive(cc_scache_t *cc, int id, uint64_t cycle, uint64_t addr, int dirty);

uint64_t cc_scache_load(cc_scache_t *cc, int id, uint64_t cycle, uint64_t addr);
uint64_t cc_scache_store(cc_scache_t *cc, int id, uint64_t cycle, uint64_t addr);

void cc_scache_print_set(cc_scache_t *cc, int cache, int id, int set_index);
void cc_scache_print_MESI_entry(cc_scache_t *cc, uint64_t addr);

// Function that sets up a testing scenario by inserting into all levels and setting MESI states; 
// Only called for debugging
void cc_scache_insert_levels(cc_scache_t *cc, int begin, int id, uint64_t addr, const int *state_vec);
// state_vec starts at L1, L2 and LLC respectively
inline static void cc_scache_l1_insert_levels(cc_scache_t *cc, int id, uint64_t addr, const int *state_vec) {
  cc_scache_insert_levels(cc, MESI_CACHE_L1, id, addr, state_vec);
}
inline static void cc_scache_l2_insert_levels(cc_scache_t *cc, int id, uint64_t addr, const int *state_vec) {
  cc_scache_insert_levels(cc, MESI_CACHE_L2, id, addr, state_vec);
}
inline static void cc_scache_llc_insert_levels(cc_scache_t *cc, int id, uint64_t addr, const int *state_vec) {
  cc_scache_insert_levels(cc, MESI_CACHE_LLC, id, addr, state_vec);
}

void cc_scache_conf_print(cc_scache_t *cc);
void cc_scache_stat_print(cc_scache_t *cc);

//* cc_ocache_t

typedef struct {
  int id;
  union {
    struct {
      ocache_t *l1;
      ocache_t *l2;
      ocache_t *llc;
    };
    ocache_t *ocaches[3];
  };
} cc_ocache_core_t;

typedef struct {
  cc_common_t commons;
  cc_ocache_core_t *cores;
  ocache_t *shared_llc;
  int default_shape;            // The shape of lines when it is not explicitly given in the shape map
} cc_ocache_t;

// All having the exact semantics as cc_scache_ counterparts
void cc_ocache_init_cores(cc_scache_t *cc, int core_count);
void cc_ocache_init_llc(cc_ocache_t *cc, int size, int physical_way_count, int latency, const char *algorithm);
void cc_ocache_init_l1_l2(cc_ocache_t *cc, int cache, int size, int way_count, int latency);
cc_ocache_t *cc_ocache_init_conf(conf_t *conf); 
cc_ocache_t *cc_ocache_init();
void cc_ocache_free(cc_ocache_t *cc);

inline static ocache_t *cc_ocache_get_core_cache(cc_ocache_t *cc, int cache, int id) {
  assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2 || cache == MESI_CACHE_LLC);
  assert(id >= 0 && id < cc->commons.core_count);
  return cc->cores[id].ocaches[cache];
}

inline static int cc_ocache_get_latency(cc_ocache_t *cc, int cache) {
  assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2 || cache == MESI_CACHE_LLC);
  return cc->commons.cache_latencies[cache];
}

void cc_ocache_set_dmap(cc_ocache_t *cc, dmap_t *dmap);
inline static void cc_ocache_clear_dmap(cc_ocache_t *cc) { cc->commons.dmap = NULL; }
void cc_ocache_set_dram(cc_ocache_t *cc, dram_t *dram);
inline static void cc_ocache_clear_dram(cc_ocache_t *cc) { cc->commons.dram = NULL; }

inline static void cc_ocache_set_dram_debug_cb(cc_ocache_t *cc, cc_dram_debug_cb_t cb) {
  cc->commons.dram_debug_cb = cb;
  return;
}

inline static int cc_ocache_get_default_shape(cc_ocache_t *cc) {
  return cc->default_shape;
}

inline static void cc_ocache_set_default_shape(cc_ocache_t *cc, int shape) {
  assert(shape >= OCACHE_SHAPE_BEGIN && shape < OCACHE_SHAPE_END);
  cc->default_shape = shape;
  return;
}

// Debugging functions for assertion

int cc_ocache_is_line_invalid(cc_ocache_t *cc, int cache, int id, uint64_t oid, uint64_t addr, int shape);
int cc_ocache_is_line_dirty(cc_ocache_t *cc, int cache, int id, uint64_t oid, uint64_t addr, int shape);
int cc_ocache_get_line_shape(cc_ocache_t *cc, int cache, int id, uint64_t oid, uint64_t addr, int shape);

// Insert recursive

uint64_t cc_ocache_llc_insert_recursive(
  cc_ocache_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int shape, int dirty);
uint64_t cc_ocache_l2_insert_recursive(
  cc_ocache_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int shape, int dirty);
uint64_t cc_ocache_l1_insert_recursive(
  cc_ocache_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int shape, int dirty);

uint64_t cc_ocache_load(cc_ocache_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr);
uint64_t cc_ocache_store(cc_ocache_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr);

void cc_ocache_print_set(cc_ocache_t *cc, int cache, int id, int set_index);
void cc_ocache_print_MESI_entry(cc_ocache_t *cc, uint64_t addr);

void cc_ocache_insert_levels(
  cc_ocache_t *cc, int begin, int id, uint64_t oid, uint64_t addr, int shape, const int *state_vec);

inline static void cc_ocache_l1_insert_levels(
  cc_ocache_t *cc, int id, uint64_t oid, uint64_t addr, int shape, const int *state_vec) {
  cc_ocache_insert_levels(cc, MESI_CACHE_L1, id, oid, addr, shape, state_vec);
}
inline static void cc_ocache_l2_insert_levels(
  cc_ocache_t *cc, int id, uint64_t oid, uint64_t addr, int shape, const int *state_vec) {
  cc_ocache_insert_levels(cc, MESI_CACHE_L2, id, oid, addr, shape, state_vec);
}
inline static void cc_ocache_llc_insert_levels(
  cc_ocache_t *cc, int id, uint64_t oid, uint64_t addr, int shape, const int *state_vec) {
  cc_ocache_insert_levels(cc, MESI_CACHE_LLC, id, oid, addr, shape, state_vec);
}

void cc_ocache_conf_print(cc_ocache_t *cc);
void cc_ocache_stat_print(cc_ocache_t *cc);

//* cc_simple_t

typedef struct {
  union {
    struct {
      ocache_t *l1;
      ocache_t *l2;
      ocache_t *llc;
    };
    ocache_t *ocaches[MESI_CACHE_END];
  };
} cc_simple_core_t;

typedef struct cc_struct_t {
  cc_common_t commons;
  int use_shape[MESI_CACHE_END];     // Whether the level uses shaped cache, default 0
  cc_simple_core_t *cores; // Note that there is only a single core
  ocache_t *shared_llc;
  int default_shape;
} cc_simple_t;

// This also transfers ownership of the param object to the CC object
cc_simple_t *cc_simple_init_conf(conf_t *conf);
cc_simple_t *cc_simple_init();
void cc_simple_free(cc_simple_t *cc);

inline static ocache_t *cc_simple_get_core_cache(cc_simple_t *cc, int cache, int id) {
  assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2 || cache == MESI_CACHE_LLC);
  assert(id == 0);
  (void)id;
  return cc->cores[0].ocaches[cache];
}

inline static int cc_simple_get_latency(cc_simple_t *cc, int cache) {
  assert(cache == MESI_CACHE_L1 || cache == MESI_CACHE_L2 || cache == MESI_CACHE_LLC);
  return cc->commons.cache_latencies[cache];
}

void cc_simple_set_dmap(cc_simple_t *cc, dmap_t *dmap); // This does not set ownership
void cc_simple_set_dram(cc_simple_t *cc, dram_t *dram); // This does not set ownership
void cc_simple_clear_dmap(cc_simple_t *cc);
void cc_simple_clear_dram(cc_simple_t *cc);

inline static void cc_simple_set_dram_debug_cb(cc_simple_t *cc, cc_dram_debug_cb_t cb) {
  cc->commons.dram_debug_cb = cb;
  return;
}

inline static void cc_simple_clear_dram_debug_cb(cc_simple_t *cc) {
  cc->commons.dram_debug_cb = NULL;
  return;
}

inline static int cc_simple_get_default_shape(cc_simple_t *cc) { return cc->default_shape; }

uint64_t cc_simple_llc_insert_recursive(
  cc_simple_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int shape, int dirty);
uint64_t cc_simple_l2_insert_recursive(
  cc_simple_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int shape, int dirty);
uint64_t cc_simple_l1_insert_recursive(
  cc_simple_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int shape, int dirty);

// Simulates load and store on the cache hierarchy; These functions only perform coherence actions without
// actually involving data and page shapes
uint64_t cc_simple_access(cc_simple_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int is_write);
uint64_t cc_simple_load(cc_simple_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr);
uint64_t cc_simple_store(cc_simple_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr);

// Used for tests
int cc_simple_is_line_invalid(cc_simple_t *cc, int cache, int id, uint64_t oid, uint64_t addr, int shape);
int cc_simple_is_line_dirty(cc_simple_t *cc, int cache, int id, uint64_t oid, uint64_t addr, int shape);
int cc_simple_get_line_shape(cc_simple_t *cc, int cache, int id, uint64_t oid, uint64_t addr, int shape);

// Just access the line without any side-effect besides changing the LRU. The line must exist in the cache
void cc_simple_touch(cc_simple_t *cc, int cache, int id, uint64_t oid, uint64_t addr, int shape);

void cc_simple_insert_levels(
  cc_simple_t *cc, int begin, int id, uint64_t oid, uint64_t addr, int shape, const int *state_vec);
inline static void cc_simple_l1_insert_levels(
  cc_simple_t *cc, int id, uint64_t oid, uint64_t addr, int shape, const int *state_vec) {
  cc_simple_insert_levels(cc, MESI_CACHE_L1, id, oid, addr, shape, state_vec);
}
inline static void cc_simple_l2_insert_levels(
  cc_simple_t *cc, int id, uint64_t oid, uint64_t addr, int shape, const int *state_vec) {
  cc_simple_insert_levels(cc, MESI_CACHE_L2, id, oid, addr, shape, state_vec);
}
inline static void cc_simple_llc_insert_levels(
  cc_simple_t *cc, int id, uint64_t oid, uint64_t addr, int shape, const int *state_vec) {
  cc_simple_insert_levels(cc, MESI_CACHE_LLC, id, oid, addr, shape, state_vec);
}

void cc_simple_print_set(cc_simple_t *cc, int cache, int id, int set_index);

void cc_simple_conf_print(cc_simple_t *cc);
void cc_simple_stat_print(cc_simple_t *cc);

//* oc_t - Overlay Compression top level class

struct oc_struct_t;

// scache/ocache's coherence controller call back function for load and store
typedef uint64_t (*oc_cc_cb_t)(struct oc_struct_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr);

// Used for oc.cc_simple_type
#define OC_CC_SIMPLE     0
#define OC_CC_SCACHE     1 
#define OC_CC_OCACHE     2

extern const char *oc_cc_simple_type_names[3];

// Currently only support single core
typedef struct oc_struct_t {
  dmap_t *dmap;          // Unified dmap and pmap; Has ownership; Default shape is configured by oc_t conf
  dram_t *dram;          // DRAM timing model; Has ownership
  // One of them is used
  union {
    cc_simple_t *cc_simple;
    cc_scache_t *cc_scache;
    cc_ocache_t *cc_ocache;
  };
  // cc type
  int cc_type;
  // This points to the commons structure to both cc, such that we do not need two functions for accessing them
  cc_common_t *cc_commons;
  // Load and store call backs
  oc_cc_cb_t load_cb;
  oc_cc_cb_t store_cb;
} oc_t;

// Initializes the cc based on conf options; Also init cc_commons and load/store cbs
void oc_init_cc(oc_t *oc, conf_t *conf);

oc_t *oc_init(conf_t *conf);
void oc_free(oc_t *oc);

// Wrapper functions

uint64_t oc_simple_load(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr);
uint64_t oc_simple_store(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr);

uint64_t oc_scache_load(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr);
uint64_t oc_scache_store(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr);

uint64_t oc_ocache_load(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr);
uint64_t oc_ocache_store(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr);

// The following two are top-level load and store, which wraps cc_simple_load() and cc_simple_store(). 
// They will take care of data operations.

// Convert an arbitrary range to cache aligned accesses
void oc_gen_aligned_line_addr(oc_t *oc, uint64_t addr, int size, uint64_t *base_addr, int *line_count);

// Writing size bytes to buf, which will be consumed by the application program
uint64_t oc_load(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int size, void *buf);
// Copy size bytes from buf to dmap's data store
uint64_t oc_store(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int size, void *buf);

inline static dmap_t *oc_get_dmap(oc_t *oc) { return oc->dmap; }
inline static pmap_t *oc_get_pmap(oc_t *oc) { return oc->dmap; }

void oc_append_stat_snapshot(oc_t *oc);
void oc_save_stat_snapshot(oc_t *oc, char *prefix);

// Remove dump files from DRAM and various cache objects
void oc_remove_dump_files(oc_t *oc);

// Prints out a given set on a given level
void oc_print_set(oc_t *oc, int cache, int id, int set_index);

// Top-level conf and stat printf
void oc_conf_print(oc_t *oc);
void oc_stat_print(oc_t *oc); // This will also write DRAM and cache stat dump to a text file

//* main_addr_map_t - A hash table mapping 1D address to 2D address

// 1 million initial entries for the addr map (default 1M)
#define MAIN_ADDR_MAP_INIT_COUNT  (1 * 1024 * 1024)

// Entry used by main class addr map
typedef struct main_addr_map_entry_struct_t {
  uint64_t addr_1d;
  uint64_t oid_2d;
  uint64_t addr_2d;
  // Singly linked list
  main_addr_map_entry_struct_t *next;
} main_addr_map_entry_t;

main_addr_map_entry_t *main_addr_map_entry_init();
void main_addr_map_entry_free(main_addr_map_entry_t *entry);

typedef struct {
  main_addr_map_entry_t *entries[MAIN_ADDR_MAP_INIT_COUNT];
  int entry_count;
  // Statistics
  uint64_t list_count;   // Number of linked lists (i.e., non-empty slots)
  uint64_t step_count;   // Number of pointer tracing (first one is not counted)
  uint64_t insert_count; // Number of insert requests (regardless of whether it succeeds)
  uint64_t find_count;   // Number of find requests
} main_addr_map_t;

main_addr_map_t *main_addr_map_init();
void main_addr_map_free(main_addr_map_t *addr_map);

// Insert new entry; Overwrite existing entry if any
// Return the entry object, either newly inserted, or existing one
main_addr_map_entry_t *main_addr_map_insert(
  main_addr_map_t *addr_map, uint64_t addr_1d, uint64_t oid_2d, uint64_t addr_2d);
// Insert a range; addr_1d and addr_2d must be aligned, but size can be arbitrary
void main_addr_map_insert_range(
  main_addr_map_t *addr_map, uint64_t addr_1d, int size, uint64_t oid_2d, uint64_t addr_2d);
// Return the map entry pointer. NULL if not found
main_addr_map_entry_t *main_addr_map_find(main_addr_map_t *addr_map, uint64_t addr_1d);

void main_addr_map_print(main_addr_map_t *addr_map);

void main_addr_map_conf_print(main_addr_map_t *addr_map);
void main_addr_map_stat_print(main_addr_map_t *addr_map);

//* main_latency_list_t - Records latencies values; Functions as std::vector

// Initial capacity of the list
#define MAIN_LATENCY_LIST_INIT_COUNT 128
// Size of the data buffer for store values
#define MAIN_LATENCY_DATA_SIZE       256

typedef struct {
  uint64_t addr;
  int op;
  int size;
  uint8_t data[MAIN_LATENCY_DATA_SIZE];
} main_latency_list_entry_t;

typedef struct {
  int capacity;
  int count;
  main_latency_list_entry_t *data;
  uint64_t reset_count;
} main_latency_list_t;

main_latency_list_t *main_latency_list_init();
void main_latency_list_free(main_latency_list_t *list);

// Resets the list to empty state
inline static void main_latency_list_reset(main_latency_list_t *list) { 
  list->count = 0; 
  list->reset_count++; 
}
void main_latency_list_append(main_latency_list_t *list, int op, uint64_t addr, int size, void *data);
main_latency_list_entry_t *main_latency_list_get(main_latency_list_t *list, int index);

inline static int main_latency_list_get_count(main_latency_list_t *list) { return list->count; }

void main_latency_list_conf_print(main_latency_list_t *list);
void main_latency_list_stat_print(main_latency_list_t *list);

//* main_t
// We cannot use 2DOC as identifier since it begins with a number

// Maximum number of bytes of a memory operation
#define MAIN_MEM_OP_MAX_SIZE        256
#define MAIN_RESULT_DIR_SIZE        256
#define MAIN_RESULT_SUFFIX_MAX_SIZE 64
#define MAIN_APP_NAME_MAX_SIZE      32

// Memory operation
#define MAIN_READ             0
#define MAIN_WRITE            1

typedef struct {
  uint64_t max_inst_count;    // Max number of instructions
  uint64_t start_inst_count;  // Starting instructions, default zero
  char *result_suffix;        // Suffix of result directory, for better recognizability; Optional, NULL if invalid
  // This could be set by either conf file key main.app_name, or by main_set_app_name()
  char *app_name;             // Name of the application, will be reflected in the result directory name; Optional
  // If set, 4 adjacent blocks are rotated to the same addr and OID 0, 1, 2, 3; Only valid for 4_1 shape
  char *addr_1d_to_2d_type; // Address translation type string; Set by main.addr_1d_to_2d_type
} main_param_t;

main_param_t *main_param_init(conf_t *conf);
void main_param_free(main_param_t *param);

void main_param_conf_print(main_param_t *param);

// zsim configuration passed from simulator to main, which will be printed out for stat
typedef struct main_zsim_conf_struct_t {
  char *key;     // Has ownership
  char *value;   // Has ownership
  struct main_zsim_conf_struct_t *next;
} main_zsim_info_t;

main_zsim_info_t *main_zsim_info_init(const char *key, const char *value);
void main_zsim_info_free(main_zsim_info_t *info);

// Main class of the design; Test driver and controller
typedef struct main_struct_t {
  // This string has ownership
  char *conf_filename;
  conf_t *conf;        // Global conf, has ownership
  main_param_t *param; // main class's own parameter
  // Components
  oc_t *oc;                          // Overlay compression
  main_addr_map_t *addr_map;         // Mapping from 1D address to 2D space
  main_latency_list_t *latency_list; // Recording mem op latency; zSim only input latency to the hierarchy after BB sim
  int mem_op_index;                  // Current index into the latency list; Used during simulation; Reset to 0 per bb
  // Progress report
  uint64_t last_inst_count;
  uint64_t last_cycle_count;
  uint64_t start_cycle_count; // Cycle count when it starts, default zero
  int progress;        // 0 - 100
  int finished;        // Init 0, only set to 1 when max_inst_count since start_inst_count is reached
  int started;         // Init 0, only set to 1 when the start_inst_count is reached
  // Return values of time(NULL), filled on sim_begin and sim_end respectively
  uint64_t begin_time;
  uint64_t end_time;
  
  // 1d-to-2d address translation call back; Set by auto_vertical_ flags. Default to using the addr map
  void (*addr_1d_to_2d_cb)(struct main_struct_t *, uint64_t, uint64_t *, uint64_t *);
  // zsim write buffer, used to redirect writes
  // Must be aligned such that certain instructions will not incur alignment errors
  uint8_t zsim_write_buffer[MAIN_MEM_OP_MAX_SIZE * 2] __attribute__((aligned(64)));
  // Offset into the buffer
  int zsim_write_offset;
  // Previous write size
  int zsim_write_size;
  // zsim configuration passed via the interface, which will be printed out when sim ends
  main_zsim_info_t *zsim_info_list;
} main_t;

// Wraps around the conf init
main_t *main_init(const char *conf_filename);
main_t *main_init_conf(conf_t *conf);
void main_free(main_t *main);

// Called before and after the simulation respectively
void main_sim_begin(main_t *main);
void main_sim_end(main_t *main);

// Generate result directory name given a buffer; Updates the buffer in-place
void main_gen_result_dir_name(main_t *main, char *buf);
void main_set_app_name(main_t *main, const char *app_name);

// Get current memory op (must be valid); Bound check is performed
inline static main_latency_list_entry_t *main_get_mem_op(main_t *main) {
  return main_latency_list_get(main->latency_list, main->mem_op_index);
}

// Main interface functions

// Call this to update progress, and simulator may terminate simulation if the end condition is met
void main_report_progress(main_t *main, uint64_t inst, uint64_t cycle);

// The following two are called during basic block execution (bb = basic block)
inline static void main_bb_read(main_t *main, uint64_t addr, int size, void *data) {
  (void)data; 
  main_latency_list_append(main->latency_list, MAIN_READ, addr, size, NULL);
}
inline static void main_bb_write(main_t *main, uint64_t addr, int size, void *data) {
  main_latency_list_append(main->latency_list, MAIN_WRITE, addr, size, data);
}

// Called when finish simulating a BB; Resets the latency list and mem op index
void main_bb_sim_finish(main_t *main);

// Translate 1D to 2D address using the address map
void main_1d_to_2d_cb(main_t *main, uint64_t addr_1d, uint64_t *oid_2d, uint64_t *addr_2d);
void main_1d_to_2d_None_cb(main_t *main, uint64_t addr_1d, uint64_t *oid_2d, uint64_t *addr_2d);
void main_1d_to_2d_auto_vertical_4_1_cb(main_t *main, uint64_t addr_1d, uint64_t *oid_2d, uint64_t *addr_2d);

// The following is called during processor simulation, which takes argument from the internal latency 
// list, rather than from external.
// Return value is finish cycle of the operation
uint64_t main_mem_op(main_t *main, uint64_t cycle);

// Adding info that will be printed at the end of the simulation, e.g., simulated workload type, etc.
void main_add_info(main_t *main, const char *key, const char *value);

// Returns a malloc()'ed string buffer which is the cwd of the program
char *main_get_cwd(main_t *main);

// The following two are called to install address translations and update per-page shape info
// The simulator should have some way to receive request from the application

// Addr and size need not be aligned; This function aligns to page boundaries
void main_update_shape(main_t *main, uint64_t addr, int size, int shape);
// addr_1d and addr_2d should be 64-byte aligned, size need not a multiple of 64, however.
void main_update_2d_addr(main_t *main, uint64_t addr_1d, int size, uint64_t oid_2d, uint64_t addr_2d);
// Write data to dmap (if data is not in any cache yet, this is equivalent of preparing data in DRAM)
// and bypass the coherence controller
void main_update_data(main_t *main, uint64_t addr_1d, int size, void *data);

// zsim debug print
void main_zsim_debug_print_all(main_t *main);

// Saves conf and stat to seperate files on the disk. The prefix serves the same purpose as the oc save function
void main_save_conf_stat(main_t *main, char *prefix);

// Not called by programmer, when sim begins and ends they will be called
void main_conf_print(main_t *main);
void main_stat_print(main_t *main);

//* zsim - zsim related functions, called by application to communicate with the simulator

// R15 addr; R14 size; R13 shape
// XCHG R15, R15
//void zsim_update_shape(uint64_t _addr, int _size, int _shape);
inline static void zsim_update_shape(uint64_t addr, int size, int shape) {
  // Write addr into R15
  __asm__ __volatile__("mov %0, %%r15\n\t"
                     : /* no output */
                     : "a"(addr)
                     : "%r15");
  __asm__ __volatile__("mov %0, %%r14\n\t"
                     : /* no output */
                     : "a"((uint64_t)size)
                     : "%r14");
  __asm__ __volatile__("mov %0, %%r13\n\t"
                     : /* no output */
                     : "a"((uint64_t)shape)
                     : "%r13");
  // XCHG R15, R15
  asm(".byte 0x4F, 0x87, 0xFF");
  return;
}

// R15 addr_1d; R14 size; R13 oid_2d; R12 addr_2d
// XCHG R14, R14
inline static void zsim_update_2d_addr(uint64_t addr_1d, int size, uint64_t oid_2d, uint64_t addr_2d) {
  // Write addr into R15
  __asm__ __volatile__("mov %0, %%r15\n\t"
                     : /* no output */
                     : "a"(addr_1d)
                     : "%r15");
  __asm__ __volatile__("mov %0, %%r14\n\t"
                     : /* no output */
                     : "a"((uint64_t)size)
                     : "%r14");
  __asm__ __volatile__("mov %0, %%r13\n\t"
                     : /* no output */
                     : "a"(oid_2d)
                     : "%r13");
  __asm__ __volatile__("mov %0, %%r12\n\t"
                     : /* no output */
                     : "a"(addr_2d)
                     : "%r12");
  // XCHG R14, R14
  asm(".byte 0x4D, 0x87, 0xF6");
  return;
}

// R15 addr_1d; R14 size; R13 oid_2d; R12 data (virtual address)
// XCHG R13, R13
inline static void zsim_update_data(uint64_t addr_1d, int size, void *data) {
  // Write addr into R15
  __asm__ __volatile__("mov %0, %%r15\n\t"
                     : /* no output */
                     : "a"(addr_1d)
                     : "%r15");
  __asm__ __volatile__("mov %0, %%r14\n\t"
                     : /* no output */
                     : "a"((uint64_t)size)
                     : "%r14");
  __asm__ __volatile__("mov %0, %%r13\n\t"
                     : /* no output */
                     : "a"(data)
                     : "%r13");
  // XCHG R13, R13
  asm(".byte 0x4D, 0x87, 0xED");
  return;
}

// XCHG R12, R12
inline static void zsim_debug_print_all() {
  asm(".byte 0x4D, 0x87, 0xE4");
  return;
}