#ifndef __DIFFSTATE_H__
#define __DIFFSTATE_H__

#include <cstdint>

#define CONFIG_DIFFTEST_DPIC
#define CONFIG_DIFFTEST_ZONESIZE 1
#define CONFIG_DIFFTEST_BUFLEN 64
#define CONFIG_DIFFTEST_BATCH
#define CONFIG_DIFFTEST_BATCH_SIZE 64
#define CONFIG_DIFFTEST_BATCH_BYTELEN 2000

#define CPU_XIANGSHAN

#define NUM_CORES 1

#define CONFIG_DIFFTEST_REFILLEVENT
#define CONFIG_DIFF_REFILL_WIDTH 11
typedef struct  {
  uint8_t  valid;
  uint64_t addr;
  uint64_t data[8];
  uint8_t  idtfr;
} DifftestRefillEvent;

#define CONFIG_DIFFTEST_VECCSRSTATE
typedef struct  {
  uint64_t vstart;
  uint64_t vxsat;
  uint64_t vxrm;
  uint64_t vcsr;
  uint64_t vl;
  uint64_t vtype;
  uint64_t vlenb;
} DifftestVecCSRState;

#define CONFIG_DIFFTEST_LRSCEVENT
typedef struct  {
  uint8_t  valid;
  uint8_t  success;
} DifftestLrScEvent;

#define CONFIG_DIFFTEST_STOREEVENT
#define CONFIG_DIFF_STORE_WIDTH 32
typedef struct  {
  uint8_t  valid;
  uint64_t addr;
  uint64_t data;
  uint8_t  mask;
} DifftestStoreEvent;

#define CONFIG_DIFFTEST_HCSRSTATE
typedef struct  {
  uint64_t virtMode;
  uint64_t mtval2;
  uint64_t mtinst;
  uint64_t hstatus;
  uint64_t hideleg;
  uint64_t hedeleg;
  uint64_t hcounteren;
  uint64_t htval;
  uint64_t htinst;
  uint64_t hgatp;
  uint64_t vsstatus;
  uint64_t vstvec;
  uint64_t vsepc;
  uint64_t vscause;
  uint64_t vstval;
  uint64_t vsatp;
  uint64_t vsscratch;
} DifftestHCSRState;

#define CONFIG_DIFFTEST_VECWRITEBACK
#define CONFIG_DIFF_WB_VEC_WIDTH 128
typedef struct  {
  uint64_t data;
} DifftestVecWriteback;

#define CONFIG_DIFFTEST_FPWRITEBACK
#define CONFIG_DIFF_WB_FP_WIDTH 192
typedef struct  {
  uint64_t data;
} DifftestFpWriteback;

#define CONFIG_DIFFTEST_ARCHVECREGSTATE
typedef struct  {
  uint64_t value[64];
} DifftestArchVecRegState;

#define CONFIG_DIFFTEST_DEBUGMODE
typedef struct  {
  uint8_t  debugMode;
  uint64_t dcsr;
  uint64_t dpc;
  uint64_t dscratch0;
  uint64_t dscratch1;
} DifftestDebugMode;

#define CONFIG_DIFFTEST_SBUFFEREVENT
#define CONFIG_DIFF_SBUFFER_WIDTH 1
typedef struct  {
  uint8_t  valid;
  uint64_t addr;
  uint8_t  data[64];
  uint64_t mask;
} DifftestSbufferEvent;

#define CONFIG_DIFFTEST_INSTRCOMMIT
#define CONFIG_DIFF_COMMIT_WIDTH 8
typedef struct  {
  uint8_t  valid;
  uint8_t  skip;
  uint8_t  isRVC;
  uint8_t  rfwen;
  uint8_t  fpwen;
  uint8_t  vecwen;
  uint8_t  wpdest;
  uint8_t  wdest;
  uint64_t pc;
  uint32_t instr;
  uint16_t robIdx;
  uint8_t  lqIdx;
  uint8_t  sqIdx;
  uint8_t  isLoad;
  uint8_t  isStore;
  uint8_t  nFused;
  uint8_t  special;
} DifftestInstrCommit;

#define CONFIG_DIFFTEST_L2TLBEVENT
#define CONFIG_DIFF_L2TLB_WIDTH 2
typedef struct  {
  uint8_t  valid;
  uint8_t  valididx[8];
  uint64_t satp;
  uint64_t vpn;
  uint64_t ppn[8];
  uint8_t  perm;
  uint8_t  level;
  uint8_t  pf;
  uint8_t  pteidx[8];
  uint64_t vsatp;
  uint64_t hgatp;
  uint64_t gvpn;
  uint8_t  g_perm;
  uint8_t  g_level;
  uint64_t s2ppn;
  uint8_t  gpf;
  uint8_t  s2xlate;
} DifftestL2TLBEvent;

#define CONFIG_DIFFTEST_ARCHINTREGSTATE
typedef struct  {
  uint64_t value[32];
} DifftestArchIntRegState;

#define CONFIG_DIFFTEST_L1TLBEVENT
#define CONFIG_DIFF_L1TLB_WIDTH 11
typedef struct  {
  uint8_t  valid;
  uint64_t satp;
  uint64_t vpn;
  uint64_t ppn;
  uint64_t vsatp;
  uint64_t hgatp;
  uint8_t  s2xlate;
} DifftestL1TLBEvent;

#define CONFIG_DIFFTEST_ATOMICEVENT
typedef struct  {
  uint8_t  valid;
  uint64_t addr;
  uint64_t data;
  uint8_t  mask;
  uint8_t  fuop;
  uint64_t out;
} DifftestAtomicEvent;

#define CONFIG_DIFFTEST_ARCHFPREGSTATE
typedef struct  {
  uint64_t value[32];
} DifftestArchFpRegState;

#define CONFIG_DIFFTEST_CSRSTATE
typedef struct  {
  uint64_t privilegeMode;
  uint64_t mstatus;
  uint64_t sstatus;
  uint64_t mepc;
  uint64_t sepc;
  uint64_t mtval;
  uint64_t stval;
  uint64_t mtvec;
  uint64_t stvec;
  uint64_t mcause;
  uint64_t scause;
  uint64_t satp;
  uint64_t mip;
  uint64_t mie;
  uint64_t mscratch;
  uint64_t sscratch;
  uint64_t mideleg;
  uint64_t medeleg;
} DifftestCSRState;

#define CONFIG_DIFFTEST_TRAPEVENT
typedef struct  {
  uint8_t  hasTrap;
  uint64_t cycleCnt;
  uint64_t instrCnt;
  uint8_t  hasWFI;
  uint32_t code;
  uint64_t pc;
} DifftestTrapEvent;

#define CONFIG_DIFFTEST_FPCSRSTATE
typedef struct  {
  uint64_t fcsr;
} DifftestFpCSRState;

#define CONFIG_DIFFTEST_TRIGGERCSRSTATE
typedef struct  {
  uint64_t tselect;
  uint64_t tdata1;
  uint64_t tinfo;
  uint64_t tcontrol;
} DifftestTriggerCSRState;

#define CONFIG_DIFFTEST_INTWRITEBACK
#define CONFIG_DIFF_WB_INT_WIDTH 224
typedef struct  {
  uint64_t data;
} DifftestIntWriteback;

#define CONFIG_DIFFTEST_ARCHEVENT
typedef struct  {
  uint8_t  valid;
  uint32_t interrupt;
  uint32_t exception;
  uint64_t exceptionPC;
  uint32_t exceptionInst;
} DifftestArchEvent;

#define CONFIG_DIFFTEST_LOADEVENT
#define CONFIG_DIFF_LOAD_WIDTH 8
typedef struct  {
  uint8_t  valid;
  uint64_t paddr;
  uint8_t  opType;
  uint8_t  isAtomic;
  uint8_t  isLoad;
} DifftestLoadEvent;

typedef struct {
  DifftestArchIntRegState        regs_int;
  DifftestCSRState               csr;
  DifftestArchFpRegState         regs_fp;
  DifftestArchVecRegState        regs_vec;
  DifftestVecCSRState            vcsr;
  DifftestHCSRState              hcsr;
  DifftestFpCSRState             fcsr;
  DifftestArchEvent              event;
  DifftestAtomicEvent            atomic;
  DifftestDebugMode              dmregs;
  DifftestFpWriteback            wb_fp[192];
  DifftestInstrCommit            commit[8];
  DifftestIntWriteback           wb_int[224];
  DifftestL1TLBEvent             l1tlb[11];
  DifftestL2TLBEvent             l2tlb[2];
  DifftestLoadEvent              load[8];
  DifftestLrScEvent              lrsc;
  DifftestRefillEvent            refill[11];
  DifftestSbufferEvent           sbuffer[1];
  DifftestStoreEvent             store[32];
  DifftestTrapEvent              trap;
  DifftestTriggerCSRState        triggercsr;
  DifftestVecWriteback           wb_vec[128];
} DiffTestState;

#endif // __DIFFSTATE_H__

