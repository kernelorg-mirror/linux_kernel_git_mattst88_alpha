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
#include <linux/kallsyms.h>

#include <asm/ptrace.h>
#include <asm/thread_info.h>

static void walk_stack(struct stack_trace *trace, unsigned long *sp,
		       unsigned long high)
{
	while ((unsigned long)sp < high) {
		unsigned long addr = *sp++;

		if (!is_kernel_text(addr))
			continue;
		if (trace->skip > 0) {
			trace->skip--;
			continue;
		}
		if (trace->nr_entries >= trace->max_entries)
			return;
		trace->entries[trace->nr_entries++] = addr;
	}
}

void save_stack_trace(struct stack_trace *trace)
{
	unsigned long *sp = (unsigned long *)current_stack_pointer;
	unsigned long high = (unsigned long)task_stack_page(current) +
			     THREAD_SIZE;

	walk_stack(trace, sp, high);
}
EXPORT_SYMBOL_GPL(save_stack_trace);

void save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	unsigned long *sp, high;

	if (!try_get_task_stack(tsk))
		return;

	if (tsk == current) {
		sp = (unsigned long *)current_stack_pointer;
	} else {
		sp = (unsigned long *)task_thread_info(tsk)->pcb.ksp;
	}
	high = (unsigned long)task_stack_page(tsk) + THREAD_SIZE;

	walk_stack(trace, sp, high);

	put_task_stack(tsk);
}
EXPORT_SYMBOL_GPL(save_stack_trace_tsk);
