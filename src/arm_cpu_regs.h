/* SPDX-License-Identifier: MIT */

#include "types.h"

#define SYS_ACTLR_EL1  sys_reg(3, 0, 1, 0, 1)
#define SYS_ACTLR_EL2  sys_reg(3, 4, 1, 0, 1)
#define SYS_ACTLR_EL3  sys_reg(3, 6, 1, 0, 1)
#define SYS_ACTLR_EL12 sys_reg(3, 5, 1, 0, 1)

#define SYS_CNTHCTL_EL2 sys_reg(3, 4, 14, 1, 0)
// HCR_EL2.E2H == 1
#define CNTHCTL_EVNTIS   BIT(17)
#define CNTHCTL_EL1NVVCT BIT(16)
#define CNTHCTL_EL1NVPCT BIT(15)
#define CNTHCTL_EL1TVCT  BIT(14)
#define CNTHCTL_EL1TVT   BIT(13)
#define CNTHCTL_ECV      BIT(12)
#define CNTHCTL_EL1PTEN  BIT(11)
#define CNTHCTL_EL1PCTEN BIT(10)
#define CNTHCTL_EL0PTEN  BIT(9)
#define CNTHCTL_EL0VTEN  BIT(8)
#define CNTHCTL_EVNTI    GENMASK(7, 4)
#define CNTHCTL_EVNTDIR  BIT(3)
#define CNTHCTL_EVNTEN   BIT(2)
#define CNTHCTL_EL0VCTEN BIT(1)
#define CNTHCTL_EL0PCTEN BIT(0)

#define SYS_CNTV_CTL_EL0  sys_reg(3, 3, 14, 3, 1)
#define SYS_CNTV_CTL_EL02 sys_reg(3, 5, 14, 3, 1)
#define SYS_CNTP_CTL_EL0  sys_reg(3, 3, 14, 2, 1)
#define SYS_CNTP_CTL_EL02 sys_reg(3, 5, 14, 2, 1)
#define SYS_CNTHV_CTL_EL2 sys_reg(3, 4, 14, 3, 1)
#define SYS_CNTHP_CTL_EL2 sys_reg(3, 4, 14, 2, 1)
#define CNTx_CTL_ISTATUS  BIT(2)
#define CNTx_CTL_IMASK    BIT(1)
#define CNTx_CTL_ENABLE   BIT(0)

#define SYS_CNTV_TVAL_EL0  sys_reg(3, 3, 14, 3, 0)
#define SYS_CNTV_CTL_EL0   sys_reg(3, 3, 14, 3, 1)
#define SYS_CNTV_CVAL_EL0  sys_reg(3, 3, 14, 3, 2)
#define SYS_CNTV_TVAL_EL02 sys_reg(3, 5, 14, 3, 0)
#define SYS_CNTV_CTL_EL02  sys_reg(3, 5, 14, 3, 1)
#define SYS_CNTV_CVAL_EL02 sys_reg(3, 5, 14, 3, 2)
#define SYS_CNTP_TVAL_EL0  sys_reg(3, 3, 14, 2, 0)
#define SYS_CNTP_CTL_EL0   sys_reg(3, 3, 14, 2, 1)
#define SYS_CNTP_CVAL_EL0  sys_reg(3, 3, 14, 2, 2)
#define SYS_CNTP_TVAL_EL02 sys_reg(3, 5, 14, 2, 0)
#define SYS_CNTP_CTL_EL02  sys_reg(3, 5, 14, 2, 1)
#define SYS_CNTP_CVAL_EL02 sys_reg(3, 5, 14, 2, 2)

#define SYS_ESR_EL2 sys_reg(3, 4, 5, 2, 0)
#define ESR_ISS2    GENMASK(36, 32)
#define ESR_EC      GENMASK(31, 26)
#define ESR_IL      BIT(25)
#define ESR_ISS     GENMASK(24, 0)

#define ESR_EC_UNKNOWN      0b000000
#define ESR_EC_WFI          0b000001
#define ESR_EC_FP_TRAP      0b000111
#define ESR_EC_PAUTH_TRAP   0b001000
#define ESR_EC_LS64         0b001010
#define ESR_EC_BTI          0b001101
#define ESR_EC_ILLEGAL      0b001110
#define ESR_EC_SVC          0b010101
#define ESR_EC_HVC          0b010110
#define ESR_EC_SMC          0b010111
#define ESR_EC_MSR          0b011000
#define ESR_EC_SVE          0b011001
#define ESR_EC_PAUTH_FAIL   0b011100
#define ESR_EC_IABORT_LOWER 0b100000
#define ESR_EC_IABORT       0b100001
#define ESR_EC_PC_ALIGN     0b100010
#define ESR_EC_DABORT_LOWER 0b100100
#define ESR_EC_DABORT       0b100101
#define ESR_EC_SP_ALIGN     0b100110
#define ESR_EC_FP_EXC       0b101100
#define ESR_EC_SERROR       0b101111
#define ESR_EC_BKPT_LOWER   0b110000
#define ESR_EC_BKPT         0b110001
#define ESR_EC_SSTEP_LOWER  0b110010
#define ESR_EC_SSTEP        0b110011
#define ESR_EC_WATCH_LOWER  0b110100
#define ESR_EC_WATCH        0b110101
#define ESR_EC_BRK          0b111100

#define ESR_ISS_DABORT_ISV   BIT(24)
#define ESR_ISS_DABORT_SAS   GENMASK(23, 22)
#define ESR_ISS_DABORT_SSE   BIT(21)
#define ESR_ISS_DABORT_SRT   GENMASK(20, 16)
#define ESR_ISS_DABORT_SF    BIT(15)
#define ESR_ISS_DABORT_AR    BIT(14)
#define ESR_ISS_DABORT_VNCR  BIT(13)
#define ESR_ISS_DABORT_SET   GENMASK(12, 11)
#define ESR_ISS_DABORT_LSR   GENMASK(12, 11)
#define ESR_ISS_DABORT_FnV   BIT(10)
#define ESR_ISS_DABORT_EA    BIT(9)
#define ESR_ISS_DABORT_CM    BIT(8)
#define ESR_ISS_DABORT_S1PTR BIT(7)
#define ESR_ISS_DABORT_WnR   BIT(6)
#define ESR_ISS_DABORT_DFSC  GENMASK(5, 0)

#define SAS_8B  0
#define SAS_16B 1
#define SAS_32B 2
#define SAS_64B 3

#define ESR_ISS_MSR_OP0       GENMASK(21, 20)
#define ESR_ISS_MSR_OP0_SHIFT 20
#define ESR_ISS_MSR_OP2       GENMASK(19, 17)
#define ESR_ISS_MSR_OP2_SHIFT 17
#define ESR_ISS_MSR_OP1       GENMASK(16, 14)
#define ESR_ISS_MSR_OP1_SHIFT 14
#define ESR_ISS_MSR_CRn       GENMASK(13, 10)
#define ESR_ISS_MSR_CRn_SHIFT 10
#define ESR_ISS_MSR_Rt        GENMASK(9, 5)
#define ESR_ISS_MSR_CRm       GENMASK(4, 1)
#define ESR_ISS_MSR_CRm_SHIFT 1
#define ESR_ISS_MSR_DIR       BIT(0)

#define SYS_HCR_EL2  sys_reg(3, 4, 1, 1, 0)
#define HCR_TWEDEL   GENMASK(63, 60)
#define HCR_TWEDEn   BIT(59)
#define HCR_TID5     BIT(58)
#define HCR_DCT      BIT(57)
#define HCR_ATA      BIT(56)
#define HCR_TTLBOS   BIT(55)
#define HCR_TTLBIS   BIT(54)
#define HCR_EnSCXT   BIT(53)
#define HCR_TOCU     BIT(52)
#define HCR_AMVOFFEN BIT(51)
#define HCR_TICAB    BIT(50)
#define HCR_TID4     BIT(49)
#define HCR_FIEN     BIT(47)
#define HCR_FWB      BIT(46)
#define HCR_NV2      BIT(45)
#define HCR_AT       BIT(44)
#define HCR_NV1      BIT(43)
#define HCR_NV1      BIT(43)
#define HCR_NV       BIT(42)
#define HCR_NV       BIT(42)
#define HCR_API      BIT(41)
#define HCR_APK      BIT(40)
#define HCR_MIOCNCE  BIT(38)
#define HCR_TEA      BIT(37)
#define HCR_TERR     BIT(36)
#define HCR_TLOR     BIT(35)
#define HCR_E2H      BIT(34)
#define HCR_ID       BIT(33)
#define HCR_CD       BIT(32)
#define HCR_RW       BIT(31)
#define HCR_TRVM     BIT(30)
#define HCR_HCD      BIT(29)
#define HCR_TDZ      BIT(28)
#define HCR_TGE      BIT(27)
#define HCR_TVM      BIT(26)
#define HCR_TTLB     BIT(25)
#define HCR_TPU      BIT(24)
#define HCR_TPCP     BIT(23)
#define HCR_TPC      BIT(23)
#define HCR_TSW      BIT(22)
#define HCR_TACR     BIT(21)
#define HCR_TIDCP    BIT(20)
#define HCR_TSC      BIT(19)
#define HCR_TID3     BIT(18)
#define HCR_TID2     BIT(17)
#define HCR_TID1     BIT(16)
#define HCR_TID0     BIT(15)
#define HCR_TWE      BIT(14)
#define HCR_TWI      BIT(13)
#define HCR_DC       BIT(12)
#define HCR_BSU      GENMASK(11, 10)
#define HCR_FB       BIT(9)
#define HCR_VSE      BIT(8)
#define HCR_VI       BIT(7)
#define HCR_VF       BIT(6)
#define HCR_AMO      BIT(5)
#define HCR_IMO      BIT(4)
#define HCR_FMO      BIT(3)
#define HCR_PTW      BIT(2)
#define HCR_SWIO     BIT(1)
#define HCR_VM       BIT(0)

#define SYS_ID_AA64MMFR0_EL1   sys_reg(3, 0, 0, 7, 0)
#define ID_AA64MMFR0_ECV       GENMASK(63, 60)
#define ID_AA64MMFR0_FGT       GENMASK(59, 56)
#define ID_AA64MMFR0_ExS       GENMASK(47, 44)
#define ID_AA64MMFR0_TGran4_2  GENMASK(43, 40)
#define ID_AA64MMFR0_TGran64_2 GENMASK(39, 36)
#define ID_AA64MMFR0_TGran16_2 GENMASK(35, 32)
#define ID_AA64MMFR0_TGran4    GENMASK(31, 28)
#define ID_AA64MMFR0_TGran64   GENMASK(27, 24)
#define ID_AA64MMFR0_TGran16   GENMASK(23, 20)
#define ID_AA64MMFR0_BigEndEL0 GENMASK(19, 16)
#define ID_AA64MMFR0_SNSMem    GENMASK(15, 12)
#define ID_AA64MMFR0_BigEnd    GENMASK(11, 8)
#define ID_AA64MMFR0_ASIDBits  GENMASK(7, 4)
#define ID_AA64MMFR0_PARange   GENMASK(3, 0)

#define SYS_PAR_EL1 sys_reg(3, 0, 7, 4, 0)
// AArch64-PAR_EL1.F == 0b0
#define PAR_ATTR GENMASK(63, 56)
#define PAR_PA   GENMASK(51, 12)
#define PAR_NS   BIT(9)
#define PAR_SH   GENMASK(8, 7)
#define PAR_F    BIT(0)
// AArch64-PAR_EL1.F == 0b1
#define PAR_S   BIT(9)
#define PAR_PTW BIT(8)
#define PAR_FST GENMASK(6, 1)

#define SYS_SCTLR_EL1  sys_reg(3, 0, 1, 0, 0)
#define SYS_SCTLR_EL12 sys_reg(3, 5, 1, 0, 0)
#define SCTLR_EPAN     BIT(57)
#define SCTLR_EnALS    BIT(56)
#define SCTLR_EnAS0    BIT(55)
#define SCTLR_EnASR    BIT(54)
#define SCTLR_TWEDEL   GENMASK(49, 46)
#define SCTLR_TWEDEn   BIT(45)
#define SCTLR_DSSBS    BIT(44)
#define SCTLR_ATA      BIT(43)
#define SCTLR_ATA0     BIT(42)
#define SCTLR_TCF      GENMASK(41, 40)
#define SCTLR_TCF0     GENMASK(39, 38)
#define SCTLR_ITFSB    BIT(37)
#define SCTLR_BT1      BIT(36)
#define SCTLR_BT0      BIT(35)
#define SCTLR_EnIA     BIT(31)
#define SCTLR_EnIB     BIT(30)
#define SCTLR_LSMAOE   BIT(29)
#define SCTLR_nTLSMD   BIT(28)
#define SCTLR_EnDA     BIT(27)
#define SCTLR_UCI      BIT(26)
#define SCTLR_EE       BIT(25)
#define SCTLR_E0E      BIT(24)
#define SCTLR_SPAN     BIT(23)
#define SCTLR_EIS      BIT(22)
#define SCTLR_IESB     BIT(21)
#define SCTLR_TSCXT    BIT(20)
#define SCTLR_WXN      BIT(19)
#define SCTLR_nTWE     BIT(18)
#define SCTLR_nTWI     BIT(16)
#define SCTLR_UCT      BIT(15)
#define SCTLR_DZE      BIT(14)
#define SCTLR_EnDB     BIT(13)
#define SCTLR_I        BIT(12)
#define SCTLR_EOS      BIT(11)
#define SCTLR_EnRCTX   BIT(10)
#define SCTLR_UMA      BIT(9)
#define SCTLR_SED      BIT(8)
#define SCTLR_ITD      BIT(7)
#define SCTLR_nAA      BIT(6)
#define SCTLR_CP15BEN  BIT(5)
#define SCTLR_SA0      BIT(4)
#define SCTLR_SA       BIT(3)
#define SCTLR_C        BIT(2)
#define SCTLR_A        BIT(1)
#define SCTLR_M        BIT(0)

#define SYS_SPSR_EL1  sys_reg(3, 0, 4, 0, 0)
#define SYS_SPSR_EL12 sys_reg(3, 5, 4, 0, 0)
#define SYS_SPSR_EL2  sys_reg(3, 4, 4, 0, 0)
// exception taken from AArch64
#define SPSR_N     BIT(31)
#define SPSR_Z     BIT(30)
#define SPSR_C     BIT(29)
#define SPSR_V     BIT(28)
#define SPSR_TCO   BIT(25)
#define SPSR_DIT   BIT(24)
#define SPSR_UAO   BIT(23)
#define SPSR_PAN   BIT(22)
#define SPSR_SS    BIT(21)
#define SPSR_IL    BIT(20)
#define SPSR_SSBS  BIT(12)
#define SPSR_BTYPE GENMASK(11, 10)
#define SPSR_D     BIT(9)
#define SPSR_A     BIT(8)
#define SPSR_I     BIT(7)
#define SPSR_F     BIT(6)
#define SPSR_M     GENMASK(4, 0)

#define SYS_TCR_EL1    sys_reg(3, 0, 2, 0, 2)
#define TCR_DS         BIT(59)
#define TCR_TCMA1      BIT(58)
#define TCR_TCMA0      BIT(57)
#define TCR_E0PD1      BIT(56)
#define TCR_E0PD0      BIT(55)
#define TCR_NFD1       BIT(54)
#define TCR_NFD0       BIT(53)
#define TCR_TBID1      BIT(52)
#define TCR_TBID0      BIT(51)
#define TCR_HWU162     BIT(50)
#define TCR_HWU161     BIT(49)
#define TCR_HWU160     BIT(48)
#define TCR_HWU159     BIT(47)
#define TCR_HWU062     BIT(46)
#define TCR_HWU061     BIT(45)
#define TCR_HWU060     BIT(44)
#define TCR_HWU059     BIT(43)
#define TCR_HPD1       BIT(42)
#define TCR_HPD0       BIT(41)
#define TCR_HD         BIT(40)
#define TCR_HA         BIT(39)
#define TCR_TBI1       BIT(38)
#define TCR_TBI0       BIT(37)
#define TCR_AS         BIT(36)
#define TCR_IPS        GENMASK(34, 32)
#define TCR_IPS_1TB    0b010UL
#define TCR_IPS_4TB    0b011UL
#define TCR_IPS_16TB   0b100UL
#define TCR_TG1        GENMASK(31, 30)
#define TCR_TG1_16K    0b01UL
#define TCR_TG1_4K     0b10UL
#define TCR_SH1        GENMASK(29, 28)
#define TCR_SH1_IS     0b11UL
#define TCR_ORGN1      GENMASK(27, 26)
#define TCR_ORGN1_WBWA 0b01UL
#define TCR_IRGN1      GENMASK(25, 24)
#define TCR_IRGN1_WBWA 0b01UL
#define TCR_EPD1       BIT(23)
#define TCR_A1         BIT(22)
#define TCR_T1SZ       GENMASK(21, 16)
#define TCR_T1SZ_48BIT 16UL
#define TCR_TG0        GENMASK(15, 14)
#define TCR_TG0_16K    0b10UL
#define TCR_TG0_4K     0b0UL
#define TCR_SH0        GENMASK(13, 12)
#define TCR_SH0_IS     0b11UL
#define TCR_ORGN0      GENMASK(11, 10)
#define TCR_ORGN0_WBWA 0b01UL
#define TCR_IRGN0      GENMASK(9, 8)
#define TCR_IRGN0_WBWA 0b01UL
#define TCR_EPD0       BIT(7)
#define TCR_T0SZ       GENMASK(5, 0)
#define TCR_T0SZ_48BIT 16UL

#define SYS_VTCR_EL2 sys_reg(3, 4, 2, 1, 2)
// Profile(A)
#define VTCR_SL2   BIT(33)
#define VTCR_DS    BIT(32)
#define VTCR_NSA   BIT(30)
#define VTCR_NSW   BIT(29)
#define VTCR_HWU62 BIT(28)
#define VTCR_HWU61 BIT(27)
#define VTCR_HWU60 BIT(26)
#define VTCR_HWU59 BIT(25)
#define VTCR_HD    BIT(22)
#define VTCR_HA    BIT(21)
#define VTCR_VS    BIT(19)
#define VTCR_PS    GENMASK(18, 16)
#define VTCR_TG0   GENMASK(15, 14)
#define VTCR_SH0   GENMASK(13, 12)
#define VTCR_ORGN0 GENMASK(11, 10)
#define VTCR_IRGN0 GENMASK(9, 8)
#define VTCR_SL0   GENMASK(7, 6)
#define VTCR_SL0   GENMASK(7, 6)
#define VTCR_T0SZ  GENMASK(5, 0)

#define SYS_PMCR_EL0 sys_reg(3, 3, 9, 12, 0)
#define PMCR_RESERVED GENMASK(63, 33)
#define PMCR_FZS BIT(32) //Freeze on Statistical Profiling Buffer Management event. (PMBLIMITR_EL1.{PMFZ,E} == {1,1} and PMBSR_EL1.S == 0b1)
#define PMCR_IMP GENMASK(31, 24) //implementer code
#define PMCR_IDCODE GENMASK(23, 16) //deprecated as per arm spec
#define PMCR_N GENMASK(15, 11) //number of event counters implemented
#define PMCR_RESERVED_2 BIT(10)
#define PMCR_FZO BIT(9) // stop event counters on overflow
#define PMCR_RESERVED_3 BIT(8)
#define PMCR_LP BIT(7) // enable long event counters
#define PMCR_LC BIT(6) //enable long cycle counters
#define PMCR_DP BIT(5) //disable cycle counter when event counting is disallowed
#define PMCR_X BIT(4) //export PMU events over IMPDEF bus
#define PMCR_D BIT(3) //set PMCCNTR to count every cycle or every 64 cycles
#define PMCR_C BIT(2) //reset PMCCNTR
#define PMCR_P BIT(1) //reset other event counters
#define PMCR_E BIT(0) //enable



#define SYS_PMCCNTR_EL0 sys_reg(3, 3, 9, 13, 0)
#define PMCCNTR_CCNT GENMASK(63, 0)

#define SYS_PMCCFILTR_EL0 sys_reg(3, 3, 14, 15, 7)
#define PMCCFILTR_RESERVED GENMASK(63, 32)
#define PMCCFILTR_P BIT(31) //sets if el1 can count cycles
#define PMCCFILTR_U BIT(30) //sets if el0 can count cycles
#define PMCCFILTR_NSK BIT(29) //if el3 implemented (not in the Apple case) - further restricts el1 counting
#define PMCCFILTR_NSU BIT(28) //ditto but for el0 counting
#define PMCCFILTR_NSH BIT(27) //ditto for non-secure el2
#define PMCCFILTR_M BIT(26) //ditto for secure EL3
#define PMCCFILTR_RESERVED_2 BIT(25)
#define PMCCFILTR_SH BIT(24) //secure el2 filter
#define PMCCFILTR_T BIT(23) //nontransactional filtering bit
#define PMCCFILTR_RLK BIT(22) // realm el1 filter bit
#define PMCCFILTR_RLU BIT(21) // realm el0 filter bit
#define PMCCFILTR_RLH BIT(20) // realm el2 filter bit
#define PMCCFILTR_RESERVED_3 GENMASK(19, 0)

#define SYS_PMCEID0_EL0 sys_reg(3, 3, 9, 12, 6)
#define PMCEID0_IDHI GENMASK(63, 32)
#define PMCEID0_IDLO GENMASK(31, 0)
//offset by 0x20 because reasons
#define SYS_PMCEID1_EL0 sys_reg(3, 3, 9, 12, 7)
#define PMCEID1_IDHI PMCEID0_IDHI
#define PMCEID1_IDLO PMCEID0_IDLO

#define SYS_PMCNTENCLR_EL0 sys_reg(3, 3, 9, 12, 2) //perf mon counter enable clear (disable reg)
#define PMCNTENCLR_RESERVED GENMASK(63, 32)
#define PMCNTENCLR_C BIT(31) //disables the PMCCNTR_EL0 reg when written to with 1
#define PMCNTENCLR_P GENMASK(30, 0) //disables each individual event counter when written to
#define PMCNTENCLR_P0 BIT(0)
#define PMCNTENCLR_P1 BIT(1)
#define PMCNTENCLR_P2 BIT(2)
#define PMCNTENCLR_P3 BIT(3)
#define PMCNTENCLR_P4 BIT(4)
#define PMCNTENCLR_P5 BIT(5)
#define PMCNTENCLR_P6 BIT(6)
#define PMCNTENCLR_P7 BIT(7)
#define PMCNTENCLR_P8 BIT(8)
#define PMCNTENCLR_P9 BIT(9)
#define PMCNTENCLR_P10 BIT(10)
#define PMCNTENCLR_P11 BIT(11)
#define PMCNTENCLR_P12 BIT(12)
#define PMCNTENCLR_P13 BIT(13)
#define PMCNTENCLR_P14 BIT(14)
#define PMCNTENCLR_P15 BIT(15)
#define PMCNTENCLR_P16 BIT(16)
#define PMCNTENCLR_P17 BIT(17)
#define PMCNTENCLR_P18 BIT(18)
#define PMCNTENCLR_P19 BIT(19)
#define PMCNTENCLR_P20 BIT(20)
#define PMCNTENCLR_P21 BIT(21)
#define PMCNTENCLR_P22 BIT(22)
#define PMCNTENCLR_P23 BIT(23)
#define PMCNTENCLR_P24 BIT(24)
#define PMCNTENCLR_P25 BIT(25)
#define PMCNTENCLR_P26 BIT(26)
#define PMCNTENCLR_P27 BIT(27)
#define PMCNTENCLR_P28 BIT(28)
#define PMCNTENCLR_P29 BIT(29)
#define PMCNTENCLR_P30 BIT(30)

#define SYS_PMCNTENSET_EL0 sys_reg(3, 3, 9, 12, 1)
#define PMCNTENSET_RESERVED GENMASK(63, 32)
#define PMCNTENSET_C BIT(31) //enables the PMCCNTR_EL0 reg when written to with 1
#define PMCNTENSET_P GENMASK(30, 0) //enables each individual event counter when written to
#define PMCNTENSET_P0 BIT(0)
#define PMCNTENSET_P1 BIT(1)
#define PMCNTENSET_P2 BIT(2)
#define PMCNTENSET_P3 BIT(3)
#define PMCNTENSET_P4 BIT(4)
#define PMCNTENSET_P5 BIT(5)
#define PMCNTENSET_P6 BIT(6)
#define PMCNTENSET_P7 BIT(7)
#define PMCNTENSET_P8 BIT(8)
#define PMCNTENSET_P9 BIT(9)
#define PMCNTENSET_P10 BIT(10)
#define PMCNTENSET_P11 BIT(11)
#define PMCNTENSET_P12 BIT(12)
#define PMCNTENSET_P13 BIT(13)
#define PMCNTENSET_P14 BIT(14)
#define PMCNTENSET_P15 BIT(15)
#define PMCNTENSET_P16 BIT(16)
#define PMCNTENSET_P17 BIT(17)
#define PMCNTENSET_P18 BIT(18)
#define PMCNTENSET_P19 BIT(19)
#define PMCNTENSET_P20 BIT(20)
#define PMCNTENSET_P21 BIT(21)
#define PMCNTENSET_P22 BIT(22)
#define PMCNTENSET_P23 BIT(23)
#define PMCNTENSET_P24 BIT(24)
#define PMCNTENSET_P25 BIT(25)
#define PMCNTENSET_P26 BIT(26)
#define PMCNTENSET_P27 BIT(27)
#define PMCNTENSET_P28 BIT(28)
#define PMCNTENSET_P29 BIT(29)
#define PMCNTENSET_P30 BIT(30)

//for now only define PMEVCNTR0
#define SYS_PMEVCNTR0_EL0 sys_reg(3, 3, 13, 8, 0)
#define PMEVCNTR_COUNTER GENMASK(63, 0)

#define SYS_PMEVTYPER0_EL0 sys_reg(3, 3, 14, 12, 0)
#define PMEVTYPER_TC GENMASK(63, 61)
#define PMEVTYPER_RESERVED GENMASK(60, 44)
#define PMEVTYPER_TH GENMASK(43, 32)
#define PMEVTYPER_P BIT(31) //controls el1 counting
#define PMEVTYPER_U BIT(30) //controls el0 counting
#define PMEVTYPER_NSK BIT(29) //el1 non-secure counting when el3 is implemented
#define PMEVTYPER_NSU BIT(28) //el0 non-secure counting when el3 implemented
#define PMEVTYPER_NSH BIT(27) //controls el2 counting
#define PMEVTYPER_M BIT(26) //controls el3 counting
#define PMEVTYPER_MT BIT(25) //multithreaded event counting
#define PMEVTYPER_SH BIT(24) //controls secure el2 counting (not implemented on Apple)
#define PMEVTYPER_T BIT(23) //transactional state filter
#define PMEVTYPER_RLK BIT(22) //realm el1 filter bit
#define PMEVTYPER_RLU BIT(21) //realm el0 filter bit
#define PMEVTYPER_RLH BIT(20) //realm el2 filter bit
#define PMEVTYPER_RESERVED_2 GENMASK(19, 16)
#define PMEVTYPER_EVTCOUNT GENMASK(15, 10)
#define PMEVTYPER_EVTCOUNT2 GENMASK(9, 0)


#define SYS_PMINTENCLR_EL1 sys_reg(3, 0, 9, 14, 2)
#define PMINTENCLR_RESERVED GENMASK(63, 32)
#define PMINTENCLR_C BIT(31) //cycle counter overflow irq is disabled if 1 is written to here
#define PMINTENCLR_P GENMASK(30, 0)

/**
 * TODO: define PMINTENCLR_P bits here
 * 
 */

#define SYS_PMINTENSET_EL1 sys_reg(3, 0, 9, 14, 1)
#define PMINTENSET_RESERVED GENMASK(63, 32)
#define PMINTENSET_C BIT(31) //cycle count overflow irq enable if 1 written to here
#define PMINTENSET_P PMINTENCLR_P


#define SYS_PMMIR_EL1 sys_reg(3, 0, 9, 13, 5)
#define PMMIR_RESERVED GENMASK(63, 24)
#define PMMIR_THWIDTH GENMASK(23, 20)
#define PMMIR_BUS_WIDTH GENMASK(19, 16)
#define PMMIR_BUS_SLOTS GENMASK(15, 8)
#define PMMIR_SLOTS GENMASK(7, 0)


#define SYS_PMOVSCLR_EL0 sys_reg(3, 3, 9, 12, 3)
#define PMOVSCLR_RESERVED GENMASK(63, 32)
#define PMOVSCLR_C BIT(31) //if 1, cycle counter has overflowed, when written to, clears overflow bit
#define PMOVSCLR_P GENMASK(30, 0)

#define SYS_PMOVSSET_EL0 sys_reg(3, 3, 9, 14, 3)
#define PMOVSSET_RESERVED PMOVSCLR_RESERVED
#define PMOVSSET_C BIT(31)
#define PMOVSSET_P GENMASK(30, 0)


#define SYS_PMSELR_EL0 sys_reg(3, 3, 9, 12, 5)
#define PMSELR_RESERVED GENMASK(63, 5)
#define PMSELR_SEL GENMASK(4, 0)

#define SYS_PMSWINC_EL0 sys_reg(3, 3, 9, 12, 4)
#define PMSWINC_RESERVED GENMASK(63, 31)
#define PMSWINC_P GENMASK(30, 0)

#define SYS_PMUSERENR_EL0 sys_reg(3, 3, 9, 14, 0)
#define PMUSERENR_RESERVED GENMASK(63, 4)
#define PMUSERENR_ER BIT(3) //enables or disables user access to event counters
#define PMUSERENR_CR BIT(2) //enables/disables user mode access to cycle counters
#define PMUSERENR_SW BIT(1) //traps software increments to el1
#define PMUSERENR_EN BIT(0) //enables/disables all pmu access from el0

#define SYS_PMXEVCNTR_EL0 sys_reg(3, 3, 9, 13, 2)
#define PMXEVCNTR_VALUE_MASK GENMASK(63, 0)

#define SYS_PMXEVTYPER_EL0 sys_reg(3, 3, 9, 13, 1)
#define PMXEVTYPER_VALUE_MASK GENMASK(63, 0)