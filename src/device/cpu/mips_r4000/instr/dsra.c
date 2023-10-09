static r4k_exc_t instr_dsra(r4k_cpu_t *cpu, r4k_instr_t instr)
{
    if (CPU_64BIT_INSTRUCTION(cpu)) {
        uint64_t rt = cpu->regs[instr.r.rt].val;
        cpu->regs[instr.r.rd].val = (uint64_t) (((int64_t) rt) >> instr.r.sa);
    } else
        return r4k_excRI;

    return r4k_excNone;
}

static void mnemonics_dsra(ptr64_t addr, r4k_instr_t instr,
        string_t *mnemonics, string_t *comments)
{
    string_printf(mnemonics, "dsra");
    disassemble_rd_rt_sa(instr, mnemonics, comments);
}
