/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include <bitmap.h>
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"

#define CEILING(x, y) (((x) + (y) - 1) / (y))
#define SECTORS_PER_PAGE CEILING(PGSIZE, DISK_SECTOR_SIZE)
#define SWAP_IN_STATE	-1

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

static struct bitmap *swap_table;

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);

	size_t max_slot = disk_size (swap_disk) / SECTORS_PER_PAGE;
	swap_table = bitmap_create (max_slot);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;

	anon_page->thread = thread_current ();
	anon_page->swap_index = SWAP_IN_STATE;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	disk_sector_t sec_num;
	int i;

	if (anon_page->swap_index == SWAP_IN_STATE)
		return false;

	
	for (i = 0; i < SECTORS_PER_PAGE; i++) {
		sec_num = (disk_sector_t) (anon_page->swap_index * SECTORS_PER_PAGE) + i;
		disk_read (swap_disk, sec_num, kva + i * DISK_SECTOR_SIZE);
	}
	
	bitmap_set (swap_table, anon_page->swap_index, false);
	anon_page->swap_index = SWAP_IN_STATE;

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	disk_sector_t sec_num;
	size_t swap_index;
	
	swap_index = bitmap_scan_and_flip (swap_table, 0, 1, false);
	if(swap_index == BITMAP_ERROR)
		PANIC("swap table is full, no enough memory");

	for (int i = 0; i < SECTORS_PER_PAGE; i++) {
		sec_num = (disk_sector_t) (swap_index * SECTORS_PER_PAGE) + i;
		disk_write (swap_disk, sec_num, page->frame->kva + i * DISK_SECTOR_SIZE);
	}

	anon_page->swap_index = swap_index;
	pml4_set_dirty (anon_page->thread->pml4, page->va, false);

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	
	if(page->frame){
		/* corresponding physical memory will be freed at process_clean_up */
		/* no need : palloc_free_page(page->frame->kva) */
		list_remove (&page->frame->elem);
		free(page->frame);
	}
	else{
		bitmap_set (swap_table, page->anon.swap_index, false);
	}
}
