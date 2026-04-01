// SPDX-License-Identifier: GPL-2.0
/*
 * Stack trace support for Alpha.
 *
 * Alpha does not have a reliable frame pointer chain, so this uses a
 * linear scan of the stack looking for kernel text addresses — the same
 * approach used by dik_show_trace() in traps.c.
 */

#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/stacktrace.h>

#include <asm/ptrace.h>

static void walk_stack(stack_trace_consume_fn consume_entry, void *cookie,
		       unsigned long *sp, unsigned long high)
{
	while ((unsigned long)sp < high) {
		unsigned long addr = *sp++;

		if (__kernel_text_address(addr))
			if (!consume_entry(cookie, addr))
				return;
	}
}

void arch_stack_walk(stack_trace_consume_fn consume_entry, void *cookie,
		     struct task_struct *task, struct pt_regs *regs)
{
	unsigned long *sp, high;

	if (regs) {
		sp = (unsigned long *)(regs + 1);
	} else if (task == current || !task) {
		sp = (unsigned long *)current_stack_pointer;
	} else {
		sp = (unsigned long *)task_thread_info(task)->pcb.ksp;
	}

	high = (unsigned long)task_stack_page(task ? task : current) +
	       THREAD_SIZE;

	walk_stack(consume_entry, cookie, sp, high);
}
