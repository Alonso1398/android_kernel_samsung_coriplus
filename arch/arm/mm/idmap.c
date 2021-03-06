#include <linux/kernel.h>

#include <asm/cputype.h>
#include <asm/idmap.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>

static void idmap_add_pmd(pud_t *pud, unsigned long addr, unsigned long end,
	unsigned long prot)
{
	pmd_t *pmd = pmd_offset(pud, addr);

	addr = (addr & PMD_MASK) | prot;
	pmd[0] = __pmd(addr);
	addr += SECTION_SIZE;
	pmd[1] = __pmd(addr);
	flush_pmd_entry(pmd);
}

static void idmap_add_pud(pgd_t *pgd, unsigned long addr, unsigned long end,
	unsigned long prot)
{
	pud_t *pud = pud_offset(pgd, addr);
	unsigned long next;

	do {
		next = pud_addr_end(addr, end);
		idmap_add_pmd(pud, addr, next, prot);
	} while (pud++, addr = next, addr != end);
}

void identity_mapping_add(pgd_t *pgd, unsigned long addr, unsigned long end)
{
	unsigned long prot, next;

	prot = PMD_TYPE_SECT | PMD_SECT_AP_WRITE;
	if (cpu_architecture() <= CPU_ARCH_ARMv5TEJ && !cpu_is_xscale())
		prot |= PMD_BIT4;

	pgd += pgd_index(addr);
	do {
		next = pgd_addr_end(addr, end);
		idmap_add_pud(pgd, addr, next, prot);
	} while (pgd++, addr = next, addr != end);
}

#ifdef CONFIG_SMP
static void idmap_del_pmd(pud_t *pud, unsigned long addr, unsigned long end)
{
	pmd_t *pmd = pmd_offset(pud, addr);
	pmd_clear(pmd);
}

static void idmap_del_pud(pgd_t *pgd, unsigned long addr, unsigned long end)
{
	pud_t *pud = pud_offset(pgd, addr);
	unsigned long next;

	do {
		next = pud_addr_end(addr, end);
		idmap_del_pmd(pud, addr, next);
	} while (pud++, addr = next, addr != end);
}

void identity_mapping_del(pgd_t *pgd, unsigned long addr, unsigned long end)
{
	unsigned long next;

	pgd += pgd_index(addr);
	do {
		next = pgd_addr_end(addr, end);
		idmap_del_pud(pgd, addr, next);
	} while (pgd++, addr = next, addr != end);
}
#else
void identity_mapping_del(pgd_t *pgd, unsigned long addr, unsigned long end)
{
}
#endif

/*
 * In order to soft-boot, we need to insert a 1:1 mapping of memory.
 * This will then ensure that we have predictable results when turning
 * the mmu off.
 */
void setup_mm_for_reboot(char mode, pgd_t *pgd)
{
	unsigned long kernel_end;

	/* If we don't have a pgd, hijack the current task. */
	if (pgd == NULL) {
		pgd = current->active_mm->pgd;
		identity_mapping_add(pgd, 0, TASK_SIZE);
	} else {
		identity_mapping_add(pgd, 0, TASK_SIZE);
		/*
		 * Extend the flat mapping into kernelspace.
		 * We leave room for the kernel image and the reserved
		 * page below swapper.
		 */
		kernel_end = ALIGN((unsigned long)_end, PMD_SIZE);
		identity_mapping_add(pgd, kernel_end, 0);
	}

	/* Clean and invalidate L1. */
	flush_cache_all();

	/* Switch exclusively to kernel mappings. */
	cpu_switch_mm(pgd, &init_mm);

	/* Flush the TLB. */
	local_flush_tlb_all();
}
