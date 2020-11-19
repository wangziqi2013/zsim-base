
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

extern BDI_param_t BDI_params[6]; // 8-4, 8-2, 8-1, 4-2, 4-1, 2-1
extern BDI_param_t *BDI_types[8]; // Selected using "type" field; Invalid entries that are not BDI will be NULL
extern int BDI_comp_order[6];     // Order of types we try for compression

// BDI type in the run time
#define BDI_TYPE_BEGIN     2
#define BDI_TYPE_END       8   // End (max + 1) value of BDI types; Other types are not BDI but may still be valid
#define BDI_TYPE_INVALID   -1  // Could not compress; Returned by BDI_comp() and BDI_decomp()
#define BDI_TYPE_NOT_FOUND -2  // Could not be found; Returned by dmap_find_compressed()   

// Selects BDI schemes; This is the internal macro we use for writing program
#define BDI_PARAM_BEGIN 0
#define BDI_8_4         0
#define BDI_8_2         1
#define BDI_8_1         2
#define BDI_4_2         3
#define BDI_4_1         4
#define BDI_2_1         5
#define BDI_PARAM_END   6

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
int FPC_comp(void *out_buf, void *_in_buf);
void FPC_decomp(void *_out_buf, void *in_buf);
void FPC_print_compressed(void *in_buf);
void FPC_print_packed_bits(uint64_t *buf, int begin_offset, int bits);

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
      uint8_t data[UTIL_CACHE_LINE_SIZE]; // Data, potentially compressed
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
  // pmap fields
  int default_shape;
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

int dmap_find_compressed(dmap_t *dmap, uint64_t oid, uint64_t _addr, void *out_buf);

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

inline static void pmap_set_default_shape(pmap_t *pmap, int shape) { pmap->default_shape = shape; }
inline static int pmap_get_count(pmap_t *pmap) { return pmap->pmap_count; }

pmap_entry_t *pmap_insert(pmap_t *pmap, uint64_t addr);
// Insert a range of pages, all have the same shape
void pmap_insert_range(pmap_t *pmap, uint64_t addr, int size, int shape);
// addr should be cache line aligned
inline static pmap_entry_t *pmap_find(pmap_t *pmap, uint64_t addr) {
  assert((addr & (UTIL_CACHE_LINE_SIZE - 1)) == 0);
  //printf("Find   0x%lX\n", addr & UTIL_PAGE_MSB_MASK);
  return dmap_find(pmap, PMAP_OID, UTIL_GET_PAGE_ADDR(addr));
} 

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

// Physical slot definition
typedef struct ocache_entry_struct_t {
  // Note that multiple compressed blocks share one OID and addr tag
  uint64_t oid;   // Make it large, avoid overflow
  uint64_t addr;  // Not shifted, but lower bits are zero
  // LRU is 0 for invalid slot such that they will always be used for insertion when present
  uint64_t lru;   // LRU counter value; Smallest is LRU line
  // 4 * 1: Ordered by OIDs
  // 1 * 4: Ordered by addresses
  // 2 * 2: First ordered by OID, then by addresses
  // None: Not compressed, act like normal cache
  int shape;
  uint8_t states;   // Valid/dirty bits, lower 4 valid, higher 4 dirty
  int8_t sizes[4];  // Compressed size of block 0, 1, 2, 3; Must be cleared if not valid
} ocache_entry_t;

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
  entry->sizes[index] = (int8_t)size;
  return;
}

inline static int ocache_entry_get_size(ocache_entry_t *entry, int index) {
  assert(index >= 0 && index < 4);
  assert(entry->sizes[index] >= 0 && entry->sizes[index] <= (int)UTIL_CACHE_LINE_SIZE);
  return (int)entry->sizes[index];
}

// Return the sum of all sizes
inline static int ocache_entry_get_all_size(ocache_entry_t *entry) {
  int ret = entry->sizes[0] + entry->sizes[1] + entry->sizes[2] + entry->sizes[3];
  assert(ret >= 0 && ret <= (int)UTIL_CACHE_LINE_SIZE);
  return ret;
}

// Whether an incoming line of given size will fit into the cache
inline static int ocache_entry_is_fit(ocache_entry_t *entry, int size) {
  return ocache_entry_get_all_size(entry) + size <= (int)UTIL_CACHE_LINE_SIZE;
}

// Clear the size array
inline static void ocache_entry_clear_all_size(ocache_entry_t *entry) {
  memset(entry->sizes, 0x00, sizeof(entry->sizes));
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
  entry->sizes[index] = 0;
  return;
}

inline static void ocache_entry_copy(ocache_entry_t *dest, ocache_entry_t *src) {
  memcpy(dest, src, sizeof(ocache_entry_t));
  return;
}

// The following are used as return status of ocache lookup
// Miss completely, not even invalid in a super block
#define OCACHE_LOOKUP_MISS               0
#define OCACHE_LOOKUP_HIT_BEGIN          1
// Line is hit and the slot is uncompressed
#define OCACHE_LOOKUP_HIT_NORMAL         1
// Line is hit and the slot is compressed
#define OCACHE_LOOKUP_HIT_COMPRESSED     2

// This represents a line in the ocache
typedef struct {
  // Result of the lookup, see OCACHE_LOOKUP_ macros
  int state;
  // If miss, this stores pointers to super blocks that may be candidates to 
  // store the incoming line
  ocache_entry_t *candidates[OCACHE_SB_SIZE];
  // In all cases this stores the index that the requested address should be in the super block
  int indices[OCACHE_SB_SIZE];
  // Number of candidates; For hits always one
  int count;
  // The following stores hit information
  ocache_entry_t *hit_entry;
  int hit_index;
  // If miss, this stores the LRU entry
  ocache_entry_t *lru_entry;
} ocache_lookup_result_t;

#define OCACHE_INSERT_SUCCESS     0 // Nothing extra needs to be done
#define OCACHE_INSERT_EVICT       1 // The entry to be evicted is copied to the evict_entry of the result

typedef struct {
  // Result of insert; Either inserted into a vacant slot, or evict an existing entry, or hit an entry
  int state;
  // The entry that will be evicted (consider this as a MSHR)
  ocache_entry_t evict_entry;
} ocache_insert_result_t;

#define OCACHE_INV_SUCCESS        0 // Invalidation succeeds
#define OCACHE_INV_EVICT          1 // Invalidation requires dirty eviction

typedef struct {
  int state;
  int hit_index;                    // The index of the line in the sb that gets evicted
  ocache_entry_t evict_entry;
} ocache_inv_result_t;

// Index computation parameters; We do some masking and shifting of addr. and OID,
// and XOR them together to derive the index
typedef struct {
  uint64_t addr_mask;         // The mask applied to line aligned address
  int addr_shift;             // Right shift amount after masking
  uint64_t oid_mask;          // The mask applied to OID
  int oid_shift;              // Right shift amount after masking
} ocache_index_t;

typedef struct ocache_struct_t {
  char *name;                // Optional name of the cache object, NULL by default
  int level;                 // Level of the cache (L1 - 0, L2 - 1, L3 - 2)
  int set_count;             // Number of sets, can be arbitrary
  int way_count;             // Number of ways, must be power of two
  int line_count;            // Total number of cache lines
  int size;                  // total_count * UTIL_CACHE_LINE_SIZE
  int set_bits;              // Number of bits to represent the set
  // Shape related
  int use_shape;             // Use shape argumnent, otherwise always use OCACHE_SHAPE_NONE
  ocache_index_t indices[OCACHE_SHAPE_COUNT]; // Index masking and shift
  uint64_t lru_counter;      // Use this to implement LRU
  // dmap_t object for compression
  dmap_t *dmap;              // Used if the cache is compressed, otherwise NULL
  // Statistics
  uint64_t vertical_attempt_count;
  uint64_t vertical_in_compressed_count;
  uint64_t vertical_not_base_count;
  uint64_t vertical_base_found_count;
  uint64_t vertical_same_type_count;
  uint64_t vertical_success_count;
  // Only count successes; Lines not using vertical comp are not considered
  uint64_t vertical_before_size_sum; // Size of lines before vertical compression
  uint64_t vertical_after_size_sum;  // Size of lines after vertical compression 
  // BDI stats
  uint64_t BDI_success_count;
  uint64_t BDI_before_size_sum;
  uint64_t BDI_after_size_sum;
  // Statistics that will be refreshed by ocache_refresh_stat()
  uint64_t valid_line_count;    // Valid physical slots
  uint64_t sb_count;            // Valid super blocks
  uint64_t sb_line_count;       // Valid lines in valid super blocks
  uint64_t sb_4_1_count;        // 4 * 1 sb count
  uint64_t sb_1_4_count;        // 1 * 4 sb count
  uint64_t sb_2_2_count;        // 2 * 2 sb count
  uint64_t no_shape_count;      // no shape line count
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

uint64_t ocache_gen_addr(ocache_t *ocache, uint64_t tag, uint64_t set_id, int shape);
uint64_t ocache_gen_addr_with_oid(ocache_t *ocache, uint64_t oid, uint64_t tag, uint64_t set_id, int shape);
void ocache_gen_addr_in_sb(ocache_t *ocache, uint64_t base_oid, uint64_t base_addr, int index, int shape,
                           uint64_t *oid_p, uint64_t *addr_p);
uint64_t ocache_gen_addr_no_shape(ocache_t *ocache, uint64_t tag, uint64_t set_id);
uint64_t ocache_gen_addr_page(ocache_t *ocache, uint64_t page, uint64_t line);

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

void ocache_lookup_read(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, ocache_lookup_result_t *result);
// This function is read-only to the ocache set
void ocache_lookup_write(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, ocache_lookup_result_t *result);
int ocache_insert_helper_get_compressed_size(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape);
void ocache_insert(ocache_t *ocache, uint64_t oid, uint64_t addr, 
                   int shape, int dirty, ocache_insert_result_t *insert_result);
void ocache_inv(ocache_t *ocache, uint64_t oid, uint64_t addr, int shape, ocache_inv_result_t *result);

// Either HIT_NORMAL or MISS; Updates hit entry and state
void ocache_lookup_read_no_shape(ocache_t *ocache, uint64_t oid, uint64_t addr, ocache_lookup_result_t *result);
// Do not push candidate, but set lru entry if miss; Also update hit entry and state
// This function is read-only to the ocache set
void ocache_lookup_write_no_shape(ocache_t *ocache, uint64_t oid, uint64_t addr, ocache_lookup_result_t *result);
// Insert a full sized line, possibly hitting an existing line, an invalid line, or evict a line
void ocache_insert_no_shape(ocache_t *ocache, uint64_t oid, uint64_t addr, int dirty, ocache_insert_result_t *result);
// Invalidate the exact address, if it is found. Eviction is needed if the line is dirty
void ocache_inv_no_shape(ocache_t *ocache, uint64_t oid, uint64_t addr, ocache_inv_result_t *result);

// Returns the MRU entry of the given set; Report error if set invalid; Only used for debugging
ocache_entry_t *ocache_get_mru(ocache_t *ocache, int set_index);
// Returns the MRU entry of the entire cache
ocache_entry_t *ocache_get_mru_global(ocache_t *ocache);

// Update statistics that are not updated during normal operation
// See ocache_t object body for more details
void ocache_refresh_stat(ocache_t *ocache);
inline static uint64_t ocache_get_valid_line_count(ocache_t *ocache) { return ocache->valid_line_count; }
inline static uint64_t ocache_get_sb_count(ocache_t *ocache) { return ocache->sb_count; }
inline static uint64_t ocache_get_sb_line_count(ocache_t *ocache) { return ocache->sb_line_count; }
inline static uint64_t ocache_get_no_shape_count(ocache_t *ocache) { return ocache->no_shape_count; }

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

// dram_t - Simple DRAM timing model

// Each sample span 10K cycle
#define DRAM_STAT_WINDOW_SIZE   10000

typedef struct dram_stat_struct_t {
  uint64_t cycle_count;
  uint32_t read_count;
  uint32_t write_count;
  uint32_t write_size;
  dram_stat_struct_t *next;
} dram_stat_t;

dram_stat_t *dram_stat_init();
void dram_stat_free(dram_stat_t *stat);

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
  uint64_t stat_window;      // By default, set to DRAM_STAT_WINDOW_SIZE; Can be set manually for debugging
  uint64_t stat_last_cycle;  // Bandwidth since the most recent interval
  uint32_t stat_read_count;  // Number of reads since "last_cycle"
  uint32_t stat_write_count; // Number of writes since "last_cycle"
  uint32_t stat_write_size;
  dram_stat_t *stat_head;    // Head of linked list
  dram_stat_t *stat_tail;    // Head of linked list
  int stat_count;
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
inline static void dram_set_stat_window(dram_t *dram, uint64_t window) {
  dram->stat_window = window;
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
  return dram_access(dram, cycle, oid, addr, dram->read_latency);
}
inline static uint64_t dram_write(dram_t *dram, uint64_t cycle, uint64_t oid, uint64_t addr, uint64_t size) {
  dram->stat_write_count++;
  dram->stat_write_size += size;
  return dram_access(dram, cycle, oid, addr, dram->write_latency);
}

// Dump stats into a file
void dram_stat_dump(dram_t *dram, const char *filename);

void dram_conf_print(dram_t *dram);
void dram_stat_print(dram_t *dram);

// cc_t

#define CC_CORE_COUNT_MAX  64
#define CC_CACHE_NAME_MAX  64

#define CC_LEVEL_COUNT     3    // Three-level hierarchy
#define CC_LEVEL_BEGIN     0
#define CC_L1              0
#define CC_L2              1
#define CC_L3              2
#define CC_LEVEL_END       3

// Coherence controller parameters
typedef struct {
  int core_count;
  // Whether the LLC is compressed
  int l3_compressed;
  // Parameters for all levels
  int sizes[CC_LEVEL_COUNT];
  int way_counts[CC_LEVEL_COUNT];
  int latencies[CC_LEVEL_COUNT];
} cc_param_t;

inline static void cc_param_set_core_count(cc_param_t *param, int core_count) { param->core_count = core_count; }
inline static int cc_param_get_core_count(cc_param_t *param) { return param->core_count; }

inline static void cc_param_set_l3_compressed(cc_param_t *param, int val) { param->l3_compressed = val; }
inline static int cc_param_get_l3_compressed(cc_param_t *param) { return param->l3_compressed; }

inline static void cc_param_set_l1_size(cc_param_t *param, int size) { param->sizes[CC_L1] = size; }
inline static void cc_param_set_l2_size(cc_param_t *param, int size) { param->sizes[CC_L2] = size; }
inline static void cc_param_set_l3_size(cc_param_t *param, int size) { param->sizes[CC_L3] = size; }

inline static int cc_param_get_l1_size(cc_param_t *param) { return param->sizes[CC_L1]; }
inline static int cc_param_get_l2_size(cc_param_t *param) { return param->sizes[CC_L2]; }
inline static int cc_param_get_l3_size(cc_param_t *param) { return param->sizes[CC_L3]; }

inline static void cc_param_set_l1_way_count(cc_param_t *param, int count) { param->way_counts[CC_L1] = count; }
inline static void cc_param_set_l2_way_count(cc_param_t *param, int count) { param->way_counts[CC_L2] = count; }
inline static void cc_param_set_l3_way_count(cc_param_t *param, int count) { param->way_counts[CC_L3] = count; }

inline static int cc_param_get_l1_way_count(cc_param_t *param) { return param->way_counts[CC_L1]; }
inline static int cc_param_get_l2_way_count(cc_param_t *param) { return param->way_counts[CC_L2]; }
inline static int cc_param_get_l3_way_count(cc_param_t *param) { return param->way_counts[CC_L3]; }

inline static void cc_param_set_l1_latency(cc_param_t *param, int latency) { param->latencies[CC_L1] = latency; }
inline static void cc_param_set_l2_latency(cc_param_t *param, int latency) { param->latencies[CC_L2] = latency; }
inline static void cc_param_set_l3_latency(cc_param_t *param, int latency) { param->latencies[CC_L3] = latency; }

inline static int cc_param_get_l1_latency(cc_param_t *param) { return param->latencies[CC_L1]; }
inline static int cc_param_get_l2_latency(cc_param_t *param) { return param->latencies[CC_L2]; }
inline static int cc_param_get_l3_latency(cc_param_t *param) { return param->latencies[CC_L3]; }

cc_param_t *cc_param_init(conf_t *conf);
void cc_param_free(cc_param_t *param);

void cc_param_read_cache_conf(
  conf_t *conf, const char *prefix, const char *cache_name, int *size, int *way_count, int *latency);

void cc_param_conf_print(cc_param_t *param);

// Passed to DRAM interface
#define CC_READ         0
#define CC_WRITE        1

// DRAM read/write call back, used by the cc
// It takes entry variable. For write, it must contain all blocks that are evicted
// For read, only oid, addr, shape is used to indicate the address to be loaded
typedef uint64_t (*cc_main_mem_cb_t)(
  struct cc_struct_t *cc, uint64_t cycle, int op, ocache_entry_t *entry
);

typedef struct cc_struct_t {
  ocache_t **caches[CC_LEVEL_COUNT]; // Array of cache pointers, first dimension is level
  int use_shape[CC_LEVEL_COUNT];     // Whether the level uses shaped cache, default 0
  ocache_t *llc;                     // Single last-level cache instance
  dmap_t *dmap;                      // Set using external functions; Optional (can be NULL); No ownership
  dram_t *dram;                      // Set using external functions; Optional; No ownership
  cc_param_t *param;                 // All level parameters, has ownership
  // Main memory access call back pointer; Use this for debugging, i.e., intercept DRAM requests
  cc_main_mem_cb_t main_mem_cb;
  // Statistics; First dimension is level
  uint64_t *read_count[CC_LEVEL_COUNT];
  uint64_t *write_count[CC_LEVEL_COUNT];
  uint64_t *read_hit_count[CC_LEVEL_COUNT];
  uint64_t *read_miss_count[CC_LEVEL_COUNT];
  uint64_t *write_hit_count[CC_LEVEL_COUNT];
  uint64_t *write_miss_count[CC_LEVEL_COUNT];
} cc_t;

// This also transfers ownership of the param object to the CC object
cc_t *cc_init(conf_t *conf);
void cc_free(cc_t *cc);

void cc_set_dmap(cc_t *cc, dmap_t *dmap); // This does not set ownership
void cc_set_dram(cc_t *cc, dram_t *dram); // This does not set ownership
inline static void cc_set_main_mem_cb(cc_t *cc, cc_main_mem_cb_t cb) { cc->main_mem_cb = cb; }

inline static ocache_t *cc_get_ocache(cc_t *cc, int level, int core) {
  assert(level >= CC_LEVEL_BEGIN && level < CC_LEVEL_END);
  assert(core >= 0 && core < cc->param->core_count);
  return cc->caches[level][core];
}

// Recursively evict an entry from the LLC, taking into consideration the evictions from upper levels
uint64_t cc_inv_llc_recursive(cc_t *cc, int id, uint64_t cycle, ocache_entry_t *entry, ocache_entry_t *result);
// Insert into the LLC, handles recursive evictions
uint64_t cc_insert_llc_recursive(
  cc_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int shape, int dirty);
// The following two call the above two for special-case handling
uint64_t cc_inv_recursive(
  cc_t *cc, int id, int level, uint64_t cycle, uint64_t oid, uint64_t addr, int *dirty);
uint64_t cc_insert_recursive(
  cc_t *cc, int id, uint64_t cycle, int start_level, uint64_t oid, uint64_t addr, int shape, int is_write);

// Simulates load and store on the cache hierarchy; These functions only perform coherence actions without
// actually involving data and page shapes
uint64_t cc_access(cc_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int is_write);
uint64_t cc_load(cc_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr);
uint64_t cc_store(cc_t *cc, int id, uint64_t cycle, uint64_t oid, uint64_t addr);

// This is the main memory call back
uint64_t cc_main_mem_cb(cc_t *cc, uint64_t cycle, int op, ocache_entry_t *entry);

void cc_conf_print(cc_t *cc);
void cc_stat_print(cc_t *cc, int verbose);

//* oc_t - Overlay Compression top level class

// Currently only support single core
typedef struct {
  dmap_t *dmap;          // Unified dmap and pmap; Has ownership
  dram_t *dram;          // DRAM timing model; Has ownership
  cc_t *cc;              // cc and cache objects
  ocache_t *l1;
  ocache_t *l2;
  ocache_t *l3;
} oc_t;

oc_t *oc_init(conf_t *conf);
void oc_free(oc_t *oc);

// The following two are top-level load and store, which wraps cc_load() and cc_store(). 
// They will take care of data operations.

// Convert an arbitrary range to cache aligned accesses
void oc_gen_line_addr(oc_t *oc, uint64_t addr, int size, uint64_t *base_addr, int *line_count);

// Writing size bytes to buf, which will be consumed by the application program
uint64_t oc_load(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int size, void *buf);
// Copy size bytes from buf to dmap's data store
uint64_t oc_store(oc_t *oc, int id, uint64_t cycle, uint64_t oid, uint64_t addr, int size, void *buf);

inline static dmap_t *oc_get_dmap(oc_t *oc) { return oc->dmap; }
inline static pmap_t *oc_get_pmap(oc_t *oc) { return oc->dmap; }

// Top-level conf and stat printf
void oc_conf_print(oc_t *oc);
void oc_stat_print(oc_t *oc); // This will write DRAM stat dump to a file

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

void main_addr_map_conf_print(main_addr_map_t *addr_map);
void main_addr_map_stat_print(main_addr_map_t *addr_map);

//* main_latency_list_t - Records latencies values; Functions as std::vector

// Initial capacity of the list
#define MAIN_LATENCY_LIST_INIT_COUNT 128
// Size of the data buffer
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
#define MAIN_MEM_OP_MAX_SIZE     256

// Memory operation
#define MAIN_READ             0
#define MAIN_WRITE            1

typedef struct {
  uint64_t max_inst_count;    // Max number of instructions
  int logging;                // Whether logging is enabled
  char *logging_filename;     // Only read if logging is set to 1; Ignored otherwise
} main_param_t;

main_param_t *main_param_init(conf_t *conf);
void main_param_free(main_param_t *param);

void main_param_conf_print(main_param_t *param);

// This struct contains requests
// The actual size of this struct is (sizeof(main_request_t) + reqiest->size)
typedef struct {
  int16_t op;          // MAIN_READ / MAIN_WRITE
  int16_t size;        // In bytes
  uint64_t cycle;      // The cycle it is called
  uint64_t addr;       // The addr it is called on (1D address)
  uint8_t data[0]; // Caller should allocate this part of the storage
} main_request_t;

// Main class of the design; Test driver and controller
typedef struct {
  // This string has ownership
  char *conf_filename;
  conf_t *conf;        // Global conf, has ownership
  main_param_t *param; // main class's own parameter
  // Components
  oc_t *oc;                          // Overlay compression
  main_addr_map_t *addr_map;         // Mapping from 1D address to 2D space
  main_latency_list_t *latency_list; // Recording latency values
  int mem_op_index;                  // Current index into the latency list; Used during simulation; Reset to 0 per bb
  // Progress report
  uint64_t last_inst_count;
  uint64_t last_cycle_count;
  int progress;        // 0 - 100
  int finished;
  // Logging related
  FILE *logging_fp;
} main_t;

// Wraps around the conf init
main_t *main_init(const char *conf_filename);
main_t *main_init_from_conf(conf_t *conf);
void main_free(main_t *main);

// Called before and after the simulation respectively
void main_sim_begin(main_t *main);
void main_sim_end(main_t *main);

// Main interface functions

// Append a log entry; Assumes logging is enabled
void main_append_log(main_t *main, uint64_t cycle, int op, uint64_t addr, int size, void *data);

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
void main_1d_to_2d(main_t *main, uint64_t addr_1d, uint64_t *oid_2d, uint64_t *addr_2d);

// The following is called during processor simulation, which takes argument from the internal latency 
// list, rather than from external.
// Return value is finish cycle of the operation
uint64_t main_mem_op(main_t *main, uint64_t cycle);

// The following two are called to install address translations and update per-page shape info
// The simulator should have some way to receive request from the application

// Addr and size need not be aligned; This function aligns to page boundaries
void main_update_shape(main_t *main, uint64_t addr, int size, int shape);
// addr_1d and addr_2d should be 64-byte aligned, size need not a multiple of 64, however.
void main_update_2d_addr(main_t *main, uint64_t addr_1d, int size, uint64_t oid_2d, uint64_t addr_2d);
// Write data to dmap (if data is not in any cache yet, this is equivalent of preparing data in DRAM)
// and bypass the coherence controller
void main_update_data(main_t *main, uint64_t addr_1d, int size, void *data);

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
inline static void zsim_update_2d(uint64_t addr_1d, int size, uint64_t oid_2d, uint64_t addr_2d) {
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
