#ifndef __HYP_MMU__
#define __HYP_MMU__

#include <asm/memory.h>
#include <asm/page.h>

/*
 * As we only have the TTBR0_EL2 register, we cannot express
 * "negative" addresses. This makes it impossible to directly share
 * mappings with the kernel.
 *
 * Instead, give the HYP mode its own VA region at a fixed offset from
 * the kernel by just masking the top bits (which are all ones for a
 * kernel address).
 */
#define HYP_PAGE_OFFSET_SHIFT	VA_BITS
#define HYP_PAGE_OFFSET_MASK	((UL(1) << HYP_PAGE_OFFSET_SHIFT) - 1)
#define HYP_PAGE_OFFSET		(PAGE_OFFSET & HYP_PAGE_OFFSET_MASK)

/*
 * Our virtual mapping for the idmap-ed MMU-enable code. Must be
 * shared across all the page-tables. Conveniently, we use the last
 * possible page, where no kernel mapping will ever exist.
 */
#define TRAMPOLINE_VA		(HYP_PAGE_OFFSET_MASK & PAGE_MASK)

#ifdef __ASSEMBLY__

/*
 * Convert a kernel VA into a HYP VA.
 * reg: VA to be converted.
 */
.macro kern_hyp_va      reg
        and     \reg, \reg, #HYP_PAGE_OFFSET_MASK
.endm

#else

#include <asm/pgalloc.h>
#include <asm/cachetype.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <linux/highmem.h>



int tp_mmu_init(void);
phys_addr_t tp_mmu_get_httbr(void);
phys_addr_t tp_get_idmap_vector(void);


#define KERN_TO_HYP(kva)	((unsigned long)kva - PAGE_OFFSET + HYP_PAGE_OFFSET)
#define USER_PAGE_OFFSET    (PAGE_OFFSET & 0x0000FFFFFFFFFFFF) // mask out the negatives

/*
 * We currently only support a 40bit IPA.
 */
#define KVM_PHYS_SHIFT	(40)
#define KVM_PHYS_SIZE	(1UL << KVM_PHYS_SHIFT)
#define KVM_PHYS_MASK	(KVM_PHYS_SIZE - 1UL)

int create_hyp_mappings(void *, void *,pgprot_t );
void __hyp_text tp_clear_icache(unsigned long vaddr,int size);
int create_hyp_io_mappings(void *from, void *to, phys_addr_t);
void free_boot_hyp_pgd(void);
void free_hyp_pgds(void);

phys_addr_t tp_mmu_get_httbr(void);
phys_addr_t tp_mmu_get_boot_httbr(void);
phys_addr_t tp_get_idmap_vector(void);

#define	tp_set_pte(ptep, pte)		set_pte(ptep, pte)
#define	tp_set_pmd(pmdp, pmd)		set_pmd(pmdp, pmd)
#define tp_pgd_addr_end(addr, end)	pgd_addr_end(addr, end)
#define tp_pud_addr_end(addr, end)	pud_addr_end(addr, end)
#define tp_pmd_addr_end(addr, end)	pmd_addr_end(addr, end)
#define tp_flush_dcache_to_poc(a,l)	__flush_dcache_area((a), (l))
#define tp_pgd_index(addr)	(((addr) >> PGDIR_SHIFT) & (PTRS_PER_S2_PGD - 1))
#define tp_pte_table_empty(ptep) 	tp_page_empty(ptep)


static inline bool tp_page_empty(void *ptr)
{
	struct page *ptr_page = virt_to_page(ptr);
	return page_count(ptr_page) == 1;
}

#ifdef __PAGETABLE_PMD_FOLDED
#define tp_pmd_table_empty(pmdp) (0)
#else
#define tp_pmd_table_empty(pmdp)  (tp_page_empty(pmdp))
#endif

#ifdef __PAGETABLE_PUD_FOLDED
#define tp_pud_table_empty(pudp) (0)
#else
#define tp_pud_table_empty(pudp) (tp_page_empty(pudp))
#endif

static inline void tp_flush_dcache_pte(pte_t pte)
{
	void *va = kmap_atomic(pte_page(pte));
	tp_flush_dcache_to_poc(va, PAGE_SIZE);
	kunmap_atomic(va);
}

static inline void tp_flush_dcache_pmd(pmd_t pmd)
{
	struct page *page = pmd_page(pmd);
	tp_flush_dcache_to_poc(page_address(page), PMD_SIZE);
}

static inline void tp_flush_dcache_pud(pud_t pud)
{
	struct page *page = pud_page(pud);
	tp_flush_dcache_to_poc(page_address(page), PUD_SIZE);
}

#define tp_virt_to_phys(x)		__virt_to_phys((unsigned long)(x))

static inline bool __tp_cpu_uses_extended_idmap(void)
{
	return __cpu_uses_extended_idmap();
}

#define hyp_pgd_order get_order(PTRS_PER_PGD * sizeof(pgd_t))

#define tp_pmd_huge(_x)	(pmd_huge(_x) || pmd_trans_huge(_x))
#define tp_pud_huge(_x)	pud_huge(_x)

#endif
#endif
