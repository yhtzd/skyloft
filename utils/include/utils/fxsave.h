/*
 * fxsave.h - Save x87 FPU, MMX Technology, and SSE State.
 *
 * See https://www.felixcloutier.com/x86/fxsave.
 */

/*
 * From NetBSD: cpu_extended_state.h
 *
 * This file contains definitions of structures that match the memory layouts
 * used on x86 processors to save floating point registers and other extended
 * cpu states.
 *
 * This includes registers (etc) used by SSE/SSE2/SSE3/SSSE3/SSE4 and the later
 * AVX instructions.
 *
 * The definitions are such that any future 'extended state' should be handled,
 * provided the kernel doesn't need to know the actual contents.
 *
 * The actual structures the cpu accesses must be aligned to 16 bytes for FXSAVE
 * and 64 for XSAVE. The types aren't aligned because copies do not need extra
 * alignment.
 *
 * The slightly different layout saved by the i387 fsave is also defined.
 * This is only normally written by pre Pentium II type cpus that don't
 * support the fxsave instruction.
 *
 * Associated save instructions:
 * FNSAVE:   Saves x87 state in 108 bytes (original i387 layout). Then
 *           reinitializes the fpu.
 * FSAVE:    Encodes to FWAIT followed by FNSAVE.
 * FXSAVE:   Saves the x87 state and XMM (aka SSE) registers to the first
 *           448 (max) bytes of a 512 byte area. This layout does not match
 *           that written by FNSAVE.
 * XSAVE:    Uses the same layout for the x87 and XMM registers, followed by
 *           a 64byte header and separate save areas for additional extended
 *           cpu states. The x87 state is always saved, the others
 *           conditionally.
 * XSAVEOPT: Same as XSAVE but only writes the registers blocks that have
 *           been modified.
 */

#pragma once

#include <string.h>

#include <utils/assert.h>
#include <utils/defs.h>

/*
 * Layout for code/data pointers relating to FP exceptions. Marked 'packed'
 * because they aren't always 64bit aligned. Since the x86 cpu supports
 * misaligned accesses it isn't worth avoiding the 'packed' attribute.
 */
union fp_addr {
    uint64_t fa_64; /* Linear address for 64bit systems */
    struct {
        uint32_t fa_off;    /* linear address for 32 bit */
        uint16_t fa_seg;    /* code/data (etc) segment */
        uint16_t fa_opcode; /* last opcode (sometimes) */
    } fa_32;
} __packed __aligned(4);

/* The x87 registers are 80 bits */
struct fpacc87 {
    uint64_t f87_mantissa; /* mantissa */
    uint16_t f87_exp_sign; /* exponent and sign */
} __packed __aligned(2);

/* The x87 registers padded out to 16 bytes for fxsave */
struct fpaccfx {
    struct fpacc87 r __aligned(16);
};

/* The SSE/SSE2 registers are 128 bits */
struct xmmreg {
    uint8_t xmm_bytes[16];
};

/*
 * FPU/MMX/SSE/SSE2 context (FXSAVE instruction).
 */
struct fxsave {
    uint16_t fx_cw;             /* FPU Control Word */
    uint16_t fx_sw;             /* FPU Status Word */
    uint8_t fx_tw;              /* FPU Tag Word (abridged) */
    uint8_t fx_zero;            /* zero */
    uint16_t fx_opcode;         /* FPU Opcode */
    union fp_addr fx_ip;        /* FPU Instruction Pointer */
    union fp_addr fx_dp;        /* FPU Data pointer */
    uint32_t fx_mxcsr;          /* MXCSR Register State */
    uint32_t fx_mxcsr_mask;     /* MXCSR_MASK */
    struct fpaccfx fx_87_ac[8]; /* 8 x87 registers */
    struct xmmreg fx_xmm[16];   /* XMM regs (8 in 32bit modes) */
    uint8_t fx_rsvd0[48];
    uint8_t fx_rsvd1[48]; /* Reserved for software */
} __aligned(16);
BUILD_ASSERT(sizeof(struct fxsave) == 512);

static inline void fxstate_init(struct fxsave *fxstate)
{
    assert(fxstate != NULL);
    memset(fxstate, 0, sizeof(struct fxsave));
    fxstate->fx_cw = 0x37f;
    fxstate->fx_tw = 0xff;
    fxstate->fx_mxcsr = 0x1f80;
}
