// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>
#include "../../util/disasm.h"

/*
 * Alpha control-transfer instructions, as printed by objdump:
 *
 *   PC-relative (opcode group 0x30-0x3f):
 *     br, bsr                          unconditional / to-subroutine
 *     beq bne blt ble bgt bge blbc blbs   integer conditional
 *     fbeq fbne fblt fble fbgt fbge       floating conditional
 *
 *   Register-indirect (JSR group, opcode 0x1a):
 *     jmp, jsr, ret, jsr_coroutine
 *
 * bsr/jsr (and the coroutine form) save a return address, so they are calls;
 * ret returns; everything else that transfers control is a jump. Alpha has no
 * machine "mov" -- objdump prints "mov"/"fmov" as pseudos for bis/cpys, so map
 * them to mov_ops when present.
 */

static int is_alpha_cond_branch(const char *name)
{
	static const char *const branches[] = {
		"beq", "bne", "blt", "ble", "bgt", "bge", "blbc", "blbs",
		"fbeq", "fbne", "fblt", "fble", "fbgt", "fbge",
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(branches); i++) {
		if (!strcmp(name, branches[i]))
			return 1;
	}
	return 0;
}

static const struct ins_ops *alpha__associate_instruction_ops(struct arch *arch, const char *name)
{
	const struct ins_ops *ops = NULL;

	if (!strcmp(name, "bsr") ||
	    !strcmp(name, "jsr") ||
	    !strcmp(name, "jsr_coroutine")) {
		ops = &call_ops;
	} else if (!strcmp(name, "ret")) {
		ops = &ret_ops;
	} else if (!strcmp(name, "br") ||
		   !strcmp(name, "jmp") ||
		   is_alpha_cond_branch(name)) {
		ops = &jump_ops;
	} else if (!strcmp(name, "mov") ||
		   !strcmp(name, "fmov")) {
		ops = &mov_ops;
	}

	if (ops)
		arch__associate_ins_ops(arch, name, ops);

	return ops;
}

const struct arch *arch__new_alpha(const struct e_machine_and_e_flags *id,
				   const char *cpuid __maybe_unused)
{
	struct arch *arch = zalloc(sizeof(*arch));

	if (!arch)
		return NULL;

	arch->name = "alpha";
	arch->id = *id;
	arch->associate_instruction_ops = alpha__associate_instruction_ops;
	arch->objdump.comment_char = '#';
	return arch;
}
