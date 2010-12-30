static exc_t instr_sw(cpu_t *cpu, instr_t instr)
{
	ptr64_t addr;
	addr.ptr = cpu->regs[instr.i.rs].val + sign_extend_16_64(instr.i.imm);
	
	return cpu_write_mem32(cpu, addr, cpu->regs[instr.i.rt].lo,
	    true);
}