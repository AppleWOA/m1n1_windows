/* SPDX-License-Identifier: MIT */

#include "hv.h"
#include "assert.h"
#include "cpu_regs.h"
#include "exception.h"
#include "smp.h"
#include "string.h"
#include "uart.h"
#include "uartproxy.h"

#define TIME_ACCOUNTING
//
// m1n1_windows change: when the vGIC is running in the guest - timer interrupts by virtue of coming from the generic timer are
// still going to come as FIQs to EL2 - we'll need to divert those to the guest as *IRQs* (to prevent Windows from crashing as it treats FIQs as
// errors).
//
extern bool vgic_inited;
extern spinlock_t bhl;

#define _SYSREG_ISS(_1, _2, op0, op1, CRn, CRm, op2)                                               \
    (((op0) << ESR_ISS_MSR_OP0_SHIFT) | ((op1) << ESR_ISS_MSR_OP1_SHIFT) |                         \
     ((CRn) << ESR_ISS_MSR_CRn_SHIFT) | ((CRm) << ESR_ISS_MSR_CRm_SHIFT) |                         \
     ((op2) << ESR_ISS_MSR_OP2_SHIFT))
#define SYSREG_ISS(...) _SYSREG_ISS(__VA_ARGS__)

#define PERCPU(x) pcpu[mrs(TPIDR_EL2)].x

struct hv_pcpu_data {
    u32 ipi_queued;
    u32 ipi_pending;
    u32 pmc_pending;
    u64 pmc_irq_mode;
    u64 exc_entry_pmcr0_cnt;
} ALIGNED(64);

struct hv_pcpu_data pcpu[MAX_CPUS];

void hv_exit_guest(void) __attribute__((noreturn));

static u64 stolen_time = 0;
static u64 exc_entry_time;

extern u64 hv_cpus_in_guest;
extern int hv_pinned_cpu;
extern int hv_want_cpu;

static bool time_stealing = true;

static void _hv_exc_proxy(struct exc_info *ctx, uartproxy_boot_reason_t reason, u32 type,
                          void *extra)
{
    int from_el = FIELD_GET(SPSR_M, ctx->spsr) >> 2;

    hv_wdt_breadcrumb('P');

    /*
     * Get all the CPUs into the HV before running the proxy, to make sure they all exit to
     * the guest with a consistent time offset.
     */
    if (time_stealing)
        hv_rendezvous();

    u64 entry_time = mrs(CNTPCT_EL0);

    ctx->elr_phys = hv_translate(ctx->elr, false, false, NULL);
    ctx->far_phys = hv_translate(ctx->far, false, false, NULL);
    ctx->sp_phys = hv_translate(from_el == 0 ? ctx->sp[0] : ctx->sp[1], false, false, NULL);
    ctx->extra = extra;

    struct uartproxy_msg_start start = {
        .reason = reason,
        .code = type,
        .info = ctx,
    };

    hv_wdt_suspend();
    int ret = uartproxy_run(&start);
    hv_wdt_resume();

    switch (ret) {
        case EXC_RET_HANDLED:
            hv_wdt_breadcrumb('p');
            if (time_stealing) {
                u64 lost = mrs(CNTPCT_EL0) - entry_time;
                stolen_time += lost;
            }
            break;
        case EXC_EXIT_GUEST:
            hv_rendezvous();
            spin_unlock(&bhl);
            hv_exit_guest(); // does not return
        default:
            printf("Guest exception not handled, rebooting.\n");
            print_regs(ctx->regs, 0);
            flush_and_reboot(); // does not return
    }
}

static void hv_maybe_switch_cpu(struct exc_info *ctx, uartproxy_boot_reason_t reason, u32 type,
                                void *extra)
{
    while (hv_want_cpu != -1) {
        if (hv_want_cpu == smp_id()) {
            hv_want_cpu = -1;
            _hv_exc_proxy(ctx, reason, type, extra);
        } else {
            // Unlock the HV so the target CPU can get into the proxy
            spin_unlock(&bhl);
            while (hv_want_cpu != -1)
                sysop("dmb sy");
            spin_lock(&bhl);
        }
    }
}

void hv_exc_proxy(struct exc_info *ctx, uartproxy_boot_reason_t reason, u32 type, void *extra)
{
    /*
     * Wait while another CPU is pinned or being switched to.
     * If a CPU switch is requested, handle it before actually handling the
     * exception. We still tell the host the real reason code, though.
     */
    while ((hv_pinned_cpu != -1 && hv_pinned_cpu != smp_id()) || hv_want_cpu != -1) {
        if (hv_want_cpu == smp_id()) {
            hv_want_cpu = -1;
            _hv_exc_proxy(ctx, reason, type, extra);
        } else {
            // Unlock the HV so the target CPU can get into the proxy
            spin_unlock(&bhl);
            while ((hv_pinned_cpu != -1 && hv_pinned_cpu != smp_id()) || hv_want_cpu != -1)
                sysop("dmb sy");
            spin_lock(&bhl);
        }
    }

    /* Handle the actual exception */
    _hv_exc_proxy(ctx, reason, type, extra);

    /*
     * If as part of handling this exception we want to switch CPUs, handle it without returning
     * to the guest.
     */
    hv_maybe_switch_cpu(ctx, reason, type, extra);
}

void hv_set_time_stealing(bool enabled, bool reset)
{
    time_stealing = enabled;
    if (reset)
        stolen_time = 0;
}

void hv_add_time(s64 time)
{
    stolen_time -= (u64)time;
}

static void hv_update_fiq(void)
{
    u64 hcr = mrs(HCR_EL2);
    bool fiq_pending = false;

    if (mrs(CNTP_CTL_EL02) == (CNTx_CTL_ISTATUS | CNTx_CTL_ENABLE)) {
        fiq_pending = true;
        reg_clr(SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2, VM_TMR_FIQ_ENA_ENA_P);
    } else {
        reg_set(SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2, VM_TMR_FIQ_ENA_ENA_P);
    }

    if (mrs(CNTV_CTL_EL02) == (CNTx_CTL_ISTATUS | CNTx_CTL_ENABLE)) {
        fiq_pending = true;
        reg_clr(SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2, VM_TMR_FIQ_ENA_ENA_V);
    } else {
        reg_set(SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2, VM_TMR_FIQ_ENA_ENA_V);
    }

    fiq_pending |= PERCPU(ipi_pending) || PERCPU(pmc_pending);

    sysop("isb");

    if ((hcr & HCR_VF) && !fiq_pending) {
        hv_write_hcr(hcr & ~HCR_VF);
    } else if (!(hcr & HCR_VF) && fiq_pending) {
        hv_write_hcr(hcr | HCR_VF);
    }
}

#define SYSREG_MAP(sr, to)                                                                         \
    case SYSREG_ISS(sr):                                                                           \
        if (is_read)                                                                               \
            regs[rt] = _mrs(sr_tkn(to));                                                           \
        else                                                                                       \
            _msr(sr_tkn(to), regs[rt]);                                                            \
        return true;

#define SYSREG_PASS(sr)                                                                            \
    case SYSREG_ISS(sr):                                                                           \
        if (is_read)                                                                               \
            regs[rt] = _mrs(sr_tkn(sr));                                                           \
        else                                                                                       \
            _msr(sr_tkn(sr), regs[rt]);                                                            \
        return true;

static bool hv_handle_msr_unlocked(struct exc_info *ctx, u64 iss)
{
    u64 reg = iss & (ESR_ISS_MSR_OP0 | ESR_ISS_MSR_OP2 | ESR_ISS_MSR_OP1 | ESR_ISS_MSR_CRn |
                     ESR_ISS_MSR_CRm);
    u64 rt = FIELD_GET(ESR_ISS_MSR_Rt, iss);
    bool is_read = iss & ESR_ISS_MSR_DIR;

    u64 *regs = ctx->regs;

    regs[31] = 0;

    switch (reg) {
        SYSREG_PASS(SYS_IMP_APL_CORE_NRG_ACC_DAT);
        SYSREG_PASS(SYS_IMP_APL_CORE_SRM_NRG_ACC_DAT);
        /* Architectural timer, for ECV */
        SYSREG_MAP(SYS_CNTV_CTL_EL0, SYS_CNTV_CTL_EL02)
        SYSREG_MAP(SYS_CNTV_CVAL_EL0, SYS_CNTV_CVAL_EL02)
        SYSREG_MAP(SYS_CNTV_TVAL_EL0, SYS_CNTV_TVAL_EL02)
        SYSREG_MAP(SYS_CNTP_CTL_EL0, SYS_CNTP_CTL_EL02)
        SYSREG_MAP(SYS_CNTP_CVAL_EL0, SYS_CNTP_CVAL_EL02)
        SYSREG_MAP(SYS_CNTP_TVAL_EL0, SYS_CNTP_TVAL_EL02)
        /* Spammy stuff seen on t600x p-cores */
        /* These are PMU/PMC registers */
        SYSREG_PASS(sys_reg(3, 2, 15, 12, 0));
        SYSREG_PASS(sys_reg(3, 2, 15, 13, 0));
        SYSREG_PASS(sys_reg(3, 2, 15, 14, 0));
        SYSREG_PASS(sys_reg(3, 2, 15, 15, 0));
        SYSREG_PASS(sys_reg(3, 1, 15, 7, 0));
        SYSREG_PASS(sys_reg(3, 1, 15, 8, 0));
        SYSREG_PASS(sys_reg(3, 1, 15, 9, 0));
        SYSREG_PASS(sys_reg(3, 1, 15, 10, 0));
        /* Noisy traps */
        SYSREG_PASS(SYS_IMP_APL_HID4)
        SYSREG_PASS(SYS_IMP_APL_EHID4)
        /* We don't normally trap these, but if we do, they're noisy */
        SYSREG_PASS(SYS_IMP_APL_GXF_STATUS_EL1)
        SYSREG_PASS(SYS_IMP_APL_CNTVCT_ALIAS_EL0)
        SYSREG_PASS(SYS_IMP_APL_TPIDR_GL1)
        SYSREG_MAP(SYS_IMP_APL_SPSR_GL1, SYS_IMP_APL_SPSR_GL12)
        SYSREG_MAP(SYS_IMP_APL_ASPSR_GL1, SYS_IMP_APL_ASPSR_GL12)
        SYSREG_MAP(SYS_IMP_APL_ELR_GL1, SYS_IMP_APL_ELR_GL12)
        SYSREG_MAP(SYS_IMP_APL_ESR_GL1, SYS_IMP_APL_ESR_GL12)
        SYSREG_MAP(SYS_IMP_APL_SPRR_PERM_EL1, SYS_IMP_APL_SPRR_PERM_EL12)
        SYSREG_MAP(SYS_IMP_APL_APCTL_EL1, SYS_IMP_APL_APCTL_EL12)
        SYSREG_MAP(SYS_IMP_APL_AMX_CTL_EL1, SYS_IMP_APL_AMX_CTL_EL12)
        /* FIXME:Might be wrong */
        SYSREG_PASS(SYS_IMP_APL_AMX_STATE_T)
        /* pass through PMU handling */
        SYSREG_PASS(SYS_IMP_APL_PMCR1)
        SYSREG_PASS(SYS_IMP_APL_PMCR2)
        SYSREG_PASS(SYS_IMP_APL_PMCR3)
        SYSREG_PASS(SYS_IMP_APL_PMCR4)
        SYSREG_PASS(SYS_IMP_APL_PMESR0)
        SYSREG_PASS(SYS_IMP_APL_PMESR1)
        SYSREG_PASS(SYS_IMP_APL_PMSR)
#ifndef DEBUG_PMU_IRQ
        SYSREG_PASS(SYS_IMP_APL_PMC0)
#endif
        SYSREG_PASS(SYS_IMP_APL_PMC1)
        SYSREG_PASS(SYS_IMP_APL_PMC2)
        SYSREG_PASS(SYS_IMP_APL_PMC3)
        SYSREG_PASS(SYS_IMP_APL_PMC4)
        SYSREG_PASS(SYS_IMP_APL_PMC5)
        SYSREG_PASS(SYS_IMP_APL_PMC6)
        SYSREG_PASS(SYS_IMP_APL_PMC7)
        SYSREG_PASS(SYS_IMP_APL_PMC8)
        SYSREG_PASS(SYS_IMP_APL_PMC9)
        /* m1n1_windows change - Trap the ARM standard PMU regs */
        case SYSREG_ISS(SYS_PMCR_EL0):
            if(is_read) {
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 calculated = 0;
                u64 pmi_mask = PMCR0_IMODE_MASK;
                //
                // Are PMIs enabled? (affects bit 0 of PMCR equivalently)
                //
                if((pmcr0_value & pmi_mask) == (PMCR0_IMODE_FIQ)) {
                    calculated |= PMCR_E;
                }
                //
                // Bits 5:1 are 0 for now,  bits 6 and 7 are on always (long events always on)
                // and bit 9 is checked by PMCR0[20] (since it deals with freeze/overflow)
                //
                if((pmcr0_value & BIT(20)) != 0) {
                    calculated |= PMCR_FZO;
                }
                calculated |= ((BIT(6)) | (BIT(7)));
                printf("HV PMUv3 Redirect: mrs x%ld, PMCR_EL0 = 0x%lx\n", rt, calculated);
                regs[rt] = calculated;
            }
            else {
                //
                // Bits [63:10] will have writes discarded (mostly ARM spec, bit 32 due to lack of support)
                //
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                int cycle_reset_requested = 0;
                //
                // Bit 9 (stop count on overflow) writes affect bit 20 of APL_PMCR0
                //
                if((regs[rt] & BIT(9)) != 0) {
                    pmcr0_value |= BIT(20);
                }
                else {
                    pmcr0_value &= ~(BIT(20));
                }
                
                //
                // Writes to bits [6:3] unsupported since no way of expressing either with Apple PMUs.
                //
                // Writing bit 2 (cycle counter reset) implies setting PMC0 to 0, so check if that's the case.
                //
                if((regs[rt] & PMCR_C) != 0) {
                    cycle_reset_requested = 1;
                }
                //
                // Bit 1 is the same as bit 2 but for event counters, unimplemented for now.
                //
                // Bit 0 controls whether event counters are enabled globally, if this is being set,
                // the closest thing on Apple platforms is the IRQ mode so set that if bit 0 is requested.
                //
                if((regs[rt] & PMCR_E) != 0) {
                    pmcr0_value &= ~(PMCR0_IMODE_MASK);
                    pmcr0_value |= PMCR0_IMODE_FIQ;
                }
                else {
                    pmcr0_value &= ~(PMCR0_IMODE_MASK);
                    pmcr0_value |= PMCR0_IMODE_OFF;
                }
                sysop("isb");
                if(cycle_reset_requested == 1) {
                    pmcr0_value &= ~(BIT(12));
                    pmcr0_value &= ~(BIT(0));
                }
                msr(SYS_IMP_APL_PMCR0, pmcr0_value);
                sysop("isb");
                if(cycle_reset_requested == 1) {
                    sysop("isb");
                    msr(SYS_IMP_APL_PMC0, 0);
                    sysop("isb");
                    pmcr0_value |= BIT(12);
                    pmcr0_value |= BIT(0);
                    sysop("isb");
                    msr(SYS_IMP_APL_PMCR0, pmcr0_value);
                    sysop("isb");
                }
                printf("HV PMUv3 Redirect (OK): msr PMCR_EL0, x%ld = 0x%lx\n", rt, regs[rt]);
            }
            return true;
        SYSREG_MAP(SYS_PMCCNTR_EL0, SYS_IMP_APL_PMC0)
        case SYSREG_ISS(SYS_PMCCFILTR_EL0):
            if(is_read) {
                u64 pmcr1_value = mrs(SYS_IMP_APL_PMCR1);
                u64 calculated_value = 0;
                //
                // If EL0/EL1 counting is disabled, set bit 30 of PMCCFILTR to 1.
                // (This is backwards from how I would've done it but okay I suppose...)
                //
                if((pmcr1_value & BIT(8)) == 0) {
                    calculated_value |= BIT(30);
                }
                if((pmcr1_value & BIT(16)) == 0) {
                    calculated_value |= BIT(31);
                }
                //
                // EL2 counting always happens as far as is known, so bit 27 is always set to 1
                // (bit set to 1 in this case means disable filtering...not sure why it's backwards.)
                //
                calculated_value |= BIT(27);
                printf("HV PMUv3 Redirect: mrs x%ld, PMCCFILTR_EL0 = 0x%lx\n", rt, calculated_value);
                regs[rt] = calculated_value;
            }
            else {
                u64 pmcr1_value = mrs(SYS_IMP_APL_PMCR1);
                //
                // If we're being asked to disable counting cycles for a given EL, set the appropriate bit for
                // PMCR1 respectively.
                //
                if((regs[rt] & PMCCFILTR_P) == 0) {
                    pmcr1_value |= BIT(16);
                }
                else {
                    pmcr1_value &= ~(BIT(16));
                }
                if((regs[rt] & PMCCFILTR_U) == 0) {
                    pmcr1_value |= BIT(8);
                }
                else {
                    pmcr1_value &= ~(BIT(16));
                }
                sysop("isb");
                msr(SYS_IMP_APL_PMCR1, pmcr1_value);
                sysop("isb");
                printf("HV PMUv3 Redirect (OK): msr PMCCFILTR_EL0, x%ld = 0x%lx\n", rt, regs[rt]);
            }
            return true;
        case SYSREG_ISS(SYS_PMCEID0_EL0):
            //
            // Unimplemented for now, return 0 for a read, discard writes.
            //
            if(is_read) {
                regs[rt] = 0;
                printf("HV PMUv3 Redirect: mrs x%ld, PMCEID0_EL0 = 0x%lx\n", rt, regs[rt]);
            }
            else {
                //
                // Do nothing here.
                //
                printf("HV PMUv3 Redirect (skipped write): msr PMCEID0_EL0, x%ld = 0x%lx\n", rt, regs[rt]);
            }
            return true;
        case SYSREG_ISS(SYS_PMCEID1_EL0):
            //
            // Unimplemented for now, return 0 for a read, discard writes.
            //
            if(is_read) {
                regs[rt] = 0;
                printf("HV PMUv3 Redirect: mrs x%ld, PMCEID1_EL0 = 0x%lx\n", rt, regs[rt]);
            }
            else {
                printf("HV PMUv3 Redirect (skipped write): msr PMCEID1_EL0, x%ld = 0x%lx\n", rt, regs[rt]);
            }
            return true;
        case SYSREG_ISS(SYS_PMCNTENCLR_EL0):
            if(is_read) {
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 calculated_value = 0;
                //
                // check what perf counters are enabled in PMCR0, and reflect that in the returned PMCNTENCLR/PMCNTENSET value
                //
                if((pmcr0_value & BIT(0)) != 0) {
                    calculated_value |= BIT(31);
                }
                printf("HV PMUv3 Redirect: mrs x%ld, PMCNTENCLR_EL0 = 0x%lx\n", rt, calculated_value);
                regs[rt] = calculated_value;
            }
            else {
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 counters_disable_mask = GENMASK(31,0);
                //
                // check if any counters are being requested to be disabled (cycle counter only for now)
                // and deal with those here.
                //
                if((regs[rt] & counters_disable_mask) != 0) {
                    if((regs[rt] & BIT(31)) != 0) {
                        pmcr0_value &= ~(BIT(0));
                    }
                    sysop("isb");
                    msr(SYS_IMP_APL_PMCR0, pmcr0_value);
                    sysop("isb");
                    printf("HV PMUv3 Redirect (OK): msr PMCNTENCLR_EL0, x%ld = 0x%lx\n", rt, regs[rt]);
                }
            }
            return true;
        case SYSREG_ISS(SYS_PMCNTENSET_EL0):
            if(is_read) {
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 calculated_value = 0;
                //
                // check what perf counters are enabled in PMCR0, and reflect that in the returned PMCNTENCLR/PMCNTENSET value
                //
                if((pmcr0_value & BIT(0)) != 0) {
                    calculated_value |= BIT(31);
                }
                printf("HV PMUv3 Redirect: mrs x%ld, PMCNTENSET_EL0 = 0x%lx\n", rt, calculated_value);
                regs[rt] = calculated_value;
            }
            else {
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 counters_enable_mask = GENMASK(31,0);
                //
                // check if any counters are being requested to be disabled (cycle counter only for now)
                // and deal with those here.
                //
                if((regs[rt] & counters_enable_mask) != 0) {
                    if((regs[rt] & BIT(31)) != 0) {
                        pmcr0_value |= BIT(0);
                    }
                    sysop("isb");
                    msr(SYS_IMP_APL_PMCR0, pmcr0_value);
                    sysop("isb");
                    printf("HV PMUv3 Redirect (OK): msr PMCNTENSET_EL0, x%ld = 0x%lx\n", rt, regs[rt]);
                }
            }
            return true;
        SYSREG_MAP(SYS_PMEVCNTR0_EL0, SYS_IMP_APL_PMC2)
        // case SYSREG_ISS(SYS_PMEVTYPER0_EL0):
        //     if(is_read) {
        //         printf("mrs(PMEVTYPER0_EL0)\n");
        //         regs[rt] = 0;
        //         int value = mrs(SYS_IMP_APL_PMCR1);
        //         if(value & GENMASK(23, 16)) {
        //             regs[rt] |= BIT(31); //privileged bit
        //         }
        //         if(value & GENMASK(15, 8)) {
        //             regs[rt] |= BIT(30); //user filter bit
        //         }
        //         regs[rt] |= (mrs(SYS_IMP_APL_PMESR0) & GENMASK(7, 0));
        //     }
        //     else {
        //         int val = mrs(SYS_IMP_APL_PMCR1);
        //         int event = mrs(SYS_IMP_APL_PMESR0) & GENMASK(7, 0);
        //         if(regs[rt] & PMEVTYPER_P) {
        //             printf("msr(PMEVTYPER0_EL0, 0x%08lx): enabling el1 counting of event\n", regs[rt]);
        //             val |= BIT(16);
        //         }
        //         if(regs[rt] & GENMASK(7, 0)) {
        //             printf("msr(PMEVTYPER0_EL0, 0x%08lx): setting event\n", regs[rt]);
        //             event |= regs[rt] & GENMASK(7, 0);
        //             msr(SYS_IMP_APL_PMESR0, event);
        //         }
        //         msr(SYS_IMP_APL_PMCR1, val);
        //     }
        //     return true;
        case SYSREG_ISS(SYS_PMINTENCLR_EL1):
            if(is_read) {
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 calculated_value = 0;
                //
                // As before, cycle counter only for now. (bit 12 = PMI enabled for cycle counter)
                //
                if((pmcr0_value & BIT(12)) != 0) {
                    calculated_value |= BIT(31);
                }
                printf("HV PMUv3 Redirect: mrs x%ld, PMINTENCLR_EL1 = 0x%lx\n", rt, calculated_value);
                regs[rt] = calculated_value;
            }
            else {
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 counter_irqs_disabled_mask = GENMASK(31,0);
                //
                // Cycle counter only for now (bits 19:12 control all PMIs for the PMUs)
                //
                if((regs[rt] & counter_irqs_disabled_mask) != 0) {
                    if((regs[rt] & (BIT(31))) != 0) {
                        pmcr0_value &= ~(BIT(12));
                    }
                    sysop("isb");
                    msr(SYS_IMP_APL_PMCR0, pmcr0_value);
                    sysop("isb");
                    printf("HV PMUv3 Redirect (OK): msr PMINTENCLR_EL1, x%ld = 0x%lx\n", rt, regs[rt]);
                }

            }
            return true;
        case SYSREG_ISS(SYS_PMINTENSET_EL1):
            if(is_read) {
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 calculated_value = 0;
                //
                // As before, cycle counter only for now. (bit 12 = PMI enabled for cycle counter)
                //
                if((pmcr0_value & BIT(12)) != 0) {
                    calculated_value |= BIT(31);
                }
                printf("HV PMUv3 Redirect: mrs x%ld, PMINTENSET_EL1 = 0x%lx\n", rt, calculated_value);
                regs[rt] = calculated_value;
            }
            else {
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 counter_irqs_enabled_mask = GENMASK(31,0);
                //
                // Cycle counter only for now (bits 19:12 control all PMIs for the PMUs)
                //
                if((regs[rt] & counter_irqs_enabled_mask) != 0) {
                    if((regs[rt] & BIT(31)) != 0) {
                        pmcr0_value |= BIT(12);
                    }
                    sysop("isb");
                    msr(SYS_IMP_APL_PMCR0, pmcr0_value);
                    sysop("isb");
                    printf("HV PMUv3 Redirect (OK): msr PMINTENSET_EL1, x%ld = 0x%lx\n", rt, regs[rt]);
                }
            }
            return true;
        case SYSREG_ISS(SYS_PMMIR_EL1):
            //
            // return 0 for now, discard writes.
            //
            if(is_read) {
                regs[rt] = 0;
                printf("HV PMUv3 Redirect: mrs x%ld, PMMIR_EL1 = 0x%lx\n", rt, regs[rt]);
            }
            else {
                printf("HV PMUv3 Redirect (skipped write): msr PMMIR_EL1, x%ld = 0x%lx\n", rt, regs[rt]);
            }
            return true;
        case SYSREG_ISS(SYS_PMOVSCLR_EL0):
            if(is_read) {
                //
                // Read the state of the PMSR register to see if a PMU has overflowed.
                // Cycle counter only for now.
                //
                u64 pmsr_value = mrs(SYS_IMP_APL_PMSR);
                u64 calculated_value = 0;
                u64 pmu_overflowed_mask = GENMASK(9, 0);
                if((pmsr_value & pmu_overflowed_mask) != 0) {
                    //
                    // bit 0 is for PMC 0
                    //
                    if((pmsr_value & BIT(0)) != 0) {
                        calculated_value |= BIT(31);
                    }
                }
                printf("HV PMUv3 Redirect: mrs x%ld, PMOVSCLR_EL0 = 0x%lx\n", rt, calculated_value);
                regs[rt] = calculated_value;
            }
            else {
                //
                // To clear the overflow bit requires a reset of the PMC (to clear PMSR)
                // so we need to disable the counter and re-enable it with the bit set to 0.
                //
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 counter_overflow_mask = GENMASK(31,0);
                if((regs[rt] & counter_overflow_mask) != 0) {
                    if((regs[rt] & BIT(31)) != 0) {
                        pmcr0_value &= ~(BIT(12));
                        pmcr0_value &= ~(BIT(0));
                        sysop("isb");
                        msr(SYS_IMP_APL_PMCR0, pmcr0_value);
                        sysop("isb");
                        sysop("isb");
                        msr(SYS_IMP_APL_PMC0, 0);
                        sysop("isb");
                        pmcr0_value |= BIT(12);
                        pmcr0_value |= BIT(0);
                        sysop("isb");
                        msr(SYS_IMP_APL_PMCR0, pmcr0_value);
                        sysop("isb");
                    }
                printf("HV PMUv3 Redirect (OK): msr PMOVSCLR_EL0, x%ld = 0x%lx\n", rt, regs[rt]);
                }
            }
            return true;
        case SYSREG_ISS(SYS_PMOVSSET_EL0):
            if(is_read) {
                //
                // Read the state of the PMSR register to see if a PMU has overflowed.
                // Cycle counter only for now.
                //
                u64 pmsr_value = mrs(SYS_IMP_APL_PMSR);
                u64 calculated_value = 0;
                u64 pmu_overflowed_mask = GENMASK(9, 0);
                if((pmsr_value & pmu_overflowed_mask) != 0) {
                    //
                    // bit 0 is for PMC 0
                    //
                    if((pmsr_value & BIT(0)) != 0) {
                        calculated_value |= BIT(31);
                    }
                }
                printf("HV PMUv3 Redirect: mrs x%ld, PMOVSSET_EL0 = 0x%lx\n", rt, calculated_value);
                regs[rt] = calculated_value;
            }
            else {
                //
                // For now, don't set the overflow bit. If this needs to change, reuse the code from before.
                //
            }
            return true;
        case SYSREG_ISS(SYS_PMSELR_EL0):
            //for now hardcode to set the cycle counter, this will very likely need to change
            if(is_read) {
                regs[rt] = 31;
                printf("HV PMUv3 Redirect: mrs x%ld, PMSELR_EL0 = 0x%lx\n", rt, regs[rt]);
            }
            else {
                printf("HV PMUv3 Redirect (skipped write): msr PMSELR_EL0, x%ld = 0x%lx\n", rt, regs[rt]);
            }
            return true;
        //SYSREG_MAP(SYS_PMSWINC_EL0, SYS_IMP_APL_PMC3)
        case SYSREG_ISS(SYS_PMUSERENR_EL0):
            if(is_read) {
                regs[rt] = 0;
                printf("HV PMUv3 Redirect: mrs x%ld, PMUSERENR_EL0 = 0x%lx\n", rt, regs[rt]);
            }
            else {
                printf("HV PMUv3 Redirect (skipped write): msr PMUSERENR_EL0, x%ld = 0x%lx\n", rt, regs[rt]);
            }
           return true;
        // SYSREG_MAP(SYS_PMXEVCNTR_EL0, SYS_IMP_APL_PMC2)
        // case SYSREG_ISS(SYS_PMXEVTYPER_EL0):
        //     if(is_read) {
        //         printf("mrs(PMXEVTYPER_EL0)\n");
        //         regs[rt] = 0;
        //         int value = mrs(SYS_IMP_APL_PMCR1);
        //         if(value & GENMASK(23, 16)) {
        //             regs[rt] |= BIT(31); //privileged bit
        //         }
        //         if(value & GENMASK(15, 8)) {
        //             regs[rt] |= BIT(30); //user filter bit
        //         }
        //         regs[rt] |= (mrs(SYS_IMP_APL_PMESR0) & GENMASK(7, 0));
        //     }
        //     else {
        //         int val = mrs(SYS_IMP_APL_PMCR1);
        //         int event = mrs(SYS_IMP_APL_PMESR0) & GENMASK(7, 0);
        //         if(regs[rt] & PMEVTYPER_P) {
        //             printf("msr(PMEVTYPER0_EL0, 0x%08lx): enabling el1 counting of event\n", regs[rt]);
        //             val |= BIT(16);
        //         }
        //         if(regs[rt] & GENMASK(7, 0)) {
        //             printf("msr(PMEVTYPER0_EL0, 0x%08lx): setting event\n", regs[rt]);
        //             event |= regs[rt] & GENMASK(7, 0);
        //             msr(SYS_IMP_APL_PMESR0, event);
        //         }
        //         msr(SYS_IMP_APL_PMCR1, val);
        //     }
        //     return true;

        /* Outer Sharable TLB maintenance instructions */
        SYSREG_PASS(sys_reg(1, 0, 8, 1, 0)) // TLBI VMALLE1OS
        SYSREG_PASS(sys_reg(1, 0, 8, 1, 1)) // TLBI VAE1OS
        SYSREG_PASS(sys_reg(1, 0, 8, 1, 2)) // TLBI ASIDE1OS
        SYSREG_PASS(sys_reg(1, 0, 8, 5, 1)) // TLBI RVAE1OS

        case SYSREG_ISS(SYS_ACTLR_EL1):
            if (is_read) {
                if (cpu_features->actlr_el2)
                    regs[rt] = mrs(SYS_ACTLR_EL12);
                else
                    regs[rt] = mrs(SYS_IMP_APL_ACTLR_EL12);
            } else {
                if (cpu_features->actlr_el2)
                    msr(SYS_ACTLR_EL12, regs[rt]);
                else
                    msr(SYS_IMP_APL_ACTLR_EL12, regs[rt]);
            }
            return true;

        case SYSREG_ISS(SYS_IMP_APL_IPI_SR_EL1):
            if (is_read)
                regs[rt] = PERCPU(ipi_pending) ? IPI_SR_PENDING : 0;
            else if (regs[rt] & IPI_SR_PENDING)
                PERCPU(ipi_pending) = false;
            return true;

        /* shadow the interrupt mode and state flag */
        case SYSREG_ISS(SYS_IMP_APL_PMCR0):
            if (is_read) {
                u64 val = (mrs(SYS_IMP_APL_PMCR0) & ~PMCR0_IMODE_MASK) | PERCPU(pmc_irq_mode);
                regs[rt] = val | (PERCPU(pmc_pending) ? PMCR0_IACT : 0);
            } else {
                PERCPU(pmc_pending) = !!(regs[rt] & PMCR0_IACT);
                PERCPU(pmc_irq_mode) = regs[rt] & PMCR0_IMODE_MASK;
                msr(SYS_IMP_APL_PMCR0, regs[rt]);
            }
            return true;

        /*
         * Handle this one here because m1n1/Linux (will) use it for explicit cpuidle.
         * We can pass it through; going into deep sleep doesn't break the HV since we
         * don't do any wfis that assume otherwise in m1n1. However, don't het macOS
         * disable WFI ret (when going into systemwide sleep), since that breaks things.
         */
        case SYSREG_ISS(SYS_IMP_APL_CYC_OVRD):
            if (is_read) {
                regs[rt] = mrs(SYS_IMP_APL_CYC_OVRD);
            } else {
                if (regs[rt] & (CYC_OVRD_DISABLE_WFI_RET | CYC_OVRD_FIQ_MODE_MASK))
                    return false;
                msr(SYS_IMP_APL_CYC_OVRD, regs[rt]);
            }
            return true;
            /* clang-format off */
        /* IPI handling */
        SYSREG_PASS(SYS_IMP_APL_IPI_CR_EL1)
        /* M1RACLES reg, handle here due to silly 12.0 "mitigation" */
        case SYSREG_ISS(sys_reg(3, 5, 15, 10, 1)):
            if (is_read)
                regs[rt] = 0;
            return true;
    }
    return false;
}

static bool hv_handle_smc(struct exc_info *ctx) {
    printf("PSCI SMC DEBUG: handling PSCI request 0x%lx\n", ctx->regs[0]);
    bool handled_smc = hv_handle_psci_smc(ctx);
    return handled_smc;
}

static bool hv_handle_msr(struct exc_info *ctx, u64 iss)
{
    u64 reg = iss & (ESR_ISS_MSR_OP0 | ESR_ISS_MSR_OP2 | ESR_ISS_MSR_OP1 | ESR_ISS_MSR_CRn |
                     ESR_ISS_MSR_CRm);
    u64 rt = FIELD_GET(ESR_ISS_MSR_Rt, iss);
    bool is_read = iss & ESR_ISS_MSR_DIR;

    u64 *regs = ctx->regs;

    regs[31] = 0;

    switch (reg) {
        /* clang-format on */
        case SYSREG_ISS(SYS_IMP_APL_IPI_RR_LOCAL_EL1): {
            assert(!is_read);
            u64 mpidr = (regs[rt] & 0xff) | (mrs(MPIDR_EL1) & 0xffff00);
            for (int i = 0; i < MAX_CPUS; i++)
                if (mpidr == smp_get_mpidr(i)) {
                    pcpu[i].ipi_queued = true;
                    msr(SYS_IMP_APL_IPI_RR_LOCAL_EL1, regs[rt]);
                    return true;
                }
            return false;
        }
        case SYSREG_ISS(SYS_IMP_APL_IPI_RR_GLOBAL_EL1):
            assert(!is_read);
            u64 mpidr = (regs[rt] & 0xff) | ((regs[rt] & 0xff0000) >> 8);
            for (int i = 0; i < MAX_CPUS; i++) {
                if (mpidr == (smp_get_mpidr(i) & 0xffff)) {
                    pcpu[i].ipi_queued = true;
                    msr(SYS_IMP_APL_IPI_RR_GLOBAL_EL1, regs[rt]);
                    return true;
                }
            }
            return false;
#ifdef DEBUG_PMU_IRQ
        case SYSREG_ISS(SYS_IMP_APL_PMC0):
            if (is_read) {
                regs[rt] = mrs(SYS_IMP_APL_PMC0);
            } else {
                msr(SYS_IMP_APL_PMC0, regs[rt]);
                printf("msr(SYS_IMP_APL_PMC0, 0x%04lx_%08lx)\n", regs[rt] >> 32,
                       regs[rt] & 0xFFFFFFFF);
            }
            return true;
#endif
    }

    return false;
}

static void hv_get_context(struct exc_info *ctx)
{
    ctx->spsr = hv_get_spsr();
    ctx->elr = hv_get_elr();
    ctx->esr = hv_get_esr();
    ctx->far = hv_get_far();
    ctx->afsr1 = hv_get_afsr1();
    ctx->sp[0] = mrs(SP_EL0);
    ctx->sp[1] = mrs(SP_EL1);
    ctx->sp[2] = (u64)ctx;
    ctx->cpu_id = smp_id();
    ctx->mpidr = mrs(MPIDR_EL1);

    sysop("isb");
}

static void hv_exc_entry(void)
{
    // Enable SErrors in the HV, but only if not already pending
    if (!(mrs(ISR_EL1) & 0x100))
        sysop("msr daifclr, 4");

    __atomic_and_fetch(&hv_cpus_in_guest, ~BIT(smp_id()), __ATOMIC_ACQUIRE);
    spin_lock(&bhl);
    hv_wdt_breadcrumb('X');
    exc_entry_time = mrs(CNTPCT_EL0);
    /* disable PMU counters in the hypervisor */
    u64 pmcr0 = mrs(SYS_IMP_APL_PMCR0);
    PERCPU(exc_entry_pmcr0_cnt) = pmcr0 & PMCR0_CNT_MASK;
    msr(SYS_IMP_APL_PMCR0, pmcr0 & ~PMCR0_CNT_MASK);
}

static void hv_exc_exit(struct exc_info *ctx)
{
    hv_wdt_breadcrumb('x');
    hv_update_fiq();
    /* reenable PMU counters */
    reg_set(SYS_IMP_APL_PMCR0, PERCPU(exc_entry_pmcr0_cnt));
    msr(CNTVOFF_EL2, stolen_time);
    spin_unlock(&bhl);
    hv_maybe_exit();
    __atomic_or_fetch(&hv_cpus_in_guest, BIT(smp_id()), __ATOMIC_ACQUIRE);

    hv_set_spsr(ctx->spsr);
    hv_set_elr(ctx->elr);
    msr(SP_EL0, ctx->sp[0]);
    msr(SP_EL1, ctx->sp[1]);
}

void hv_exc_sync(struct exc_info *ctx)
{
    hv_wdt_breadcrumb('S');
    hv_get_context(ctx);
    bool handled = false;
    u32 ec = FIELD_GET(ESR_EC, ctx->esr);

    switch (ec) {
        case ESR_EC_MSR:
            hv_wdt_breadcrumb('m');
            handled = hv_handle_msr_unlocked(ctx, FIELD_GET(ESR_ISS, ctx->esr));
            break;
        //
        // for Blizzard/Avalanche and later - we need to explicitly check for SMC EC to handle SMCs
        //
        case ESR_EC_SMC:
            hv_wdt_breadcrumb('s');
            handled = hv_handle_smc(ctx);
            break;
        case ESR_EC_IMPDEF:
            hv_wdt_breadcrumb('a');
            if(ctx->afsr1 == 0x1c00000) {
                /**
                 * m1n1_windows change: add SMC handling support.
                 * 
                 * right now the only reason a guest OS would fire an SMC is due to 
                 * requesting a PSCI service.
                */
                handled = hv_handle_smc(ctx);
                break;
            }
            switch (FIELD_GET(ESR_ISS, ctx->esr)) {
                case ESR_ISS_IMPDEF_MSR:
                    handled = hv_handle_msr_unlocked(ctx, ctx->afsr1);
                    break;
            }
            break;
    }

    if (handled) {
        hv_wdt_breadcrumb('#');
        ctx->elr += 4;
        hv_set_elr(ctx->elr);
        hv_update_fiq();
        hv_wdt_breadcrumb('s');
        return;
    }

    hv_exc_entry();

    switch (ec) {
        case ESR_EC_DABORT_LOWER:
            hv_wdt_breadcrumb('D');
            handled = hv_handle_dabort(ctx);
            break;
        case ESR_EC_MSR:
            hv_wdt_breadcrumb('M');
            handled = hv_handle_msr(ctx, FIELD_GET(ESR_ISS, ctx->esr));
            break;
        case ESR_EC_IMPDEF:
            hv_wdt_breadcrumb('A');
            switch (FIELD_GET(ESR_ISS, ctx->esr)) {
                case ESR_ISS_IMPDEF_MSR:
                    handled = hv_handle_msr(ctx, ctx->afsr1);
                    break;
            }
            break;
    }

    if (handled) {
        hv_wdt_breadcrumb('+');
        ctx->elr += 4;
    } else {
        hv_wdt_breadcrumb('-');
        // VM code can forward a nested SError exception here
        if (FIELD_GET(ESR_EC, ctx->esr) == ESR_EC_SERROR)
            hv_exc_proxy(ctx, START_EXCEPTION_LOWER, EXC_SERROR, NULL);
        else
            hv_exc_proxy(ctx, START_EXCEPTION_LOWER, EXC_SYNC, NULL);
    }

    hv_exc_exit(ctx);
    hv_wdt_breadcrumb('s');
}

void hv_exc_irq(struct exc_info *ctx)
{
    hv_wdt_breadcrumb('I');
    hv_get_context(ctx);
    hv_exc_entry();
    hv_exc_proxy(ctx, START_EXCEPTION_LOWER, EXC_IRQ, NULL);
    hv_exc_exit(ctx);
    hv_wdt_breadcrumb('i');
}

void hv_exc_fiq(struct exc_info *ctx)
{
    bool tick = false;

    hv_maybe_exit();

    //
    // TODO: inject the FIQ to the guest as an IRQ if vGIC is enabled.
    //

    if (mrs(CNTP_CTL_EL0) == (CNTx_CTL_ISTATUS | CNTx_CTL_ENABLE)) {
        msr(CNTP_CTL_EL0, CNTx_CTL_ISTATUS | CNTx_CTL_IMASK | CNTx_CTL_ENABLE);
        tick = true;
    }

    int interruptible_cpu = hv_pinned_cpu;
    if (interruptible_cpu == -1)
        interruptible_cpu = boot_cpu_idx;

    if (smp_id() != interruptible_cpu && !(mrs(ISR_EL1) & 0x40) && hv_want_cpu == -1) {
        // Non-interruptible CPU and it was just a timer tick (or spurious), so just update FIQs
        hv_update_fiq();
        hv_arm_tick(true);
        return;
    }

    // Slow (single threaded) path
    hv_wdt_breadcrumb('F');
    hv_get_context(ctx);
    hv_exc_entry();

    // Only poll for HV events in the interruptible CPU
    if (tick) {
        if (smp_id() == interruptible_cpu) {
            hv_tick(ctx);
            hv_arm_tick(false);
        } else {
            hv_arm_tick(true);
        }
    }

    if (mrs(CNTV_CTL_EL0) == (CNTx_CTL_ISTATUS | CNTx_CTL_ENABLE)) {
        msr(CNTV_CTL_EL0, CNTx_CTL_ISTATUS | CNTx_CTL_IMASK | CNTx_CTL_ENABLE);
        hv_exc_proxy(ctx, START_HV, HV_VTIMER, NULL);
    }

    u64 reg = mrs(SYS_IMP_APL_PMCR0);
    if ((reg & (PMCR0_IMODE_MASK | PMCR0_IACT)) == (PMCR0_IMODE_FIQ | PMCR0_IACT)) {
#ifdef DEBUG_PMU_IRQ
        printf("[FIQ] PMC IRQ, masking and delivering to the guest\n");
#endif
        reg_clr(SYS_IMP_APL_PMCR0, PMCR0_IACT | PMCR0_IMODE_MASK);
        PERCPU(pmc_pending) = true;
    }

    reg = mrs(SYS_IMP_APL_UPMCR0);
    if (FIELD_GET(UPMCR0_IMODE_T8020, reg) == UPMCR0_IMODE_FIQ &&
        (mrs(SYS_IMP_APL_UPMSR) & UPMSR_IACT)) {
        printf("[FIQ] UPMC IRQ, masking");
        reg_clr(SYS_IMP_APL_UPMCR0, UPMCR0_IMODE_T8020);
        hv_exc_proxy(ctx, START_EXCEPTION_LOWER, EXC_FIQ, NULL);
    }

    if (mrs(SYS_IMP_APL_IPI_SR_EL1) & IPI_SR_PENDING) {
        if (PERCPU(ipi_queued)) {
            PERCPU(ipi_pending) = true;
            PERCPU(ipi_queued) = false;
        }
        msr(SYS_IMP_APL_IPI_SR_EL1, IPI_SR_PENDING);
        sysop("isb");
    }

    hv_maybe_switch_cpu(ctx, START_HV, HV_CPU_SWITCH, NULL);

    // Handles guest timers
    hv_exc_exit(ctx);
    hv_wdt_breadcrumb('f');
}

void hv_exc_serr(struct exc_info *ctx)
{
    hv_wdt_breadcrumb('E');
    hv_get_context(ctx);
    hv_exc_entry();
    hv_exc_proxy(ctx, START_EXCEPTION_LOWER, EXC_SERROR, NULL);
    hv_exc_exit(ctx);
    hv_wdt_breadcrumb('e');
}
