
#ifndef NVOVERLAY_UTIL_H
#define NVOVERLAY_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <error.h>
#include <unistd.h>
// If zsim macro is defined, we use zsim's own impl of assert()
#ifndef ZSIM_PATH
#include <assert.h>
#else
#include "log.h"
#endif

// Error reporting and system call assertion
#define SYSEXPECT(expr) do { if(!(expr)) { perror(__func__); exit(1); } } while(0)
#define error_exit(fmt, ...) do { fprintf(stderr, "%s error: " fmt, __func__, ##__VA_ARGS__); exit(1); } while(0);
#ifndef NDEBUG
#define dbg_printf(fmt, ...) do { fprintf(stderr, fmt, ##__VA_ARGS__); } while(0);
#else
#define dbg_printf(fmt, ...) do {} while(0);
#endif

// Branching macro (this may have already been defined in other source files)
#ifndef likely
#define likely(x)       __builtin_expect((x),1)
#endif
#ifndef unlikely
#define unlikely(x)     __builtin_expect((x),0)
#endif

// Global def of cache line size
#define UTIL_CACHE_LINE_SIZE 64UL
#define UTIL_PAGE_SIZE       4096UL
// Global def of cache line bits
#define UTIL_CACHE_LINE_BITS 6
#define UTIL_PAGE_BITS       12
// Masks
#define UTIL_CACHE_LINE_LSB_MASK (UTIL_CACHE_LINE_SIZE - 1)
#define UTIL_CACHE_LINE_MSB_MASK (~(UTIL_CACHE_LINE_LSB_MASK))
#define UTIL_PAGE_LSB_MASK   (UTIL_PAGE_SIZE - 1)
#define UTIL_PAGE_MSB_MASK   (~(UTIL_PAGE_LSB_MASK))

// Reports error if the addr is not cache line aligned
#define ASSERT_CACHE_ALIGNED(addr) do { if(addr & UTIL_CACHE_LINE_LSB_MASK) {\
    error_exit("Address must be cache line aligned (see 0x%lX)\n", addr); } \
  } while(0);

#define ASSERT_PAGE_ALIGNED(addr) do { if(addr & UTIL_PAGE_LSB_MASK) {\
  error_exit("Address must be page aligned (see 0x%lX)\n", addr); } \
} while(0);

int util_log2_int32(int num, const char *name);
int util_log2_uint64(uint64_t num, const char *name);

inline static int popcount_int32(int bitmap) { return __builtin_popcount(bitmap); }
inline static int popcount_uint64(uint64_t bitmap) { return __builtin_popcountl(bitmap); }

inline static int ffs_int32(int num) { return __builtin_ffs(num); }
inline static int ffs_uint64(uint64_t num) { return __builtin_ffsl(num); }

// Align the address to the nearest page boundary below it
inline static void *page_align_down(void *addr) { return (void *)((uint64_t)addr & ~(UTIL_PAGE_SIZE - 1)); }
inline static void *page_align_up(void *addr) { return (void *)(((uint64_t)addr + UTIL_PAGE_SIZE - 1) & ~(UTIL_PAGE_SIZE - 1)); }
// Given an address range, return the number of page frames the range crosses
inline static int num_aligned_page(void *begin, uint64_t size) {
  return ((uint8_t *)page_align_up((uint8_t *)begin + size) - (uint8_t *)page_align_down(begin)) / UTIL_PAGE_SIZE;
}
inline static int page_line_offset(uint64_t addr) { 
  return (int)((addr & UTIL_PAGE_LSB_MASK) >> (UTIL_PAGE_BITS - UTIL_CACHE_LINE_BITS));
}
// This is only used for debugging, thus not inlined
uint64_t addr_gen(uint64_t page, uint64_t cache, uint64_t offset);

// [low, high]
void assert_int32_range(int num, int low, int high, const char *name);
void assert_int32_power2(int num, const char *name);
void assert_uint64_range(uint64_t num, uint64_t low, uint64_t high, const char *name);
void assert_uint64_power2(uint64_t num, const char *name);

inline static int streq(const char *a, const char *b) { return strcmp(a, b) == 0; }

//* conf_t

// Used in "options" of find_* functions
#define CONF_NONE   0x00000000
// Whether the number should be power of two
#define CONF_POWER2 0x00000001
// Whether the number should be in a range (given by params)
#define CONF_RANGE  0x00000002
// Whether we support abbr. such as K/k for kilo and M/m for million
#define CONF_ABBR   0x00000004
// Whether we support size suffix such as B/KB/MB/GB. This should not be mixed with CONF_ABBR
#define CONF_SIZE   0x00000008

// Integer ranges
#define CONF_INT32_MAX  0x7FFFFFFF
#define CONF_INT32_MIN  0x80000000
#define CONF_UINT64_MAX (-1UL)
#define CONF_UINT64_MIN (0UL)

typedef struct conf_node_struct_t {
  char *key;
  char *value;
  int line;
  int accessed;   // Whether this conf node has been accessed by a find_* function; Use this to find unused option
  struct conf_node_struct_t *next;
} conf_node_t;

// Configuration files are flat structured "key = value" pairs; Lines beginning 
// with "#" are comments
typedef struct conf_struct_t {
  int item_count;          // Number of key-value pairs
  conf_node_t *head;       // Linked list of configurations
  char *filename;          // File name of the conf file
} conf_t;

void conf_node_free(conf_node_t *node);

void conf_insert(conf_t *conf, const char *k, const char *v, int klen, int vlen, int line);
void conf_insert_ext(conf_t *conf, const char *k, const char *v); // Insert external files, line is set to -1
conf_t *conf_init(const char *filename);                    // Open a file and load contents into the conf buffer
void conf_free(conf_t *conf);
int conf_remove(conf_t *conf, const char *key); // Returns 1 if the entry exists
int conf_rewrite(conf_t *conf, const char *key, const char *value); // Returns if the entry exists
conf_node_t *conf_find(conf_t *conf, const char *key);        // Returns node pointer
int conf_find_str(conf_t *conf, const char *key, char **ret); // Returns 1 if found; 0 if not
int conf_find_int32(conf_t *conf, const char *key, int *ret);       // Returns 1 if converts successfully
int conf_find_uint64(conf_t *conf, const char *key, uint64_t *ret); // Returns 1 if converts successfully
int conf_find_bool(conf_t *conf, const char *key, int *ret); // Returns 1 if converts successfully
// Allow size suffix
int conf_find_uint64_size(conf_t *conf, const char *key, uint64_t *ret);
// Allow abbreviation. K for thousand, M for million.
int conf_find_uint64_abbr(conf_t *conf, const char *key, uint64_t *ret); 

char *conf_find_str_mandatory(conf_t *conf, const char *key); // The string key must exist
int conf_find_bool_mandatory(conf_t *conf, const char *key);  // The boolean key must exist
int conf_find_int32_range(conf_t *conf, const char *key, int low, int high, int options); // Returns the value
uint64_t conf_find_uint64_range(conf_t *conf, const char *key, uint64_t low, uint64_t high, int options); // Returns the value
uint64_t conf_find_uint64_abbr_range(conf_t *conf, const char *key, uint64_t low, uint64_t high, int options); // Returns the value
// The following two are just short hands
inline static int conf_find_int32_mandatory(conf_t *conf, const char *key) { 
  return conf_find_int32_range(conf, key, 0, 0, CONF_NONE);
}
inline static int conf_find_uint64_mandatory(conf_t *conf, const char *key) { 
  return conf_find_uint64_range(conf, key, 0UL, 0UL, CONF_NONE);
}

void conf_print(conf_t *conf);
int conf_selfcheck(conf_t *conf);
void conf_print_unused(conf_t *conf);

inline static uint64_t conf_get_item_count(conf_t *conf) { return conf->item_count; }

// Called by nvoverlay after init
void conf_conf_print(conf_t *conf);
void conf_dump(conf_t *conf, const char *filename);

// Conf iter interface
typedef conf_node_t *conf_it_t;
inline static void conf_begin(conf_t *conf, conf_it_t *it)  { *it = conf->head; }
inline static int  conf_is_end(conf_t *conf, conf_it_t *it) { return *it == NULL; (void)conf; }
inline static void conf_next(conf_t *conf, conf_it_t *it)   { *it = (*it)->next; (void)conf; }
inline static const char *conf_it_key(conf_it_t *it) { return (*it)->key; }
inline static const char *conf_it_value(conf_it_t *it) { return (*it)->value; }

//* tracer_t

// Number of records in the buffer before we flush it
#ifdef UTIL_TEST
// For better testing
#define TRACER_BUFFER_SIZE   2
#else 
#define TRACER_BUFFER_SIZE   4096
#endif
#define TRACER_MAX_CORE      64

#define TRACER_MODE_BEGIN    0
#define TRACER_MODE_WRITE    0
#define TRACER_MODE_READ     1
#define TRACER_MODE_END      2

#define TRACER_CAP_MODE_BEGIN 0
#define TRACER_CAP_MODE_NONE  0
#define TRACER_CAP_MODE_INST  1
#define TRACER_CAP_MODE_LOAD  2
#define TRACER_CAP_MODE_STORE 3
#define TRACER_CAP_MODE_MEMOP 4
#define TRACER_CAP_MODE_END   5

#define TRACE_TYPE_BEGIN      0
#define TRACER_LOAD           0
#define TRACER_STORE          1
#define TRACER_L1_EVICT       2
#define TRACER_L2_EVICT       3
#define TRACER_L3_EVICT       4
#define TRACER_TYPE_END       5

#define TRACER_KEEP_FILE      0
#define TRACER_REMOVE_FILE    1

#define TRACER_CORE_ACTIVE    0
#define TRACER_CORE_HALTED    1

extern const char *tracer_mode_names[2];
extern const char *tracer_cap_mode_names[5];
extern const char *tracer_record_type_names[5];
extern const char *tracer_cleanup_names[2];
extern const char *tracer_core_status_names[2];

typedef struct {
  int8_t type;          // Load/store
  uint8_t id;           // Core ID
  uint64_t line_addr;   // Address of the memop
  uint64_t cycle;       // Cycle of the op
} tracer_record_t;

typedef struct {
  char *filename;
  FILE *fp;
  tracer_record_t buffer[TRACER_BUFFER_SIZE];
  int write_index; // Next write item
  int read_index;  // Next read item
  int max_index;   // Maximum valid index - only used for read; If EOF we set index and max_index to 0
  int id;          // Core ID
  int status;      // Set to 0 if it stops accepting traces
  // Statistics
  uint64_t filesize;
  uint64_t record_count; // Set for both reads and writes
  uint64_t load_count;
  uint64_t store_count;
  uint64_t inst_count;   // This is only sync'ed on loads or stores (core must support)
  uint64_t memop_count;  // Equals load + store
  uint64_t read_count;   // Only used for read - number of records read from the file
  uint64_t fread_count;  // Library call counts
  uint64_t fwrite_count;
} tracer_core_t;

typedef struct {
  int core_count;             // Number of trace buffers
  tracer_core_t **cores;      // One buffer per core
  char *basename;             // Base file name without serial number
  int mode;                   // TRACER_MODE_WRITE or READ; tracer.mode = "read" | "write"
  int cap_mode;               // tracer.cap_mode = "none" | "inst" | "load" | "store" | "memop"
  uint64_t cap;               // Maximum number of records (per core) to be logged; 0 means not capped; tracer.cap = ###
  int cleanup;                // Whether to cleanup the files on exit
  int active_core_count;      // Number of cores that still accept traces
} tracer_t;

void tracer_record_print(tracer_record_t *rec);
void tracer_record_print_buf(tracer_record_t *rec, char *buffer, int size);

tracer_core_t *tracer_core_init(const char *basename, int index, int mode);
void tracer_core_free(tracer_core_t *tracer_core, int do_flush);

// Flush all memory entries to the disk
void tracer_core_flush(tracer_core_t *tracer_core);
int tracer_core_fill(tracer_core_t *core);
void tracer_core_seek(tracer_core_t *core);
tracer_record_t *tracer_core_get(tracer_core_t *core);
void tracer_core_remove_file(tracer_core_t *tracer_core);

void tracer_core_begin(tracer_core_t *core);
inline static int tracer_core_is_end(tracer_core_t *core) {
  return core->read_count == core->record_count && core->read_index == core->max_index;
}
tracer_record_t *_tracer_core_next(tracer_core_t *core, int inc_index);
// Take a look at the current record
inline static tracer_record_t *tracer_core_peek(tracer_core_t *core) { return _tracer_core_next(core, 0); }
inline static tracer_record_t *tracer_core_next(tracer_core_t *core) { return _tracer_core_next(core, 1); }

tracer_t *tracer_init(const char *basename, int core_count, int mode);
void tracer_free(tracer_t *tracer);
void tracer_set_cap_mode(tracer_t *trace, int cap_mode, uint64_t cap);
void tracer_set_cleanup(tracer_t *tracer, int value);

void tracer_halt_core(tracer_t *tracer, int id);
void tracer_insert(tracer_t *tracer, int type, int id, uint64_t line_addr, uint64_t cycle);
// Print [begin, begin + count) records
void tracer_print(tracer_t *tracer, int id, uint64_t begin, uint64_t count); 

void tracer_begin(tracer_t *tracer);
tracer_record_t *tracer_next(tracer_t *tracer);

inline static int tracer_get_core_count(tracer_t *tracer) { return tracer->core_count; }
uint64_t tracer_get_record_count(tracer_t *tracer);
// Per-core record count
uint64_t tracer_get_core_record_count(tracer_t *tracer, int id);

void tracer_conf_print(tracer_t *tracer);
void tracer_stat_print(tracer_t *tracer);

#endif