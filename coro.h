#ifndef _CORO_H_
#define _CORO_H_

#include <stddef.h>
#ifndef CORODEF
#define CORODEF
#endif

#ifndef STACK_SIZE
#include <unistd.h>
#define STACK_SIZE 512*getpagesize()
#endif

#ifndef CORO_ALLOC
#include <stdlib.h>
#include <sys/mman.h>
#define CORO_ALLOC malloc
#define CORO_MMAP mmap
#define CORO_MUNMAP munmap
#define CORO_FREE free // If you provide ALLOC you should also provide FREE so they're tied here
#endif

typedef enum { CORO_READY, CORO_RUNNING, CORO_DEAD } CORO_STATE;
typedef struct Coroutine* Coro;

CORODEF Coro coro_create(void *co);
CORODEF void* coro_yield(void *pass);
CORODEF void coro_destroy(Coro co); 
CORODEF void* coro_resume(Coro co, void *pass);
#define CORO_IMPLEMENTATION
#ifdef CORO_IMPLEMENTATION
#undef CORO_IMPLEMENTATION

// Valid for Apple Silicon (ARM64)
#if defined(__aarch64__) || defined(__arm64__)
typedef struct __attribute__((aligned(16))) Context {
  size_t fp;   // 0 (x29)
  size_t lr;   // 8 (x30) <--- This is where we store the function pointer
  size_t sp;   // 16
  size_t x19;  // 24
  size_t x20;  // 32
  size_t x21;  // 40
  size_t x22;  // 48
  size_t x23;  // 56
  size_t x24;  // 64
  size_t x25;  // 72
  size_t x26;  // 80 
  size_t x27;  // 88
  size_t x28;  // 96
  __attribute__((aligned(16))) __uint128_t  v8,   // Full SIMD registers
                                            v9, 
                                            v10, 
                                            v11, 
                                            v12, 
                                            v13, 
                                            v14, 
                                            v15;
} Context;

static void __attribute__((naked)) swap_context(struct Context *old_ctx, struct Context *new_ctx) {
  asm(
    "stp fp, lr, [x0, #0]\n"
    "ldp fp, lr, [x1, #0]\n"
    "mov x2, sp\n"
    "stp x2, x19, [x0, #16]\n"
    "ldp x2, x19, [x1, #16]\n"
    "and x2, x2, #-16\n"
    "mov sp, x2\n"
    "stp x20, x21, [x0, #32]\n"
    "ldp x20, x21, [x1, #32]\n"
    "stp x22, x23, [x0, #48]\n"
    "ldp x22, x23, [x1, #48]\n"
    "stp x24, x25, [x0, #64]\n"
    "ldp x24, x25, [x1, #64]\n"
    "stp x26, x27, [x0, #80]\n"
    "ldp x26, x27, [x1, #80]\n"
    "str x28, [x0, #96]\n"
    "ldr x28, [x1, #96]\n"

    "stp d8, d9, [x0, #104]\n"
    "stp d10, d11, [x0, #120]\n"
    "stp d12, d13, [x0, #136]\n"
    "stp d14, d15, [x0, #152]\n"

    "ldp d8, d9, [x1, #104]\n"
    "ldp d10, d11, [x1, #120]\n"
    "ldp d12, d13, [x1, #136]\n"
    "ldp d14, d15, [x1, #152]\n"
    "ret\n");
}

static void __attribute__((naked)) _turnaround(void) {
  asm(
    "blr x19\n" 
    #ifdef __APPLE__
    "bl ___coro_finish\n"
    #else
    "bl __coro_finish\n"
    #endif
  );
}
#elif defined(__arm__) && !defined(__aarch64__)
/*
 Layout of Context (32-bit ARMv6):
 Offsets (bytes)
  0   r4
  4   r5
  8   r6
 12   r7
 16   r8
 20   r9
 24   r10
 28   r11  (frame pointer if used)
 32   sp  (r13)
 36   lr  (r14)  <-- optional: can be used to store "return" or function pointer if desired
*/

typedef struct __attribute__((aligned(8))) Context {
  size_t r4;   // 0
  size_t r5;   // 4
  size_t r6;   // 8
  size_t r7;   //12
  size_t r8;   //16
  size_t r9;   //20
  size_t r10;  //24
  size_t fp;  //28 (frame pointer)
  size_t sp;   //32
  size_t lr;   //36
} Context;

/*
 * swap_context(old_ctx, new_ctx)
 *
 * Called with:
 *   r0 -> old_ctx
 *   r1 -> new_ctx
 *
 * Effects:
 *   Saves callee-saved registers (r4..r11), sp and lr into *old_ctx
 *   Restores r4..r11, sp and lr from *new_ctx
 *   Branches to lr (which should point into the resumed context)
 *
 * Note:
 *  - We reserve r4 as the "function-pointer holder" (analogous to x19 in
 *    your AArch64 code). The coroutine entry function address should be
 *    placed into r4 in the stored context if needed.
 */
static void __attribute__((naked)) swap_context(struct Context *old_ctx, struct Context *new_ctx) {
  __asm__ volatile(
    /* save callee-saved registers into *old_ctx */
    "str r4,  [r0, #0]\n"   /* old_ctx->r4 */
    "str r5,  [r0, #4]\n"
    "str r6,  [r0, #8]\n"
    "str r7,  [r0, #12]\n"
    "str r8,  [r0, #16]\n"
    "str r9,  [r0, #20]\n"
    "str r10, [r0, #24]\n"
    "str r11, [r0, #28]\n"

    /* save sp and lr */
    "mov r2, sp\n"
    "str r2, [r0, #32]\n"   /* old_ctx->sp */
    "str lr, [r0, #36]\n"   /* old_ctx->lr */

    /* load registers from *new_ctx */
    "ldr r4,  [r1, #0]\n"   /* new_ctx->r4 */
    "ldr r5,  [r1, #4]\n"
    "ldr r6,  [r1, #8]\n"
    "ldr r7,  [r1, #12]\n"
    "ldr r8,  [r1, #16]\n"
    "ldr r9,  [r1, #20]\n"
    "ldr r10, [r1, #24]\n"
    "ldr r11, [r1, #28]\n"

    /* restore sp and lr from new_ctx */
    "ldr r2, [r1, #32]\n"
    "mov sp, r2\n"
    "ldr lr, [r1, #36]\n"

    /* branch to lr to resume new context */
    "bx lr\n"
  );
}

/*
 * _turnaround
 *
 * This mirrors your _pinpoint/_turnaround function: it does an indirect branch
 * to the saved "entry" register (r4). If the user-provided coroutine function
 * returns, call __coro_finish and then exit with status 0.
 *
 * Calling convention: no args; naked so we control registers exactly.
 *
 * Note: on AArch64 you used `blr x19`. Here we use `blx r4` so that `lr`
 * receives the return address just like blr did on AArch64.
 */
static void __attribute__((naked)) _turnaround(void) {
  __asm__ volatile(
    "blx r4\n"          /* call the function pointer stored in r4 (like blr x19) */
    "bl __coro_finish\n"
  );
}

#else

typedef struct __attribute__((aligned(16))) Context {
  size_t rip;
  size_t rsp;
  size_t rbx;
  size_t rbp;
  size_t r12;
  size_t r13;
  size_t r14;
  size_t r15;
  // Floating Point/SIMD Registers (Full state, needs 16-byte alignment)
  // Use a struct to ensure alignment if possible, or force alignment on the whole Context struct.
  __attribute__((aligned(16))) char fpu_state[512];
} Context;

static void __attribute__((naked)) swap_context(Context *old, Context *new) {
  asm(
    "movq (%rsp), %rax\n"
    "movq %rax, (%rdi)\n"
    "leaq +8(%rsp), %rax\n"
    "movq %rax, 8(%rdi)\n"
    "movq %rbx, 16(%rdi)\n"
    "movq %rbp, 24(%rdi)\n"
    "movq %r12, 32(%rdi)\n"
    "movq %r13, 40(%rdi)\n"
    "movq %r14, 48(%rdi)\n"
    "movq %r15, 56(%rdi)\n"
    "fxsave 64(%rdi)\n"
    "movq 8(%rsi), %rsp\n"
    "movq 16(%rsi), %rbx\n"
    "movq 24(%rsi), %rbp\n"
    "movq 32(%rsi), %r12\n"
    "movq 40(%rsi), %r13\n"
    "movq 48(%rsi), %r14\n"
    "movq 56(%rsi), %r15\n"
    "fxrstor 64(%rsi)\n"
    "jmp *(%rsi)\n");
}

static void __attribute__((naked)) _turnaround(void) {
  asm(
    "call *%r12\n"
    "movq %rax, %rdi\n"
    "call __coro_finish\n"
    "ud2\n");
}
#endif

static Coro __G_CURRENT = NULL;
void __coro_finish(void); 

struct Coroutine {
  Context ctx;
  Coro caller;
  CORO_STATE state;
  void *result;
  size_t *stack;
};

CORODEF Coro coro_create(void *co) {
  Coro coro = CORO_ALLOC(sizeof(*coro));
  //coro->stack = CORO_ALLOC(STACK_SIZE);
  coro->state = CORO_READY;
#if defined(__aarch64__) || defined(__arm64__)
  coro->stack = CORO_MMAP(NULL, STACK_SIZE + getpagesize(), PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE , -1, 0);
  coro->ctx.sp = (size_t)((char *)coro->stack + STACK_SIZE);
  coro->ctx.lr = (size_t)_turnaround;
  coro->ctx.fp = 0;
  coro->ctx.x19 = (size_t)co;
#elif defined (__arm__)
  coro->stack = CORO_MMAP(NULL, STACK_SIZE + getpagesize(), PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_GROWSDOWN | MAP_STACK, -1, 0);
  coro->ctx.sp = (size_t)((char *)coro->stack + STACK_SIZE);
  coro->ctx.lr = (size_t)_turnaround;
  coro->ctx.fp = 0;
  coro->ctx.r4 = (size_t)co;
#else
  coro->stack = CORO_MMAP(NULL, STACK_SIZE + getpagesize(), PROT_NONE, MAP_PRIVATE | MAP_STACK | MAP_ANONYMOUS | MAP_GROWSDOWN, -1, 0);
  coro->ctx.rsp = (size_t)((char *)coro->stack + STACK_SIZE) & ~0xF;
  coro->ctx.rip = (size_t)_turnaround;
  coro->ctx.rbp = 0;
  coro->ctx.r12 = (size_t)co;
#endif
  mprotect((char*)coro->stack + getpagesize(), STACK_SIZE, PROT_READ | PROT_WRITE);
  return coro;
}

CORODEF void* coro_resume(Coro co, void *pass) {
  if (NULL == __G_CURRENT) {
    static struct Coroutine main_coro;
    __G_CURRENT = &main_coro;
  }
  if (co->state == CORO_DEAD || NULL == co) return NULL; 
  co->state = CORO_RUNNING;
  co->result = pass;
  co->caller = __G_CURRENT;
  Coro old = __G_CURRENT;
  __G_CURRENT = co;
  swap_context(&(co->caller->ctx), &(co->ctx));
  __G_CURRENT = old;
  return co->result;
}

CORODEF void* coro_yield(void *pass) {
  Coro self = __G_CURRENT;
  Coro target = self->caller;
  self->result = pass;
  __G_CURRENT = target;
  swap_context(&(self->ctx), &(target->ctx));
  __G_CURRENT = self;
  return __G_CURRENT->result;
}

CORODEF void coro_destroy(Coro co) {
  CORO_MUNMAP(co->stack, STACK_SIZE);
  CORO_FREE(co);
}

CORODEF void __coro_finish() {
  __G_CURRENT->state = CORO_DEAD;
  coro_yield(NULL);
}

#endif // IMPLEMENTATION
#endif // HEADER

