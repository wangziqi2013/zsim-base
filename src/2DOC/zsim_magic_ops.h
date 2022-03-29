
// This file contains magic ops that can be called by the compiled binary to pass
// information to the simulator.

#ifndef _ZSIM_MAGIC_OPS_H
#define _ZSIM_MAGIC_OPS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  ZSIM_MAGIC_OP_HELLO_WORLD,
  ZSIM_MAGIC_OP_PRINT_STR,
  ZSIM_MAGIC_OP_START_SIM,
  ZSIM_MAGIC_OP_GET_ADDR_MAPPING,
  ZSIM_MAGIC_OP_ADDR_MAP_ALLOC,
  // Random number related
  ZSIM_MAGIC_OP_GET_RAND32,
  ZSIM_MAGIC_OP_GET_RAND64,
  // Memory allocation related instrumentation
  ZSIM_MAGIC_OP_MALLOC,
  ZSIM_MAGIC_OP_CALLOC,
  ZSIM_MAGIC_OP_REALLOC,
  ZSIM_MAGIC_OP_FREE,
  // Progress control
  ZSIM_MAGIC_OP_PAUSE_SIM,     // Pause simulation after it has started
  ZSIM_MAGIC_OP_RESUME_SIM,    // Resume simulation
  // Stat control
  ZSIM_MAGIC_OP_APPEND_STAT_SNAPSHOT,
};

// The struct that contains full information about the magic op
typedef struct {
  int op;
  union {
    void *arg;
    int rand_32;
    uint64_t rand_64;
  };
} zsim_magic_op_t;

// This is used to request allocation on the 2D space, with op ZSIM_MAGIC_OP_ADDR_MAP_ALLOC
typedef struct {
  int type_id;
  uint64_t addr_1d;
  int size;
} main_addr_map_alloc_data_t;

// This struct is used to pass information from the application to the simulator
// The application allocates a struct like this, and passes the pointer to zSim using zsim_magic_op()
typedef struct {
  int type_id;        // Not used
  union {
    uint64_t ptr;     // malloc, calloc, and free uses this
    uint64_t new_ptr; // realloc uses this
  };
  int size;           // malloc, calloc, and realloc uses this
  int count;          // calloc uses this
  uint64_t old_ptr;   // realloc uses this
} zsim_alloc_t;

void zsim_magic_op(zsim_magic_op_t *op);
void zsim_magic_op_hello_world();
void zsim_magic_op_print_str(const char *s);
void zsim_magic_op_start_sim();
void zsim_magic_op_pause_sim();
void zsim_magic_op_resume_sim();
void zsim_magic_op_malloc(int size, uint64_t ptr);
void zsim_magic_op_calloc(int count, int size, uint64_t ptr);
void zsim_magic_op_realloc(uint64_t old_ptr, int size, uint64_t new_ptr);
void zsim_magic_op_free(uint64_t ptr);
void zsim_magic_op_append_stat_snapshot();

#ifdef __cplusplus
}
#endif

#endif