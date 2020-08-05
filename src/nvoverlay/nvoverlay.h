
#ifndef _NVOVERLAY_H
#define _NVOVERLAY_H

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <error.h>
#include <unistd.h>
#include <time.h>
// If zsim macro is defined, we use zsim's own impl of assert()
#ifndef ZSIM_PATH
#include <assert.h>
#else
#include "log.h"
#endif

#include <sys/mman.h>

#include "util.h"

// Forward declaration in order for other functions to take pointer to this as arg
struct nvoverlay_struct_t; 
struct overlay_struct_t;
struct overlay_epoch_struct_t;
struct vtable_struct_t;
struct ver_struct_t;

//* fifo_t

typedef struct fifo_node_struct_t {
  uint64_t value;  // Value of the variable
  struct fifo_node_struct_t *next;
} fifo_node_t;

fifo_node_t *fifo_node_init();
void fifo_node_free(fifo_node_t *fifo_node);

typedef struct {
  fifo_node_t *tail; // Inserting end
  fifo_node_t *head; // Reading end
  uint64_t item_count;
} fifo_t;

fifo_t *fifo_init();
void fifo_free(fifo_t *fifo);

void fifo_insert(fifo_t *fifo, uint64_t value);

// Iterator type
typedef fifo_node_t *fifo_it_t;

inline static void fifo_begin(fifo_t *fifo, fifo_it_t *it) { *it = fifo->head; (void)fifo; }
inline static int fifo_is_end(fifo_t *fifo, fifo_it_t *it) { return *it == NULL; (void)fifo; }
inline static void fifo_next(fifo_t *fifo, fifo_it_t *it) { *it = (*it)->next; (void)fifo; }
inline static uint64_t fifo_it_value(fifo_it_t *it) { return (*it)->value; }

inline static uint64_t fifo_get_item_count(fifo_t *fifo) { return fifo->item_count; }

//* ht64_t

// Copied from the web
inline static uint64_t ht64_hash_func(uint64_t h) {
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdUL;
  h ^= h >> 33;
  h *= 0xc4ceb9fe1a85ec53UL;
  h ^= h >> 33;
  return h;
}

#define HT64_DEFAULT_INIT_BUCKETS  128 // If not specified then we use this value
#define HT64_LOAD_FACTOR           16  // Maximum average number of nodes per bucket

typedef struct ht64_node_struct_t {
  uint64_t key;
  uint64_t value;
  struct ht64_node_struct_t *next;
} ht64_node_t;

// Keys are sorted in a single bucket, which simplifies searching
typedef struct {
  ht64_node_t **buckets; // Array of buckets
  uint64_t item_count;   // # of items
  uint64_t bucket_count; // # of buckets
  uint64_t bucket_mask;  // LSBs are set to 1 for extracting the index
} ht64_t;

// Iterator type to iterate over addresses
typedef struct {
  uint64_t bucket;   // Current bucket
  ht64_node_t *node; // Current node
} ht64_it_t;

typedef void (*ht64_free_cb_t)(void *);

ht64_node_t *ht64_node_init();
void ht64_node_free(ht64_node_t *node);

ht64_t *ht64_init();
ht64_t *ht64_init_size(int bucket_count);
void ht64_free_cb(ht64_t *ht64, ht64_free_cb_t cb);
inline static void ht64_free(ht64_t *ht64) { ht64_free_cb(ht64, NULL); }

void ht64_resize(ht64_t *ht64);
// Return 1 if insert succeeds, 0 if key already exists
int ht64_insert(ht64_t *ht64, uint64_t key, uint64_t value);
// Default val specifies the return value if the key does not exist
uint64_t ht64_find(ht64_t *ht64, uint64_t key, uint64_t default_val);
// Remove and return the value of the key; If key not found return default val
uint64_t ht64_remove(ht64_t *ht64, uint64_t key, uint64_t default_val);
// Clear all elements from the hash table, but keep the bucket
void ht64_clear(ht64_t *ht64); 

// Iterator interface
void ht64_begin(ht64_t *ht64, ht64_it_t *it);
inline static int ht64_is_end(ht64_t *ht64, ht64_it_t *it) {
  return it->bucket == ht64->bucket_count; // This overflows to the next non-existing bucket
}
void ht64_next(ht64_t *ht64, ht64_it_t *it);

// The following are remove iteration functions
typedef ht64_it_t ht64_rm_it_t;
inline static void ht64_rm_begin(ht64_t *ht64, ht64_it_t *it) { return ht64_begin(ht64, it); }
inline static int ht64_is_rm_end(ht64_t *ht64, ht64_it_t *it) { return ht64_is_end(ht64, it); }
void ht64_rm_next(ht64_t *ht64, ht64_it_t *it);

// User is responsible for ensuring the iter is valid
inline static uint64_t ht64_it_key(ht64_it_t *it) { return it->node->key; }
inline static uint64_t ht64_it_value(ht64_it_t *it) { return it->node->value; }

inline static uint64_t ht64_get_item_count(ht64_t *ht64) { return ht64->item_count; }
inline static uint64_t ht64_get_bucket_count(ht64_t *ht64) { return ht64->bucket_count; }

//* mtable_t

// If this is not defined the JIT call has no effect
#define MTABLE_ENABLE_JIT

// Call back function type for mtable_free; NULL if not needed
typedef void (*mtable_free_cb_t)(void *);
// Just-in-time compiled lookup function pointer type
typedef void **(*mtable_jit_lookup_t)(void **, uint64_t);
// Call back function used by table traverse;
// Arguments: (key, node value, arbitrary argument you passed to traverse func)
typedef void (*mtable_traverse_cb_t)(uint64_t, void *, void *); 
// ARgs: Partial key at current node level; Node pointer; whether it is leaf node; Arg pointer
typedef void (*mtable_page_traverse_cb_t)(uint64_t key, void *page, int item_count, int is_leaf, void *arg); 

typedef struct mtable_idx_struct_t {
  int level;     // Iteration number; Starts from 0
  uint64_t mask; // LSB are 1 and rest are 0; Apply mask after shift
  int rshift;    // Number of bits to right shift before applying mask
  int bits;      // Number of bits that form the index
  int pg_size;   // Number of bytes in an index page of this level
  struct mtable_idx_struct_t *next; // Linked list; Start from the first level
} mtable_idx_t;

typedef struct {
  uint64_t **root;
  mtable_idx_t *idx;
  int idx_size;   // Number of elements in the idx linked list
  uint64_t page_count; // Number of pages ever added
  uint64_t size;        // Memory usage of all pages
  int jit_lookup_size; // The size of the lookup function
  mtable_jit_lookup_t lookup_cb; // Read-only lookup function pointer
} mtable_t;

mtable_idx_t *mtable_idx_init(int start_bit, int bits);
void mtable_idx_free(mtable_idx_t *idx);

mtable_t *mtable_init();
void mtable_free_cb(mtable_t *mtable, mtable_free_cb_t cb);
void mtable_free(mtable_t *mtable);

void mtable_idx_add(mtable_t *mtable, int start_bit, int bits);
void mtable_idx_print(mtable_t *mtable);
// Called by nvoverlay after init
void mtable_conf_print(mtable_t *mtable);

void **mtable_insert(mtable_t *mtable, uint64_t key);
void *mtable_find(mtable_t *mtable, uint64_t key);

inline static uint64_t mtable_get_size(mtable_t *mtable) { return mtable->size; }
inline static uint64_t mtable_get_page_count(mtable_t *mtable) { return mtable->page_count; }

void mtable_jit_lookup(mtable_t *mtable); // Generate code into lookup_cb pointer
void mtable_jit_lookup_print(mtable_t *mtable);

void mtable_print(mtable_t *mtable);
// Traver through all leaf node valid values (not NULL)
void mtable_traverse(mtable_t *mtable, mtable_traverse_cb_t cb, void *arg);
// Traverse through all pages in the tree - this requires some internal knowledge of the tree structure
// The traverse follows a top-down manner: Inner first, then leaf
void mtable_page_traverse(mtable_t *mtable, mtable_page_traverse_cb_t cb, void *arg);

//* bitmap64_t

// Maximum position in the bitmap
#define BITMAP64_MAX_POS  63

// Abstract type for bitmap - currently only support 64 bit bitmap
typedef struct {
  uint64_t x;
} bitmap64_t;

void bitmap64_print_bitstr(bitmap64_t *bitmap);

// We call inline init as clear
inline static void bitmap64_clear(bitmap64_t *bitmap) {
  memset(bitmap, 0x00, sizeof(bitmap64_t));
}

inline static void bitmap64_add(bitmap64_t *bitmap, int pos) { 
  assert(pos >= 0 && pos < (int)sizeof(uint64_t) * 8); 
  bitmap->x |= (0x1UL << pos); 
}

inline static void bitmap64_remove(bitmap64_t *bitmap, int pos) {
  assert(pos >= 0 && pos < (int)sizeof(uint64_t) * 8); 
  bitmap->x &= (~(0x1UL << pos)); 
}

// Returns the pos of set bit; If multiple bits are set which one is returned is undefined
inline static int bitmap64_pos(bitmap64_t *bitmap) {
  assert(bitmap->x != 0UL);
  return ffs_uint64(bitmap->x) - 1;
}

// Clear all other pos except the specified one
inline static void bitmap64_set(bitmap64_t *bitmap, int pos) {
  assert(pos >= 0 && pos < (int)sizeof(uint64_t) * 8); 
  bitmap->x = 0x1UL << pos;
}

inline static int bitmap64_popcount(bitmap64_t *bitmap) {
  return popcount_uint64(bitmap->x);
}

inline static int bitmap64_is_set(bitmap64_t *bitmap, int pos) {
  assert(pos >= 0 && pos < (int)sizeof(uint64_t) * 8); 
  return !!(bitmap->x & (0x1UL << pos));
}

// Set the bitmap to all-1 - common way of initialization
inline static void bitmap64_set_all_one(bitmap64_t *bitmap) {
  bitmap->x = -1UL;
}

// Combines pos and remove: Returns a pos and remove it from the bitmap
inline static int bitmap64_pos_and_remove(bitmap64_t *bitmap) {
  int pos = bitmap64_pos(bitmap);
  bitmap64_remove(bitmap, pos);
  return pos;
}

inline static void bitmap64_copy_to(bitmap64_t *to, bitmap64_t *from) {
  memcpy(to, from, sizeof(bitmap64_t));
}

// Set last_pos to -1 to begin iteration. The value of last_pos is the value returned from this function 
// from the last iter
// Iteration ends when state changes to -1 and/or return value is -1 (happen together)
// This function does not modify bitmap
inline static int bitmap64_iter(bitmap64_t *bitmap, int last_pos) {
  // First mask off lower bits using ~((0x1UL << (last_pos + 1)) - 1), then use ffs to find the LSB "1"
  // pos == -1 if the number is 0UL after masking
  //printf("%d 0x%lX\n", last_pos, ~((0x1UL << (last_pos + 1)) - 1));
  //printf("0x%lX\n", 0x1UL << (last_pos + 1));
  // NOTE: If last_pos == 63, then << (last_pos + 1) will do nothing since the ALU only supports 5 bits shift amount
  if(last_pos == 63) return -1;
  int pos = ffs_uint64(bitmap->x & (~((0x1UL << (last_pos + 1)) - 1))) - 1;
  return pos;
}

// Used within the iter loop. Returns 1 if the last_pos is the last element in the iteration
inline static int bitmap64_iter_is_last(bitmap64_t *bitmap, int last_pos) {
  return (bitmap->x & (~((0x1UL << (last_pos + 1)) - 1))) == 0UL;
}

//* omt_t - The master mapping table (current consistent snapshot of the system)

typedef struct {
  mtable_t *mtable;          // Mapping table; Maps cache line address to a pointer to (overlay_epoch_t *)
  uint64_t epoch;            // The current stable epoch of the snapshot
  // Statistics
  uint64_t write_count;      // Number of writes for updating the OMT
  uint64_t merge_line_count; // Number of line merges (even if no new page is created)
  uint64_t working_set_size; // Working set size (data maintained on NVM as the stable version)
} omt_t;

omt_t *omt_init();
void omt_free(omt_t *omt);

inline static uint64_t omt_get_write_count(omt_t *omt) { return omt->write_count; }
inline static uint64_t omt_get_working_set_size(omt_t *omt) { return omt->working_set_size; }
inline static uint64_t omt_get_size(omt_t *omt) { return mtable_get_size(omt->mtable); }
inline static struct overlay_epoch_struct_t *omt_find(omt_t *omt, uint64_t line_addr) {
  ASSERT_CACHE_ALIGNED(line_addr);
  return (struct overlay_epoch_struct_t *)mtable_find(omt->mtable, line_addr);
}

struct overlay_epoch_struct_t *omt_merge_line(
  omt_t *omt, struct overlay_epoch_struct_t *overlay_epoch, uint64_t line_addr);

typedef struct {
  uint64_t valid_leaf_value_count; // Number of not-NULL pointers in leaf nodes
  uint64_t leaf_value_count;       // Number of value slots in all leaf pages
  uint64_t leaf_count;             // Number of leaf nodes
  uint64_t inner_count;            // Number of inner nodes
  uint64_t size;                   // Size of the OMT (sum of all pages)
} omt_info_t;

omt_info_t omt_get_info(omt_t *omt);

void omt_selfcheck(omt_t *omt);

void omt_conf_print(omt_t *omt);
void omt_stat_print(omt_t *omt);

//* cpu_t

struct ver_struct_t;

// Since we represent sharer's list with uint64_t, there can only be 64 cores at most
#define CORE_COUNT_MAX (BITMAP64_MAX_POS + 1)

// These two are used with cap_status
#define CORE_CAP_STATUS_ACTIVE   0
#define CORE_CAP_STATUS_HALTED   1

extern const char *core_cap_status_names[2];

// This struct holds processor-local variables
typedef struct {
  uint64_t epoch;    // Current local epoch
  uint64_t last_walk_epoch;      // Last tag walk epoch
  // Statistics
  uint64_t epoch_store_count;    // Number of stores yet happened in the local epoch - this is incremented by nvoverlay
  uint64_t total_store_count;    // Total number of stores
  uint64_t tag_walk_evict_count; // Number of cache lines evicted from cache walk
  uint64_t tag_walk_count;       // Per-core tag walk count
  uint64_t last_inst_count;      // This is updated by a special reporting record
  uint64_t last_cycle_count;     // This is updated by a special call back, only during online sim
  int cap_status;                // Status on cap, see CORE_CAP_* above
} core_t;

core_t *core_init();
void core_free(core_t *core);

inline static uint64_t core_get_epoch(core_t *core) { return core->epoch; }

// Macros we use to access tag array
#define CPU_TAG_MAX     2
#define CPU_TAG_L1      0
#define CPU_TAG_L2      1

// The following are used by cpu_tag_op() to receive the command
#define CPU_TAG_OP_ADD     0
#define CPU_TAG_OP_REMOVE  1
#define CPU_TAG_OP_SET     2
#define CPU_TAG_OP_CLEAR   3

extern const char *cpu_tag_op_names[];

typedef struct {
  int sets; 
  int ways;
  uint64_t mask;      // Applied after shifting to right
  int core_ver_count; // Number of versions per core, i.e. ways * sets
  int set_bits;       // Log2 of sets
  struct ver_struct_t **tags;
} cpu_tag_t;

// This is called when tag walk needs to write back a line to NVM
// Some args are not needed, but just for debugging purposes
typedef void (*cpu_tag_walk_cb_t)(struct nvoverlay_struct_t *nvoverlay, 
  uint64_t line_addr, int id, uint64_t version, uint64_t cycle);

typedef struct {
  core_t *cores;   // An array of core objects
  int core_count;  // Number of core objects
  // Optional tracking for L1 and L2; They are enabled using 
  cpu_tag_t tag_arrays[CPU_TAG_MAX]; // L1 and L2
  struct nvoverlay_struct_t *nvoverlay; 
  cpu_tag_walk_cb_t tag_walk_cb;
  // Statistics
  uint64_t coherence_advance_count;  // Number of advances caused by coherence
  uint64_t skip_epoch_count;         // Number of times a core advances its epoch by more than 1
  uint64_t total_advance_count;      // Total number of advances including all reasons
  uint64_t total_inst_count;         // Total number of inst reported on all cores
  uint64_t total_cycle_count;        // Total number of cycles reported on all cores
} cpu_t;

cpu_t *cpu_init(int core_count, cpu_tag_walk_cb_t cb);
void cpu_free(cpu_t *cpu);
inline static void cpu_set_parent(cpu_t *cpu, struct nvoverlay_struct_t *nvoverlay) { cpu->nvoverlay = nvoverlay; }
void cpu_core_recv(cpu_t *cpu, int id, uint64_t version);
void cpu_advance_epoch(cpu_t *cpu, int id);
uint64_t cpu_min_epoch(cpu_t *cpu);
uint64_t cpu_max_epoch(cpu_t *cpu);

inline static uint64_t cpu_get_epoch(cpu_t *cpu, int id) {
  assert(id >= 0 && id < cpu->core_count);
  return cpu->cores[id].epoch;
}

void cpu_tag_init(cpu_t *cpu, int level, int sets, int ways);

// These two are used for iterating a set within a core, given an address
inline static struct ver_struct_t **cpu_addr_tag_begin(cpu_t *cpu, int level, int id, int64_t addr) {
  assert(level >= 0 && level < CPU_TAG_MAX);
  cpu_tag_t *tag_array = &cpu->tag_arrays[level]; // Select L1 or L2 using level
  assert(tag_array->tags != NULL);
  int set_index = (int)((addr >> UTIL_CACHE_LINE_BITS) & tag_array->mask);
  // Indexing order: core, set, way
  return tag_array->tags + id * tag_array->core_ver_count + set_index * tag_array->ways;
}
inline static struct ver_struct_t **cpu_addr_tag_end(cpu_t *cpu, int level, struct ver_struct_t **begin) {
  return begin + cpu->tag_arrays[level].ways;
}

// These two are used for iterating over all tags for a core
inline static struct ver_struct_t **cpu_core_tag_begin(cpu_t *cpu, int level, int id) {
  assert(level >= 0 && level < CPU_TAG_MAX);
  cpu_tag_t *tag_array = &cpu->tag_arrays[level];
  assert(tag_array->tags != NULL); // Did not initialize tag array
  return tag_array->tags + id * tag_array->core_ver_count;
}
inline static struct ver_struct_t **cpu_core_tag_end(cpu_t *cpu, int level, struct ver_struct_t **begin) {
  return begin + cpu->tag_arrays[level].core_ver_count;
}

// The tag array is driven by coherence actions in vtable
void cpu_tag_insert(cpu_t *cpu, int level, int core, struct ver_struct_t *ver);
void cpu_tag_remove(cpu_t *cpu, int level, int core, struct ver_struct_t *ver);
struct ver_struct_t **cpu_tag_find(cpu_t *cpu, int level, int core, uint64_t line_addr);
void cpu_tag_op(cpu_t *cpu, int op, int level, int core, struct ver_struct_t *ver);

void cpu_tag_walk(cpu_t *cpu, int id, uint64_t cycle, uint64_t target_epoch); // Performs tag walk and changes state of versions
uint64_t cpu_tag_walk_passive(cpu_t *cpu, int id);

void cpu_tag_print(cpu_t *cpu, int level); // Prints an entire level

// Check consistency between tag and ver_t stored in it
// We need vtable to check whether versions are missing
void cpu_tag_selfcheck(cpu_t *cpu, struct vtable_struct_t *vtable, int level, int core); 
// Checks a single core, both levels, and also consistency between levels
void cpu_tag_selfcheck_core(cpu_t *cpu, struct vtable_struct_t *vtable, int core);
void cpu_tag_selfcheck_all(cpu_t *cpu, struct vtable_struct_t *vtable);

// Only used for debugging - generate addresses for a particular set
inline static uint64_t cpu_addr_gen(cpu_t *cpu, int level, uint64_t tag, int set_index) {
  cpu_tag_t *tag_array = &cpu->tag_arrays[level];
  assert(tag_array->tags != NULL);
  if(set_index < 0 || set_index >= tag_array->sets) {
    error_exit("Illegal set index: %d (should be [0, %d))\n", set_index, tag_array->sets);
  }
  return (((uint64_t)set_index & tag_array->mask) << UTIL_CACHE_LINE_BITS) | 
         (tag << (tag_array->set_bits + UTIL_CACHE_LINE_BITS));
}

inline static core_t *cpu_get_core(cpu_t *cpu, int index) { 
  assert(index >= 0 && index < cpu->core_count);
  return &cpu->cores[index]; 
}

inline static int cpu_get_core_count(cpu_t *cpu) { return cpu->core_count; }
inline static uint64_t cpu_get_last_walk_epoch(cpu_t *cpu, int id) {
  assert(id >= 0 && id < cpu->core_count);
  return cpu->cores[id].last_walk_epoch; 
}
inline static uint64_t cpu_get_total_inst_count(cpu_t *cpu) { return cpu->total_inst_count; }
inline static uint64_t cpu_get_total_cycle_count(cpu_t *cpu) { return cpu->total_cycle_count; }

int cpu_tag_get_ways(cpu_t *cpu, int level);
int cpu_tag_get_sets(cpu_t *cpu, int level);

void cpu_selfcheck(cpu_t *cpu);

// Called by nvoverlay after init
void cpu_conf_print(cpu_t *cpu);
void cpu_stat_print(cpu_t *cpu, int verbose);

//* vtable_t

#define STATE_I 0
#define STATE_S 1
#define STATE_E 2 // Do not implememt E state bc it's only a runtime optimization
#define STATE_M 3

#define OWNER_OTHER 0
#define OWNER_L1    1
#define OWNER_L2    2

#define EVICT_OMC   0x00000001
#define EVICT_LLC   0x00000002
#define EVICT_BOTH  (EVICT_OMC | EVICT_LLC)

// We make this a large number since it is expected that the working set be large
#define VTABLE_HT_INIT_BUCKET_COUNT 4096

// If owner is OWNER_OTHER then only the two bitmaps are used, and global version is other_ver
typedef struct ver_struct_t {
  uint64_t addr;           // Address tag (always 64 byte aligned)
  int l1_state, l2_state;  // MESI state
  int owner;               // Owner of the version (L1, L2, LLC + DRAM)
  int l3_dirty;            // Whether the line is dirty in LLC (set when dirty evict from L2)
  bitmap64_t l1_bitmap;      // L1 caches that have a copy
  bitmap64_t l2_bitmap;      // L2 caches that have a copy
  uint64_t l1_ver, l2_ver, other_ver; // L1 ver and L2 ver are only valid when they are owners
} ver_t;

ver_t *ver_init();
void ver_free(void *ver);  // Use void * to make it match the prototype of the call back

// This is the call back type of eviction
// Args: addr, core id, version, cycle, type
typedef void (*vtable_evict_cb_t)(struct nvoverlay_struct_t *, uint64_t, int, uint64_t, uint64_t, int);
// Call back type for cores receiving versioned data
typedef void (*vtable_core_recv_cb_t)(struct nvoverlay_struct_t *, int, uint64_t);
// Call back type for tag array update
// Args: op code, cache level, core ID, version object
// For INSERT and REMOVE the ID indicates the mask; For SET the ver_t must be the before value, and the core ID indicates
// the core that we set
typedef void (*vtable_core_tag_cb_t)(struct nvoverlay_struct_t *, int, int, int, ver_t *);
// Takes ID as second argument
// This is used to get the last tag walk epoch on a core
typedef uint64_t (*vtable_core_stable_epoch_cb_t)(struct nvoverlay_struct_t *, int);
// Reports individual LLC-mem bus txns. Arguments: # of txns; current cycle
typedef void (*vtable_l3_bus_txn_cb_t)(struct nvoverlay_struct_t *, int, uint64_t);

typedef struct vtable_struct_t {
  ht64_t *vers;                         // Index for ver_t
  vtable_evict_cb_t evict_cb;           // Eviction call back function
  vtable_core_recv_cb_t core_recv_cb;   // Core data receival function
  vtable_core_tag_cb_t core_tag_cb;     // Core tag change function
  vtable_core_stable_epoch_cb_t core_stable_epoch_cb; // Core get stable epoch function
  vtable_l3_bus_txn_cb_t l3_bus_txn_cb; // Report LLC-mem bus txns for statistics
  struct nvoverlay_struct_t *nvoverlay; // Back pointer used for call back function
  // Statistics
  uint64_t omc_evict_count;
  uint64_t llc_evict_count;
  // Events that caused eviction to OMC
  uint64_t st_evict_count;      // Store write back
  uint64_t ld_inv_evict_count;  // Load downgrade to dirty line
  uint64_t st_inv_evict_count;  // Store invalidation to dirty line
  uint64_t l1_cap_evict_count;  // L1 capacity miss eviction (not including those triggered by L2)
  uint64_t l2_cap_evict_count;  // L2 capacity miss eviction (not including those triggered by L3)
  uint64_t l3_cap_evict_count;  // L3 capacity miss eviction
  // Events we measure for LLC-controller bandwidth simulation
  uint64_t l3_bus_txn_count;    // Bus txns between LLC and DRAM controller (1 for fetch, 2 for dirty wb + fetch)
} vtable_t;

inline static uint64_t vtable_get_omc_evict_count(vtable_t *vtable) { return vtable->omc_evict_count; }
inline static uint64_t vtable_get_llc_evict_count(vtable_t *vtable) { return vtable->llc_evict_count; }

vtable_t *vtable_init(vtable_evict_cb_t evict_cb, vtable_core_recv_cb_t core_recv_cb, 
  vtable_core_tag_cb_t core_tag_cb, vtable_core_stable_epoch_cb_t core_stable_epoch_cb,
  vtable_l3_bus_txn_cb_t l3_bus_txn_cb);
void vtable_free(vtable_t *vtable);
// If this is not called, the nvoverlay in the call back arg will be NULL
void vtable_set_parent(vtable_t *vtable, struct nvoverlay_struct_t *overlay);
ver_t *vtable_insert(vtable_t *vtable, uint64_t addr);
// Returns NULL if addr does not exist
inline static ver_t *vtable_find(vtable_t *vtable, uint64_t addr) {
  return (ver_t *)ht64_find(vtable->vers, addr, 0UL);
}

// We call this eviction wrapper for statistics
inline static void vtable_evict_wrapper(vtable_t *vtable, uint64_t addr, int id, uint64_t version, uint64_t cycle, int type) {
  if(type & EVICT_LLC) vtable->llc_evict_count++;
  if(type & EVICT_OMC) vtable->omc_evict_count++;
  vtable->evict_cb(vtable->nvoverlay, addr, id, version, cycle, type);
  return;
}

inline static ht64_t *vtable_get_vers(vtable_t *vtable) { return vtable->vers; }

void ver_sharer_print(bitmap64_t *bitmap);
void ver_print(ver_t *ver);

inline static int vtable_l1_has_ver(ver_t *ver, int id) { return bitmap64_is_set(&ver->l1_bitmap, id); }
inline static int vtable_l2_has_ver(ver_t *ver, int id) { return bitmap64_is_set(&ver->l2_bitmap, id); }

inline static void vtable_l1_add_ver(vtable_t *vtable, ver_t *ver, int id) { 
  vtable->core_tag_cb(vtable->nvoverlay, CPU_TAG_OP_ADD, CPU_TAG_L1, id, ver);
  bitmap64_add(&ver->l1_bitmap, id); 
}
inline static void vtable_l2_add_ver(vtable_t *vtable, ver_t *ver, int id) { 
  vtable->core_tag_cb(vtable->nvoverlay, CPU_TAG_OP_ADD, CPU_TAG_L2, id, ver);
  bitmap64_add(&ver->l2_bitmap, id); 
}

inline static void vtable_l1_rm_ver(vtable_t *vtable, ver_t *ver, int id) { 
  vtable->core_tag_cb(vtable->nvoverlay, CPU_TAG_OP_REMOVE, CPU_TAG_L1, id, ver);
  bitmap64_remove(&ver->l1_bitmap, id); 
}
inline static void vtable_l2_rm_ver(vtable_t *vtable, ver_t *ver, int id) { 
  vtable->core_tag_cb(vtable->nvoverlay, CPU_TAG_OP_REMOVE, CPU_TAG_L2, id, ver);
  bitmap64_remove(&ver->l2_bitmap, id); 
}

inline static void vtable_l1_set_ver(vtable_t *vtable, ver_t *ver, int id) { 
  vtable->core_tag_cb(vtable->nvoverlay, CPU_TAG_OP_SET, CPU_TAG_L1, id, ver);
  bitmap64_set(&ver->l1_bitmap, id); 
}
inline static void vtable_l2_set_ver(vtable_t *vtable, ver_t *ver, int id) { 
  vtable->core_tag_cb(vtable->nvoverlay, CPU_TAG_OP_SET, CPU_TAG_L2, id, ver);
  bitmap64_set(&ver->l2_bitmap, id); 
}

inline static void vtable_l1_clear_sharer(vtable_t *vtable, ver_t *ver, int id) { 
  vtable->core_tag_cb(vtable->nvoverlay, CPU_TAG_OP_CLEAR, CPU_TAG_L1, id, ver);
  bitmap64_clear(&ver->l1_bitmap); 
}
inline static void vtable_l2_clear_sharer(vtable_t *vtable, ver_t *ver, int id) { 
  vtable->core_tag_cb(vtable->nvoverlay, CPU_TAG_OP_CLEAR, CPU_TAG_L2, id, ver);
  bitmap64_clear(&ver->l2_bitmap); 
}

inline static int vtable_l1_num_sharer(ver_t *ver) { return bitmap64_popcount(&ver->l1_bitmap); }
inline static int vtable_l2_num_sharer(ver_t *ver) { return bitmap64_popcount(&ver->l2_bitmap); }

// Returns one of the sharer's identity, if there are many which one is returned is undefined
// Assume the bitmap is not empty
inline static int vtable_l1_sharer(ver_t *ver) { return bitmap64_pos(&ver->l1_bitmap); }
inline static int vtable_l2_sharer(ver_t *ver) { return bitmap64_pos(&ver->l2_bitmap); }

void vtable_l1_load(vtable_t *vtable, uint64_t addr, int id, uint64_t epoch, uint64_t cycle);
void vtable_l1_store(vtable_t *vtable, uint64_t addr, int id, uint64_t epoch, uint64_t cycle);

void vtable_l1_eviction(vtable_t *vtable, uint64_t addr, int id, uint64_t epoch, uint64_t cycle);
// It takes an extra arg since L3 evict also calls this function
void _vtable_l2_eviction(vtable_t *vtable, uint64_t addr, int id, uint64_t epoch, uint64_t cycle, int from_l3);
inline static void vtable_l2_eviction(vtable_t *vtable, uint64_t addr, int id, uint64_t epoch, uint64_t cycle) {
  _vtable_l2_eviction(vtable, addr, id, epoch, cycle, 0); // From L3 is set to zero
}
void vtable_l3_eviction(vtable_t *vtable, uint64_t addr, int id, uint64_t epoch, uint64_t cycle);

// Called by nvoverlay after init
void vtable_conf_print(vtable_t *vtable);
void vtable_stat_print(vtable_t *vtable);

//* omcbuf_t - Essentially a persistent cache struct on OMC for absorbing writes to the NVM

// Call back function type; Args are: address (cache line aligned), epoch and cycle
typedef void (*omcbuf_evict_cb_t)(struct nvoverlay_struct_t *, uint64_t addr, uint64_t epoch, uint64_t cycle);

typedef struct {
  uint64_t tag;     // Both cache line offset and index bits have been shifted out
  uint64_t epoch;   // -1 means invalid entry
  uint64_t lru;
} omcbuf_entry_t;

typedef struct {
  uint64_t lru_counter;
  int sets;
  int ways;
  int set_idx_bits;
  uint64_t set_mask; // Already rshifted-out cache line offsets
  omcbuf_entry_t *array;
  omcbuf_evict_cb_t evict_cb;         // Eviction function call back
  struct nvoverlay_struct_t *nvoverlay; // NULL for testing
  // Statistics
  uint64_t item_count;      // Current number of valid items
  uint64_t access_count;    // Total number of writes
  uint64_t hit_count;       // Hits 
  uint64_t miss_count;      // Misses
  uint64_t evict_count;     // All Evictions
  uint64_t tag_walk_evict_count; // Only evictions for tag walk
  uint64_t last_walk_epoch; // Epoch of last tag walk - after this epoch there should be no insert with lower epoch
} omcbuf_t;

omcbuf_t *omcbuf_init(int sets, int ways, omcbuf_evict_cb_t evict_cb);
void omcbuf_free(omcbuf_t *omcbuf);
void omcbuf_set_parent(omcbuf_t *omcbuf, struct nvoverlay_struct_t *nvoverlay); 

void omcbuf_insert(omcbuf_t *omcbuf, uint64_t addr, uint64_t epoch, uint64_t cycle);

// Everything smaller than the target will be evicted (they will not be written anyway)
void omcbuf_tag_walk(omcbuf_t *omcbuf, uint64_t target_epoch, uint64_t cycle);

inline static uint64_t omcbuf_addr_gen(omcbuf_t *omcbuf, uint64_t tag, int set) {
  if(set < 0 || set >= omcbuf->sets) {
    error_exit("Illegal set: %d (should be [0, %d))\n", set, omcbuf->sets);
  }
  return (tag << (omcbuf->set_idx_bits + UTIL_CACHE_LINE_BITS)) | 
         (((uint64_t)set & omcbuf->set_mask) << UTIL_CACHE_LINE_BITS);
}

inline static uint64_t omcbuf_get_evict_count(omcbuf_t *omcbuf) { return omcbuf->evict_count; }
inline static uint64_t omcbuf_get_tag_walk_evict_count(omcbuf_t *omcbuf) { return omcbuf->tag_walk_evict_count; }
inline static uint64_t omcbuf_get_item_count(omcbuf_t *omcbuf) { return omcbuf->item_count; }
inline static uint64_t omcbuf_get_access_count(omcbuf_t *omcbuf) { return omcbuf->access_count; }

// Called by nvoverlay after init
void omcbuf_conf_print(omcbuf_t *omcbuf);
void omcbuf_stat_print(omcbuf_t *omcbuf);

//* overlay_t

typedef struct {
  bitmap64_t bitmap;  // At most 64 cache lines in a page - will not be modified after 
  int ref_count;      // Incremented on insertion; Decremented on deref
} overlay_page_t;

typedef struct overlay_epoch_struct_t {
  uint64_t epoch;
  mtable_t *mtable;    // The mapping table for the current epoch; Entries are overlay_page_t objects
  // The following two will be adjusted during GC
  uint64_t size;               // Size of all pages in the overlay (byte size)
  uint64_t overlay_page_count; // Number of pages in this epoch's overlay; Incremented when a new page is created
  int merged;          // Whether the epoch has been merged into master. If true there cannot be more inserts, and only deref
} overlay_epoch_t;

typedef struct overlay_struct_t {
  ht64_t *epochs;              // We use hash table to map epoch number to the overlay_t object
  // Statistics
  uint64_t size;               // Sum of all nodes' size variables in the linked list
  uint64_t min_size;           // This is size of overlay with page shrinking
  uint64_t epoch_count;        // Number of epochs in the mapping table
  uint64_t epoch_init_count;   // Total number of insertions
  uint64_t epoch_gc_count;     // Total number of GC'ed epochs
  uint64_t merge_count;        // Number of merges into this OMT
  uint64_t line_gc_count;      // Number of lines that are GC'ed
  uint64_t page_gc_count;      // Number of pages that are GC'ed
  uint64_t page_gc_size;       // Sum of GC'ed page sizes (we use non-shrinking page size, i.e. bitmap not ref count)
} overlay_t;

overlay_page_t *overlay_page_init();
void overlay_page_free(void *overlay_page); // Also pass this as call back

overlay_epoch_t *overlay_epoch_init();
void overlay_epoch_free(void *overlay_epoch);

// Returns the size class of the page given cache line count in the page
uint64_t overlay_page_size_class(int line_count);
void overlay_page_print(overlay_t *overlay, uint64_t epoch, uint64_t page_addr);

uint64_t overlay_epoch_insert(overlay_epoch_t *overlay_epoch, uint64_t addr);
// Addr here should be page aligned
overlay_page_t *overlay_epoch_find(overlay_epoch_t *overlay_epoch, uint64_t addr);

inline static void overlay_epoch_traverse(overlay_epoch_t *overlay_epoch, mtable_traverse_cb_t cb, void *arg) {
  mtable_traverse(overlay_epoch->mtable, cb, arg);
}

uint64_t overlay_epoch_line_count(overlay_epoch_t *overlay_epoch); // Count number of lines in the node
uint64_t overlay_epoch_size(overlay_epoch_t *epoch); // Compute overlay storage using bitmap (not ref count)
uint64_t overlay_epoch_min_size(overlay_epoch_t *epoch); // Compute minimum storage to store the epoch
uint64_t overlay_mapping_size(overlay_t *overlay); // Compute sum of mapping table sizes on existing epochs
uint64_t overlay_line_count(overlay_t *overlay); // Compute cache line counts in all epochs using bitmaps

void overlay_epoch_print(overlay_epoch_t *overlay_epoch);

inline static uint64_t overlay_epoch_get_overlay_page_count(overlay_epoch_t *overlay_epoch) {
  return overlay_epoch->overlay_page_count;
}
inline static uint64_t overlay_epoch_get_size(overlay_epoch_t *overlay_epoch) {
  return overlay_epoch->size;
}

overlay_t *overlay_init();
void overlay_free(overlay_t *overlay);

void overlay_insert(overlay_t *overlay, uint64_t addr, uint64_t epoch);
void overlay_remove(overlay_t *overlay, uint64_t epoch);
// Find the overlay node given overlay and epoch number; Returns NULL if does not exist
inline static overlay_epoch_t *overlay_find(overlay_t *overlay, uint64_t epoch) {
  overlay_epoch_t *overlay_epoch = (overlay_epoch_t *)ht64_find(overlay->epochs, epoch, 0UL);
  return overlay_epoch;
}
overlay_page_t *overlay_find_page(overlay_t *overlay, uint64_t epoch, uint64_t page_addr);

inline static uint64_t overlay_get_size(overlay_t *overlay) { return overlay->size; }
inline static uint64_t overlay_get_epoch_count(overlay_t *overlay) { return overlay->epoch_count; }

// Argument type for the call back function
typedef struct {
  struct overlay_struct_t *overlay;
  struct overlay_epoch_struct_t *overlay_epoch;
  omt_t *omt;
} overlay_merge_cb_arg_t;

// Following functions are used for overlay merge
void overlay_epoch_merge(overlay_t *overlay, uint64_t epoch, omt_t *omt); // Merge the given epoch to OMT
void overlay_line_unlink(overlay_t *overlay, overlay_epoch_t *overlay_epoch, uint64_t page_addr);
void overlay_gc_epoch(overlay_t *overlay, overlay_epoch_t *overlay_epoch);

void overlay_selfcheck(overlay_t *overlay);

// Called by nvoverlay after init
void overlay_conf_print(overlay_t *overlay);
void overlay_stat_print(overlay_t *overlay);

//* nvm_t

// Call back function that will be invoked every time an operation has completed
typedef void (*nvm_op_complete_cb_t)(struct nvoverlay_struct_t *nvoverlay, uint64_t cycle);

typedef struct {
  uint64_t *banks; // Stores the next available cycle of the bank
  int bank_count;  // Number of banks
  int bank_bit;    // Number of bits in the mask
  uint64_t mask;   // Address mask for exracting bank index
  uint64_t rlat;   // Read latency
  uint64_t wlat;   // Write latency
  nvm_op_complete_cb_t op_complete_cb;  // Call back function from init argument; If NULL then not called
  struct nvoverlay_struct_t *nvoverlay; // Used by the call back function
  // Statistics
  uint64_t write_count;             // Total writes
  uint64_t uncontended_write_count; // When write cycle > last avail cycle
  uint64_t read_count;
  uint64_t uncontended_read_count; 
} nvm_t;

nvm_t *nvm_init(int bank_count, uint64_t rlat, uint64_t wlat, nvm_op_complete_cb_t op_complete_cb);
void nvm_free(nvm_t *nvm);
// Must be called before invoking nvm_access() function
inline static void nvm_set_parent(nvm_t *nvm, struct nvoverlay_struct_t *nvoverlay) { nvm->nvoverlay = nvoverlay; }

// Unified function for reads and writes; The last arg is a pointer to the statistics variable
uint64_t nvm_access(nvm_t *nvm, uint64_t addr, uint64_t cycle, uint64_t lat, uint64_t *uncontended_counter); 

inline static uint64_t nvm_read(nvm_t *nvm, uint64_t addr, uint64_t cycle) {
  nvm->read_count++;
  return nvm_access(nvm, addr, cycle, nvm->rlat, &nvm->uncontended_read_count);
}

inline static uint64_t nvm_write(nvm_t *nvm, uint64_t addr, uint64_t cycle) {
  nvm->write_count++;
  return nvm_access(nvm, addr, cycle, nvm->wlat, &nvm->uncontended_write_count);
}

uint64_t nvm_sync(nvm_t *nvm); // Returns the maximum next available cycle across all banks
uint64_t nvm_min(nvm_t *nvm);  // Returns the minimum next available cycle across all banks - used for debugging

inline static int nvm_get_bank_count(nvm_t *nvm) { return nvm->bank_count; }
inline static uint64_t nvm_get_wlat(nvm_t *nvm) { return nvm->wlat; }
inline static uint64_t nvm_get_read_count(nvm_t *nvm) { return nvm->read_count; }
inline static uint64_t nvm_get_write_count(nvm_t *nvm) { return nvm->write_count; }

// Generate address that can be mapped to a certain bank of the NVM
uint64_t nvm_addr_gen(nvm_t *nvm, uint64_t tag, uint64_t bank);

// Called by nvoverlay after init
void nvm_conf_print(nvm_t *nvm);
void nvm_stat_print(nvm_t *nvm);

//* logdump_t - Collecting logs of events and dump to a file

#define LOGDUMP_BUF_COUNT 4096 // Number of entries in the in-memory buffer

typedef struct {
  uint64_t time;          // Can be serial or cycle or whatever you use for time
  uint64_t value;         // Whatever you want to be a value
} logdump_entry_t;

typedef struct {
  char *filename;             // Name of the backing file
  logdump_entry_t buffer[LOGDUMP_BUF_COUNT]; // Buffer before we flush to the file
  int index;                  // Current index
  FILE *fp;                   // File pointer
} logdump_t;

logdump_t *logdump_init(const char *filename);
void logdump_free(logdump_t *logdump);
void logdump_append(logdump_t *logdump, uint64_t time, uint64_t value);
void logdump_conf_print(logdump_t *logdump);
void logdump_stat_print(logdump_t *logdump);

//* picl_t

// Picl's log entry size: 64B data + 8B addr/status bits
#define PICL_LOG_ENTRY_SIZE (64UL + 8UL)

// Used as ht64 arguments
#define PICL_ADDR_PRESENT 0x1UL
#define PICL_ADDR_MISSING 0x0UL

// Call back type for picl eviction
typedef void (*picl_evict_cb_t)(nvoverlay_struct_t *nvoverlay, uint64_t line_addr, uint64_t cycle);

// This implements a simple write set tracking for PiCL
typedef struct {
  ht64_t *ht64;                   // This tracks current write set - only used as a set
  nvoverlay_struct_t *nvoverlay;  // Calls the call back function using this as argument
  picl_evict_cb_t evict_cb;       // Eviction call back for NVM write
  uint64_t log_ptr;               // Log write pointer; Reset for every epoch advance
  uint64_t epoch_size;            // Number of stores in the epoch
  logdump_t *nvm_logdump;         // Log object for NVM object; NULL if disabled
  // Component
  cpu_t *cpu;                     // We use this to store stat
  nvm_t *nvm;                     // NVM timing
  nvm_t *thynvm;                  // Second NVM timing (ThyNVM)
  nvm_t *l2_nvm;                  // Third NVM timing (L2 PiCL)
  ht64_t *l2_ht64;                // L2 working set hash table
  // Statistics
  uint64_t line_count;            // Number of lines currently in the current epoch's write set
  uint64_t max_line_count;        // Maximum number of lines in all epochs
  uint64_t epoch_count;           // Number of epochs it has seen (not including the current one)
  uint64_t epoch_store_count;     // Number of stores in the current epoch (cleared when advancing epoch)
  uint64_t total_store_count;     // Number of stores in all epochs (not cleared when advancing epoch)
  uint64_t log_write_count;       // Number of log entries (in fact log entries are larger than 64B)
  uint64_t epoch_log_write_count; // Log write count in the current epoch; Cleared in next epoch
  uint64_t tag_walk_evict_count;  // Number of evictions incurred by tag walk
  uint64_t l3_evict_count;        // Number of writes to NVM caused by LLC evict (only used in LLC mode)
  uint64_t l2_evict_count;        // Number of writes to NVM caused by L2 evict (only used in L2 mode)
  uint64_t l2_tag_walk_evict_count; // Number of writes by L2 tag walk at the end of the epoch
  uint64_t l2_log_write_count;    // Number of log writes for L2 PiCL
  fifo_t *log_sizes;              // List of log sizes in each epoch
  // ThyNVM stats
  uint64_t stall_cycles;          // Stall cycles at the end of each epoch (wait for prev epoch)
} picl_t; 

picl_t *picl_init(picl_evict_cb_t evict_cb);
void picl_free(picl_t *picl);
inline static void picl_set_parent(picl_t *picl, nvoverlay_struct_t *nvoverlay) {
  picl->nvoverlay = nvoverlay;
}

void picl_store(picl_t *picl, uint64_t line_addr, uint64_t cycle);
void picl_l2_eviction(picl_t *picl, uint64_t line_addr, uint64_t cycle);
void picl_l3_eviction(picl_t *picl, uint64_t line_addr, uint64_t cycle);
void picl_advance_epoch(picl_t *picl, uint64_t cycle); // Epochs are global in PiCL

inline static uint64_t picl_get_epoch_count(picl_t *picl) { return picl->epoch_count; }
inline static uint64_t picl_get_line_count(picl_t *picl) { return picl->line_count; }

inline static void picl_set_epoch_size(picl_t *picl, uint64_t epoch_size) { picl->epoch_size = epoch_size; }
inline static uint64_t picl_get_epoch_size(picl_t *picl) { return picl->epoch_size; }
inline static uint64_t picl_get_epoch_store_count(picl_t *picl) { return picl->epoch_store_count; }
inline static uint64_t picl_get_total_store_count(picl_t *picl) { return picl->total_store_count; }

// Log + tag walk + LLC evict
inline static uint64_t picl_get_total_evict_count(picl_t *picl) {
  return picl->log_write_count + picl->tag_walk_evict_count + picl->l3_evict_count;
}

void picl_selfcheck(picl_t *picl);

void picl_conf_print(picl_t *picl);
void picl_stat_print(picl_t *picl, int verbose);

//* nvoverlay_t

#define NVOVERLAY_MODE_NONE   0
#define NVOVERLAY_MODE_FULL   1
#define NVOVERLAY_MODE_PICL   2
#define NVOVERLAY_MODE_TRACER 3
#define NVOVERLAY_MODE_ALL    4 // All models we have - we can run them at the same time


extern const char *nvoverlay_mode_names[5];

// Call back type for instrumenting trace driven engine
typedef void (*nvoverlay_trace_driven_cb_t)(struct nvoverlay_struct_t *, tracer_record_t *);

// Call back types for external interface
typedef void (*nvoverlay_intf_cb_t)(struct nvoverlay_struct_t *nvoverlay, int id, uint64_t line_addr, 
                                    uint64_t cycle, uint64_t serial);

// The following are used to call other call backs
#define NVOVERLAY_OTHER_HELLO  0 // Prints hello world, used to verify that it works
#define NVOVERLAY_OTHER_INST   1 // Report number of instructions executed on a core (spec. by ID); Inst passed by cycle
#define NVOVERLAY_OTHER_CONF   2 // Print conf
#define NVOVERLAY_OTHER_STAT   3 // Print stat
#define NVOVERLAY_OTHER_CYCLE  4 // Report cycles on a core

// NVOverlay inteface call back functions
typedef struct {
  nvoverlay_intf_cb_t load_cb;
  nvoverlay_intf_cb_t store_cb;
  nvoverlay_intf_cb_t l1_evict_cb;
  nvoverlay_intf_cb_t l2_evict_cb;
  nvoverlay_intf_cb_t l3_evict_cb;
  nvoverlay_intf_cb_t other_cb;    // Generic interface
  uint64_t other_arg;              // Generic argument
} nvoverlay_intf_t;

typedef struct nvoverlay_struct_t {
  // NVOverlay member variable
  int mode;
  int trace_driven_enabled;     // Whether TD is enabled
  uint64_t epoch_size;          // Epoch size for nvoverlay full (picl epoch size is picl.epoch_size)
  uint64_t tag_walk_freq;       // Tag walk frequency, in # of epochs advanced since last walk
  uint64_t *stable_epochs;      // Last epoch when a tag walk is done - compute current stable epoch
  uint64_t last_stable_epoch;   // Last stable epoch - updated when we compute current stable epoch
  nvoverlay_trace_driven_cb_t trace_driven_cb; // Trace driven call back  - called for every record. ignored if NULL
  bitmap64_t core_mask;         // Only cores in this mask will be used to compute stable epoch
  int tag_walk_passive;         // Whether we perform passive tag walk (default set to zero)
  uint64_t sample_freq;         // Minimum number of cycles between two samples
  uint64_t last_sample_serial;  // Last serial number that we sampled stat
  int stat_verbose;             // Whether print all core information when stat print has the option
  fifo_t *samples;              // Samples taken periodically after some number of serials
  char *sample_filename;        // Prefix of dump file name for writing out samples; Optional if sampling is disabled
  uint64_t inst_cap;            // Number of inst we simulate for each core; All cores must be no less than this
  uint64_t cycle_cap;           // Number of cycles we simulate for each core; All cores must be no less than this
  int has_cap;                  // Set to 1 if there is a cap; 0 if not. If zero should not call cap related function
  int cap_core_count;           // Number of cores that are below the cap - init'ed to cpu.cores. -1 means already finished
  time_t begin_time;            // Start time of simulation, set in init function
  logdump_t *dram_logdump;      // Baseline DRAM access log, populated by vtable call back function
  logdump_t *nvm_logdump;       // NVM access log, populated by NVM call back function
  // Components
  conf_t *conf;              // Configuration file
  vtable_t *vtable;          // Tracking versions
  cpu_t *cpu;                // Core info
  omcbuf_t *omcbuf;          // OMC buffer
  overlay_t *overlay;        // backend overlay store
  omt_t *omt;                // Master table (OMT)
  nvm_t *nvm;                // NVM banks
  // PiCL
  picl_t *picl;
  // Tracer
  tracer_t *tracer;
  // Statistics
  uint64_t tag_walk_count;   // Number of tag walks on all cores 
  uint64_t sample_count;     // Number of samples taken
} nvoverlay_t;

// This function inits the conf and nvoverlay object
nvoverlay_t *nvoverlay_init(const char *conf_file);
// This function frees the conf and nvoverlay object
void nvoverlay_free(nvoverlay_t *nvoverlay);

void nvoverlay_trace_driven_check_core_mask(nvoverlay_t *nvoverlay);
void nvoverlay_trace_driven_start(nvoverlay_t *nvoverlay);
void nvoverlay_trace_driven_init(nvoverlay_t *nvoverlay);
inline static void nvoverlay_set_trace_driven_cb(nvoverlay_t *nvoverlay, nvoverlay_trace_driven_cb_t cb) {
  nvoverlay->trace_driven_cb = cb;
  return;
}

// Following init/free functions do not need to free nvoverlay and conf
// They can also assume that the mode variable is already set
void nvoverlay_init_full(nvoverlay_t *nvoverlay);
void nvoverlay_free_full(nvoverlay_t *nvoverlay);

void nvoverlay_init_tracer(nvoverlay_t *nvoverlay);
void nvoverlay_free_tracer(nvoverlay_t *nvoverlay);

void nvoverlay_init_picl(nvoverlay_t *nvoverlay);
void nvoverlay_free_picl(nvoverlay_t *nvoverlay);

void nvoverlay_noop_noop(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);

void nvoverlay_full_load(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);
void nvoverlay_full_store(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);
void nvoverlay_full_l1_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);
void nvoverlay_full_l2_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);
void nvoverlay_full_l3_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);

void nvoverlay_tracer_load(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);
void nvoverlay_tracer_store(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);
void nvoverlay_tracer_l1_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);
void nvoverlay_tracer_l2_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);
void nvoverlay_tracer_l3_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);

void nvoverlay_picl_load(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);
void nvoverlay_picl_store(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);
void nvoverlay_picl_l1_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);
void nvoverlay_picl_l2_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);
void nvoverlay_picl_l3_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);

void nvoverlay_all_load(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);
void nvoverlay_all_store(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);
void nvoverlay_all_l1_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);
void nvoverlay_all_l2_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);
void nvoverlay_all_l3_evict(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);

void nvoverlay_other(nvoverlay_t *nvoverlay, int id, uint64_t line_addr, uint64_t cycle, uint64_t serial);
void nvoverlay_inst(nvoverlay_t *nvoverlay, int id, uint64_t inst_count);
void nvoverlay_cycle(nvoverlay_t *nvoverlay, int id, uint64_t cycle_count);

extern nvoverlay_intf_t nvoverlay_intf; // This is the one used by external applications

extern nvoverlay_intf_t nvoverlay_intf_noop;
extern nvoverlay_intf_t nvoverlay_intf_full;
extern nvoverlay_intf_t nvoverlay_intf_picl;
extern nvoverlay_intf_t nvoverlay_intf_all;
extern nvoverlay_intf_t nvoverlaay_intf_tracer; // Only log the trace into a file

inline static nvm_t *nvoverlay_get_nvm(nvoverlay_t *nvoverlay) { 
  assert(nvoverlay->mode == NVOVERLAY_MODE_FULL || nvoverlay->mode == NVOVERLAY_MODE_ALL); 
  return nvoverlay->nvm; 
}
inline static nvm_t *nvoverlay_get_picl_nvm(nvoverlay_t *nvoverlay) { 
  assert(nvoverlay->mode == NVOVERLAY_MODE_PICL || nvoverlay->mode == NVOVERLAY_MODE_ALL); 
  return nvoverlay->picl->nvm; 
}
inline static picl_t *nvoverlay_get_picl(nvoverlay_t *nvoverlay) { return nvoverlay->picl; }
inline static conf_t *nvoverlay_get_conf(nvoverlay_t *nvoverlay) { return nvoverlay->conf; }
inline static tracer_t *nvoverlay_get_tracer(nvoverlay_t *nvoverlay) { return nvoverlay->tracer; }
inline static vtable_t *nvoverlay_get_vtable(nvoverlay_t *nvoverlay) { return nvoverlay->vtable; }
inline static cpu_t *nvoverlay_get_cpu(nvoverlay_t *nvoverlay) { 
  assert(nvoverlay->mode == NVOVERLAY_MODE_FULL); return nvoverlay->cpu; 
}
inline static cpu_t *nvoverlay_get_picl_cpu(nvoverlay_t *nvoverlay) { 
  assert(nvoverlay->mode == NVOVERLAY_MODE_PICL); return nvoverlay->picl->cpu; 
}
inline static int nvoverlay_get_mode(nvoverlay_t *nvoverlay) { return nvoverlay->mode; }

inline static int nvoverlay_has_cap(nvoverlay_t *nvoverlay) { return nvoverlay->has_cap; }
inline static int nvoverlay_get_cap_core_count(nvoverlay_t *nvoverlay) { return nvoverlay->cap_core_count; }
inline static void nvoverlay_set_cap_core_count(nvoverlay_t *nvoverlay, int value) { 
  nvoverlay->cap_core_count = value; 
}
inline static int nvoverlay_get_inst_cap(nvoverlay_t *nvoverlay) { return nvoverlay->inst_cap; }
inline static int nvoverlay_get_cycle_cap(nvoverlay_t *nvoverlay) { return nvoverlay->cycle_cap; }
// Returns the valid cap or 0UL if no cap
inline static uint64_t nvoverlay_get_cap(nvoverlay_t *nvoverlay) {
  if(nvoverlay->inst_cap != 0UL) return nvoverlay->inst_cap;
  else if(nvoverlay->inst_cap != 0UL) return nvoverlay->cycle_cap;
  return 0UL;
}
// Returns either inst or cycle progress, return value might > 1.0
inline static double nvoverlay_get_core_progress(nvoverlay_t *nvoverlay, int id) {
  if(nvoverlay->inst_cap != 0UL) {
    return (double)(cpu_get_core(nvoverlay->cpu, id)->last_inst_count) / (double)nvoverlay->inst_cap;
  } else if(nvoverlay->cycle_cap != 0UL) {
    return (double)(cpu_get_core(nvoverlay->cpu, id)->last_cycle_count) / (double)nvoverlay->cycle_cap;
  }
  // It is possible that both are zero, and we return 0
  return 0.0f;
}

// Same as above, except that we compute overall progress
inline static double nvoverlay_get_total_progress(nvoverlay_t *nvoverlay) {
  int core_count = cpu_get_core_count(nvoverlay->cpu);
  if(nvoverlay->inst_cap != 0UL) {
    return (double)cpu_get_total_inst_count(nvoverlay->cpu) / ((double)nvoverlay->inst_cap * core_count);
  } else if(nvoverlay->cycle_cap != 0UL) {
    return (double)cpu_get_total_cycle_count(nvoverlay->cpu) / ((double)nvoverlay->cycle_cap * core_count);
  }
  return 0.0f;
}

inline static uint64_t nvoverlay_get_total_inst_count(nvoverlay_t *nvoverlay) {
  return cpu_get_total_inst_count(nvoverlay->cpu);
}
inline static uint64_t nvoverlay_get_total_cycle_count(nvoverlay_t *nvoverlay) {
  return cpu_get_total_cycle_count(nvoverlay->cpu);
}

void nvoverlay_core_mask_add(nvoverlay_t *nvoverlay, int pos);
void nvoverlay_core_mask_remove(nvoverlay_t *nvoverlay, int pos);
inline static void nvoverlay_core_mask_clear(nvoverlay_t *nvoverlay) { 
  bitmap64_clear(&nvoverlay->core_mask); 
}
void nvoverlay_core_mask_set(nvoverlay_t *nvoverlay, int pos);
inline static bitmap64_t *nvoverlay_get_core_mask(nvoverlay_t *nvoverlay) { return &nvoverlay->core_mask; }
inline static int nvoverlay_core_mask_is_set(nvoverlay_t *nvoverlay, int pos) {
  return bitmap64_is_set(&nvoverlay->core_mask, pos);
}
inline static int nvoverlay_core_mask_popcount(nvoverlay_t *nvoverlay) {
  return bitmap64_popcount(&nvoverlay->core_mask);
}

// These two only consider active cores marked by the core mask
// Note this is not stable epoch - stable epochs are not always current core epoch
uint64_t nvoverlay_get_min_epoch(nvoverlay_t *nvoverlay);
uint64_t nvoverlay_get_max_epoch(nvoverlay_t *nvoverlay);

void nvoverlay_selfcheck(nvoverlay_t *nvoverlay);
void nvoverlay_selfcheck_full(nvoverlay_t *nvoverlay);

void nvoverlay_conf_print_full(nvoverlay_t *nvoverlay);
void nvoverlay_conf_print_picl(nvoverlay_t *nvoverlay);
void nvoverlay_conf_print_tracer(nvoverlay_t *nvoverlay);

void nvoverlay_stat_print_full(nvoverlay_t *nvoverlay);
void nvoverlay_stat_print_picl(nvoverlay_t *nvoverlay);
void nvoverlay_stat_print_tracer(nvoverlay_t *nvoverlay);

void nvoverlay_conf_print(nvoverlay_t *nvoverlay);
void nvoverlay_stat_print(nvoverlay_t *nvoverlay);

#define SAMPLE_OVERLAY_SIZE 0 // Byte size of Overlay, all epochs
#define SAMPLE_OMT_SIZE     1 // Byte size of OMT
#define SAMPLE_EPOCH_SKEW   2 // Difference between largest and smallest epochs across all marked cores
#define SAMPLE_COUNT        3 // Number of vars per sample point

extern const char *overlay_sample_names[SAMPLE_COUNT];

typedef struct {
  uint64_t serial;
  uint64_t data[SAMPLE_COUNT];
} nvoverlay_sample_t;

// This function will populate the sample_t object
nvoverlay_sample_t *nvoverlay_sample_init(nvoverlay_t *nvoverlay);
void nvoverlay_sample_free(nvoverlay_sample_t *sample);
inline static uint64_t nvoverlay_sample_item_count(nvoverlay_t *nvoverlay) { 
  return fifo_get_item_count(nvoverlay->samples); 
}
inline static int nvoverlay_sample_is_enabled(nvoverlay_t *nvoverlay) {
  return nvoverlay->sample_freq != 0UL;
}

// This function is called for every operation to check whether we should sample
void nvoverlay_stat_sample(nvoverlay_t *nvoverlay, uint64_t serial);
char *nvoverlay_stat_dump_filename(nvoverlay_t *nvoverlay, int index);
void nvoverlay_stat_dump(nvoverlay_t *nvoverlay);

//* sim_t

// sim->mode values
#define SIM_MODE_BEGIN          0
#define SIM_MODE_NONE           0
#define SIM_MODE_TRACER         1
#define SIM_MODE_NVOVERLAY      2
#define SIM_MODE_PICL           3
#define SIM_MODE_ALL            4
#define SIM_MODE_END            5

// Printed names
extern const char *sim_mode_names[SIM_MODE_END];

typedef struct {
  // Global simulation variables
  int mode;
  int trace_driven_enabled;     // Whether TD is enabled
  nvoverlay_trace_driven_cb_t trace_driven_cb; // Trace driven call back  - called for every record. ignored if NULL
  bitmap64_t core_mask;         // Only cores in this mask will be used to compute stable epoch
  int stat_verbose;             // Whether print all core information when stat print has the option
  uint64_t inst_cap;            // Number of inst we simulate for each core; All cores must be no less than this
  uint64_t cycle_cap;           // Number of cycles we simulate for each core; All cores must be no less than this
  int has_cap;                  // Set to 1 if there is a cap; 0 if not. If zero should not call cap related function
  int cap_core_count;           // Number of cores that are below the cap - init'ed to cpu.cores. -1 means already finished
  time_t begin_time;            // Start time of simulation, set in c'tor
  // Components
  char *conf_filename;          // File name of configuration
  conf_t *conf;
} sim_t;

sim_t *sim_init(const char *filename);
void sim_free(sim_t *sim);

const char *sim_get_mode_name(sim_t *sim);

//* zsim

// They are called in zsim source files, so they begin with "nvoverlay"
#define nvoverlay_printf(fmt, ...) do { fprintf(stdout, "[NVOverlay] " fmt, ##__VA_ARGS__); } while(0);
#define nvoverlay_error(fmt, ...) do { \
  fprintf(stderr, "[NVOverlay] %s error: " fmt, __func__, ##__VA_ARGS__); exit(1); \
} while(0);

void nvoverlay_hello_world(); // Called in zsim's init
void nvoverlay_check_conf(nvoverlay_t *nvoverlay);

#endif