
// This file contains magic ops that can be called by the compiled binary to pass
// information to the simulator.

#include <stdint.h>

//* zsim - zsim related functions, called by application to communicate with the simulator

// Print hello world with the value of the arg
#define ZSIM_MAGIC_OP_HELLO_WORLD            0
// Print a string, and arg points to the string
#define ZSIM_MAGIC_OP_PRINT_STR              1
// Allocate a 2D address on the main addr map
#define ZSIM_MAGIC_OP_ADDR_MAP_ALLOC         10
// Start simulation immediately; This can be overridden by simulator options
#define ZSIM_START_SIM                       11
// Return the mapping entry in arg (the entry is only valid in the lifetime of the addr_map)
#define ZSIM_MAGIC_OP_GET_ADDR_MAPPING       12
// Returns in arg as a 32-bit random integer (obtained from C library rand())
#define ZSIM_MAGIC_OP_GET_RAND32             13
// Return in the arg as a 64-bit random integer
#define ZSIM_MAGIC_OP_GET_RAND64             14

// The struct that contains full information about the magic op
typedef struct {
  int op;
  union {
    void *arg;
    int rand_32;
    uint64_t rand_64;
  };
} zsim_magic_op_t;

// This struct is used to pass information from the application to the simulator
// The application allocates a struct like this, and passes the pointer to zSim using zsim_magic_op()
typedef struct {
  int type_id;
  uint64_t addr_1d;
  int size;
} main_addr_map_alloc_data_t;

// General-purpose magic op
inline static void zsim_magic_op(zsim_magic_op_t *op) {
  // Write op pointer into R10
  __asm__ __volatile__("mov %0, %%r10\n\t"
                     : /* no output */
                     : "a"(op)
                     : "%r10");
  // XOR R10, R10
  __asm__ __volatile__ (".byte 0x4D, 0x87, 0xD2");
  return;
}

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

// XCHG R11, R11
inline static void zsim_start_sim() {
  asm(".byte 0x4D, 0x87, 0xDB");
  return;
}
