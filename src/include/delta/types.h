#ifndef DELTA_TYPES_H
#define DELTA_TYPES_H

/* DELTA uses a small, self-contained type vocabulary.  No hosted libc is
 * linked into the kernel; these definitions are the ABI foundation. */

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed char        i8;
typedef signed short       i16;
typedef signed int         i32;
typedef signed long long   i64;
typedef u64                usize;
typedef i64                isize;
typedef u64                uintptr_t;

typedef _Bool bool;
#define true  1
#define false 0

#define NULL ((void *)0)

#define PACKED      __attribute__((packed))
#define ALIGNED(n)  __attribute__((aligned(n)))
#define NORETURN    __attribute__((noreturn))
#define UNUSED      __attribute__((unused))
#define USED        __attribute__((used))
#define INLINE      static inline __attribute__((always_inline))
#define NOINLINE    __attribute__((noinline))

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b)    ((a) < (b) ? (a) : (b))
#define MAX(a, b)    ((a) > (b) ? (a) : (b))
#define ALIGN_UP(v, a)   (((v) + ((a) - 1)) & ~((u64)(a) - 1))
#define ALIGN_DOWN(v, a) ((v) & ~((u64)(a) - 1))

#define KB (1024ULL)
#define MB (1024ULL * KB)
#define GB (1024ULL * MB)

typedef struct {
    u32 lo;
    u32 hi;
} u64_parts;

/* Compiler-provided varargs support. */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_copy(dst, src)  __builtin_va_copy(dst, src)

#endif /* DELTA_TYPES_H */
