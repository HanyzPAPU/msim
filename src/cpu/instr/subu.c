static exc_t instr_subu(cpu_t *cpu, instr_t instr)
{
	uint32_t rs = cpu->regs[instr.r.rs].lo;
	uint32_t rt = cpu->regs[instr.r.rt].lo;
	
	cpu->regs[instr.r.rd].val = sign_extend_32_64(rs - rt);
	
	return excNone;
}