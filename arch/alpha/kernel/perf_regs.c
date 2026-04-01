// SPDX-License-Identifier: GPL-2.0

#include <linux/perf_event.h>
#include <asm/perf_regs.h>
#include <asm/ptrace.h>

u64 perf_reg_value(struct pt_regs *regs, int idx)
{
	switch (idx) {
	case PERF_REG_ALPHA_R0:
		return regs->r0;
	case PERF_REG_ALPHA_R1:
		return regs->r1;
	case PERF_REG_ALPHA_R2:
		return regs->r2;
	case PERF_REG_ALPHA_R3:
		return regs->r3;
	case PERF_REG_ALPHA_R4:
		return regs->r4;
	case PERF_REG_ALPHA_R5:
		return regs->r5;
	case PERF_REG_ALPHA_R6:
		return regs->r6;
	case PERF_REG_ALPHA_R7:
		return regs->r7;
	case PERF_REG_ALPHA_R8:
		return regs->r8;
	case PERF_REG_ALPHA_R16:
		return regs->r16;
	case PERF_REG_ALPHA_R17:
		return regs->r17;
	case PERF_REG_ALPHA_R18:
		return regs->r18;
	case PERF_REG_ALPHA_R19:
		return regs->r19;
	case PERF_REG_ALPHA_R20:
		return regs->r20;
	case PERF_REG_ALPHA_R21:
		return regs->r21;
	case PERF_REG_ALPHA_R22:
		return regs->r22;
	case PERF_REG_ALPHA_R23:
		return regs->r23;
	case PERF_REG_ALPHA_R24:
		return regs->r24;
	case PERF_REG_ALPHA_R25:
		return regs->r25;
	case PERF_REG_ALPHA_R26:
		return regs->r26;
	case PERF_REG_ALPHA_R27:
		return regs->r27;
	case PERF_REG_ALPHA_R28:
		return regs->r28;
	case PERF_REG_ALPHA_GP:
		return regs->gp;
	case PERF_REG_ALPHA_PC:
		return regs->pc;
	case PERF_REG_ALPHA_PS:
		return regs->ps;
	default:
		WARN_ON_ONCE(1);
		return 0;
	}
}

#define REG_RESERVED (~((1ULL << PERF_REG_ALPHA_MAX) - 1))

int perf_reg_validate(u64 mask)
{
	if (!mask || mask & REG_RESERVED)
		return -EINVAL;

	return 0;
}

u64 perf_reg_abi(struct task_struct *task)
{
	return PERF_SAMPLE_REGS_ABI_64;
}

void perf_get_regs_user(struct perf_regs *regs_user,
			struct pt_regs *regs)
{
	regs_user->regs = task_pt_regs(current);
	regs_user->abi = perf_reg_abi(current);
}
