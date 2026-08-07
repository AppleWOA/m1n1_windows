/**
 * Copyright (c) 2025, NTASP authors.
 * 
 * Module Name:
 *     hv_vgic.c
 * 
 * Abstract:
 *     The vGIC virtual device code for the m1n1 hypervisor.
 * 
 * 
 * Environment:
 *     m1n1 in hypervisor mode.
 * 
 * License:
 *     SPDX-License-Identifier: (BSD-2-Clause-Patent OR MIT)
 * 
 *     Inspiration borrowed from the KVM vGIC driver in the Linux source tree. Original copyright notice below.
 *     
*/

#include "hv.h"
#include "hv_vgic.h"
#include "assert.h"
#include "cpu_regs.h"
#include "display.h"
#include "memory.h"
#include "pcie.h"
#include "smp.h"
#include "string.h"
#include "usb.h"
#include "utils.h"
#include "aic.h"
#include "malloc.h"
#include "heapblock.h"
#include "smp.h"
#include "string.h"
#include "types.h"
#include "uartproxy.h"

/**
 * General idea of how this should work:
 * 
 * Apple Silicon chips since the M1 implement the GIC CPU interface registers in hardware, meaning
 * only the distributor, the core specific redistributors, and (potentially) an ITS need to be emulated by m1n1.
 * 
 * As such, this file implements most of the code needed to make this possible. The emulated distributor/redistributors
 * will need to meet a few constraints (namely it's limited by what the GIC CPU interface supports)
 * 
 * Apple's vGIC CPU interface has the following characteristics (on M1 and M2):
 * - 32 levels of virtual priority and preemption priority (5 preemption/priority bits)
 * - 16 bits of virtual interrupt ID bits (meaning up to 65535 interrupts are supported theoretically, however practically limited by the number of IRQs the AIC supports)
 * - supports guest-generated SEIs upon writing to GIC registers in a bad way 
 *   (note that an errata here exists on pre-M3 SoCs that can result in a host SError - we implement special handling for this.)
 * - 3 level affinity (aff2/aff1/aff0 valid, aff3 invalid/reserved as 0)
 * - legacy operation is not supported (ICC_SRE_EL2.SRE is reserved, set to 1) (no GICv2 operations)
 * - TDIR bit is supported (FEAT_GICv3_TDIR)
 * - extended SPI and PPI ranges are not supported on the CPU interface per ICC_CTLR_EL1.ExtRange
 * - 8 list registers
 * - direct injection of virtual interrupts are not supported (not a GICv4, and by extension, no NMIs supported)
 * - IRQ/FIQ bypass are not supported
 * 
 * 
 * The mappings are different for platforms with 36-bit vs 42-bit physical addressing, with 36-bit platforms tentatively
 * having the distributor being mapped to 0xF00000000, redistributors at offset +0x10000000
 * and 42-bit platforms having the distributor at 0x5000000000, redistributors at offset +0x100000000
 * 
 * A major note about processor affinities: since AICv2 platforms don't support setting core affinities easily,
 * the tentative solution is to do routing to any virtual CPU once we receive an IRQ, we can't assume
 * that the core that got the IRQ is the one that needs to be signaled. (for FIQs, because they're core specific,
 * we'll know which core needs to be signaled in those cases.)
 * 
 * If extended SPIs are enabled via the define, *right now* the plan is to not enable trapping of the CPU
 * interface registers (this would be needed typically to make the view of the CPU interface and distributor consistent)
 * but right now we'd like to not trap these registers in vGIC mode so as to not tank the performance of the vGIC.
 * Note that if Linux is the guest, this will trip the warn in gic_cpu_init but it shouldn't be a major concern either way.
 * 
 */
#ifdef ENABLE_VGIC_MODULE
#define DIST_BASE_36_BIT 0xF00000000
#define REDIST_BASE_36_BIT 0xF10000000
#define DIST_BASE_42_BIT 0x5000000000
#define REDIST_BASE_42_BIT 0x5100000000
//
// This is tentative - depends on if direct MSIs or ITS translated IRQs end up being easier to implement.
//
#define ITS_BASE_36_BIT 0xF20000000
#define ITS_BASE_42_BIT 0x5200000000

#define ENABLE_VGIC_LOGGING 1

#if ENABLE_VGIC_LOGGING
#define vgic_log(...) printf(__VA_ARGS__)
#else
#define vgic_log(...)                                                                               \
    do {                                                                                           \
    } while (0)
#endif


vgicv3_dist *distributor;
vgicv3_vcpu_redist *redistributors;
vgicv3_its *interrupt_translation_service;
static u64 dist_base, redist_base, its_base;
static u16 num_cpus;
static bool vgic_inited;
static u64 igrpen1;


static bool handle_vgic_its_access(struct exc_info *ctx, u64 addr, u64 *val, bool write, int width)
{
    u64 relative_addr;
    bool register_handled;
    bool unimplemented_reg_accessed;
    u32 reg_num;
    relative_addr = addr - its_base;
    register_handled = false;
    unimplemented_reg_accessed = false;
    reg_num = 0;
    if(write) {
        switch(relative_addr) {
            case GITS_CTLR:
                interrupt_translation_service->its_ctl_region.gits_ctl_reg = *val;
                register_handled = true;
                break;
            case GITS_BASER0 ... GITS_BASER7:
                reg_num = (relative_addr - GITS_BASER0) / 8;
                interrupt_translation_service->its_ctl_region.gits_baser[reg_num] = *val;
                register_handled = true;
                break;
            default:
                //
                // we're dealing with a register that is banked n times, we need to get to the if statements.
                //
                break;
        }

    }
    else {
        switch(relative_addr) {
            case GITS_CTLR:
                *val = interrupt_translation_service->its_ctl_region.gits_ctl_reg;
                register_handled = true;
                break;
            case GITS_BASER0 ... GITS_BASER7:
                reg_num = (relative_addr - GITS_BASER0) / 8;
                *val = interrupt_translation_service->its_ctl_region.gits_baser[reg_num];
                register_handled = true;
                break;
            default:
                //
                // we're dealing with a register that is banked n times, we need to get to the if statements.
                //
                break;
        }
    }

    vgic_log("HV vGIC DEBUG [INFO] [ITS]: 0x%llx = 0x%llx ", relative_addr, *val);
    if(write) {
        vgic_log("[Written]");
    }
    else {
        vgic_log("[Read]");
    }
    if(unimplemented_reg_accessed) {
        vgic_log("[Unimplemented]\n");
    }
    else {
        vgic_log("\n");
    }
    return register_handled;
}


//
// Description:
//   the vGIC guest interrupt handler for distributor writes.
//
// Return values:
//   true - access has been handled successfully, even if the access itself is either bad or not permitted.
//   false - access was not handled successfully.
//
static bool handle_vgic_dist_access(struct exc_info *ctx, u64 addr, u64 *val, bool write, int width)
{
    u64 relative_addr;
    bool register_handled;
    bool unimplemented_reg_accessed;
    relative_addr = addr - dist_base;
    register_handled = false;
    unimplemented_reg_accessed = false;
    if(write) {
        //
        // The guest attempted to write a register.
        // Handle it based on what they're trying to write, and preserve the value if
        // the value is going to a RW register.
        // Emit a warning (to become an error later) if the guest is attempting to write a register that doesn't exist or is read only.
        //

        //
        // This switch statement covers all the unique one of a kind registers.
        //
        switch(relative_addr) {
            case GIC_DIST_CTLR:
                //
                // GICD_CTLR has fields that we cannot change (due to the underlying physical environment or constraints)
                // and fields we can change, so check for RO fields here first.
                //
                u32 gicd_ctlr_new_val = (u32)(*val);
                vgic_log("HV vGIC DEBUG: guest writing GICD_CTLR = 0x%x, old value 0x%x\n", gicd_ctlr_new_val, distributor->gicd_ctl_reg);
                bool is_rwp_to_be_set = false;
                if(((gicd_ctlr_new_val & GENMASK(30, 8)) != 0) || ((gicd_ctlr_new_val & BIT(5)) != 0) || ((gicd_ctlr_new_val & GENMASK(3, 2)) != 0)) {
                    //
                    // these bits are RES0 - clear out this bitmask.
                    //
                    gicd_ctlr_new_val &= ~(GENMASK(30, 8));
                    gicd_ctlr_new_val &= ~(GENMASK(3, 2));
                    gicd_ctlr_new_val &= ~(BIT(5));
                    vgic_log("HV vGIC DEBUG [WARN]: guest attempted to write RES0 bits in GICD_CTLR, discarding\n");
                }
                
                if((gicd_ctlr_new_val & BIT(6)) == 0) {
                    //
                    // the guest is trying to set DS = 0. we do not support this so ensure that bit 6 is always written,
                    // however we need to emit a warning because this means that our GIC configuration is wrong.
                    //
                    gicd_ctlr_new_val |= BIT(6);
                    vgic_log("HV vGIC DEBUG [WARN]: guest attempted to set DS = 0, discarding\n");
                }
                if((gicd_ctlr_new_val & BIT(4)) == 0) {
                    //
                    // the guest is trying to set ARE = 0. we do not support this so ensure that bit 4 is always written,
                    // however we need to emit a warning because this means that our GIC configuration is wrong.
                    //
                    gicd_ctlr_new_val |= BIT(4);
                    vgic_log("HV vGIC DEBUG [WARN]: guest attempted to set ARE = 0, discarding\n");
                }
                if((((gicd_ctlr_new_val & BIT(7)) != 0) && ((distributor->gicd_ctl_reg & BIT(7)) == 0)) 
                || (((gicd_ctlr_new_val & BIT(7)) == 0) && ((distributor->gicd_ctl_reg & BIT(7)) != 0))) {
                    //
                    // the guest is trying to set E1NWF either way. we need to know about any attempt to change this, as it affects IRQ behavior.
                    // we also need to flag that RWP needs to be set to 1.
                    //
                    is_rwp_to_be_set = true;
                    vgic_log("HV vGIC DEBUG [INFO]: guest is changing E1NWF\n");
                }
                if(((gicd_ctlr_new_val & BIT(1)) == 0) && ((distributor->gicd_ctl_reg & BIT(1)) != 0)) {
                    //
                    // the guest is trying to set EnableGrp1 = 0. we need to know about any attempt to set this, as it affects IRQ behavior.
                    // we also need to flag that RWP needs to be set to 1.
                    //
                    is_rwp_to_be_set = true;
                    vgic_log("HV vGIC DEBUG [INFO]: guest is setting EnableGrp1 = 0\n");
                }
                if(((gicd_ctlr_new_val & BIT(0)) == 0) && ((distributor->gicd_ctl_reg & BIT(0)) != 0)) {
                    //
                    // the guest is trying to set EnableGrp1 = 0. we need to know about any attempt to set this, as it affects IRQ behavior.
                    // we also need to flag that RWP needs to be set to 1.
                    //
                    is_rwp_to_be_set = true;
                    vgic_log("HV vGIC DEBUG [INFO]: guest is setting EnableGrp0 = 0\n");
                }

                //
                // RWP (Register Write Pending bit) - this bit is a tad bit special - it's RO, but it has to be set if bits 0 or 1 are transitioning
                // from 1 to 0.
                //
                if(is_rwp_to_be_set == true) {
                    //
                    // set RWP here - then start propagating the effects immediately after.
                    //
                    gicd_ctlr_new_val |= BIT(31);
                }

                distributor->gicd_ctl_reg = gicd_ctlr_new_val;
                if(is_rwp_to_be_set == true) {
                    //
                    // TODO: start the changes signaled by RWP.
                    //
                    //hv_vgicv3_apply_gic_dist_changes(gicd_ctlr_new_val);
                }
                register_handled = true;
                break;
            case GIC_DIST_TYPER:
            case GIC_DIST_TYPER2:
            case GIC_DIST_IIDR:
                //
                // these registers are totally RO, so leave their values unchanged.
                //
                vgic_log("HV vGIC DEBUG [WARN]: guest attempted to change a read-only register (0x%x), discarding\n", relative_addr);
                register_handled = true;
                break;
            case GIC_DIST_STATUSR:
                //
                // GICD_STATUSR is a bit special, software must write 1 to ack an error, which then *clears* the bit.
                // Note that [31:4] are always RES0.
                //
                u32 gicd_statusr_new_val = (u32)(*val);
                u32 gicd_statusr_current_val = distributor->gicd_err_sts;
                if((gicd_statusr_new_val & GENMASK(31, 4)) != 0) {
                    gicd_statusr_new_val &= ~(GENMASK(31, 4));
                    vgic_log("HV vGIC DEBUG [WARN]: guest attempted to write RES0 bits in GICD_STATUSR, discarding\n");
                }
                if(((gicd_statusr_new_val & BIT(3)) != 0) & ((gicd_statusr_current_val & BIT(3)) != 0)) {
                    gicd_statusr_current_val &= ~(BIT(3));
                    vgic_log("HV vGIC DEBUG [INFO]: clearing WROD bit in GICD_STATUSR\n");
                }
                if(((gicd_statusr_new_val & BIT(2)) != 0) & ((gicd_statusr_current_val & BIT(2)) != 0)) {
                    gicd_statusr_current_val &= ~(BIT(2));
                    vgic_log("HV vGIC DEBUG [INFO]: clearing RWOD bit in GICD_STATUSR\n");
                }
                if(((gicd_statusr_new_val & BIT(1)) != 0) & ((gicd_statusr_current_val & BIT(1)) != 0)) {
                    gicd_statusr_current_val &= ~(BIT(1));
                    vgic_log("HV vGIC DEBUG [INFO]: clearing WRD bit in GICD_STATUSR\n");
                }
                if(((gicd_statusr_new_val & BIT(0)) != 0) & ((gicd_statusr_current_val & BIT(0)) != 0)) {
                    gicd_statusr_current_val &= ~(BIT(0));
                    vgic_log("HV vGIC DEBUG [INFO]: clearing RRD bit in GICD_STATUSR\n");
                }
                distributor->gicd_err_sts = gicd_statusr_current_val;
                register_handled = true;
                break;
            //
            // right now, MBIS is disabled - so these four registers are reserved.
            //
            case GIC_DIST_SETSPI_NSR:
            case GIC_DIST_CLRSPI_NSR:
            case GIC_DIST_CLRSPI_SR:
            case GIC_DIST_SETSPI_SR:
                register_handled = true;
                break;
            
            case GIC_DIST_SGIR:
                //
                // This register is reserved too, since affinity routing is always enabled.
                //
                register_handled = true;
                break;

            case GIC_DIST_IROUTER32 ... GIC_DIST_IROUTER1019:
                u32 reg_num;
                u64 mpidr = 0;
                u32 cpu_num;
                reg_num = (relative_addr - GIC_DIST_IROUTER32) / 8;
                distributor->gicd_interrupt_router_regs[reg_num] = *val;
                
                mpidr |= (u64)MPIDR_AFF0(*val);
                mpidr |= (u64)MPIDR_AFF1(*val) << 8;
                mpidr |= (u64)MPIDR_AFF2(*val) << 16;
                mpidr |= (u64)MPIDR_AFF3(*val) << 32;
                cpu_num = smp_get_id(mpidr);

                aic_set_affinity(reg_num + 32, cpu_num);
                vgic_log("HV vGIC DEBUG [INFO] [Distributor]: interrupt routing register %d = %d\n", reg_num, cpu_num);
                register_handled = true;
                break;
#ifdef EXTENDED_SPI_ENABLE
            case GIC_DIST_IROUTER0E ... GIC_DIST_IROUTER1023E:
                u32 reg_num;
                u64 mpidr = 0;
                u32 cpu_num;
                reg_num = (relative_addr - GIC_DIST_IROUTER0E) / 8;
                distributor->gicd_interrupt_router_ext_spi_range_regs[reg_num] = *val;
                
                mpidr |= (u64)MPIDR_AFF0(*val);
                mpidr |= (u64)MPIDR_AFF1(*val) << 8;
                mpidr |= (u64)MPIDR_AFF2(*val) << 16;
                mpidr |= (u64)MPIDR_AFF3(*val) << 32;
                cpu_num = smp_get_id(mpidr);

                aic_set_affinity(reg_num + 32, cpu_num);
                vgic_log("HV vGIC DEBUG [INFO] [Distributor]: interrupt routing register (extended-SPI range) %d = %d\n", reg_num, cpu_num);
                register_handled = true;
                break;
#endif
            default:
                //
                // we're dealing with a register that is banked n times, we need to get to the if statements.
                //
                break;
        }

        //
        // Fair warning this code is probably dicey...
        //
        if((register_handled == false) && (relative_addr >= GIC_DIST_IGROUPR0) && (relative_addr <= GIC_DIST_IGROUPR31) ) {
            //
            // the guest is trying to change the group of a given interrupt.
            //
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_IGROUPR0) / 4;

            //
            // TODO: bank GICD_IGROUPR0 for cores 0-7 - GIC spec requires it - but since we're booting with 1 core atm, we can ignore
            // this for now.
            //

            distributor->gicd_interrupt_group_regs[reg_num] = *val;
            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ISENABLER0) && (relative_addr <= GIC_DIST_ISENABLER31) ) {
            //
            // enables an IRQ to be forwarded to a CPU interface.
            //
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_ISENABLER0) / 4;
            u32 value_is_enabler, value_ic_enabler, current_val;
            u32 irq_num;
            value_is_enabler = distributor->gicd_interrupt_set_enable_regs[reg_num];
            value_ic_enabler = distributor->gicd_interrupt_clear_enable_regs[reg_num];
            current_val = *val;

            //
            // if 1 is written to the bits in these registers, they need to read 1 in GICD_ICENABLER[0:31] as well.
            // also this is banked for the first 8 processor cores - so changes must reflect across all of them.
            //
            // There has to be a way more efficient way of doing this...
            //

            for(u32 i = 0; i < 32; i++) {
                if( ( (current_val & BIT(i)) != 0 ) && ( ( value_is_enabler & BIT(i) ) == 0) ) {
                    value_is_enabler |= BIT(i);
                    value_ic_enabler |= BIT(i);      
                    irq_num = (32 * reg_num) + i;

                    aic_set_mask(irq_num, false);
                    vgic_log("HV vGIC DEBUG [Info] [AIC]: unmasking irq %d\n", irq_num);
                }
            }
            if(reg_num == 0) {

            }
            else {
                distributor->gicd_interrupt_set_enable_regs[reg_num] = value_is_enabler;
                distributor->gicd_interrupt_clear_enable_regs[reg_num] = value_ic_enabler;
            }

            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ICENABLER0) && (relative_addr <= GIC_DIST_ICENABLER31) ) {
            //
            // disables an IRQ to be forwarded to a CPU interface.
            //
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_ICENABLER0) / 4;
            u32 irq_num;
            u32 value_is_enabler, value_ic_enabler, current_val;
            value_is_enabler = distributor->gicd_interrupt_set_enable_regs[reg_num];
            value_ic_enabler = distributor->gicd_interrupt_clear_enable_regs[reg_num];
            current_val = *val;

            //
            // if 1 is written to the bits in these registers, they need to read 0 in GICD_ISENABLER[0:31] as well.
            // also this is banked for the first 8 processor cores - so changes must reflect across all of them.
            //
            // There has to be a way more efficient way of doing this...
            //
            for(u32 i = 0; i < 32; i++) {
                if( ( (current_val & BIT(i)) != 0 ) && ( ( value_ic_enabler & BIT(i) ) != 0) ) {
                    value_is_enabler &= ~BIT(i);
                    value_ic_enabler &= ~BIT(i);      
                    irq_num = (32 * reg_num) + i;

                    aic_set_mask(irq_num, true);
                    vgic_log("HV vGIC DEBUG [Info] [AIC]: masking irq %d\n", irq_num);
                }
            }
            if(reg_num == 0) {

            }
            else {
                distributor->gicd_interrupt_set_enable_regs[reg_num] = value_is_enabler;
                distributor->gicd_interrupt_clear_enable_regs[reg_num] = value_ic_enabler;
            }
            register_handled = true;
            //
            // ICENABLER register writes require RWP dependent things to be updated, set the bit.
            //
            distributor->gicd_ctl_reg |= BIT(31);
            //
            // TODO: propagate the changes
            //

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ISPENDR0) && (relative_addr <= GIC_DIST_ISPENDR31) ) {
            //
            // sets an IRQ to pending
            //
            u32 reg_num, irq_num;
            reg_num = (relative_addr - GIC_DIST_ISPENDR0) / 4;
            u32 value_is_enabler, value_ic_enabler, current_val;
            value_is_enabler = distributor->gicd_interrupt_set_pending_regs[reg_num];
            value_ic_enabler = distributor->gicd_interrupt_clear_pending_regs[reg_num];
            current_val = *val;

            //
            // if 1 is written to the bits in these registers, they need to read 1 in GICD_ICENABLER[1:31] as well.
            // also this is banked for the first 8 processor cores - so changes must reflect across all of them.
            //
            // There has to be a way more efficient way of doing this...
            //

            for (u32 i = 0; i < 32; i++) {
                if( ( (current_val & BIT(i)) != 0 ) && ( ( value_is_enabler & BIT(i) ) == 0) ) {
                    value_is_enabler |= BIT(i);
                    value_ic_enabler |= BIT(i);
                    irq_num = (32 * reg_num) + i;
                    //
                    // TODO: do this
                    //
                    vgic_log("HV vGIC DEBUG [ERROR]: ISPENDR not implemented for irq %d\n", irq_num);
                }
            }
            if(reg_num == 0) {
                //
                // don't attempt to write these registers, since affinity routing is always on.
                //
            }
            else {
                distributor->gicd_interrupt_set_pending_regs[reg_num] = value_is_enabler;
                distributor->gicd_interrupt_clear_pending_regs[reg_num] = value_ic_enabler;
            }
            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ICPENDR0) && (relative_addr <= GIC_DIST_ICPENDR31) ) {
            //
            // clears the pending state from an IRQ
            //
            u32 reg_num, irq_num;
            reg_num = (relative_addr - GIC_DIST_ICPENDR0) / 4;
            u32 value_is_enabler, value_ic_enabler, current_val;
            value_is_enabler = distributor->gicd_interrupt_set_pending_regs[reg_num];
            value_ic_enabler = distributor->gicd_interrupt_clear_pending_regs[reg_num];
            current_val = *val;

            //
            // if 1 is written to the bits in these registers, they need to read 0 in GICD_ISENABLER[0:31] as well.
            // also this is banked for the first 8 processor cores - so changes must reflect across all of them.
            //
            // There has to be a way more efficient way of doing this...
            //
            for (u32 i = 0; i < 32; i++) {
                if( ( (current_val & BIT(i)) != 0 ) && ( ( value_ic_enabler & BIT(i) ) != 0) ) {
                    value_is_enabler &= ~BIT(i);
                    value_ic_enabler &= ~BIT(i);
                    irq_num = (32 * reg_num) + i;
                    //
                    // TODO: do this
                    //
                    vgic_log("HV vGIC DEBUG [ERROR]: ICPENDR not implemented for irq %d\n", irq_num);
                }  
            }
            if(reg_num == 0) {

            }
            else {
                distributor->gicd_interrupt_set_pending_regs[reg_num] = value_is_enabler;
                distributor->gicd_interrupt_clear_pending_regs[reg_num] = value_ic_enabler;
            }
            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ISACTIVER0) && (relative_addr <= GIC_DIST_ISACTIVER31) ) {
            //
            // clears the pending state from an IRQ
            //
            u32 reg_num, irq_num;
            reg_num = (relative_addr - GIC_DIST_ISACTIVER0) / 4;
            u32 value_is_enabler, value_ic_enabler, current_val;
            value_is_enabler = distributor->gicd_interrupt_set_active_regs[reg_num];
            value_ic_enabler = distributor->gicd_interrupt_clear_active_regs[reg_num];
            current_val = *val;

            //
            // if 1 is written to the bits in these registers, they need to read 0 in GICD_ISACTIVER[0:31] as well.
            // also this is banked for the first 8 processor cores - so changes must reflect across all of them.
            //
            // There has to be a way more efficient way of doing this...
            //
            for (u32 i = 0; i < 32; i++) {
                if( ( (current_val & BIT(i)) != 0 ) && ( ( value_ic_enabler & BIT(i) ) == 0) ) {
                    value_is_enabler &= ~BIT(i);
                    value_ic_enabler &= ~BIT(i);
                    irq_num = (32 * reg_num) + i;
                    //
                    // TODO: do this
                    //
                    vgic_log("HV vGIC DEBUG [ERROR]: ISACTIVER not implemented for irq %d\n", irq_num);
                }  
            }
            if(reg_num == 0) {

            }
            else {
                distributor->gicd_interrupt_set_active_regs[reg_num] = value_is_enabler;
                distributor->gicd_interrupt_clear_active_regs[reg_num] = value_ic_enabler;
            }
            register_handled = true;
        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ICACTIVER0) && (relative_addr <= GIC_DIST_ICACTIVER31) ) {
            //
            // clears the pending state from an IRQ
            //
            u32 reg_num, irq_num;
            reg_num = (relative_addr - GIC_DIST_ICACTIVER0) / 4;
            u32 value_is_enabler, value_ic_enabler, current_val;
            value_is_enabler = distributor->gicd_interrupt_set_active_regs[reg_num];
            value_ic_enabler = distributor->gicd_interrupt_clear_active_regs[reg_num];
            current_val = *val;

            //
            // if 1 is written to the bits in these registers, they need to read 0 in GICD_ISACTIVER[0:31] as well.
            // also this is banked for the first 8 processor cores - so changes must reflect across all of them.
            //
            // There has to be a way more efficient way of doing this...
            //
            for (u32 i = 0; i < 32; i++) {
                if( ( (current_val & BIT(i)) != 0 ) && ( ( value_ic_enabler & BIT(i) ) != 0) ) {
                    value_is_enabler &= ~BIT(i);
                    value_ic_enabler &= ~BIT(i);
                    irq_num = (32 * reg_num) + i;
                    //
                    // TODO: do this
                    //
                    vgic_log("HV vGIC DEBUG [ERROR]: ICACTIVER not implemented for irq %d\n", irq_num);
                }  
            }
            if(reg_num == 0) {

            }
            else {
                distributor->gicd_interrupt_set_active_regs[reg_num] = value_is_enabler;
                distributor->gicd_interrupt_clear_active_regs[reg_num] = value_ic_enabler;
            }
            register_handled = true;
        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_IPRIORITYR0) && (relative_addr <= GIC_DIST_IPRIORITYR254) ) {
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_IPRIORITYR0) / 4;
            distributor->gicd_interrupt_priority_regs[reg_num] = *val;
            vgic_log("HV vGIC DEBUG [INFO] [Distributor]: interrupt priority register %d = 0x%llx\n", reg_num, *val);
            register_handled = true;
            //unimplemented_reg_accessed = true;
        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ITARGETSR0) && (relative_addr <= GIC_DIST_ITARGETSR254) ) {
            //
            // These are RES0 - since affinity routing is always enabled on Apple platforms.
            //
            vgic_log("HV vGIC DEBUG [WARN]: GICD_ITARGETS registers are RES0 - discarding write\n");
            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ICFGR0) && (relative_addr <= GIC_DIST_ICFGR63) ) {
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_ICFGR0) / 4;
            //
            // Unimplemented for now (we only support the timer interrupt right now - and those are managed by the redistributors)
            //
            distributor->gicd_interrupt_config_regs[reg_num] = *val;
            vgic_log("HV vGIC DEBUG [INFO] [Distributor]: interrupt configuration register %d = 0x%llx\n", reg_num, *val);
            register_handled = true;
            //unimplemented_reg_accessed = true;
        }
#ifdef EXTENDED_SPI_ENABLE
        //
        // For the extended SPI ranges, because architecturally these interrupts start from 4096, it is not possible to entirely preserve
        // the one-to-one mapping rule we had for AIC-GIC translation.
        // If extended SPIs are enabled, AIC IRQs 0-1019 remain mapped one to one, while AIC IRQ 1020 and onwards map onto
        // GIC IRQs 4096-5119
        //
        else if((register_handled == false) && (relative_addr >= GIC_DIST_IGROUPR0E) && (relative_addr <= GIC_DIST_IGROUPR31E) ) {
            //
            // the guest is trying to change the group of a given interrupt in the extended-SPI range.
            //
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_IGROUPR0E) / 4;

            //
            // IGROUPR0E-IGROUPR7E are not banked, unlike their non-E equivalents.
            //

            distributor->gicd_interrupt_group_regs_ext_spi_range[reg_num] = *val;
            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ISENABLER0E) && (relative_addr <= GIC_DIST_ISENABLER31E) ) {
            //
            // enables an IRQ to be forwarded to a CPU interface.
            //
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_ISENABLER0E) / 4;
            u32 value_is_enabler, value_ic_enabler, current_val;
            u32 irq_num;
            value_is_enabler = distributor->gicd_interrupt_set_enable_ext_spi_range_regs[reg_num];
            value_ic_enabler = distributor->gicd_interrupt_clear_enable_ext_spi_range_regs[reg_num];
            current_val = *val;

            //
            // if 1 is written to the bits in these registers, they need to read 1 in GICD_ISENABLER[0:31]E as well.
            //
            // There has to be a way more efficient way of doing this...
            //

            for(u32 i = 0; i < 32; i++) {
                if( ( (current_val & BIT(i)) != 0 ) && ( ( value_is_enabler & BIT(i) ) == 0) ) {
                    value_is_enabler |= BIT(i);
                    value_ic_enabler |= BIT(i);      
                    irq_num = ((32 * reg_num) + 4096) + i;

                    aic_set_mask((irq_num-3076), false);
                    vgic_log("HV vGIC DEBUG [Info] [AIC]: unmasking irq %d (maps to GIC IRQ %d)\n", (irq_num-3076), irq_num);
                }
            }
            if(reg_num == 0) {

            }
            else {
                distributor->gicd_interrupt_set_enable_ext_spi_range_regs[reg_num] = value_is_enabler;
                distributor->gicd_interrupt_clear_enable_ext_spi_range_regs[reg_num] = value_ic_enabler;
            }

            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ICENABLER0E) && (relative_addr <= GIC_DIST_ICENABLER31E) ) {
            //
            // disables an IRQ to be forwarded to a CPU interface.
            //
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_ICENABLER0E) / 4;
            u32 irq_num;
            u32 value_is_enabler, value_ic_enabler, current_val;
            value_is_enabler = distributor->gicd_interrupt_set_enable_ext_spi_range_regs[reg_num];
            value_ic_enabler = distributor->gicd_interrupt_clear_enable_ext_spi_range_regs[reg_num];
            current_val = *val;

            //
            // if 1 is written to the bits in these registers, they need to read 0 in GICD_ICENABLER[0:31]E as well.
            //
            // There has to be a way more efficient way of doing this...
            //
            for(u32 i = 0; i < 32; i++) {
                if( ( (current_val & BIT(i)) != 0 ) && ( ( value_ic_enabler & BIT(i) ) != 0) ) {
                    value_is_enabler &= ~BIT(i);
                    value_ic_enabler &= ~BIT(i);      
                    irq_num = ((32 * reg_num) + 4096) + i;

                    aic_set_mask((irq_num-3076), true);
                    vgic_log("HV vGIC DEBUG [Info] [AIC]: unmasking irq %d (maps to GIC IRQ %d)\n", (irq_num-3076), irq_num);
                }
            }
            if(reg_num == 0) {

            }
            else {
                distributor->gicd_interrupt_set_enable_regs[reg_num] = value_is_enabler;
                distributor->gicd_interrupt_clear_enable_regs[reg_num] = value_ic_enabler;
            }
            register_handled = true;
            //
            // ICENABLER register writes require RWP dependent things to be updated, set the bit.
            //
            distributor->gicd_ctl_reg |= BIT(31);
            //
            // TODO: propagate the changes
            //

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ISPENDR0E) && (relative_addr <= GIC_DIST_ISPENDR31E) ) {
            //
            // sets an IRQ to pending
            //
            u32 reg_num, irq_num;
            reg_num = (relative_addr - GIC_DIST_ISPENDR0E) / 4;
            u32 value_is_enabler, value_ic_enabler, current_val;
            value_is_enabler = distributor->gicd_interrupt_set_pending_ext_spi_range_regs[reg_num];
            value_ic_enabler = distributor->gicd_interrupt_clear_pending_ext_spi_range_regs[reg_num];
            current_val = *val;

            //
            // if 1 is written to the bits in these registers, they need to read 1 in GICD_ICENABLER[1:31] as well.
            // also this is banked for the first 8 processor cores - so changes must reflect across all of them.
            //
            // There has to be a way more efficient way of doing this...
            //

            for (u32 i = 0; i < 32; i++) {
                if( ( (current_val & BIT(i)) != 0 ) && ( ( value_is_enabler & BIT(i) ) == 0) ) {
                    value_is_enabler |= BIT(i);
                    value_ic_enabler |= BIT(i);
                    irq_num = ((32 * reg_num) + 4096) + i;
                    //
                    // TODO: do this
                    //
                    vgic_log("HV vGIC DEBUG [ERROR]: ISPENDR not implemented for irq %d\n", (irq_num-3076));
                }
            }
            if(reg_num == 0) {
                //
                // don't attempt to write these registers, since affinity routing is always on.
                //
            }
            else {
                distributor->gicd_interrupt_set_pending_ext_spi_range_regs[reg_num] = value_is_enabler;
                distributor->gicd_interrupt_clear_pending_ext_spi_range_regs[reg_num] = value_ic_enabler;
            }
            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ICPENDR0E) && (relative_addr <= GIC_DIST_ICPENDR31E) ) {
            //
            // sets an IRQ to pending
            //
            u32 reg_num, irq_num;
            reg_num = (relative_addr - GIC_DIST_ICPENDR0E) / 4;
            u32 value_is_enabler, value_ic_enabler, current_val;
            value_is_enabler = distributor->gicd_interrupt_set_pending_ext_spi_range_regs[reg_num];
            value_ic_enabler = distributor->gicd_interrupt_clear_pending_ext_spi_range_regs[reg_num];
            current_val = *val;

            //
            // if 1 is written to the bits in these registers, they need to read 1 in GICD_ICENABLER[1:31] as well.
            // also this is banked for the first 8 processor cores - so changes must reflect across all of them.
            //
            // There has to be a way more efficient way of doing this...
            //

            for (u32 i = 0; i < 32; i++) {
                if( ( (current_val & BIT(i)) != 0 ) && ( ( value_is_enabler & BIT(i) ) == 0) ) {
                    value_is_enabler &= ~BIT(i);
                    value_ic_enabler &= ~BIT(i);
                    irq_num = ((32 * reg_num) + 4096) + i;
                    //
                    // TODO: do this
                    //
                    vgic_log("HV vGIC DEBUG [ERROR]: ICPENDR not implemented for irq %d\n", (irq_num-3076));
                }
            }
            if(reg_num == 0) {
                //
                // don't attempt to write these registers, since affinity routing is always on.
                //
            }
            else {
                distributor->gicd_interrupt_set_pending_ext_spi_range_regs[reg_num] = value_is_enabler;
                distributor->gicd_interrupt_clear_pending_ext_spi_range_regs[reg_num] = value_ic_enabler;
            }
            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ISACTIVER0E) && (relative_addr <= GIC_DIST_ISACTIVER31E) ) {
            //
            // clears the pending state from an IRQ
            //
            u32 reg_num, irq_num;
            reg_num = (relative_addr - GIC_DIST_ISACTIVER0E) / 4;
            u32 value_is_enabler, value_ic_enabler, current_val;
            value_is_enabler = distributor->gicd_interrupt_set_active_ext_spi_range_regs[reg_num];
            value_ic_enabler = distributor->gicd_interrupt_clear_active_ext_spi_range_regs[reg_num];
            current_val = *val;

            //
            // if 1 is written to the bits in these registers, they need to read 0 in GICD_ISACTIVER[0:31] as well.
            // also this is banked for the first 8 processor cores - so changes must reflect across all of them.
            //
            // There has to be a way more efficient way of doing this...
            //
            for (u32 i = 0; i < 32; i++) {
                if( ( (current_val & BIT(i)) != 0 ) && ( ( value_ic_enabler & BIT(i) ) == 0) ) {
                    value_is_enabler |= BIT(i);
                    value_ic_enabler |= BIT(i);
                    irq_num = ((32 * reg_num) + 4096) + i;
                    //
                    // TODO: do this
                    //
                    vgic_log("HV vGIC DEBUG [ERROR]: ISACTIVER not implemented for irq %d\n", (irq_num-3076));
                }  
            }
            if(reg_num == 0) {

            }
            else {
                distributor->gicd_interrupt_set_active_ext_spi_range_regs[reg_num] = value_is_enabler;
                distributor->gicd_interrupt_clear_active_ext_spi_range_regs[reg_num] = value_ic_enabler;
            }
            register_handled = true;
        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ICACTIVER0E) && (relative_addr <= GIC_DIST_ICACTIVER31E) ) {
            //
            // clears the pending state from an IRQ
            //
            u32 reg_num, irq_num;
            reg_num = (relative_addr - GIC_DIST_ICACTIVER0E) / 4;
            u32 value_is_enabler, value_ic_enabler, current_val;
            value_is_enabler = distributor->gicd_interrupt_set_active_ext_spi_range_regs[reg_num];
            value_ic_enabler = distributor->gicd_interrupt_clear_active_ext_spi_range_regs[reg_num];
            current_val = *val;

            //
            // if 1 is written to the bits in these registers, they need to read 0 in GICD_ISACTIVER[0:31] as well.
            // also this is banked for the first 8 processor cores - so changes must reflect across all of them.
            //
            // There has to be a way more efficient way of doing this...
            //
            for (u32 i = 0; i < 32; i++) {
                if( ( (current_val & BIT(i)) != 0 ) && ( ( value_ic_enabler & BIT(i) ) == 0) ) {
                    value_is_enabler &= ~BIT(i);
                    value_ic_enabler &= ~BIT(i);
                    irq_num = ((32 * reg_num) + 4096) + i;
                    //
                    // TODO: do this
                    //
                    vgic_log("HV vGIC DEBUG [ERROR]: ISACTIVER not implemented for irq %d\n", (irq_num-3076));
                }  
            }
            if(reg_num == 0) {

            }
            else {
                distributor->gicd_interrupt_set_active_ext_spi_range_regs[reg_num] = value_is_enabler;
                distributor->gicd_interrupt_clear_active_ext_spi_range_regs[reg_num] = value_ic_enabler;
            }
            register_handled = true;
        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_IPRIORITYR0E) && (relative_addr <= GIC_DIST_IPRIORITYR255E) ) {
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_IPRIORITYR0E) / 4;
            distributor->gicd_interrupt_priority_ext_spi_range_regs[reg_num] = *val;
            vgic_log("HV vGIC DEBUG [INFO] [Distributor]: interrupt priority register %d = 0x%llx\n", reg_num, *val);
            register_handled = true;
            //unimplemented_reg_accessed = true;
        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ICFGR0E) && (relative_addr <= GIC_DIST_ICFGR63E) ) {
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_ICFGR0E) / 4;
            //
            // Unimplemented for now (we only support the timer interrupt right now - and those are managed by the redistributors)
            //
            distributor->gicd_interrupt_config_regs[reg_num] = *val;
            vgic_log("HV vGIC DEBUG [INFO] [Distributor]: interrupt configuration register %d = 0x%llx\n", reg_num, *val);
            register_handled = true;
            //unimplemented_reg_accessed = true;
        }
#endif
        else if(register_handled == false){
            //
            // the register is unknown (or unimplemented) - print a warning.
            //
            vgic_log("HV vGIC DEBUG [ERR] - guest attempted to access unknown register 0x%llx\n", relative_addr);
            register_handled = true;
            unimplemented_reg_accessed = true;
        }
    }
    else {
        //
        // The guest is attempting to read a register.
        // Handle it appropriately. Emit a warning (to become an error later) if a register is write only or doesn't exist
        //
        switch(relative_addr) {
            case GIC_DIST_CTLR:
                *val = distributor->gicd_ctl_reg;
                register_handled = true;
                break;
            case GIC_DIST_TYPER:
                *val = distributor->gicd_type_reg;
                register_handled = true;
                break;
            case GIC_DIST_TYPER2:
                *val = distributor->gicd_type_reg_2;
                register_handled = true;
                break;
            case GIC_DIST_IIDR:
                *val = distributor->gicd_imp_id_reg;
                register_handled = true;
                break;
            case GIC_DIST_STATUSR:
                *val = distributor->gicd_err_sts;
                register_handled = true;
                break;
            case GIC_DIST_SETSPI_NSR:
            case GIC_DIST_CLRSPI_NSR:
            case GIC_DIST_CLRSPI_SR:
            case GIC_DIST_SETSPI_SR:
            case GIC_DIST_SGIR:
                *val = 0; // these registers are write only so force return 0 to the guest.
                register_handled = true;
                break;
            //
            // for some reason HAL needs this specific ID register. (the others are IMPDEF it seems)
            //
            case GIC_DIST_PIDR2: // make Hal happy
                *val = 0xff;
                register_handled = true;
                break;
            case GIC_DIST_IROUTER32 ... GIC_DIST_IROUTER1019:
                u32 reg_num;
                reg_num = (relative_addr - GIC_DIST_IROUTER32) / 8;
                *val = distributor->gicd_interrupt_router_regs[reg_num];
                register_handled = true;
                break;
#ifdef EXTENDED_SPI_ENABLE
            case GIC_DIST_IROUTER0E ... GIC_DIST_IROUTER1023E:
                u32 reg_num;
                reg_num = (relative_addr - GIC_DIST_IROUTER0E) / 8;
                *val = distributor->gicd_interrupt_router_ext_spi_range_regs[reg_num];
                register_handled = true;
                break;
#endif
            default:
                //
                // we're dealing with a register that is banked n times, we need to get to the if statements.
                //
                break;
        }
        if((register_handled == false) && (relative_addr >= GIC_DIST_IGROUPR0) && (relative_addr <= GIC_DIST_IGROUPR31) ) {
            //
            // the guest is trying to change the group of a given interrupt.
            //
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_IGROUPR0) / 4;

            //
            // TODO: bank GICD_IGROUPR0 for cores 0-7 - GIC spec requires it - but since we're booting with 1 core atm, we can ignore
            // this for now.
            //

            *val = distributor->gicd_interrupt_group_regs[reg_num];
            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ISENABLER0) && (relative_addr <= GIC_DIST_ISENABLER31) ) {
            //
            // enables an IRQ to be forwarded to a CPU interface.
            //
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_ISENABLER0) / 4;
            *val = distributor->gicd_interrupt_set_enable_regs[reg_num];
            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ICENABLER0) && (relative_addr <= GIC_DIST_ICENABLER31) ) {
            //
            // disables an IRQ to be forwarded to a CPU interface.
            //
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_ICENABLER0) / 4;
            *val = distributor->gicd_interrupt_clear_enable_regs[reg_num];
            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ISPENDR0) && (relative_addr <= GIC_DIST_ISPENDR31) ) {
            //
            // sets an IRQ to pending
            //
            u32 reg_num, irq_num;
            reg_num = (relative_addr - GIC_DIST_ISPENDR0) / 4;
            distributor->gicd_interrupt_set_pending_regs[reg_num];
            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ICPENDR0) && (relative_addr <= GIC_DIST_ICPENDR31) ) {
            //
            // clears the pending state from an IRQ
            //
            u32 reg_num, irq_num;
            reg_num = (relative_addr - GIC_DIST_ICPENDR0) / 4;
            *val = distributor->gicd_interrupt_clear_pending_regs[reg_num];
            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ISACTIVER0) && (relative_addr <= GIC_DIST_ISACTIVER31) ) {
            //
            // 
            //
            // clears the pending state from an IRQ
            //
            u32 reg_num, irq_num;
            reg_num = (relative_addr - GIC_DIST_ISACTIVER0) / 4;
            *val = distributor->gicd_interrupt_set_active_regs[reg_num];
            register_handled = true;
        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ICACTIVER0) && (relative_addr <= GIC_DIST_ICACTIVER31) ) {
            //
            // clears the pending state from an IRQ
            //
            u32 reg_num, irq_num;
            reg_num = (relative_addr - GIC_DIST_ICACTIVER0) / 4;
            *val = distributor->gicd_interrupt_clear_active_regs[reg_num];
            register_handled = true;
        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_IPRIORITYR0) && (relative_addr <= GIC_DIST_IPRIORITYR254) ) {
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_IPRIORITYR0) / 4;
            *val = distributor->gicd_interrupt_priority_regs[reg_num];
            vgic_log("HV vGIC DEBUG [INFO] [Distributor]: interrupt priority register %d = 0x%llx\n", reg_num, *val);
            register_handled = true;
        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ITARGETSR0) && (relative_addr <= GIC_DIST_ITARGETSR254) ) {
            //
            // These are RES0 - since affinity routing is always enabled on Apple platforms.
            //
            *val = 0;
            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ICFGR0) && (relative_addr <= GIC_DIST_ICFGR63) ) {
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_ICFGR0) / 4;
            //
            // Unimplemented for now (we only support the timer interrupt right now - and those are managed by the redistributors)
            //
            *val = distributor->gicd_interrupt_config_regs[reg_num];
            vgic_log("HV vGIC DEBUG [INFO] [Distributor]: interrupt configuration register %d = 0x%llx\n", reg_num, *val);
            register_handled = true;
            //unimplemented_reg_accessed = true;
        }
#ifdef EXTENDED_SPI_ENABLE
        if((register_handled == false) && (relative_addr >= GIC_DIST_IGROUPR0E) && (relative_addr <= GIC_DIST_IGROUPR31E) ) {
            //
            // the guest is trying to change the group of a given interrupt.
            //
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_IGROUPR0E) / 4;

            //
            // TODO: bank GICD_IGROUPR0 for cores 0-7 - GIC spec requires it - but since we're booting with 1 core atm, we can ignore
            // this for now.
            //

            *val = distributor->gicd_interrupt_group_regs_ext_spi_range[reg_num];
            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ISENABLER0E) && (relative_addr <= GIC_DIST_ISENABLER31E) ) {
            //
            // enables an IRQ to be forwarded to a CPU interface.
            //
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_ISENABLER0E) / 4;
            *val = distributor->gicd_interrupt_set_enable_ext_spi_range_regs[reg_num];
            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ICENABLER0E) && (relative_addr <= GIC_DIST_ICENABLER31E) ) {
            //
            // disables an IRQ to be forwarded to a CPU interface.
            //
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_ICENABLER0E) / 4;
            *val = distributor->gicd_interrupt_clear_enable_ext_spi_range_regs[reg_num];
            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ISPENDR0E) && (relative_addr <= GIC_DIST_ISPENDR31E) ) {
            //
            // sets an IRQ to pending
            //
            u32 reg_num, irq_num;
            reg_num = (relative_addr - GIC_DIST_ISPENDR0E) / 4;
            distributor->gicd_interrupt_set_pending_ext_spi_range_regs[reg_num];
            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ICPENDR0E) && (relative_addr <= GIC_DIST_ICPENDR31E) ) {
            //
            // clears the pending state from an IRQ
            //
            u32 reg_num, irq_num;
            reg_num = (relative_addr - GIC_DIST_ICPENDR0) / 4;
            *val = distributor->gicd_interrupt_clear_pending_ext_spi_range_regs[reg_num];
            register_handled = true;

        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ISACTIVER0E) && (relative_addr <= GIC_DIST_ISACTIVER31E) ) {
            //
            // 
            //
            // clears the pending state from an IRQ
            //
            u32 reg_num, irq_num;
            reg_num = (relative_addr - GIC_DIST_ISACTIVER0E) / 4;
            *val = distributor->gicd_interrupt_set_active_ext_spi_range_regs[reg_num];
            register_handled = true;
        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ICACTIVER0E) && (relative_addr <= GIC_DIST_ICACTIVER31E) ) {
            //
            // clears the pending state from an IRQ
            //
            u32 reg_num, irq_num;
            reg_num = (relative_addr - GIC_DIST_ICACTIVER0E) / 4;
            *val = distributor->gicd_interrupt_clear_active_ext_spi_range_regs[reg_num];
            register_handled = true;
        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_IPRIORITYR0E) && (relative_addr <= GIC_DIST_IPRIORITYR255E) ) {
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_IPRIORITYR0E) / 4;
            *val = distributor->gicd_interrupt_priority_ext_spi_range_regs[reg_num];
            vgic_log("HV vGIC DEBUG [INFO] [Distributor]: interrupt priority register %d = 0x%llx\n", reg_num, *val);
            register_handled = true;
        }
        else if ( (register_handled == false) && (relative_addr >= GIC_DIST_ICFGR0E) && (relative_addr <= GIC_DIST_ICFGR63E) ) {
            u32 reg_num;
            reg_num = (relative_addr - GIC_DIST_ICFGR0E) / 4;
            //
            // Unimplemented for now (we only support the timer interrupt right now - and those are managed by the redistributors)
            //
            *val = distributor->gicd_interrupt_config_regs[reg_num];
            vgic_log("HV vGIC DEBUG [INFO] [Distributor]: interrupt configuration register %d = 0x%llx\n", reg_num, *val);
            register_handled = true;
            //unimplemented_reg_accessed = true;
        }
#endif
        else if (register_handled == false) {
            //
            // the register is unknown (or unimplemented) - print a warning.
            //
            vgic_log("HV vGIC DEBUG [ERR] - guest attempted to access unknown register 0x%llx\n", relative_addr);
            register_handled = true;
            unimplemented_reg_accessed = true;
        }
    }
    vgic_log("HV vGIC DEBUG [INFO] [Distributor]: 0x%llx = 0x%llx ", relative_addr, *val);
    if(write) {
        vgic_log("[Written]");
    }
    else {
        vgic_log("[Read]");
    }
    if(unimplemented_reg_accessed) {
        vgic_log("[Unimplemented]\n");
    }
    else {
        vgic_log("\n");
    }
    return register_handled;
}



//
// Description:
//   the vGIC guest interrupt handler for redistributor writes.
//
// Return values:
//   true - access has been handled successfully, even if the access itself is either bad or not permitted.
//   false - access was not handled successfully.
//
static bool handle_vgic_redist_access(struct exc_info *ctx, u64 addr, u64 *val, bool write, int width)
{
    u64 relative_addr;
    bool register_handled;
    bool unimplemented_reg_accessed;
    relative_addr = addr - redist_base;
    register_handled = false;
    unimplemented_reg_accessed = false;
    u8 cpu_num;
    u32 value_is_enabler, value_ic_enabler, current_val;
    u32 irq_num;
    u32 reg_num;
    u32 reg_offset;
    value_ic_enabler = 0;
    value_is_enabler = 0;
    current_val = 0;
    irq_num = 0;
    reg_num = 0;
    reg_offset = 0;

    cpu_num = ctx->cpu_id;
    if(write) {
        //
        // The guest attempted to write a register.
        // Handle it based on what they're trying to write, and preserve the value if
        // the value is going to a RW register.
        // Emit a warning (to become an error later) if the guest is attempting to write a register that doesn't exist or is read only.
        //
        
        switch(relative_addr) {
            //
            // RD region
            //
            case GIC_REDIST_CTLR:
                u32 gicr_ctlr_new_val = (u32)(*val);
                vgic_log("HV vGIC DEBUG: guest writing GICR_CTLR = 0x%x, old value 0x%x\n", gicr_ctlr_new_val, redistributors[cpu_num].rd_region.gicr_ctl_reg);
                bool is_uwp_to_be_set = false;
                bool is_rwp_to_be_set = false;
                //
                // like RWP in the distributor's case, the redistributor has it's own version of this type of bit (UWP),
                // where certain actions will trigger updates (IPIs in this case.)
                // we have to deal with this once the CPU interface is brought up.
                // the redistributors also have their own RWP bits which need to be handled similarly

                //
                // bits 30-27 and 23-4 are RES0 so discard writes.
                //
                if(((gicr_ctlr_new_val & GENMASK(30, 27)) != 0) || ((gicr_ctlr_new_val & GENMASK(23, 4)) != 0)) {
                    //
                    // these bits are RES0 - clear out this bitmask.
                    //
                    gicr_ctlr_new_val &= ~(GENMASK(30, 27));
                    gicr_ctlr_new_val &= ~(GENMASK(23, 4));
                    vgic_log("HV vGIC DEBUG [WARN]: guest attempted to write RES0 bits in GICR_CTLR, discarding\n");
                }

                //
                // since DS = 1 - bit 26 (DPG1S) is RAZ/WI
                //
                if( ( (gicr_ctlr_new_val) & BIT(26) ) != 0 ) {
                    //
                    // clear the bit
                    //
                    gicr_ctlr_new_val &= ~BIT(26);
                }

                //
                // setting or clearing bits 25 and 24 (DPG1NS and DPG0) will trigger an RWP change.
                //
                if( ( ( (gicr_ctlr_new_val) & BIT(25) ) != 0 ) && ( (redistributors[cpu_num].rd_region.gicr_ctl_reg) & BIT(25) == 0 ) 
                 || ( ( (gicr_ctlr_new_val) & BIT(24) ) != 0 ) && ( (redistributors[cpu_num].rd_region.gicr_ctl_reg) & BIT(24) == 0 ) 
                 || ( ( (gicr_ctlr_new_val) & BIT(25) ) == 0 ) && ( (redistributors[cpu_num].rd_region.gicr_ctl_reg) & BIT(25) != 0 ) 
                 || ( ( (gicr_ctlr_new_val) & BIT(24) ) == 0 ) && ( (redistributors[cpu_num].rd_region.gicr_ctl_reg) & BIT(24) != 0 ) ) {
                    //
                    // signal that RWP is going to be changed.
                    //
                    is_rwp_to_be_set = true;
                }

                //
                // bits 2 and 1 are RO - so discard writes to those bits.
                //
                if(((gicr_ctlr_new_val & BIT(2)) == 0) || ((gicr_ctlr_new_val & BIT(1)) == 0)) {
                    //
                    // guest is attempting to clear these RO bits - discard the write.
                    //
                    gicr_ctlr_new_val |= (BIT(2) | BIT(1));
                    vgic_log("HV vGIC DEBUG [WARN]: guest attempted to write read-only bits in GICR_CTLR, discarding\n");
                }

                //
                // EnableLPIs if cleared will trigger an RWP write.
                //
                if(((gicr_ctlr_new_val & BIT(0)) == 0) || ((redistributors[cpu_num].rd_region.gicr_ctl_reg & BIT(0)) != 0)) {
                    is_rwp_to_be_set = true;
                }

                //
                // start propagating the effects of the RWP changes.
                //
                if(is_rwp_to_be_set == true) {
                    //
                    // set RWP here - then start propagating the effects immediately after.
                    //
                    gicr_ctlr_new_val |= BIT(31);
                }

                redistributors[cpu_num].rd_region.gicr_ctl_reg = gicr_ctlr_new_val;
                if(is_rwp_to_be_set == true) {
                    //
                    // TODO: start the changes signaled by RWP.
                    //
                    //hv_vgicv3_apply_gic_redist_changes(gicr_ctlr_new_val);
                }

                redistributors[cpu_num].rd_region.gicr_ctl_reg = gicr_ctlr_new_val;
                register_handled = true;
                break;
            case GIC_REDIST_IIDR:
            case GIC_REDIST_TYPER:
            case GIC_REDIST_MPAMIDR:
                //
                // these are simple - the registers are read only so discard any write attempts.
                //
                vgic_log("HV vGIC DEBUG [WARN]: guest attempted to change a read-only register (0x%x), discarding\n", relative_addr);
                register_handled = true;
                break;
            case GIC_REDIST_STATUSR:
                //
                // GICR_STATUSR is a bit special, software must write 1 to ack an error, which then *clears* the bit.
                // Note that [31:4] are always RES0.
                //
                u32 gicr_statusr_new_val = (u32)(*val);
                u32 gicr_statusr_current_val = redistributors[cpu_num].rd_region.gicr_status_reg;
                if((gicr_statusr_new_val & GENMASK(31, 4)) != 0) {
                    gicr_statusr_new_val &= ~(GENMASK(31, 4));
                    vgic_log("HV vGIC DEBUG [WARN]: guest attempted to write RES0 bits in GICD_STATUSR, discarding\n");
                }
                if(((gicr_statusr_new_val & BIT(3)) != 0) & ((gicr_statusr_current_val & BIT(3)) != 0)) {
                    gicr_statusr_current_val &= ~(BIT(3));
                    vgic_log("HV vGIC DEBUG [INFO]: clearing WROD bit in GICD_STATUSR\n");
                }
                if(((gicr_statusr_new_val & BIT(2)) != 0) & ((gicr_statusr_current_val & BIT(2)) != 0)) {
                    gicr_statusr_current_val &= ~(BIT(2));
                    vgic_log("HV vGIC DEBUG [INFO]: clearing RWOD bit in GICD_STATUSR\n");
                }
                if(((gicr_statusr_new_val & BIT(1)) != 0) & ((gicr_statusr_current_val & BIT(1)) != 0)) {
                    gicr_statusr_current_val &= ~(BIT(1));
                    vgic_log("HV vGIC DEBUG [INFO]: clearing WRD bit in GICD_STATUSR\n");
                }
                if(((gicr_statusr_new_val & BIT(0)) != 0) & ((gicr_statusr_current_val & BIT(0)) != 0)) {
                    gicr_statusr_current_val &= ~(BIT(0));
                    vgic_log("HV vGIC DEBUG [INFO]: clearing RRD bit in GICD_STATUSR\n");
                }
                redistributors[cpu_num].rd_region.gicr_status_reg = gicr_statusr_current_val;
                register_handled = true;
                break;
            case GIC_REDIST_WAKER:
                redistributors[cpu_num].rd_region.gicr_wake_reg = *val;
                register_handled = true;
                break;
            case GIC_REDIST_PARTIDR:
                redistributors[cpu_num].rd_region.gicr_partidr = *val;
                register_handled = true;
                break;
            case GIC_REDIST_SETLPIR:
                redistributors[cpu_num].rd_region.gicr_setlpir = *val;
                //
                // TODO: actually do the action here.
                //
                vgic_log("HV vGIC DEBUG [WARN]: GICR_SETLPIR is currently unimplemented!\n");
                unimplemented_reg_accessed = true;
                register_handled = true;
                break;
            case GIC_REDIST_CLRLPIR:
                redistributors[cpu_num].rd_region.gicr_clrlpir = *val;
                //
                // TODO: actually do the action here.
                //
                vgic_log("HV vGIC DEBUG [WARN]: GICR_CLRLPIR is currently unimplemented!\n");
                unimplemented_reg_accessed = true;
                register_handled = true;
                break;
            case GIC_REDIST_PROPBASER:
                redistributors[cpu_num].rd_region.gicr_propbaser = *val;
                register_handled = true;
                break;
            case GIC_REDIST_PENDBASER:
                redistributors[cpu_num].rd_region.gicr_pendbaser = *val;
                register_handled = true;
                break;
            case GIC_REDIST_INVLPIR:
                redistributors[cpu_num].rd_region.gicr_invlpir = *val;
                //
                // TODO: implement this. note that for INTID bits, bits 31:16 are unused since IDbits = 16 for us.
                //
                vgic_log("HV vGIC DEBUG [WARN]: GICR_INVLPIR is currently unimplemented!\n");
                unimplemented_reg_accessed = true;
                register_handled = true;
                break;
            case GIC_REDIST_INVALLR:
                //
                // Any write to this register will invalidate all LPI config data - but the bits themselves are RES0.
                // TODO: implement this.
                //
                redistributors[cpu_num].rd_region.gicr_invallr = 0;
                vgic_log("HV vGIC DEBUG [WARN]: GICR_INVALLR is currently unimplemented!\n");
                unimplemented_reg_accessed = true;
                register_handled = true;
                break;
            case GIC_REDIST_SYNCR:
                //
                // this register is read only - but has special handling. currently unimplemented.
                //
                vgic_log("HV vGIC DEBUG [WARN]: GICR_SYNCR is currently unimplemented!\n");
                unimplemented_reg_accessed = true;
                register_handled = true;
                break;
            //
            // SGI region
            //
            case GIC_REDIST_IGROUPR0:
                redistributors[cpu_num].sgi_region.gicr_igroupr0 = *val;
                register_handled = true;
                break;
            case GIC_REDIST_ISENABLER0:
                // u32 value_is_enabler, value_ic_enabler, current_val;
                // u32 irq_num;
                value_is_enabler = redistributors[cpu_num].sgi_region.gicr_isactiver0;
                value_ic_enabler = redistributors[cpu_num].sgi_region.gicr_icactiver0;
                current_val = *val;

                //
                // if 1 is written to the bits in these registers, they need to read 1 in GICR_ICENABLER0 as well.
                // also this is banked for the first 8 processor cores - so changes must reflect across all of them.
                //
                // There has to be a way more efficient way of doing this...
                //

                for(u32 i = 0; i < 32; i++) {
                    if( ( (current_val & BIT(i)) != 0 ) && ( ( value_is_enabler & BIT(i) ) == 0) ) {
                        value_is_enabler |= BIT(i);
                        value_ic_enabler |= BIT(i);      
                        irq_num = i;
                        //
                        // TODO: do the AIC operation associated with this.
                        //        
                    }
                }
                redistributors[cpu_num].sgi_region.gicr_isactiver0 = value_is_enabler;
                redistributors[cpu_num].sgi_region.gicr_icactiver0 = value_ic_enabler;
                //TODO: should we also write to gicr_isenabler0?
                redistributors[cpu_num].sgi_region.gicr_isenabler0 = *val;
                register_handled = true;
                break;
            case GIC_REDIST_ICENABLER0:
                // u32 value_is_enabler, value_ic_enabler, current_val;
                // u32 irq_num;
                value_is_enabler = redistributors[cpu_num].sgi_region.gicr_isactiver0;
                value_ic_enabler = redistributors[cpu_num].sgi_region.gicr_icactiver0;
                current_val = *val;

                //
                // if 1 is written to the bits in these registers, they need to read 1 in GICD_ICENABLER[0:31] as well.
                // also this is banked for the first 8 processor cores - so changes must reflect across all of them.
                //
                // There has to be a way more efficient way of doing this...
                //

                for(u32 i = 0; i < 32; i++) {
                    if( ( (current_val & BIT(i)) != 0 ) && ( ( value_is_enabler & BIT(i) ) != 0) ) {
                        value_is_enabler &= ~BIT(i);
                        value_ic_enabler &= ~BIT(i);      
                        irq_num = i;
                        //
                        // TODO: do the AIC operation associated with this.
                        //        
                    }
                }
                redistributors[cpu_num].sgi_region.gicr_isactiver0 = value_is_enabler;
                redistributors[cpu_num].sgi_region.gicr_icactiver0 = value_ic_enabler;
                register_handled = true;
                break;
            case GIC_REDIST_ISPENDR0:
                // u32 value_is_enabler, value_ic_enabler, current_val;
                // u32 irq_num;
                value_is_enabler = redistributors[cpu_num].sgi_region.gicr_ispendr0;
                value_ic_enabler = redistributors[cpu_num].sgi_region.gicr_icpendr0;
                current_val = *val;

                //
                // if 1 is written to the bits in these registers, they need to read 1 in GICD_ICENABLER[0:31] as well.
                // also this is banked for the first 8 processor cores - so changes must reflect across all of them.
                //
                // There has to be a way more efficient way of doing this...
                //

                for(u32 i = 0; i < 32; i++) {
                    if( ( (current_val & BIT(i)) != 0 ) && ( ( value_is_enabler & BIT(i) ) == 0) ) {
                        value_is_enabler |= BIT(i);
                        value_ic_enabler |= BIT(i);      
                        irq_num = i;
                        //
                        // TODO: do the AIC operation associated with this.
                        //        
                    }
                }
                register_handled = true;
                redistributors[cpu_num].sgi_region.gicr_isactiver0 = value_is_enabler;
                redistributors[cpu_num].sgi_region.gicr_icactiver0 = value_ic_enabler;
                break;
            case GIC_REDIST_ICPENDR0:
                // u32 value_is_enabler, value_ic_enabler, current_val;
                // u32 irq_num;
                value_is_enabler = redistributors[cpu_num].sgi_region.gicr_ispendr0;
                value_ic_enabler = redistributors[cpu_num].sgi_region.gicr_icpendr0;
                current_val = *val;

                //
                // if 1 is written to the bits in these registers, they need to read 1 in GICD_ICENABLER[0:31] as well.
                // also this is banked for the first 8 processor cores - so changes must reflect across all of them.
                //
                // There has to be a way more efficient way of doing this...
                //

                for(u32 i = 0; i < 32; i++) {
                    if( ( (current_val & BIT(i)) != 0 ) && ( ( value_is_enabler & BIT(i) ) != 0) ) {
                        value_is_enabler &= ~BIT(i);
                        value_ic_enabler &= ~BIT(i);      
                        irq_num = i;
                        //
                        // TODO: do the AIC operation associated with this.
                        //        
                    }
                }
                register_handled = true;
                redistributors[cpu_num].sgi_region.gicr_isactiver0 = value_is_enabler;
                redistributors[cpu_num].sgi_region.gicr_icactiver0 = value_ic_enabler;
                break;
            case GIC_REDIST_ISACTIVER0:
                // u32 value_is_enabler, value_ic_enabler, current_val;
                // u32 irq_num;
                value_is_enabler = redistributors[cpu_num].sgi_region.gicr_isactiver0;
                value_ic_enabler = redistributors[cpu_num].sgi_region.gicr_icactiver0;
                current_val = *val;

                //
                // if 1 is written to the bits in these registers, they need to read 1 in GICD_ICENABLER[0:31] as well.
                // also this is banked for the first 8 processor cores - so changes must reflect across all of them.
                //
                // There has to be a way more efficient way of doing this...
                //

                for(u32 i = 0; i < 32; i++) {
                    if( ( (current_val & BIT(i)) != 0 ) && ( ( value_is_enabler & BIT(i) ) == 0) ) {
                        value_is_enabler |= BIT(i);
                        value_ic_enabler |= BIT(i);      
                        irq_num = i;
                        //
                        // TODO: do the AIC operation associated with this.
                        //        
                    }
                }
                register_handled = true;
                redistributors[cpu_num].sgi_region.gicr_isactiver0 = value_is_enabler;
                redistributors[cpu_num].sgi_region.gicr_icactiver0 = value_ic_enabler;
                break;
            case GIC_REDIST_ICACTIVER0:
                // u32 value_is_enabler, value_ic_enabler, current_val;
                // u32 irq_num;
                value_is_enabler = redistributors[cpu_num].sgi_region.gicr_isactiver0;
                value_ic_enabler = redistributors[cpu_num].sgi_region.gicr_icactiver0;
                current_val = *val;

                //
                // if 1 is written to the bits in these registers, they need to read 1 in GICD_ICENABLER[0:31] as well.
                // also this is banked for the first 8 processor cores - so changes must reflect across all of them.
                //
                // There has to be a way more efficient way of doing this...
                //

                for(u32 i = 0; i < 32; i++) {
                    if( ( (current_val & BIT(i)) != 0 ) && ( ( value_is_enabler & BIT(i) ) != 0) ) {
                        value_is_enabler &= ~BIT(i);
                        value_ic_enabler &= ~BIT(i);      
                        irq_num = i;
                        //
                        // TODO: do the AIC operation associated with this.
                        //        
                    }
                }
                register_handled = true;
                redistributors[cpu_num].sgi_region.gicr_isactiver0 = value_is_enabler;
                redistributors[cpu_num].sgi_region.gicr_icactiver0 = value_ic_enabler;
                break;
            case GIC_REDIST_ICFGR0:
                redistributors[cpu_num].sgi_region.gicr_icfgr0 = *val;
                register_handled = true;
                break;
            case GIC_REDIST_ICFGR1:
                redistributors[cpu_num].sgi_region.gicr_icfgr1 = *val;
                register_handled = true;
                break;
            case GIC_REDIST_IGRPMODR0:
                redistributors[cpu_num].sgi_region.gicr_igrpmodr0 = *val;
                register_handled = true;
                break;
            case GIC_REDIST_NSACR:
                redistributors[cpu_num].sgi_region.gicr_nsacr = *val;
                register_handled = true;
                break;
            case GIC_REDIST_IPRIORITYR0 ... GIC_REDIST_IPRIORITYR3 + 3:
                // u32 reg_num;
                reg_num = (relative_addr - GIC_REDIST_IPRIORITYR0) / 4;
                reg_offset = (relative_addr - GIC_REDIST_IPRIORITYR0) % 4;

                if(reg_offset == 0)
                    redistributors[cpu_num].sgi_region.gicr_sgi_ipriority_reg[reg_num] = *val;
                else{
                    //TODO: handle width
                    u8 *reg_u8 = (u8 *)&redistributors[cpu_num].sgi_region.gicr_sgi_ipriority_reg[reg_num];
                    reg_u8 += reg_offset;
                    *reg_u8 = *val & 0xFF;
                }
                register_handled = true;
                break;
            case GIC_REDIST_IPRIORITYR4 ... GIC_REDIST_IPRIORITYR7 + 3:
                // u32 reg_num;
                reg_num = (relative_addr - GIC_REDIST_IPRIORITYR4) / 4;
                reg_offset = (relative_addr - GIC_REDIST_IPRIORITYR4) % 4;
                if(reg_offset == 0)
                    redistributors[cpu_num].sgi_region.gicr_ppi_ipriority_reg[reg_num] = *val;
                else{
                    //TODO: handle width
                    u8 *reg_u8 = (u8 *)&redistributors[cpu_num].sgi_region.gicr_ppi_ipriority_reg[reg_num];
                    reg_u8 += reg_offset;
                    *reg_u8 = *val & 0xFF;
                }
                register_handled = true;
                break;
            default:
                //
                // an unimplemented register.
                //
                unimplemented_reg_accessed = true;
                break;
        }
    }
    else {
        //
        // The guest is attempting to read a register.
        // Handle it appropriately. Emit a warning (to become an error later) if a register is write only or doesn't exist
        //
        
        
        switch(relative_addr) {
            //
            // RD region
            //
            case GIC_REDIST_CTLR:
                *val = redistributors[cpu_num].rd_region.gicr_ctl_reg;
                register_handled = true;
                break;
            case GIC_REDIST_IIDR:
                *val = redistributors[cpu_num].rd_region.gicr_iidr;
                register_handled = true;
                break;
            case GIC_REDIST_TYPER:
                *val = redistributors[cpu_num].rd_region.gicr_type_reg;
                register_handled = true;
                break;
            case GIC_REDIST_STATUSR:
                *val = redistributors[cpu_num].rd_region.gicr_status_reg;
                register_handled = true;
                break;
            case GIC_REDIST_WAKER:
                *val = redistributors[cpu_num].rd_region.gicr_wake_reg;
                register_handled = true;
                break;
            case GIC_REDIST_MPAMIDR:
                *val = redistributors[cpu_num].rd_region.gicr_mpamidr;
                register_handled = true;
                break;
            case GIC_REDIST_PARTIDR:
                *val = redistributors[cpu_num].rd_region.gicr_partidr;
                register_handled = true;
                break;
            case GIC_REDIST_SETLPIR:
            case GIC_REDIST_CLRLPIR:
            case GIC_REDIST_INVLPIR:
            case GIC_REDIST_INVALLR:
                //
                // these registers are write-only so reads in our case will return 0 (only meaningful action here is writes)
                *val = 0;
                register_handled = true;
                break;
            case GIC_REDIST_PROPBASER:
                *val = redistributors[cpu_num].rd_region.gicr_propbaser;
                register_handled = true;
                break;
            case GIC_REDIST_PENDBASER:
                *val = redistributors[cpu_num].rd_region.gicr_pendbaser;
                register_handled = true;
                break;
            case GIC_REDIST_SYNCR:
                *val = redistributors[cpu_num].rd_region.gicr_iidr;
                register_handled = true;
                break;
            //
            // SGI region
            //
            case GIC_REDIST_IGROUPR0:
                *val = redistributors[cpu_num].sgi_region.gicr_igroupr0;
                register_handled = true;
                break;
            case GIC_REDIST_ISENABLER0:
                *val = redistributors[cpu_num].sgi_region.gicr_isenabler0;
                register_handled = true;
                break;
            case GIC_REDIST_ICENABLER0:
                *val = redistributors[cpu_num].sgi_region.gicr_icenabler0;
                register_handled = true;
                break;
            case GIC_REDIST_ISPENDR0:
                *val = redistributors[cpu_num].sgi_region.gicr_ispendr0;
                register_handled = true;
                break;
            case GIC_REDIST_ICPENDR0:
                *val = redistributors[cpu_num].sgi_region.gicr_icpendr0;
                register_handled = true;
                break;
            case GIC_REDIST_ISACTIVER0:
                *val = redistributors[cpu_num].sgi_region.gicr_isactiver0;
                register_handled = true;
                break;
            case GIC_REDIST_ICACTIVER0:
                *val = redistributors[cpu_num].sgi_region.gicr_icactiver0;
                register_handled = true;
                break;
            case GIC_REDIST_ICFGR0:
                *val = redistributors[cpu_num].sgi_region.gicr_icfgr0;
                register_handled = true;
                break;
            case GIC_REDIST_ICFGR1:
                *val = redistributors[cpu_num].sgi_region.gicr_icfgr1;
                register_handled = true;
                break;
            case GIC_REDIST_IGRPMODR0:
                *val = redistributors[cpu_num].sgi_region.gicr_igrpmodr0;
                register_handled = true;
                break;
            case GIC_REDIST_NSACR:
                *val = redistributors[cpu_num].sgi_region.gicr_nsacr;
                register_handled = true;
                break;
            case GIC_REDIST_IPRIORITYR0 ... GIC_REDIST_IPRIORITYR3 + 3:
                // u32 reg_num;
                reg_num = (relative_addr - GIC_REDIST_IPRIORITYR0) / 4;
                reg_offset = (relative_addr - GIC_REDIST_IPRIORITYR0) % 4;

                if(reg_offset == 0)
                    *val = redistributors[cpu_num].sgi_region.gicr_sgi_ipriority_reg[reg_num];
                else{
                    //TODO: handle width
                    u8 *reg_u8 = (u8 *)&redistributors[cpu_num].sgi_region.gicr_sgi_ipriority_reg[reg_num];
                    reg_u8 += reg_offset;
                    *val = *reg_u8;
                }
                register_handled = true;
                break;
            case GIC_REDIST_IPRIORITYR4 ... GIC_REDIST_IPRIORITYR7 + 3:
                // u32 reg_num;
                reg_num = (relative_addr - GIC_REDIST_IPRIORITYR4) / 4;
                reg_offset = (relative_addr - GIC_REDIST_IPRIORITYR4) % 4;
                if(reg_offset == 0)
                    redistributors[cpu_num].sgi_region.gicr_ppi_ipriority_reg[reg_num] = *val;
                else{
                    //TODO: handle width
                    u8 *reg_u8 = (u8 *)&redistributors[cpu_num].sgi_region.gicr_ppi_ipriority_reg[reg_num];
                    reg_u8 += reg_offset;
                    *val = *reg_u8;
                }
                register_handled = true;
                break;
            default:
                //
                // an unimplemented register.
                //
                unimplemented_reg_accessed = true;
                break;
        }
        
    }
    vgic_log("HV vGIC DEBUG [INFO] [Redistributor]: 0x%llx = 0x%llx ", relative_addr, *val);
    if(write) {
        vgic_log("[Written]");
    }
    else {
        vgic_log("[Read]");
    }
    if(unimplemented_reg_accessed) {
        vgic_log("[Unimplemented]\n");
    }
    else {
        vgic_log("\n");
    }
    return register_handled;
}

/**
 * @brief hv_vgicv3_init_dist_registers
 * 
 * Sets up the initial values for the distributor registers.
 * 
 * For registers that deal with unsupported features, set them to 0 and just never interact with them
 * 
 * For write only registers, set them to 0, and emulate the effect upon attempting to write that register.
 * Read-only registers, set their value here and don't let the guest touch their values.
 * 
 */
void hv_vgicv3_init_dist_registers(void)
{
    memset(distributor, 0, sizeof(vgicv3_dist));
    //
    // For now - taking the easy route of saying that at least 1024 IRQs are supported on all platforms.
    //
    distributor->gicd_ctl_reg = (BIT(6) | BIT(4) | BIT(1) | BIT(0));
#ifdef EXTENDED_SPI_ENABLE
    //
    // If extended SPI support is enabled, the GIC type will be defined as the following:
    // - Extended SPIs supported (up to the max of INTID 5119 - unless we *wanted* to break spec and eat into the 5120-8191 reserved range...)
    // - Affinity level 0 can go up to 15
    // - 1 of N SPI interrupts are supported (AIC2 default behavior)
    // - Affinity 3 invalid
    // - 16 interrupt ID bits (to match what the CPU interface supports)
    // - LPIs/MSIs supported (MSIs not using an ITS)
    //
    distributor->gicd_type_reg = (GENMASK(31, 27)
                                 | BIT(22) 
                                 | BIT(21) 
                                 | BIT(20) 
                                 | BIT(19) 
                                 | BIT(17) 
                                 | BIT(4) 
                                 | BIT(3) 
                                 | BIT(2) 
                                 | BIT(1) 
                                 | BIT(0));
#else
    //
    // GIC type will be defined as the following:
    // - No extended SPIs (Update on 6/10/2025: maz in Asahi IRC says we can probably expose extended SPIs? could also look into some other hacks for > 1024 IRQ platforms)
    // - Affinity level 0 can go up to 15
    // - 1 of N SPI interrupts are supported (kind of how AIC2 can behave?)
    // - Affinity 3 invalid
    // - 16 interrupt ID bits (to match what the CPU interface supports)
    // - LPIs/MSIs supported (MSIs not using an ITS)
    //
    distributor->gicd_type_reg = (BIT(22) 
                                 | BIT(21) 
                                 | BIT(20) 
                                 | BIT(19) 
                                 | BIT(17) 
                                 | BIT(4) 
                                 | BIT(3) 
                                 | BIT(2) 
                                 | BIT(1) 
                                 | BIT(0));
#endif

    distributor->gicd_imp_id_reg = (BIT(10) | BIT(5) | BIT(4) | BIT(3) | BIT(1) | BIT(0));
    distributor->gicd_type_reg_2 = 0; 
    distributor->gicd_err_sts = 0;
    return;

}

void hv_vgicv3_assign_redist_affinity_value(u16 cpu_num, bool last_cpu) {
    u32 cpu_affinity_value;
    uint64_t mpidr_val;
    uint64_t gicr_typer;
    mpidr_val = smp_get_mpidr(cpu_num);
    //
    // Affinity level 3 is always 0.
    //
    cpu_affinity_value = (0 << 24);
    //
    // Affinity level 2 signifies if we're targeting a P-core or E-core cluster.
    // (0x0 for an E-core, 0x1 for a P-core)
    //
    cpu_affinity_value |= ((mpidr_val >> 16) & 0xFF) << 16;
    //
    // Affinity level 1 signifies the cluster number on the local die (for multi-die systems it's cluster_num + (die_num * 8)).
    //
    cpu_affinity_value |= ((mpidr_val >> 8) & 0xFF) << 8;
    //
    // Affinity level 0 is the core number on the local cluster.
    //
    cpu_affinity_value |= ((mpidr_val) & 0xFF);
    gicr_typer = (((uint64_t)cpu_affinity_value) << 32);
#ifdef EXTENDED_SPI_ENABLE

    //
    // If we're forcing extended SPIs to be enabled, we should set bits [31:27] to 1 so we can use all of the natively
    // supported
#else
    //
    // Apple silicon platforms (at least the M1 and M2 and the Pro counterparts) do not natively support the extended PPI/SPI ranges
    // so bits 31:27 remain 0. If M3 or M4 do support the extended ranges, check the Chip ID here and toggle those bits.
    // (Unlikely, as even though M1 Ultra has > 16 cores, we do not have those ranges on that platform, which means we probably will need
    // to have a solution for those platforms.)
    //
#endif

    //
    // We're also sharing a common LPI configuration table across all the vCPUs.
    //
    // Put the processor/CPU number in the right field here, 
    // the way we're doing it here ensures we have a one to one mapping with how m1n1 identifies the CPUs.
    //
    gicr_typer |= (cpu_num << 8);

    //
    // Leave out MPAM support for now - we can't assume the CPU supports it.
    // Let processors opt out of interrupts though. (bit 5, GICR_TYPER.DPGS bit)
    //
    gicr_typer |= BIT(5);
    if(last_cpu == true) {
        //
        // this is the last redistributor, set bit 4 to indicate this.
        //
        gicr_typer |= BIT(4);
    }

    //
    // If we need an ITS, we need to comment out the bottom line, since we wouldn't be supporting direct LPI injection to
    // redistributors.
    //
    gicr_typer |= BIT(3);
    //
    // say that we have physical LPIs to be safe.
    //
    gicr_typer |= BIT(0);

    //
    // write back this feature set to our vGIC registers.
    //
    redistributors[cpu_num].rd_region.gicr_type_reg = gicr_typer;

    return;

}

void hv_vgicv3_init_redist_registers(void) {
    memset(redistributors, 0, (sizeof(vgicv3_vcpu_redist) * num_cpus));
    for(u16 i = 0; i < num_cpus; i++) {
        bool last_cpu = (i + 1 == num_cpus) ? true : false;
        redistributors[i].rd_region.gicr_ctl_reg = (BIT(2) | BIT(1));
        redistributors[i].rd_region.gicr_iidr = (BIT(10) | BIT(5) | BIT(4) | BIT(3) | BIT(1) | BIT(0));
        //
        // assign affinity values to redistributors.
        //
        hv_vgicv3_assign_redist_affinity_value(i, last_cpu);
        redistributors[i].rd_region.gicr_status_reg = 0;
        redistributors[i].rd_region.gicr_wake_reg = (BIT(2) | BIT(1)); //GICR_WAKER reset values, currently not using bits 31 or 0.
        //
        // Generate and set the LPI configuration table here.
        // (Right now this is ignored just to test if stuff is working since we have no MSIs and LPIs are disabled right now.)
        //
        

    }
}


/**
 * @brief hv_vgicv3_init_list_registers
 * 
 * Enables the platform's list registers for use by the guest OS.
 * 
 */
void hv_vgicv3_init_list_registers(void)
{
    msr(ICH_LR0_EL2, 0);
    msr(ICH_LR1_EL2, 0);
    msr(ICH_LR2_EL2, 0);
    msr(ICH_LR3_EL2, 0);
    msr(ICH_LR4_EL2, 0);
    msr(ICH_LR5_EL2, 0);
    msr(ICH_LR6_EL2, 0);
    msr(ICH_LR7_EL2, 0);
}


/**
 * @brief hv_vgicv3_enable_virtual_interrupts
 * 
 * Enables virtual interrupts for the guest.
 * 
 * Note that actual interrupts are always handled by m1n1, then passed onto the vGIC which will signal the virtual interrupt to the OS.
 * 
 * @return
 * 0 - success
 * -1 - there was an error.
 */

int hv_vgicv3_enable_virtual_interrupts(void)
{
    //set VMCR to reset values, then enable virtual group 0 and 1 interrupts
    msr(ICH_VMCR_EL2, 0);
    msr(ICH_VMCR_EL2, (BIT(1)));
    //bit 0 enables the virtual CPU interface registers
    //AMO/IMO/FMO set by m1n1 on boot
    msr(ICH_HCR_EL2, (BIT(0) | BIT(2)));


    return 0;
}

u8 hv_vgic3_get_priority(u64 intd){
    u64 reg_num = 0;
    u64 reg_offset = 0;
    u8 *reg_val = NULL;
    
    if(intd <= 15){
        reg_num = intd / 4;
        reg_offset = intd % 4;
        reg_val = (u8 *)&redistributors[smp_id()].sgi_region.gicr_sgi_ipriority_reg[reg_num];
    }
    else if(intd >= 16 && intd <= 31){
        reg_num = (intd - 16) / 4;
        reg_offset = (intd - 16) % 4;
        reg_val = (u8 *)&redistributors[smp_id()].sgi_region.gicr_ppi_ipriority_reg[reg_num];
    }
    else{
        reg_num = (intd - 32) / 4;
        reg_offset = (intd - 32) % 4;
        reg_val = (u8 *)&distributor->gicd_interrupt_priority_regs[reg_num];
    }
    reg_val += reg_offset;

    return *reg_val;
}

int hv_vgic3_get_free_lr(void)
{
    u64 elrsr = mrs(ICH_ELRSR_EL2);
    if (!elrsr)
        return -1;
    return __builtin_ctzll(elrsr);
}

u64 hv_vgic3_read_lr(u32 lr_num){
    switch(lr_num){
        case 0:
            return mrs(ICH_LR0_EL2);
            break;
        case 1:
            return mrs(ICH_LR1_EL2);
            break;
        case 2:
            return mrs(ICH_LR2_EL2);
            break;
        case 3:
            return mrs(ICH_LR3_EL2);
            break;
        case 4:
            return mrs(ICH_LR4_EL2);
            break;
        case 5:
            return mrs(ICH_LR5_EL2);
            break;
        case 6:
            return mrs(ICH_LR6_EL2);
            break;
        case 7:
            return mrs(ICH_LR7_EL2);
            break;
    }
}

void hv_vgic3_write_lr(u32 lr_num, u64 lr_val){
    switch(lr_num){
        case 0:
            msr(ICH_LR0_EL2, lr_val);
            break;
        case 1:
            msr(ICH_LR1_EL2, lr_val);
            break;
        case 2:
            msr(ICH_LR2_EL2, lr_val);
            break;
        case 3:
            msr(ICH_LR3_EL2, lr_val);
            break;
        case 4:
            msr(ICH_LR4_EL2, lr_val);
            break;
        case 5:
            msr(ICH_LR5_EL2, lr_val);
            break;
        case 6:
            msr(ICH_LR6_EL2, lr_val);
            break;
        case 7:
            msr(ICH_LR7_EL2, lr_val);
            break;
    }
    sysop("isb");
}


void hv_vgic3_inject_irq(u32 vintid, u8 priority, bool active, bool pending, bool hw_status, u64 hw_irq){
    u64 val = 0;
    val |= (u64)(vintid & ICH_LR_VIRTUAL_MASK) << ICH_LR_VIRTUAL_SHIFT;
    val |= (u64)(priority & ICH_LR_PRIORITY_MASK) << ICH_LR_PRIORITY_SHIFT;
    val |= ICH_LR_GRP1;

    if(active)
        val |= ICH_LR_STATE_ACTIVE;
    if(pending)
        val |= ICH_LR_STATE_PENDING;
    if(hw_status){
        val |= ICH_LR_HW;
        val |= hw_irq << ICH_LR_PHYSICAL_SHIFT;
    }
    else{
        val |= ICH_LR_MAINTENANCE_IRQ;
    }
    

    int free_lr = hv_vgic3_get_free_lr();
    hv_vgic3_write_lr(free_lr, val);
    sysop("isb");
}

int hv_vgic3_do_iar1(void){
    bool found = false;
    u8 found_priority = 0xff;
    u8 found_lr = -1; 
    for(int lr = 0; lr < 8; lr++){
        u64 lr_val = hv_vgic3_read_lr(lr);
        if(lr_val & ICH_LR_STATE_PENDING){
            u8 priority = (lr_val >> ICH_LR_PRIORITY_SHIFT) & ICH_LR_PRIORITY_MASK;
            if(priority < found_priority){
                found_lr = lr;
                found_priority = priority;
            }
        }
    }

    if(found_lr != -1){
        u64 lr_val = hv_vgic3_read_lr(found_lr);
        lr_val &= ~ICH_LR_STATE_PENDING;
        lr_val |= ICH_LR_STATE_ACTIVE;
        hv_vgic3_write_lr(found_lr, lr_val);
        return (lr_val >> ICH_LR_VIRTUAL_SHIFT) & ICH_LR_VIRTUAL_MASK;
    }

    return 0x3FF;
}

void hv_vgic3_do_eoir1(u64 reg){
    u32 intd = reg & ICH_LR_VIRTUAL_MASK;
    for(int lr = 0; lr < 8; lr++){
        u64 lr_val = hv_vgic3_read_lr(lr);
        //vgic_log("CHECKING LR: 0x%lx %d %d %d\n", lr_val, intd, (lr_val >> ICH_LR_VIRTUAL_SHIFT) & ICH_LR_VIRTUAL_MASK, lr_val & ICH_LR_STATE_ACTIVE);
        if( ((lr_val >> ICH_LR_VIRTUAL_SHIFT) & ICH_LR_VIRTUAL_MASK) == intd && (lr_val & ICH_LR_STATE_ACTIVE)){
            //vgic_log("DOING EOIR 0x%lx, found LR%d: 0x%lx, setting to 0\n", reg, lr, lr_val);
            hv_vgic3_write_lr(lr, 0);
        }
    }
}

void hv_vgic3_set_igrpen1(u64 reg){
    igrpen1 = reg;
    if(reg == 0){
        for(int lr = 0; lr < 8; lr++)
            hv_vgic3_write_lr(lr, 0);
    }
}

u64 hv_vgic3_get_igrpen1(void){
    return igrpen1;
}

#endif

/**
 * @brief hv_vgicv3_init
 * 
 * Initializes the vGIC and prepares it for use by the guest OS.
 * 
 * Note that this function is only expected to be called once.
 * 
 * @return 
 * 
 * 0 - success, vGIC is ready for use by the guest
 * -1 - an error has occurred during vGIC initialization, refer to m1n1 output log for details on the specific error 
 */

void hv_vgicv3_init(void)
{
#ifdef ENABLE_VGIC_MODULE
    printf("HV vGIC DEBUG: start\n");
    vgic_inited = false;
    //
    // First things first - set the parameters appropriately based on whether
    // we're running on a 36-bit or 42-bit platform.
    // Also for now, we are catering to "lowest common denominator" for all chips,
    // so on more powerful systems we may not be using all cores.
    //
    switch(chip_id) {
        case T8103:
        case T8112:
            dist_base = DIST_BASE_36_BIT;
            redist_base = REDIST_BASE_36_BIT;
            its_base = ITS_BASE_36_BIT;
            num_cpus = 8;
            break;
        // case T8010:
        // case T8015:
        // case T8011:
        // case T8012:
        //     dist_base = DIST_BASE_36_BIT;
        //     redist_base = REDIST_BASE_36_BIT;
        //     its_base = ITS_BASE_36_BIT;
        //     break;
        case T6000:
            dist_base = DIST_BASE_42_BIT;
            redist_base = REDIST_BASE_42_BIT;
            its_base = ITS_BASE_42_BIT;
            num_cpus = 8; // cannot assume that we have 10 cores for M1 Pro
        case T6001:
            dist_base = DIST_BASE_42_BIT;
            redist_base = REDIST_BASE_42_BIT;
            its_base = ITS_BASE_42_BIT;
            num_cpus = 10;
        case T6002:
            dist_base = DIST_BASE_42_BIT;
            redist_base = REDIST_BASE_42_BIT;
            its_base = ITS_BASE_42_BIT;
            num_cpus = 20;
        case T6020:
            dist_base = DIST_BASE_42_BIT;
            redist_base = REDIST_BASE_42_BIT;
            its_base = ITS_BASE_42_BIT;
            num_cpus = 10;
        case T6021:
            dist_base = DIST_BASE_42_BIT;
            redist_base = REDIST_BASE_42_BIT;
            its_base = ITS_BASE_42_BIT;
            num_cpus = 12;
        case T6022:
            dist_base = DIST_BASE_42_BIT;
            redist_base = REDIST_BASE_42_BIT;
            its_base = ITS_BASE_42_BIT;
            num_cpus = 24;
        // case T6030:
        // case T6031:
        // case 0x6032:
        // case T6034:
        // case 0x6040:
        // case 0x6041:
        // case T8122:
        //     dist_base = DIST_BASE_42_BIT;
        //     redist_base = REDIST_BASE_42_BIT;
        //     its_base = ITS_BASE_42_BIT;
        //     break;
    }
    //
    // Step 1 - distributor setup.
    //
    printf("HV vGIC DEBUG: setting up distributor\n");
    distributor = heapblock_alloc(sizeof(vgicv3_dist));
    hv_vgicv3_init_dist_registers();
    //
    // Map the vGIC distributor into unoccupied MMIO space.
    //
    printf("HV vGIC DEBUG: mapping distributor into guest space\n");
    hv_map_hook(dist_base, handle_vgic_dist_access, 0x10000);


    /* Redistributor setup */
    printf("HV vGIC DEBUG: setting up redistributors\n");
    redistributors = heapblock_alloc(sizeof(vgicv3_vcpu_redist) * num_cpus);
    hv_vgicv3_init_redist_registers();
    printf("HV vGIC DEBUG: mapping redistributors into guest space\n");
    hv_map_hook(redist_base, handle_vgic_redist_access, ((0x20000) * num_cpus));

    //
    // ITS setup (for MSIs - PCIe devices usually signal via these.)
    // Disabled for now, seems like direct injection into the guest is easier.
    //
    interrupt_translation_service = heapblock_alloc(sizeof(vgicv3_its));
    hv_map_hook(its_base, handle_vgic_its_access, 0x10000);

    //vGIC setup is complete.
    vgic_inited = true;
    return;
#else
    printf("HV vGIC DEBUG: Disabled\n");
    return;
#endif //ENABLE_VGIC_MODULE
}

