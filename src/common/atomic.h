#pragma once
#include <stdint.h>

/*
 * C11 <stdatomic.h> / _Atomic:
 * - GCC 4.8: header does not exist
 * - clang 3.4 (CentOS 7): header may exist, but atomic libcalls emit futex
 *   opcode 0x7e7f; kernel 3.10 returns ENOSYS and the caller spins forever
 * __sync_fetch_and_add is a lock xadd on x86_64 for both compilers.
 */
#if defined(__clang__) && defined(__clang_major__) && (__clang_major__ < 4)
#elif defined(__GNUC__) && !defined(__clang__) && (__GNUC__ < 5)
#elif defined(__has_include)
#  if __has_include(<stdatomic.h>)
#    include <stdatomic.h>
#    define ALLIGATOR_HAS_STDATOMIC 1
#  endif
#endif

#ifdef ALLIGATOR_HAS_STDATOMIC
typedef _Atomic uint64_t alligator_atomic_u64;
static inline uint64_t alligator_atomic_fetch_add_u64(alligator_atomic_u64 *p, uint64_t v)
{
	return atomic_fetch_add(p, v);
}
#else
typedef volatile uint64_t alligator_atomic_u64;
static inline uint64_t alligator_atomic_fetch_add_u64(alligator_atomic_u64 *p, uint64_t v)
{
	return __sync_fetch_and_add(p, v);
}
#endif
