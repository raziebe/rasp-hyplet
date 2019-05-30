#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/hyplet.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>		/* for file_operations */
#include <linux/slab.h>	/* versioning */
#include <linux/cdev.h>
#include "acqusition_trap.h"
#include "hyp_mmu.h"
#include "hypletS.h"

unsigned long __hyp_text __hyp_phys_to_virt(unsigned long addr,struct hyplet_vm *vm)
{
	return ( (unsigned long)((addr) - vm->hyp_memstart_addr) | PAGE_OFFSET );
}

unsigned long __hyp_text hyp_phys_to_virt(unsigned long addr,struct hyplet_vm *vm)
{
	if (is_hyp())
		return KERN_TO_HYP( __hyp_phys_to_virt(addr, vm) - KERN_TO_HYP(0)) & HYP_PAGE_OFFSET_MASK;
	return (unsigned long) phys_to_virt(addr);
}


static inline long make_special_page_desc(unsigned long real_phyaddr,int s2_rw)
{
	unsigned long addr = real_phyaddr;

	return (DESC_AF) | (0b11 << DESC_SHREABILITY_SHIFT) |
	                ( s2_rw  << DESC_S2AP_SHIFT) | (0b1111 << 2) |
	                  DESC_TABLE_BIT | DESC_VALID_BIT | addr;
}

/*
 * Given a physical address, search in the page table
 * and find the page descriptor and change its access rights
 */
unsigned long*  __hyp_text  ipa_find_page_desc(struct hyplet_vm *vm,unsigned long phy_addr)
{
	int i,j ,k;
	unsigned long temp;
	unsigned long *desc1;
	unsigned long *desc2;
	unsigned long *desc3;

	desc1  = vm->vttbr_el2_kern;
	if (is_hyp())
		desc1  = (unsigned long*)KERN_TO_HYP(vm->vttbr_el2_kern);

	i = phy_addr / 0x40000000; // 1GB
	temp = desc1[i]  & 0x000FFFFFFFFFFC00LL;

	desc2 = (unsigned long *) hyp_phys_to_virt(temp, vm);
	j = (phy_addr & 0x3FFFFFFF) / 0x200000; // 2MB
	temp = desc2[j]  & 0x000FFFFFFFFFFC00LL;

	desc3 = (unsigned long *) hyp_phys_to_virt(temp, vm);
	k = (phy_addr & 0x1FFFFF) / PAGE_SIZE;

	return &desc3[k];
}

/*
 * Call in EL2 context.
 * Walk on the page table and set each page to readonly
 */
void __hyp_text   walk_ipa_el2(struct hyplet_vm *vm,int s2_page_access)
{
	int i,j ,k, n;
	unsigned long *desc0 = (unsigned long *)KERN_TO_HYP(vm->ipa_desc_zero);
	unsigned long temp;
	unsigned long *desc1;
	unsigned long *desc2;
	unsigned long *desc3;

	vm->ipa_pages = 0;

	for (i = 0 ; i < PAGE_SIZE/sizeof(long); i++){

		if (desc0[i]) {
			temp  = desc0[i] & 0x000FFFFFFFFFFC00LL;
			desc1 = (unsigned long *) hyp_phys_to_virt(temp, vm);

			for (j = 0 ; j < PAGE_SIZE/sizeof(long); j++){
				if (desc1[j]){
					temp = desc1[j] & 0x000FFFFFFFFFFC00LL;
					desc2 = (unsigned long *)hyp_phys_to_virt(temp, vm);

					for (k = 0 ; k < PAGE_SIZE/sizeof(long); k++){

						if (desc2[k]){

							temp = desc2[k] & 0x000FFFFFFFFFFC00LL;
							desc3 = (unsigned long *)  hyp_phys_to_virt(temp, vm);

							for (n = 0 ; n < PAGE_SIZE/sizeof(long); n++){
								if (desc3[n]){
									/*
									 * set page access rights to S2_RW.
									 * */
									temp = desc3[n] & 0x000FFFFFFFFFFC00LL;
									desc3[n] = make_special_page_desc(temp, s2_page_access);
									vm->ipa_pages++;
								}
							}
						}
					}
				}
			}
		}
	}
}



/*
 * Called in EL2 to handle a faulted address
 */
unsigned long __hyp_text hyplet_handle_abrt(struct hyplet_vm *vm,
		unsigned long phy_addr)
{
	unsigned long* desc;
	unsigned long *temp;

	// first clean the attributes bits: address is in bits 47..12
	phy_addr &= 0xFFFFFFFFF000;

	// Find the descriptor in the MMU
	desc = ipa_find_page_desc(vm, phy_addr);
	// return descriptor to its RW
	*desc = make_special_page_desc(phy_addr, S2_PAGE_ACCESS_RW);
	hyplet_invld_ipa_tlb(phy_addr >> 12);
	vm->ipa_pages_processed++;
	// copy its content
	if (is_device_mem(vm,phy_addr)){
		return 0x99;
	}

	temp = (unsigned long *)hyp_phys_to_virt(phy_addr, vm);
	return (unsigned long)temp[4];
}

void debug_func(void *addr)
{
	printk("about to crash %p\n",(addr));
}
/* user interface  */
static struct proc_dir_entry *procfs = NULL;

static int proc_open(struct inode *inode, struct file *filp)
{
	filp->private_data = (void *)0x01;
	return 0;
}

static ssize_t proc_read(struct file *filp, char __user * page,
			 size_t size, loff_t * off)
{
	ssize_t len = 0;
	int cpu;

	if (!filp->private_data)
		return 0;

	for_each_possible_cpu(cpu){
		struct hyplet_vm *vm;

		vm = hyplet_get(cpu);
		len += sprintf(page + len,
				"%d pages processed = %d\n",
				vm->ipa_pages,vm->ipa_pages_processed);
	}
	len += sprintf(page + len, "Nr Io Addresses %ld\n",get_ioaddressesNR());
	filp->private_data = 0x00;
	return len;
}


static struct file_operations acqusition_proc_ops = {
	.open = proc_open,
	.read = proc_read,
};

unsigned long uaddr_to_physaddr(unsigned long uaddr)
{
        unsigned long pfn;
        struct page *pages[1];
        int nr;

        nr = get_user_pages_fast(uaddr,1, 0, (struct page **)&pages);
        if (nr <= 0){
               return 0x00;
        }
        pfn = page_to_pfn(pages[0]);
        put_page(pages[0]);
        return __pfn_to_phys(pfn);
}



static ssize_t honeypot_ops_write(struct file *filp,
	const char __user *umem, size_t size, loff_t *off)
{
    struct hyplet_vm *vm =  hyplet_get_vm();
    phys_addr_t phys_addr;
    long long virt_addr;
    long long virt_start;
    long long virt_end;
    unsigned long* desc;

    virt_start = (long long)( ((long)umem + PAGE_SIZE) & PAGE_MASK);
    virt_end = virt_start + PAGE_SIZE;
    printk("raz: virt start 0x%llx\n",
                     (virt_start + PAGE_SIZE) & PAGE_MASK );
     /*
      * Walk over the entire address range
      * and mark it as not accessible.
      * */
     for (virt_addr = virt_start;
                     virt_addr < virt_end; virt_addr += PAGE_SIZE) {

//             phys_addr = virt_to_phys((void*)virt_addr);
             desc = ipa_find_page_desc(vm, phys_addr);

            phys_addr =  uaddr_to_physaddr(virt_addr);
            printk("2. phys addr %lx\n",phys_addr);
            desc = ipa_find_page_desc(vm, phys_addr);
             *desc = make_special_page_desc(phys_addr,
                             S2_PAGE_ACCESS_NONE);

     }
     return size;
}

static ssize_t honeypot_ops_read(struct file *filp, char __user *umem,
				size_t size, loff_t *off)
{
	int n = 0;
	struct hyplet_vm *vm =  hyplet_get_vm();
	return size-n;
}

static struct file_operations honeypot_ops = {
	write: honeypot_ops_write,
	read:  honeypot_ops_read,
};


int honeypot_ops_major = 0;

void acqusion_init_procfs(void)
{
	procfs = proc_create_data("hyplet_stats", 
			O_RDWR, NULL, &acqusition_proc_ops, NULL);

	honeypot_ops_major = register_chrdev(0, "honeypot", &honeypot_ops);
	if (honeypot_ops_major < 0){
		printk(MODULE_NAME "Failed to create honeypot character dev");
	}

}



