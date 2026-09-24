/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    YmmSwitch - does the upper half of a YMM register survive a task switch?

    Two tasks each keep a private 32-byte pattern in ymm5 across a busy-spin
    that is long enough to be preempted by the scheduler tick, then read the
    register back.  A kernel that saves and restores only the 512-byte FXSAVE
    block (XMM = the low 128 bits) hands each task the other task's upper
    128 bits after a switch: the low half always matches, the high half does
    not, and the foreign value is the other task's pattern.

      YmmSwitch [iterations] [spin]

    Prints per task: iterations, low-half mismatches, high-half mismatches and
    the first foreign high half seen.  A correct kernel prints 0 / 0.

    Build: x86_64-aros-gcc -O2 -mavx -o YmmSwitch YmmSwitch.c
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <exec/tasks.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

struct Result {
    uint64_t iterations;
    uint64_t low_bad;
    uint64_t high_bad;
    uint8_t  foreign[16];
    int      have_foreign;
    volatile int done;
};

static struct Result g_res[2];
static uint64_t g_iterations = 200000;
static uint64_t g_spin = 40000;

/* The pattern lives in ymm5 for the whole spin; nothing else touches it. */
static void run(struct Result *r, uint8_t fill)
{
    uint8_t pattern[32] __attribute__((aligned(32)));
    uint8_t out[32] __attribute__((aligned(32)));
    uint64_t i;

    for (i = 0; i < 32; i++)
        pattern[i] = (uint8_t)(fill + i);

    for (i = 0; i < g_iterations; i++) {
        uint64_t spin = g_spin;
        asm volatile(
            "vmovdqa (%1), %%ymm5\n\t"
            "1:\n\t"
            "dec %0\n\t"
            "jnz 1b\n\t"
            "vmovdqa %%ymm5, (%2)\n\t"
            : "+r"(spin)
            : "r"(pattern), "r"(out)
            : "ymm5", "memory", "cc");
        r->iterations++;
        if (memcmp(out, pattern, 16) != 0)
            r->low_bad++;
        if (memcmp(out + 16, pattern + 16, 16) != 0) {
            r->high_bad++;
            if (!r->have_foreign) {
                memcpy(r->foreign, out + 16, 16);
                r->have_foreign = 1;
            }
        }
    }
    r->done = 1;
}

static void worker(void)
{
    run(&g_res[1], 0x80);
}

static void report(const char *name, const struct Result *r, uint8_t fill)
{
    int i;
    printf("%s: pattern %02x.., iterations %llu, low-half mismatches %llu, high-half mismatches %llu\n",
           name, fill, (unsigned long long)r->iterations,
           (unsigned long long)r->low_bad, (unsigned long long)r->high_bad);
    if (r->have_foreign) {
        printf("%s: first foreign high half:", name);
        for (i = 0; i < 16; i++)
            printf(" %02x", r->foreign[i]);
        printf("\n");
    }
}

int main(int argc, char **argv)
{
    struct Process *child;

    if (argc > 1)
        g_iterations = strtoull(argv[1], NULL, 0);
    if (argc > 2)
        g_spin = strtoull(argv[2], NULL, 0);

    memset(g_res, 0, sizeof(g_res));

    child = CreateNewProcTags(NP_Entry, (IPTR)worker,
                              NP_Name, (IPTR)"YmmSwitch-B",
                              NP_Priority, 0,
                              TAG_DONE);
    if (!child) {
        printf("YmmSwitch: CreateNewProc failed\n");
        return 20;
    }

    run(&g_res[0], 0x10);

    while (!g_res[1].done)
        Delay(5);

    report("task A", &g_res[0], 0x10);
    report("task B", &g_res[1], 0x80);
    printf("verdict: %s\n",
           (g_res[0].high_bad || g_res[1].high_bad) ? "YMM upper halves NOT preserved across task switches"
                                                    : "YMM state preserved");
    return (g_res[0].high_bad || g_res[1].high_bad) ? 10 : 0;
}
