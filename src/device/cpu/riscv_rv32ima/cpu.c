#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "cpu.h"
#include "debug.h"
#include "csr.h"
#include "../../../assert.h"
#include "../../../physmem.h"
#include "../../../main.h"

#define RV_START_ADDRESS UINT32_C(0x0)


static void init_regs(rv_cpu_t *cpu) {
    ASSERT(cpu != NULL);

    // expects that default value for any variable is 0

    cpu->pc = RV_START_ADDRESS;
    cpu->pc_next = RV_START_ADDRESS + 4;
}


void rv_cpu_init(rv_cpu_t *cpu, unsigned int procno){

    ASSERT(cpu!=NULL);

    memset(cpu, 0, sizeof(rv_cpu_t));

    init_regs(cpu);

    rv_init_csr(&cpu->csr, procno);

    cpu->priv_mode = rv_mmode;
}   


typedef struct {
    unsigned int v: 1;
    unsigned int r: 1;
    unsigned int w: 1;
    unsigned int x: 1;
    unsigned int u: 1;
    unsigned int g: 1;
    unsigned int a: 1;
    unsigned int d: 1;
    unsigned int rsw: 2;
    unsigned int ppn: 22;
} sv32_pte_t;

static_assert(sizeof(sv32_pte_t) == 4);

#define is_pte_leaf(pte) (pte.r | pte.w | pte.x)
#define is_pte_valid(pte) (pte.v && (!pte.w || pte.r))
#define pte_ppn0(pte) (pte.ppn & 0x0003FF)
#define pte_ppn1(pte) (pte.ppn & 0x3FFC00)

// Dirty hack
typedef union {
    sv32_pte_t pte;
    uint32_t val;
} sv32_pte_helper_t;
#define pte_from_uint(val) (((sv32_pte_helper_t)(val)).pte)
#define uint_from_pte(pte) (((sv32_pte_helper_t)(pte)).val)

#define sv32_effective_priv(cpu) (rv_csr_mstatus_mprv(cpu) ? rv_csr_mstatus_mpp(cpu) : cpu->priv_mode)

static bool is_access_allowed(rv_cpu_t *cpu, sv32_pte_t pte, bool wr, bool fetch) {
    if(wr && !pte.w) return false;
    if(fetch && !pte.x) return false;

    // Page is executable and I can read from executable pages
    bool rx = rv_csr_sstatus_mxr(cpu) && pte.x;
    
    if(!wr && !pte.r && !rx) return false;

    if(sv32_effective_priv(cpu) == rv_smode){
        if(!rv_csr_sstatus_sum(cpu) && pte.u) return false;
        if(fetch && pte.u) return false;
    }

    if(sv32_effective_priv(cpu) == rv_umode){
        if(!pte.u) return false;
    }

    return false;
}

static ptr36_t make_phys_from_ppn(uint32_t virt, sv32_pte_t pte, bool megapage){
    ptr36_t page_offset = virt & 0x00000FFF;
    ptr36_t virt_vpn0   = virt & 0x003FF000;
    ptr36_t pte_ppn0    = (ptr36_t)pte_ppn0(pte) << 12;
    ptr36_t pte_ppn1    = (ptr36_t)pte_ppn1(pte) << 12;
    ptr36_t phys_ppn0   = megapage ? virt_vpn0 : pte_ppn0;

    return pte_ppn1 | phys_ppn0 | page_offset;
}

rv_exc_t rv_convert_addr(rv_cpu_t *cpu, uint32_t virt, ptr36_t *phys, bool wr, bool fetch, bool noisy){
    ASSERT(cpu != NULL);
    ASSERT(phys != NULL);
    ASSERT(!(wr && fetch));

    #define page_fault_exception (fetch ? rv_exc_instruction_page_fault : (wr ? rv_exc_store_amo_page_fault : rv_exc_load_page_fault))

    bool satp_active = (!rv_csr_satp_is_bare(cpu)) && (sv32_effective_priv(cpu) <= rv_smode);

    if(!satp_active){
        *phys = virt;
        return rv_exc_none;
    }

    uint32_t vpn0     =    virt & 0x003FF000;
    uint32_t vpn1     =    virt & 0xFFC00000;
    uint32_t ppn      = rv_csr_satp_ppn(cpu);

    bool is_megapage = false;

    // name of variables according to spec
    int PAGESIZE = 12;
    int PTESIZE = 4;

    ptr36_t a = ((ptr36_t)ppn) << PAGESIZE;
    ptr36_t pte_addr = a + vpn1*PTESIZE;

    // PMP or PMA check goes here if implemented
    uint32_t pte_val = physmem_read32(cpu->csr.mhartid, pte_addr, true);

    sv32_pte_t pte = pte_from_uint(pte_val);

    if(!is_pte_valid(pte)) return page_fault_exception;

    if(is_pte_leaf(pte)) {
        // MEGAPAGE
        // Missaligned megapage
        if(pte_ppn0(pte) != 0) return page_fault_exception;
        is_megapage = true;
        goto leaf_pte;
    }

    // PMP or PMA check goes here if implemented
    a = pte.ppn << PAGESIZE;
    pte_addr = a + vpn0*PTESIZE;
    pte_val = physmem_read32(cpu->csr.mhartid, pte_addr, true);
    pte = pte_from_uint(pte_val);

    if(!is_pte_valid(pte)) return page_fault_exception;
    // Non-leaf on last level
    if(!is_pte_leaf(pte)) return page_fault_exception;

leaf_pte:
    if(!is_access_allowed(cpu, pte, wr, fetch)) return page_fault_exception;

    pte.a = 1;
    pte.d |= wr ? 1 : 0;

    pte_val = uint_from_pte(pte);
    physmem_write32(cpu->csr.mhartid, pte_addr, pte_val, true);

    *phys = make_phys_from_ppn(virt, pte, is_megapage);

    return rv_exc_none;
}

rv_exc_t rv_read_mem32(rv_cpu_t *cpu, uint32_t virt, uint32_t *value, bool fetch, bool noisy){
    ASSERT(cpu != NULL);
    ASSERT(value != NULL);
    //TODO: check alignment

    ptr36_t phys;
    rv_exc_t ex = rv_convert_addr(cpu, virt, &phys, false, fetch, noisy);
    
    if(ex != rv_exc_none){
        return ex;
    }

    *value = physmem_read32(cpu->csr.mhartid, phys, true);
    return rv_exc_none;
}

rv_exc_t rv_read_mem16(rv_cpu_t *cpu, uint32_t virt, uint16_t *value, bool fetch, bool noisy){
    ASSERT(cpu != NULL);
    ASSERT(value != NULL);
    //TODO: check alignment

    ptr36_t phys;
    rv_exc_t ex = rv_convert_addr(cpu, virt, &phys, false, fetch, noisy);
    
    if(ex != rv_exc_none){
        return ex;
    }

    *value = physmem_read16(cpu->csr.mhartid, phys, true);
    return rv_exc_none;
}

rv_exc_t rv_read_mem8(rv_cpu_t *cpu, uint32_t virt, uint8_t *value, bool noisy){
    ASSERT(cpu != NULL);
    ASSERT(value != NULL);
    //TODO: check alignment

    ptr36_t phys;
    rv_exc_t ex = rv_convert_addr(cpu, virt, &phys, false, false, noisy);
    
    if(ex != rv_exc_none){
        return ex;
    }

    *value = physmem_read8(cpu->csr.mhartid, phys, true);
    return rv_exc_none;
}

rv_exc_t rv_write_mem8(rv_cpu_t *cpu, uint32_t virt, uint8_t value, bool noisy){
    ASSERT(cpu != NULL);
    // TODO: check alignment

    ptr36_t phys;
    rv_exc_t ex = rv_convert_addr(cpu, virt, &phys, false, false, noisy);
    
    if(ex != rv_exc_none){
        return ex;
    }

    if(physmem_write8(cpu->csr.mhartid, phys, value, true)){
        return rv_exc_none;
    }
    //TODO: handle write into invalid memory
    return rv_exc_none;

}

rv_exc_t rv_write_mem16(rv_cpu_t *cpu, uint32_t virt, uint16_t value, bool noisy){
    ASSERT(cpu != NULL);
    // TODO: check alignment

    ptr36_t phys;
    rv_exc_t ex = rv_convert_addr(cpu, virt, &phys, false, false, noisy);
    
    if(ex != rv_exc_none){
        return ex;
    }

    if(physmem_write16(cpu->csr.mhartid, phys, value, true)){
        return rv_exc_none;
    }
    //TODO: handle write into invalid memory
    return rv_exc_none;
}


rv_exc_t rv_write_mem32(rv_cpu_t *cpu, uint32_t virt, uint32_t value, bool noisy){
    ASSERT(cpu != NULL);
    // TODO: check alignment

    ptr36_t phys;
    rv_exc_t ex = rv_convert_addr(cpu, virt, &phys, false, false, noisy);
    
    if(ex != rv_exc_none){
        return ex;
    }

    if(physmem_write32(cpu->csr.mhartid, phys, value, true)){
        return rv_exc_none;
    }
    //TODO: handle write into invalid memory
    return rv_exc_none;
}

void rv_cpu_set_pc(rv_cpu_t *cpu, uint32_t value){
    ASSERT(cpu != NULL);
    /* Set both pc and pc_next
     * This should be called from the debugger to jump somewhere
     * and in case the new instruction does not modify pc_next,
     * the processor would then jump back to where it was before this call
     */
    cpu->pc = value;
    cpu->pc_next = value+4;    
}



static void m_trap(rv_cpu_t* cpu, rv_exc_t ex){
    ASSERT(ex != rv_exc_none);

    bool is_interrupt = ex & RV_INTERRUPT_EXC_BITS;
    
    cpu->csr.mepc = is_interrupt ? cpu->pc_next : cpu->pc;
    cpu->csr.mcause = ex;
    cpu->csr.mtval = cpu->csr.tval_next;

    // MPIE = MIE
    {
        bool mie_set = rv_csr_mstatus_mie(cpu);

        if(mie_set){
            cpu->csr.mstatus |= rv_csr_mstatus_mpie_mask;
        }
        else {
            cpu->csr.mstatus &= ~rv_csr_mstatus_mpie_mask;
        }
    }
    // MIE = 0
    {
        cpu->csr.mstatus &= ~rv_csr_mstatus_mie_mask;
    }
    // MPP = cpu->priv_mode
    {
        cpu->csr.mstatus &= ~rv_csr_mstatus_mpp_mask;
        cpu->csr.mstatus |= ((uint32_t)cpu->priv_mode << rv_csr_mstatus_mpp_pos) & rv_csr_mstatus_mpp_mask;
    }

    cpu->priv_mode = rv_mmode;

    int mode = cpu->csr.mtvec & rv_csr_mtvec_mode_mask;
    uint32_t base = cpu->csr.mtvec & ~rv_csr_mtvec_mode_mask;

    if(mode == rv_csr_mtvec_mode_direct){
        cpu->pc_next = base;
    }
    else if(mode == rv_csr_mtvec_mode_vectored){
        if(is_interrupt) {
            cpu->pc_next = base + 4 * (ex & ~RV_INTERRUPT_EXC_BITS);
        }
        else {
            cpu->pc_next = base;
        }
    }
    else {
        ASSERT(false);
    }
}

static void s_trap(rv_cpu_t* cpu, rv_exc_t ex){
    ASSERT(ex != rv_exc_none);

    bool is_interrupt = ex & RV_INTERRUPT_EXC_BITS;

    cpu->csr.sepc = is_interrupt ? cpu->pc_next : cpu->pc;
    cpu->csr.scause = ex;
    cpu->csr.stval = cpu->csr.tval_next;

    // SPIE = SIE
    {
        bool sie_set = rv_csr_sstatus_sie(cpu);

        if(sie_set){
            cpu->csr.mstatus |= rv_csr_sstatus_spie_mask;
        }
        else {
            cpu->csr.mstatus &= ~rv_csr_sstatus_spie_mask;
        }
    }
    // SIE = 0
    {
        cpu->csr.mstatus &= ~rv_csr_sstatus_sie_mask;
    }
    // SPP = cpu->priv_mode
    {
        cpu->csr.mstatus &= ~rv_csr_sstatus_spp_mask;
        cpu->csr.mstatus |= ((uint32_t)cpu->priv_mode << rv_csr_sstatus_spp_pos) & rv_csr_sstatus_spp_mask;
    }

    cpu->priv_mode = rv_smode;

    int mode = cpu->csr.stvec & rv_csr_mtvec_mode_mask;
    uint32_t base = cpu->csr.stvec & ~rv_csr_mtvec_mode_mask;

    if(mode == rv_csr_mtvec_mode_direct){
        cpu->pc_next = base;
    }
    else if(mode == rv_csr_mtvec_mode_vectored){
        if(is_interrupt) {
            cpu->pc_next = base + 4 * (ex & ~RV_INTERRUPT_EXC_BITS);
        }
        else {
            cpu->pc_next = base;
        }
    }
    else {
        ASSERT(false);
    }
}

static void handle_exception(rv_cpu_t* cpu, rv_exc_t ex){
    uint32_t mask = RV_EXCEPTION_MASK(ex);
    bool delegated = cpu->csr.medeleg & mask;

    if(delegated && cpu->priv_mode != rv_mmode){
        printf("s trap from %i\n", cpu->priv_mode);
        s_trap(cpu, ex);
    }
    else {
        printf("m trap from %i\n", cpu->priv_mode);
        m_trap(cpu, ex);
    }
}

static void try_handle_interrupt(rv_cpu_t* cpu){
    // no interrupt pending
    if(cpu->csr.mip == 0) return;

    // PRIORITY: MEI, MSI, MTI, SEI, SSI, STI
    #define trap_if_set(cpu, mask, interrupt, trap_func)    \
        if(mask & RV_EXCEPTION_MASK(interrupt)){            \
            trap_func(cpu, interrupt);                      \
            return;                                         \
        }

    // TRAP to M-mode
    // ((priv_mode == M && MIE) || (priv_mode < M)) && MIP[i] && MIE[i] && !MIDELEG[i]

    bool can_trap_to_M = (cpu->priv_mode == rv_mmode && rv_csr_mstatus_mie(cpu)) || (cpu->priv_mode < rv_mmode);

    if(can_trap_to_M) {
        uint32_t m_mode_active_interrupt_mask = cpu->csr.mip & cpu->csr.mie & ~cpu->csr.mideleg;
        if(m_mode_active_interrupt_mask == 0) goto handle_s_mode;
        
        trap_if_set(cpu, m_mode_active_interrupt_mask, rv_exc_machine_external_interrupt, m_trap);
        trap_if_set(cpu, m_mode_active_interrupt_mask, rv_exc_machine_software_interrupt, m_trap);
        trap_if_set(cpu, m_mode_active_interrupt_mask, rv_exc_machine_timer_interrupt, m_trap);
        trap_if_set(cpu, m_mode_active_interrupt_mask, rv_exc_supervisor_external_interrupt, m_trap);
        trap_if_set(cpu, m_mode_active_interrupt_mask, rv_exc_supervisor_software_interrupt, m_trap);
        trap_if_set(cpu, m_mode_active_interrupt_mask, rv_exc_supervisor_timer_interrupt, m_trap);
    }

    // TRAP to S-mode
    // ((priv_mode == S && SIE) || (priv_mode < M)) && SIP[i] && SIE[i]

handle_s_mode: ;
    bool can_trap_to_S = (cpu->priv_mode == rv_smode && rv_csr_sstatus_sie(cpu)) || (cpu->priv_mode < rv_smode);
    if(can_trap_to_S) {
        // mask to only account S mode interrupts
        uint32_t s_mode_active_interrupt_mask = cpu->csr.mip & cpu->csr.mie & rv_csr_si_mask;
        if(s_mode_active_interrupt_mask == 0) return;

        // M-interrupts can be here theoretically by spec, but we don't allow the delegation of M interrupts in msim (which is allowed in spec)
        trap_if_set(cpu, s_mode_active_interrupt_mask, rv_exc_supervisor_external_interrupt, s_trap);
        trap_if_set(cpu, s_mode_active_interrupt_mask, rv_exc_supervisor_software_interrupt, s_trap);
        trap_if_set(cpu, s_mode_active_interrupt_mask, rv_exc_supervisor_timer_interrupt, s_trap);
    }
}

static void account_hmp(rv_cpu_t* cpu, int i){
    ASSERT((i >= 0 && i < 29));
    
    uint32_t mask = (1 << i);
    bool inhibited = cpu->csr.mcountinhibit & mask;
    
    if(inhibited) return;

    csr_hpm_event_t event = cpu->csr.hpmevents[i];

    switch(event){
        case(hpm_u_cycles):{
            if(cpu->priv_mode == rv_umode){
                cpu->csr.hpmcounters[i]++;
            }
            break;
        }
        case(hpm_s_cycles):{
            if(cpu->priv_mode == rv_smode){
                cpu->csr.hpmcounters[i]++;
            }
            break;
        }
        case(hpm_m_cycles):{
            if(cpu->priv_mode == rv_mmode){
                cpu->csr.hpmcounters[i]++;
            }
            break;
        }
        case(hpm_w_cycles):{
            // TODO: count stalled cycles (probably when the current instruction is WFI or cpu->stdby is true)
            break;
        }
        default:
            break;
    }
}

static void account(rv_cpu_t* cpu){
    if(!(cpu->csr.mcountinhibit & 0b001))
        cpu->csr.cycle++;

    // TODO: Should not account instructions that raise exceptions (ecall included)
    if(!(cpu->csr.mcountinhibit & 0b100))
        cpu->csr.instret++;

    for(int i = 0; i < 29; ++i){
        account_hmp(cpu, i);
    }
}

void rv_cpu_step(rv_cpu_t *cpu){
    ASSERT(cpu != NULL);

    ptr36_t phys;
    rv_exc_t ex;
    while((ex = rv_convert_addr(cpu, cpu->pc, &phys, false, true, true)) != rv_exc_none){
        // TODO: handle exception
    }

    rv_instr_t instr_data = (rv_instr_t)physmem_read32(cpu->csr.mhartid, phys, false);

    rv_instr_func_t instr_func = rv_instr_decode(instr_data);
    
    if(machine_trace)
        rv_idump(cpu, cpu->pc, instr_data);

    ex = instr_func(cpu, instr_data);

    account(cpu);
    
    if(ex != rv_exc_none){

        if(ex == rv_exc_illegal_instruction){
            cpu->csr.tval_next = instr_data.val;
        }
        handle_exception(cpu, ex);
    }
    else {
        // If any interrupts are pending, handle them
        try_handle_interrupt(cpu);
    }

    // x0 is always 0
    cpu->regs[0] = 0;
    cpu->pc = cpu->pc_next;
    cpu->pc_next = cpu->pc + 4;
    cpu->csr.tval_next = 0;
}

bool rv_sc_access(rv_cpu_t *cpu, ptr36_t phys){
    ASSERT(cpu != NULL);
    // We align down because of writes that are shorter than 4 B
    // As long as all writes are aligned, and 32 bits at max, this works
    bool hit = cpu->reserved_addr == ALIGN_DOWN(phys, 4);
    if(hit) {
        cpu->reserved_valid = false;
    }
    return hit;
}    




/* Interrupts
 * This is supposed to be used with devices and interprocessor communication,
 * devices should raise a Machine/Supervisor External Interrupt,
 * but interprocessor interrupts should be Machine/Supervisor Software Interrupts.
 * So we use the argument no to differentiate based on the exception code (see rv_exc_t definiton)
 */

void rv_interrupt_up(rv_cpu_t *cpu, unsigned int no){
    ASSERT(cpu != NULL);

    // default to MEI if no is invalid
    if( no != rv_exc_supervisor_software_interrupt &&
        no != rv_exc_machine_software_interrupt &&
        no != rv_exc_supervisor_external_interrupt &&
        no != rv_exc_machine_external_interrupt
    ){
        no = rv_exc_machine_external_interrupt;
    }

    uint32_t mask = RV_EXCEPTION_MASK(no);

    cpu->csr.mip |= mask;
}

void rv_interrupt_down(rv_cpu_t *cpu, unsigned int no){
    ASSERT(cpu != NULL);
    //! for simplicity just clears the bit
    //! if this interrupt could be raised by different means,
    //! this would not work!
    
    // default to MEI if no is invalid
    if( no != rv_exc_supervisor_software_interrupt &&
        no != rv_exc_machine_software_interrupt &&
        no != rv_exc_supervisor_external_interrupt &&
        no != rv_exc_machine_external_interrupt
    ){
        no = rv_exc_machine_external_interrupt;
    }

    uint32_t mask = RV_EXCEPTION_MASK(no);

    cpu->csr.mip &= ~mask;
}