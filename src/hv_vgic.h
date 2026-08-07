/* SPDX-License-Identifier: MIT */

#ifndef HV_VGIC_H
#define HV_VGIC_H
#ifdef ENABLE_VGIC_MODULE

#define VIRQ_QUEUE_SIZE 32

//========
// Offsets
//========

//
// Distributor offsets
//
#define GIC_DIST_CTLR 0x0
#define GIC_DIST_TYPER 0x4
#define GIC_DIST_IIDR 0x8
#define GIC_DIST_TYPER2 0xC
#define GIC_DIST_STATUSR 0x10
#define GIC_DIST_SETSPI_NSR 0x40
#define GIC_DIST_CLRSPI_NSR 0x48
#define GIC_DIST_SETSPI_SR 0x50
#define GIC_DIST_CLRSPI_SR 0x58


#define GIC_DIST_IGROUPR0 0x80
#define GIC_DIST_IGROUPR31 0xFC

#define GIC_DIST_ISENABLER0 0x100
#define GIC_DIST_ISENABLER31 0x17C

#define GIC_DIST_ICENABLER0 0x180
#define GIC_DIST_ICENABLER31 0x1FC

#define GIC_DIST_ISPENDR0 0x200
#define GIC_DIST_ISPENDR31 0x27C

#define GIC_DIST_ICPENDR0 0x280
#define GIC_DIST_ICPENDR31 0x2FC

#define GIC_DIST_ISACTIVER0 0x300
#define GIC_DIST_ISACTIVER31 0x37C

#define GIC_DIST_ICACTIVER0 0x380
#define GIC_DIST_ICACTIVER31 0x3FC

#define GIC_DIST_IPRIORITYR0 0x400
#define GIC_DIST_IPRIORITYR254 0x7F8

//
// Apple platforms only support affinity routing *on*, so these will be reserved.
//
#define GIC_DIST_ITARGETSR0 0x800
#define GIC_DIST_ITARGETSR254 0xBF8

#define GIC_DIST_ICFGR0 0xC00
#define GIC_DIST_ICFGR63 0xCFC

//
// These are RAZ/WI. (GICD_CTLR.DS = 1 always for us.)
//
#define GIC_DIST_IGRPMODR0 0xD00
#define GIC_DIST_IGRPMODR31 0xD7C

//
// Ditto the above.
//
#define GIC_DIST_NSACR0 0xE00
#define GIC_DIST_NSACR63 0xEFC
#define GIC_DIST_SGIR 0xF00
#define GIC_DIST_CPENDSGIR0 0xF10
#define GIC_DIST_CPENDSGIR3 0xF1C
#define GIC_DIST_SPENDSGIR0 0xF20
#define GIC_DIST_SPENDSGIR3 0xF2C

//
// NMI registers. Since Apple does not virtualize a GICv4, we can't use these.
//
#define GIC_DIST_INMIR0 0xF80
#define GIC_DIST_INMIR31 0xFFC

//
// Extended SPI range group registers
//
#define GIC_DIST_IGROUPR0E 0x1000
#define GIC_DIST_IGROUPR31E 0x107C
#define GIC_DIST_ISENABLER0E 0x1200
#define GIC_DIST_ISENABLER31E 0x127C
#define GIC_DIST_ICENABLER0E 0x1400
#define GIC_DIST_ICENABLER31E 0x147C
#define GIC_DIST_ISPENDR0E 0x1600
#define GIC_DIST_ISPENDR31E 0x167C
#define GIC_DIST_ICPENDR0E 0x1800
#define GIC_DIST_ICPENDR31E 0x187C
#define GIC_DIST_ISACTIVER0E 0x1A00
#define GIC_DIST_ISACTIVER31E 0x1A7C
#define GIC_DIST_ICACTIVER0E 0x1C00
#define GIC_DIST_ICACTIVER31E 0x1C7C
#define GIC_DIST_IPRIORITYR0E 0x2000
#define GIC_DIST_IPRIORITYR255E 0x23FC
#define GIC_DIST_ICFGR0E 0x3000
#define GIC_DIST_ICFGR63E 0x30FC
#define GIC_DIST_IGRPMODR0E 0x3400
#define GIC_DIST_IGRPMODR31E 0x347C
#define GIC_DIST_NSACR0E 0x3600
#define GIC_DIST_NSACR63E 0x36FC

#define GIC_DIST_INMIR0E 0x3B00
#define GIC_DIST_INMIR31E 0x3B7C

#define GIC_DIST_IROUTER32 0x6100
#define GIC_DIST_IROUTER1019 0x7FD8
#define GIC_DIST_IROUTER0E 0x8000
#define GIC_DIST_IROUTER1023E 0x9FFC

#define GIC_DIST_PIDR2 0xFFE8


//
// Redistributor offsets
//

#define GIC_REDIST_CTLR 0x0
#define GIC_REDIST_IIDR 0x4
#define GIC_REDIST_TYPER 0x8
#define GIC_REDIST_STATUSR 0x10
#define GIC_REDIST_WAKER 0x14
#define GIC_REDIST_MPAMIDR 0x18
#define GIC_REDIST_PARTIDR 0x1C
#define GIC_REDIST_SETLPIR 0x40
#define GIC_REDIST_CLRLPIR 0x48
#define GIC_REDIST_PROPBASER 0x70
#define GIC_REDIST_PENDBASER 0x78
#define GIC_REDIST_INVLPIR 0xA0
#define GIC_REDIST_INVALLR 0xB0
#define GIC_REDIST_SYNCR 0xC0

//
// SGI base relative registers in the redistributor
//
#define GIC_REDIST_IGROUPR0 0x10080
#define GIC_REDIST_ISENABLER0 0x10100
#define GIC_REDIST_ICENABLER0 0x10180
#define GIC_REDIST_ISPENDR0 0x10200
#define GIC_REDIST_ICPENDR0 0x10280
#define GIC_REDIST_ISACTIVER0 0x10300
#define GIC_REDIST_ICACTIVER0 0x10380
#define GIC_REDIST_IPRIORITYR0 0x10400
#define GIC_REDIST_IPRIORITYR1 0x10404
#define GIC_REDIST_IPRIORITYR2 0x10408
#define GIC_REDIST_IPRIORITYR3 0x1040C
#define GIC_REDIST_IPRIORITYR4 0x10410
#define GIC_REDIST_IPRIORITYR5 0x10414
#define GIC_REDIST_IPRIORITYR6 0x10418
#define GIC_REDIST_IPRIORITYR7 0x1041C
#define GIC_REDIST_ICFGR0 0x10C00
#define GIC_REDIST_ICFGR1 0x10C04
#define GIC_REDIST_IGRPMODR0 0x10D00
#define GIC_REDIST_NSACR 0x10E00

#define GITS_CTLR 0x000
#define GITS_IIDR 0x004
#define GITS_TYPER 0x008
#define GITS_CBASER 0x080
#define GITS_CWRITER 0x088
#define GITS_CREADR 0x090
#define GITS_BASER_NR_REGS 8
#define GITS_BASER0 0x100
#define GITS_BASER1 0x108
#define GITS_BASER2 0x110
#define GITS_BASER3 0x118
#define GITS_BASER4 0x120
#define GITS_BASER5 0x128
#define GITS_BASER6 0x130
#define GITS_BASER7 0x138
#define GITS_PIDR2 GICR_PIDR2

#define ICH_LR_VIRTUAL_MASK          0xffff
#define ICH_LR_VIRTUAL_SHIFT         0
#define ICH_LR_CPUID_MASK            0x7
#define ICH_LR_CPUID_SHIFT           10
#define ICH_LR_PHYSICAL_MASK         0x1fff
#define ICH_LR_PHYSICAL_SHIFT        32
#define ICH_LR_STATE_MASK            0x3
#define ICH_LR_STATE_SHIFT           62
#define ICH_LR_STATE_PENDING         (1ULL << 62)
#define ICH_LR_STATE_ACTIVE          (1ULL << 63)
#define ICH_LR_PRIORITY_MASK         0xff
#define ICH_LR_PRIORITY_SHIFT        48
#define ICH_LR_HW_MASK               0x1
#define ICH_LR_HW_SHIFT              61
#define ICH_LR_GRP_MASK              0x1
#define ICH_LR_GRP_SHIFT             60
#define ICH_LR_MAINTENANCE_IRQ       (1ULL << 41)
#define ICH_LR_GRP1                  (1ULL << 60)
#define ICH_LR_HW                    (1ULL << 61)


#define ICH_SGI_IRQMODE_SHIFT        40
#define ICH_SGI_IRQMODE_MASK         0x1
#define ICH_SGI_TARGET_OTHERS        1ULL
#define ICH_SGI_TARGET_LIST          0
#define ICH_SGI_IRQ_SHIFT            24
#define ICH_SGI_IRQ_MASK             0xf
#define ICH_SGI_TARGETLIST_MASK      0xffff
#define ICH_SGI_RS_SHIFT             44
#define ICH_SGI_RS_MASK              0xf
#define ICH_SGI_AFF1(x)              (((x) >> 16) & 0xFF)
#define ICH_SGI_AFF2(x)              (((x) >> 32) & 0xFF)
#define ICH_SGI_AFF3(x)              (((x) >> 48) & 0xFF)


//========
// Structs
//========

/**
 * Distributor registers
 * 
 * These are global to the system, accesses from the guest via MMIO writes or reads will read/write data from an instance
 * of this struct.
 * 
 */

typedef struct vgicv3_distributor_registers {
    //0x0000-0x0010
    // Control, type, implementer ID, type register 2, error status regs

    //GICD_CTLR
    unsigned int gicd_ctl_reg;
    //GICD_TYPER
    unsigned int gicd_type_reg;
    //GICD_IIDR
    unsigned int gicd_imp_id_reg;
    //GICD_TYPER2
    unsigned int gicd_type_reg_2;
    //GICD_STATUSR
    unsigned int gicd_err_sts;
    
    //0x0040 - GICD_SETSPI_NSR
    // Set SPI reg, non secure mode
    unsigned int gicd_set_spi_reg;
    
    //0x0048 - GICD_CLRSPI_NSR
    // Clear SPI reg, non secure mode
    unsigned int gicd_clear_spi_reg;

    //0x0080-0x00fc
    unsigned int gicd_interrupt_group_regs[32];

    //0x0100-0x017c
    unsigned int gicd_interrupt_set_enable_regs[32];

    //0x0180-0x01fc
    unsigned int gicd_interrupt_clear_enable_regs[32];

    //0x0200-0x027c
    unsigned int gicd_interrupt_set_pending_regs[32];

    //0x0280-0x02fc
    unsigned int gicd_interrupt_clear_pending_regs[32];

    //0x0300-0x037c
    unsigned int gicd_interrupt_set_active_regs[32];

    //0x038c-0x03fc
    unsigned int gicd_interrupt_clear_active_regs[32];

    //0x0400-0x07f8
    unsigned int gicd_interrupt_priority_regs[255];

    //0x0800-0x081c - GICD_ITARGETSR0-R7
    //reserved, Apple SoCs do not support legacy operation, so this is useless
    unsigned int gicd_interrupt_processor_target_regs_ro[8];

    //0x0820-0xBF8 - GICD_ITARGETSR8-R255
    //ditto above
    unsigned int gicd_interrupt_processor_target_regs_ro_2[248];

    //0x0C00-0x0CFC - GICD_ICFGR0-63
    unsigned int gicd_interrupt_config_regs[64];

    //0x0D00-0x0D7C - GICD_IGRPMODR0-31
    // RAZ/WI since we only support a single security state (EL2/GL2 get treated equally in this regard)
    unsigned int gicd_interrupt_group_modifier_regs[32];

    //0x0E00-0x0EFC - GICD_NSACR0-63
    //i have doubts as to whether this is necessary, given M series don't implement EL3
    unsigned int gicd_interrupt_nonsecure_access_ctl_regs[64];

    //0x0F00 - GICD_SGIR (software generated interrupts)
    unsigned int gicd_interrupt_software_generated_reg;

    //0x0F10-0x0F1C - GICD_CPENDSGIR0-3
    unsigned int gicd_interrupt_sgi_clear_pending_regs[4];

    //0x0F20-0x0F2C - GICD_SPENDSGIR0-3
    unsigned int gicd_interrupt_sgi_set_pending_regs[4];

    //0x0F80-0x0FFC - GICD_INMIR - NMI Regs
    //Apple SoCs as of 8/17/2022 do not implement NMI, these will never be used by anything but add them so that the size of the dist follows ARM spec
    unsigned int gicd_interrupt_nmi_regs[32];

    //0x1000-0x107C - GICD_IGROUPR0E-31E
    unsigned int gicd_interrupt_group_regs_ext_spi_range[32];

    //0x1200-0x127C - GICD_ISENABLER0E-31E
    unsigned int gicd_interrupt_set_enable_ext_spi_range_regs[32];

    //0x1400-0x147C - GICD_ICENABLER0E-31E
    unsigned int gicd_interrupt_clear_enable_ext_spi_range_regs[32];

    //0x1600-0x167C - GICD_ISPENDR0E-31E
    unsigned int gicd_interrupt_set_pending_ext_spi_range_regs[32];

    //0x1800-0x187C - GICD_ICPENDR0E-31E
    unsigned int gicd_interrupt_clear_pending_ext_spi_range_regs[32];

    //0x1A00-0x1A7C - GICD_ISACTIVER0E-31E
    unsigned int gicd_interrupt_set_active_ext_spi_range_regs[32];

    //0x1C00-0x1C7C - GICD_ICACTIVER0E-31E
    unsigned int gicd_interrupt_clear_active_ext_spi_range_regs[32];

    //0x2000-0x23FC - GICD_IPRIORITYR0E-255E
    unsigned int gicd_interrupt_priority_ext_spi_range_regs[256];

    //0x3000-0x30FC - GICD_ICFGR0E-63E
    unsigned int gicd_interrupt_ext_spi_config_regs[64];

    //0x3400-0x347C - GICD_IGRPMODR0E-61E
    unsigned int gicd_interrupt_group_modifier_ext_spi_range_regs[32];

    //0x3600-0x367C - GICD_NSACR0E-31E
    unsigned int gicd_non_secure_ext_spi_range_interrupt_regs[32];

    //0x3B00-0x3B7C
    //NMI regs for extended SPI range
    //ditto above point, no NMI support on Apple chips, but add it so that the size of the dist is the same as ARM spec
    unsigned int gicd_interrupt_nmi_reg_ext_spi_range[32];

    //0x6100-0x7FD8 - GICD_IROUTER(32-1019)
    unsigned long gicd_interrupt_router_regs[988];

    //0x8000-0x9FFC - GICD_IROUTER(0-1023)E
    unsigned long gicd_interrupt_router_ext_spi_range_regs[1024];


} vgicv3_dist;
//
// Redistributor registers (we're emulating a GICv3 so only need the RD and SGI frames.)
//
typedef struct vgicv3_redistributor_rd_region {
    //GICR_CTLR
    unsigned int gicr_ctl_reg;
    //GICR_IIDR
    unsigned int gicr_iidr;
    //GICR_TYPER
    uint64_t gicr_type_reg;
    //GICR_STATUSR
    unsigned int gicr_status_reg;
    //GICR_WAKER
    unsigned int gicr_wake_reg;
    //GICR_MPAMIDR
    unsigned int gicr_mpamidr;
    //GICR_PARTIDR
    unsigned int gicr_partidr;
    //0x20 - 0x3C: IMPDEF registers - we can put whatever type of extra register we need in this region.
    unsigned int gicr_impdef_reserved0[7];
    //0x40 - GICR_SETLPIR
    uint64_t gicr_setlpir;
    //0x48 - GICR_CLRLPIR
    uint64_t gicr_clrlpir;
    //0x50-0x6C - GICR reserved region
    // unsigned int gicr_reserved0[7];
    //0x70 - GICR_PROPBASER
    uint64_t gicr_propbaser;
    //0x78 - GICR_PENDBASER
    uint64_t gicr_pendbaser;
    //0x80-0xA0 - reserved region.
    // uint64_t gicr_reserved1[4];
    //0xA0 - GICR_INVLPIR
    uint64_t gicr_invlpir;
    //0xA8 - reserved
    // uint64_t gicr_reserved2;
    //0xB0 - GICR_INVALLR
    uint64_t gicr_invallr;
    //0xB8 - reserved
    // uint64_t gicr_reserved3;
    //0xC0 - GICR_SYNCR
    uint64_t gicr_syncr;
    //0xC8 - 0xFC - reserved
    // unsigned int gicr_reserved4[13];
    //0x100 - GICR IMPDEF register
    uint64_t gicr_impdef_reserved1;
    //0x108 - reserved
    // uint64_t gicr_reserved5;
    //0x110 - GICR IMPDEF register
    uint64_t gicr_impdef_reserved2;
    //0x118-0xBFFC - reserved
    // unsigned int gicr_reserved6[12217];
    //0xc000-0xFFCC - reserved
    // unsigned int gicr_reserved8[4083];
    //0xFFCC - filler
    // unsigned int gicr_reserved9;
    //0xFFD0 - 0xFFFC - reserved for id registers
    uint64_t gicr_reserved_idreg[6];
    
} vgicv3_redistributor_rd_region_t;

typedef struct vgicv3_redistributor_sgi_region {
    //0x0-0x7C - padding
    // uint64_t gicr_sgi_region_padding0[16];
    //0x80 - GICR_IGROUPR0
    unsigned int gicr_igroupr0;
    //0x84 - GICR_IGROUPR1E
    unsigned int gicr_igroupr1e;
    //0x88 - GICR_IGROUPR2E
    unsigned int gicr_igroupr2e;
    //0x8C-0x100 - padding
    // unsigned int gicr_sgi_region_padding1[5];
    //0x100 - GICR_ISENABLER0
    unsigned int gicr_isenabler0;
    //0x104 - GICR_ISENABLER1E
    unsigned int gicr_isenabler1e;
    //0x108 - GICR_ISENABLER2E
    unsigned int gicr_isenabler2e;
    //0x10C-0x180 - padding
    // unsigned int gicr_sgi_region_padding2[29];
    //0x180 - GICR_ICENABLER0
    unsigned int gicr_icenabler0;
    //0x184 - GICR_ICENABLER1E
    unsigned int gicr_icenabler1e;
    //0x188 - GICR_ICENABLER2E
    unsigned int gicr_icenabler2e;
    //0x18C-0x200 - padding
    // unsigned int gicr_sgi_region_padding3[29];
    //0x200 - GICR_ISPENDR0
    unsigned int gicr_ispendr0;
    //0x204 - GICR_ISPENDR1E
    unsigned int gicr_ispendr1e;
    //0x208 - GICR_ISPENDR2E
    unsigned int gicr_ispendr2e;
    //0x20C-0x280 - padding
    // unsigned int gicr_sgi_region_padding4[29];
    //0x280 - GICR_ICPENDR0
    unsigned int gicr_icpendr0;
    //0x284 - GICR_ICPENDR1E
    unsigned int gicr_icpendr1e;
    //0x288 - GICR_ICPENDR2E
    unsigned int gicr_icpendr2e;
    //0x28C-0x300 - padding
    // unsigned int gicr_sgi_region_padding5[29];
    //0x300 - GICR_ISACTIVER0
    unsigned int gicr_isactiver0;
    //0x304 - GICR_ISACTIVER1E
    unsigned int gicr_isactiver1e;
    //0x308 - GICR_ISACTIVER2E
    unsigned int gicr_isactiver2e;
    //0x30C-0x380 - padding
    // unsigned int gicr_sgi_region_padding6[29];
    //0x380 - GICR_ICACTIVER0
    unsigned int gicr_icactiver0;
    //0x384 - GICR_ICACTIVER1E
    unsigned int gicr_icactiver1e;
    //0x388 - GICR_ICACTIVER2E
    unsigned int gicr_icactiver2e;
    //0x38C-0x3FC - padding
    // unsigned int gicr_sgi_region_padding7[28];
    //0x400-0x40C - GICR_IPRIORITYR(0-3) (SGI priorities)
    unsigned int gicr_sgi_ipriority_reg[4];
    //0x410-0x41C - GICR_IPRIORITYR(4-7) (PPI/core specific interrupt priorities)
    unsigned int gicr_ppi_ipriority_reg[4];
    //0x420-0x45C - GICR_IPRIORITYR(8-23)E
    unsigned int gicr_ipriorityr_ext_ppi[15];
    //0x460-0xBFC - padding
    // unsigned int gicr_sgi_region_padding8[487];
    //0xC00 - GICR_ICFGR0
    unsigned int gicr_icfgr0;
    //0xC04 - GICR_ICFGR1
    unsigned int gicr_icfgr1;
    //0xC08 - GICR_ICFGR2E
    unsigned int gicr_icfgr2e;
    //0xC0C - GICR_ICFGR3E
    unsigned int gicr_icfgr3e;
    //0xC10 - GICR_ICFGR4E
    unsigned int gicr_icfgr4e;
    //0xC14 - GICR_ICFGR5E
    unsigned int gicr_icfgr5e;
    //0xC14-0xCFC - padding
    // unsigned int gicr_sgi_region_padding9[58];
    //0xD00 - GICR_IGRPMODR0
    unsigned int gicr_igrpmodr0;
    //0xD04 - GICR_IGRPMODR1E
    unsigned int gicr_igrpmodr1e;
    //0xD08 - GICR_IGRPMODR2E
    unsigned int gicr_igrpmodr2e;
    //0xD0C-0xDFC - padding
    // unsigned int gicr_sgi_region_padding10[60];
    //0xE00 - GICR_NSACR
    unsigned int gicr_nsacr;
    //0xE04-0xF7C - reserved
    // unsigned int gicr_sgi_region_reserved0[94];
    //0xF80 - GICR_INMIR0
    unsigned int gicr_inmir0;
    //0xF84-0xFFC - GICR_INMIR(1-2)E (leaving this RAZ/WI for now)
    unsigned int gicr_inmir_e[30];
    //0x1000-0xBFFC - reserved
    // unsigned int gicr_sgi_region_reserved1[11263];
    //0xC000-0xFFCC - IMPDEF registers
    unsigned int gicr_sgi_region_impdef_reserved0[4083];
    //0xFFD0-0xFFFC - reserved
    // unsigned int gicr_sgi_region_reserved2[11];


} vgicv3_redistributor_sgi_region_t;

typedef struct vgicv3_redistributor_region {
    vgicv3_redistributor_rd_region_t rd_region;
    vgicv3_redistributor_sgi_region_t sgi_region;
} vgicv3_vcpu_redist;

//
// ITS registers
//

typedef struct vgicv3_its_ctl_region {
    //0x0 - GITS_CTLR
    unsigned int gits_ctl_reg;
    //0x4 - GITS_IIDR
    unsigned int gits_iidr;
    //0x8 - GITS_TYPER
    uint64_t gits_type_reg;
    //0x10 - GITS_MPAMIDR
    unsigned int gits_mpamidr;
    //0x14 - GITS_PARTIDR
    unsigned int gits_partidr;
    //0x18 - GITS_MPIDR
    unsigned int gits_mpidr;
    //0x1C - reserved
    unsigned int gits_reserved0;
    //0x20-0x3C - IMPDEF reserved
    unsigned int gits_impdef_reserved0[7];
    //0x40 - GITS_STATUSR
    unsigned int gits_statusr;
    //0x44 - reserved
    unsigned int gits_reserved1;
    //0x48 - GITS_UMSIR
    uint64_t gits_umsir;
    //0x50-0x7C - reserved
    unsigned int gits_reserved2[11];
    //0x80 - GITS_CBASER
    uint64_t gits_cbaser;
    //0x88 - GITS_CWRITER
    uint64_t gits_cwriter;
    //0x90 - GITS_CREADR
    uint64_t gits_creadr;
    //0x98-0xFC - reserved
    unsigned int gits_reserved3[25];
    //0x100-0x138 - GITS_BASER(0-7)
    uint64_t gits_baser[8];
    //0x140-0xBFFC - reserved
    unsigned int gits_reserved4[12207];
    //0xC000-0xFFCC - IMPDEF registers
    unsigned int gits_impdef_reserved1[4083];
    //0xFFD0-0xFFFC - ID register reserved
    unsigned int gits_impdef_reserved2[11];
} vgicv3_its_ctl_region_t;

typedef struct vgicv3_its_translation_region {
    //0x0-0x3C - reserved
    unsigned int gits_translation_reserved0[15];
    //0x40 - GITS_TRANSLATER
    unsigned int gits_translater;
    //0x44-0xFFFC - reserved
    unsigned int gits_translation_reserved1[16366];
} vgicv3_its_translation_region_t;

typedef struct vgicv3_its_region {
    vgicv3_its_ctl_region_t its_ctl_region;
    //TODO: check if there needs to be a filler here.
    vgicv3_its_translation_region_t its_translation_region;
} vgicv3_its;


/* vGIC */

void hv_vgicv3_init(void);

void hv_vgicv3_init_dist_registers(void);

void hv_vgicv3_init_redist_registers(void);

void hv_vgicv3_init_list_registers(void);

int hv_vgicv3_enable_virtual_interrupts(void);

u8 hv_vgic3_get_priority(u64 intd);

int hv_vgic3_get_free_lr(void);

u64 hv_vgic3_read_lr(u32 lr_num);

void hv_vgic3_write_lr(u32 lr_num, u64 lr_val);

void hv_vgic3_inject_irq(u32 vintid, u8 priority, bool active, bool pending, bool hw_status, u64 hw_irq);

int hv_vgic3_do_iar1(void);

void hv_vgic3_do_eoir1(u64 reg);

void hv_vgic3_set_igrpen1(u64 reg);

u64 hv_vgic3_get_igrpen1(void);

typedef struct {
    u32 vintid;
    u8 priority;
    bool active;
    bool pending;
    bool hw_status;
    u64 hw_irq;
} virq_t;

typedef struct {
    virq_t     buf[VIRQ_QUEUE_SIZE];
    u32        head;
    u32        tail;
    spinlock_t p_lock;
} virq_queue_t;

static inline void virq_queue_init(virq_queue_t *q)
{
    q->head = q->tail = 0;
    spin_init(&q->p_lock);
}

static inline bool virq_queue_push(virq_queue_t *q, const virq_t *v)
{
    bool result = false;
    spin_lock(&q->p_lock);

    u32 head = __atomic_load_n(&q->head, __ATOMIC_RELAXED);
    u32 tail = __atomic_load_n(&q->tail, __ATOMIC_ACQUIRE);
    if ((head - tail) < VIRQ_QUEUE_SIZE) {
        q->buf[head & (VIRQ_QUEUE_SIZE - 1)] = *v;
        __atomic_store_n(&q->head, head + 1, __ATOMIC_RELEASE);
        result = true;
    }

    sysop("dsb ishst");
    spin_unlock(&q->p_lock);
    return result;
}

static inline bool virq_queue_pop(virq_queue_t *q, virq_t *out)
{
    u32 tail = __atomic_load_n(&q->tail, __ATOMIC_RELAXED);
    u32 head = __atomic_load_n(&q->head, __ATOMIC_ACQUIRE);
    if (tail == head)
        return false;

    *out = q->buf[tail & (VIRQ_QUEUE_SIZE - 1)];
    __atomic_store_n(&q->tail, tail + 1, __ATOMIC_RELEASE);

    sysop("dsb ishst");
    return true;
}

#endif //ENABLE_VGIC_MODULE
#endif //HV_VGIC_H