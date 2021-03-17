
// This file contains magic ops that can be called by the compiled binary to pass
// information to the simulator.

#include <stdint.h>

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

// XCHG R11, R11
inline static void zsim_start_sim() {
  asm(".byte 0x4D, 0x87, 0xDB");
  return;
}
