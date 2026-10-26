/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "kernels.h"

void bench_prepare(uint32_t *memory, uint32_t words)
{
    /* Odd stride forms a single cycle for all power-of-two working sets. */
    for (uint32_t i = 0; i < words; i++) {
        memory[i] = (i + 17) & (words - 1);
    }
}

__attribute__((noinline)) uint32_t bench_cpu(unsigned kind, uint32_t iterations,
                                             uint32_t *memory, uint32_t words)
{
    uint32_t a = 1, b = 3, c = 5, d = 7;
    /* Every measured load/store must execute, even for predictable data. */
    volatile uint32_t *m = memory;
    switch (kind) {
    case 0: /* Dependent integer arithmetic. */
        for (uint32_t i = 0; i < iterations; i++) {
            __asm__ volatile("imull $1664525, %0\n\taddl $1013904223, %0"
                             : "+r"(a)
                             :
                             : "cc");
        }
        return a;
    case 1: /* Four independent integer chains. */
        for (uint32_t i = 0; i < iterations; i++) {
            __asm__ volatile("addl $3,%0\n\taddl $5,%1\n\t"
                             "addl $7,%2\n\taddl $11,%3"
                             : "+r"(a), "+r"(b), "+r"(c), "+r"(d)
                             :
                             : "cc");
        }
        return a ^ b ^ c ^ d;
    case 2: /* Explicit data-dependent branch, not compiler if-conversion. */
        for (uint32_t i = 0; i < iterations; i++) {
            __asm__ volatile("imull $1664525,%0\n\taddl $1013904223,%0\n\t"
                             "testl $65536,%0\n\tjz 1f\n\t"
                             "addl $3,%1\n\tjmp 2f\n1:\n\t"
                             "addl $7,%1\n2:"
                             : "+r"(a), "+r"(b)
                             :
                             : "cc");
        }
        return a ^ b;
    case 3: /* Volatile sequential loads; no eliminated or vectorized reads. */
        for (uint32_t i = 0; i < iterations; i++) {
            a += m[i & (words - 1)];
        }
        return a;
    case 4: /* Dependent fixed-stride pointer traversal. */
        a = 0;
        for (uint32_t i = 0; i < iterations; i++) {
            a = m[a];
        }
        return a;
    case 5: /* Sequential stores plus readback. */
        for (uint32_t i = 0; i < iterations; i++) {
            m[i & (words - 1)] = i;
        }
        for (uint32_t i = 0; i < words; i++) {
            a ^= m[i];
        }
        return a;
    case 6: { /* Exact integer-valued SSE additions, no denormals/rounding. */
        float v[4] __attribute__((aligned(16))) = { 0, 0, 0, 0 };
        const float one[4] __attribute__((aligned(16))) = { 1, 1, 1, 1 };
        __asm__ volatile("xorps %%xmm0,%%xmm0\n\tmovaps %0,%%xmm1"
                         :
                         : "m"(one)
                         : "xmm0", "xmm1");
        for (uint32_t i = 0; i < iterations; i++) {
            __asm__ volatile("addps %%xmm1,%%xmm0" : : : "xmm0");
        }
        __asm__ volatile("movaps %%xmm0,%0" : "=m"(v));
        return (uint32_t)v[0] + 3 * (uint32_t)v[1] + 5 * (uint32_t)v[2] +
               7 * (uint32_t)v[3];
    }
    case 7: { /* MMX packed integer additions; EMMS restores x87 state. */
        uint64_t result, ones = UINT64_C(0x0000000100000001);
        __asm__ volatile("pxor %%mm0,%%mm0\n\tmovq %0,%%mm1"
                         :
                         : "m"(ones)
                         : "mm0", "mm1");
        for (uint32_t i = 0; i < iterations; i++) {
            __asm__ volatile("paddd %%mm1,%%mm0" : : : "mm0");
        }
        __asm__ volatile("movq %%mm0,%0\n\temms" : "=m"(result));
        return (uint32_t)result + (uint32_t)(result >> 32);
    }
    case 8: { /* x87 exact additions. */
        double result;
        __asm__ volatile("fldz");
        for (uint32_t i = 0; i < iterations; i++) {
            __asm__ volatile("fld1\n\tfaddp");
        }
        __asm__ volatile("fstpl %0" : "=m"(result));
        return (uint32_t)result;
    }
    default:
        return 0;
    }
}
