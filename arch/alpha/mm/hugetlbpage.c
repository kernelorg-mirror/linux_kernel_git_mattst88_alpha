// SPDX-License-Identifier: GPL-2.0
/*
 * Alpha huge page support, built on the page table entry granularity hint.
 *
 * A hint of order N marks a PTE as one of a block of 8^N physically
 * contiguous, naturally aligned pages that the translation buffer may map
 * with a single entry.  With 8KB pages the usable sizes are 64KB, 512KB and
 * 4MB; there is no leaf entry above the last level, so those are the only
 * huge pages Alpha can offer.
 *
 * The hint is advisory, so an implementation that ignores it still translates
 * correctly through the individual PTEs.  What is not advisory is the shape of
 * the block: every PTE keeps the physical frame number of its own page, and
 * all of them have to agree in bits <15:0>.  Every update below therefore
 * rewrites the whole block.
 */

#include <linux/hugetlb.h>
#include <linux/mm.h>
#include <linux/pgtable.h>

#include <asm/tlbflush.h>

/*
 * Every hint size is smaller than PMD_SIZE, so a block always fits inside a
 * single last level page table and its entry count is just its page count.
 */
static inline unsigned long num_contig_ptes(unsigned long sz)
{
	return sz >> PAGE_SHIFT;
}

/* Hint order that maps @sz, or 0 if @sz is not a hint size. */
static unsigned int gh_order_from_size(unsigned long sz)
{
	unsigned int order;

	for_each_gh_order(order)
		if (gh_cont_size(order) == sz)
			return order;

	return 0;
}

pte_t *huge_pte_alloc(struct mm_struct *mm, struct vm_area_struct *vma,
		      unsigned long addr, unsigned long sz)
{
	unsigned int order = gh_order_from_size(sz);
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	if (!order)
		return NULL;

	pgd = pgd_offset(mm, addr);
	p4d = p4d_alloc(mm, pgd, addr);
	if (!p4d)
		return NULL;

	pud = pud_alloc(mm, p4d, addr);
	if (!pud)
		return NULL;

	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd)
		return NULL;

	return pte_alloc_huge(mm, pmd, addr & gh_cont_mask(order));
}

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr,
		       unsigned long sz)
{
	unsigned int order = gh_order_from_size(sz);
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	if (!order)
		return NULL;

	pgd = pgd_offset(mm, addr);
	if (!pgd_present(pgdp_get(pgd)))
		return NULL;

	p4d = p4d_offset(pgd, addr);
	if (!p4d_present(p4dp_get(p4d)))
		return NULL;

	pud = pud_offset(p4d, addr);
	if (!pud_present(pudp_get(pud)))
		return NULL;

	pmd = pmd_offset(pud, addr);
	if (!pmd_present(pmdp_get(pmd)))
		return NULL;

	return pte_offset_huge(pmd, addr & gh_cont_mask(order));
}

/*
 * A block is only as large as its own hint, so the walk can skip to the end of
 * the containing last level page table but no further.
 */
unsigned long hugetlb_mask_last_page(struct hstate *h)
{
	return PMD_SIZE - huge_page_size(h);
}

pte_t huge_ptep_get(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	pte_t orig_pte = ptep_get(ptep);
	unsigned long i, ncontig;

	if (!pte_present(orig_pte) || !pte_huge(orig_pte))
		return orig_pte;

	ncontig = gh_pte_num(pte_gh_order(orig_pte));

	for (i = 0; i < ncontig; i++, ptep++) {
		pte_t pte = ptep_get(ptep);

		if (pte_dirty(pte))
			orig_pte = pte_mkdirty(orig_pte);
		if (pte_young(pte))
			orig_pte = pte_mkyoung(orig_pte);
	}

	return orig_pte;
}

static pte_t get_clear_contig(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, unsigned long ncontig)
{
	pte_t pte = ptep_get_and_clear(mm, addr, ptep);
	bool present = pte_present(pte);

	while (--ncontig) {
		pte_t tmp_pte;

		ptep++;
		addr += PAGE_SIZE;
		tmp_pte = ptep_get_and_clear(mm, addr, ptep);
		if (present) {
			if (pte_dirty(tmp_pte))
				pte = pte_mkdirty(pte);
			if (pte_young(tmp_pte))
				pte = pte_mkyoung(pte);
		}
	}

	return pte;
}

static pte_t get_clear_contig_flush(struct mm_struct *mm, unsigned long addr,
				    pte_t *ptep, unsigned long ncontig)
{
	pte_t orig_pte = get_clear_contig(mm, addr, ptep, ncontig);
	struct vm_area_struct vma = TLB_FLUSH_VMA(mm, 0);

	if (!pte_none(orig_pte))
		flush_tlb_range(&vma, addr, addr + ncontig * PAGE_SIZE);

	return orig_pte;
}

static void clear_flush(struct mm_struct *mm, unsigned long addr, pte_t *ptep,
			unsigned long ncontig)
{
	struct vm_area_struct vma = TLB_FLUSH_VMA(mm, 0);
	unsigned long i, saddr = addr;

	for (i = 0; i < ncontig; i++, addr += PAGE_SIZE, ptep++)
		ptep_get_and_clear(mm, addr, ptep);

	flush_tlb_range(&vma, saddr, addr);
}

pte_t arch_make_huge_pte(pte_t entry, unsigned int shift, vm_flags_t flags)
{
	unsigned int order = gh_order_from_size(1UL << shift);

	/*
	 * Not every caller asks for a size Alpha can hint; mm/debug_vm_pgtable.c
	 * probes with PMD_SHIFT.  Leave the entry alone rather than complaining.
	 */
	if (order)
		entry = pte_mkgh(entry, order);

	return entry;
}

void set_huge_pte_at(struct mm_struct *mm, unsigned long addr, pte_t *ptep,
		     pte_t pte, unsigned long sz)
{
	unsigned long i, ncontig = num_contig_ptes(sz);

	/* The hint requires the block to be naturally aligned in the VA too. */
	VM_WARN_ON_ONCE(addr & (sz - 1));

	if (!pte_present(pte)) {
		/*
		 * Swap, migration and marker entries hold no frame number, so
		 * every slot gets the identical value rather than a walk.
		 */
		for (i = 0; i < ncontig; i++, ptep++, addr += PAGE_SIZE)
			set_ptes(mm, addr, ptep, pte, 1);
		return;
	}

	/* The caller may have built this from scratch, so set the hint here. */
	pte = pte_mkgh(pte, gh_order_from_size(sz));

	/*
	 * ARM 11.6.1 requires an already valid entry to be invalidated
	 * everywhere before it is rewritten, and the invalidate has to assume
	 * the hint is zero, so it must cover every page of the block.
	 */
	if (pte_present(ptep_get(ptep)))
		clear_flush(mm, addr, ptep, ncontig);

	/*
	 * set_ptes() advances the frame number for each entry, which is what
	 * the hint needs: unlike an architecture that encodes the block in the
	 * PTE itself, every Alpha PTE still maps its own page.
	 */
	set_ptes(mm, addr, ptep, pte, ncontig);
}

pte_t huge_ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, unsigned long sz)
{
	return get_clear_contig(mm, addr, ptep, num_contig_ptes(sz));
}

/*
 * The block size comes from the hstate rather than from the entry, because
 * this is also reached for migration entries, which carry no hint.
 */
pte_t huge_ptep_clear_flush(struct vm_area_struct *vma, unsigned long addr,
			    pte_t *ptep)
{
	unsigned long sz = huge_page_size(hstate_vma(vma));

	return get_clear_contig_flush(vma->vm_mm, addr, ptep,
				      num_contig_ptes(sz));
}

pte_t huge_ptep_modify_prot_start(struct vm_area_struct *vma, unsigned long addr,
				  pte_t *ptep)
{
	return huge_ptep_clear_flush(vma, addr, ptep);
}

int huge_ptep_set_access_flags(struct vm_area_struct *vma, unsigned long addr,
			       pte_t *ptep, pte_t pte, int dirty)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long sz = huge_page_size(hstate_vma(vma));
	unsigned long ncontig = num_contig_ptes(sz);
	pte_t orig_pte;

	orig_pte = get_clear_contig_flush(mm, addr, ptep, ncontig);

	pte = pte_mkgh(pte, gh_order_from_size(sz));
	if (pte_dirty(orig_pte))
		pte = pte_mkdirty(pte);
	if (pte_young(orig_pte))
		pte = pte_mkyoung(pte);

	set_ptes(mm, addr, ptep, pte, ncontig);

	return 1;
}

void huge_ptep_set_wrprotect(struct mm_struct *mm, unsigned long addr,
			     pte_t *ptep)
{
	pte_t pte = ptep_get(ptep);
	unsigned long ncontig;
	pte_t orig_pte;

	VM_WARN_ON_ONCE(!pte_present(pte));

	if (!pte_huge(pte)) {
		ptep_set_wrprotect(mm, addr, ptep);
		return;
	}

	/*
	 * pte_wrprotect() sets _PAGE_FOW, which lives in the bits the whole
	 * block must agree on, so this takes the break before make path.
	 */
	ncontig = gh_pte_num(pte_gh_order(pte));
	orig_pte = pte_wrprotect(get_clear_contig_flush(mm, addr, ptep, ncontig));
	set_ptes(mm, addr, ptep, orig_pte, ncontig);
}

void huge_pte_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep,
		    unsigned long sz)
{
	unsigned long i, ncontig = num_contig_ptes(sz);

	for (i = 0; i < ncontig; i++, addr += PAGE_SIZE, ptep++)
		pte_clear(mm, addr, ptep);
}

bool __init arch_hugetlb_valid_size(unsigned long size)
{
	return gh_order_from_size(size) != 0;
}

#ifdef CONFIG_ARCH_ENABLE_HUGEPAGE_MIGRATION
bool arch_hugetlb_migration_supported(struct hstate *h)
{
	return gh_order_from_size(huge_page_size(h)) != 0;
}
#endif

static __init int alpha_hugetlbpage_init(void)
{
	unsigned int order;

	for_each_gh_order(order)
		hugetlb_add_hstate(gh_cont_shift(order) - PAGE_SHIFT);

	return 0;
}
arch_initcall(alpha_hugetlbpage_init);
