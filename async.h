//   TODO:
// - Make it thread safe
// - Implement more architectures

#ifndef CORO_H
#define CORO_H

#ifndef CORODEF
#define CORODEF
#endif

#ifndef CORO_ALLOC
#include <stdlib.h>
#include <sys/mman.h>
#define CORO_ALLOC malloc
#define CORO_MMAP mmap
#define CORO_MUNMAP munmap
#define CORO_FREE free // If you provide ALLOC you should also provide FREE so they're tied here
#endif

#ifndef STACK_SIZE
#include <unistd.h>
#define PAGE_SIZE getpagesize()
#define STACK_SIZE (512 * PAGE_SIZE)
#endif

typedef struct Coroutine* Coro;

typedef enum {
	CORO_READY,
	CORO_RUNNING,
	CORO_DEAD,
} CORO_STATE;

CORODEF Coro coro_create(void *fn);
CORODEF void coro_destroy(Coro co);
CORODEF void* coro_resume(Coro co, void *args);
CORODEF void* coro_yield(void *args);

#define coro_y(type, ...) coro_yield(&(type){__VA_ARGS__})

#endif

#ifdef CORO_IMPLEMENTATION
#undef CORO_IMPLEMENTATION

typedef struct {
	size_t RSP;
	size_t RDI;
	size_t RSI;
	size_t RBX;
	size_t RBP;
	size_t R12;
	size_t R13;
	size_t R14;
	size_t R15;
} Context;

static void __store_context(Context *c);
static void __load_context(Context *c);
static void __coro_exit();

struct Coroutine {
	Context *ctx;
	Coro caller;
	size_t fn;
	CORO_STATE state;
	void *yielded;
  void *args;
	size_t *stack;
};

CORODEF Coro coro_create(void *fn) {
	Coro c = (struct Coroutine*)CORO_ALLOC(sizeof(struct Coroutine));
	c->ctx = (Context*)CORO_ALLOC(sizeof(Context));
	c->stack = CORO_MMAP(NULL, STACK_SIZE + PAGE_SIZE, PROT_NONE, MAP_PRIVATE | MAP_STACK | MAP_ANONYMOUS, -1, 0);
	mprotect((char*)c->stack + PAGE_SIZE, STACK_SIZE, PROT_READ | PROT_WRITE);
	c->ctx->RBP = (size_t)((char *)c->stack + PAGE_SIZE);
	c->ctx->RSP = (size_t)((char *)c->stack + STACK_SIZE) & ~0x0F;
	c->ctx->RSP -= 8;
	*(size_t *)(c->ctx->RSP) = (size_t)__coro_exit;
	c->fn = (size_t)fn;
	c->caller = NULL;
	c->state = CORO_READY;
	c->yielded = NULL;
  c->args = NULL;
	return c;
}

static Coro __G__COR0 = NULL;

CORODEF void* coro_resume(Coro co, void *args) {
	if (NULL == co || co->state == CORO_DEAD) return NULL;
	static Context ctx;
	static struct Coroutine main;
	main.ctx = &ctx;
	if (NULL == __G__COR0) {
		__G__COR0 = &main;
	}
	__store_context(__G__COR0->ctx);
	co->caller = __G__COR0;
	__G__COR0 = co;

	if (co->state == CORO_READY) {
		co->state = CORO_RUNNING;
		co->ctx->RSP -= 8;
		*(size_t *)(co->ctx->RSP) = co->fn;
		co->ctx->RDI = (size_t)args;
	} else {
		co->args = args;
	}
	__load_context(co->ctx);

	__G__COR0 = co->caller;
	return co->yielded;
}

CORODEF void* coro_yield(void *args) {
	__G__COR0->yielded = args;
	__store_context(__G__COR0->ctx);
	__load_context(__G__COR0->caller->ctx);
	return __G__COR0->args;
}

CORODEF void coro_destroy(Coro co) {
  CORO_MUNMAP(co->stack, STACK_SIZE + PAGE_SIZE);
	CORO_FREE(co->ctx);
  CORO_FREE(co);
}

static void __attribute__((naked)) __load_context(Context *c) {
	(void)c;
	asm(
			"movq 0(%rdi), %rsp\n"
			"movq 16(%rdi), %rsi\n"
			"movq 24(%rdi), %rbx\n"
			"movq 32(%rdi), %rbp\n"
			"movq 40(%rdi), %r12\n"
			"movq 48(%rdi), %r13\n"
			"movq 56(%rdi), %r14\n"
			"movq 64(%rdi), %r15\n"
			"movq 8(%rdi), %rdi\n" // Last otherwise corrupts parameter
			"ret\n"
		 );
}

static void __attribute__((naked)) __store_context(Context *c) {
	(void)c;
	asm(
			"movq %rsp, (%rdi)\n"
			"movq %rdi, 8(%rdi)\n"
			"movq %rsi, 16(%rdi)\n"
			"movq %rbx, 24(%rdi)\n"
			"movq %rbp, 32(%rdi)\n"
			"movq %r12, 40(%rdi)\n"
			"movq %r13, 48(%rdi)\n"
			"movq %r14, 56(%rdi)\n"
			"movq %r15, 64(%rdi)\n"
			"ret\n"
		 );
}

static void __coro_exit() {
	__G__COR0->state = CORO_DEAD;
	coro_yield(NULL);
	return;
}

#endif // IMPL
