/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "lib/kernel/bitmap.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

struct bitmap *swap_table;
/* the number of sector in each page */
size_t SECTORS_IN_PAGE = PGSIZE / DISK_SECTOR_SIZE;

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
	swap_disk = disk_get(1,1);
	size_t swap_disk_index = disk_size(swap_disk) / SECTORS_IN_PAGE;
	printf("swap disk index : %d\n", swap_disk_index);
	swap_table = bitmap_create(swap_disk_index);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;

	/* check swap_index is valid */
	if (!bitmap_test(swap_table, anon_page->swap_index))
		return false;
	
	/* read data from disk */
	for(int i = 0; i < SECTORS_IN_PAGE; i++){
		size_t swap_slot_size = (anon_page->swap_index) * SECTORS_IN_PAGE;
		size_t sector_index = swap_slot_size + i;
		off_t offset = i * DISK_SECTOR_SIZE;

		disk_read(swap_disk, sector_index, kva + offset);
	}

	/* after reading, clear swap_table */
	bitmap_set(swap_table, anon_page->swap_index, 0);

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	/* find the index which is empty in the swap_table */
	size_t swap_index = bitmap_scan(swap_table, 0, 1, false);

	if(swap_index == BITMAP_ERROR) 
		PANIC("No more free swap slot!");
	
	if (page == NULL || page->frame == NULL || page->frame->kva == NULL)
		return false;

	/* write data to disk */
	for(int i = 0; i < SECTORS_IN_PAGE; i++){
		size_t swap_slot_size = swap_index * SECTORS_IN_PAGE;
		size_t sector_index = swap_slot_size + i;
		off_t offset = i * DISK_SECTOR_SIZE;

		disk_write(swap_disk, sector_index, page->frame->kva + offset);
	}

	/* convert the swap_index(0) to 1 */
	bitmap_set(swap_table, swap_index, 1);

	/* save the index where the data has stored */
	anon_page->swap_index = swap_index;

	pml4_clear_page(thread_current()->pml4, page->va);
	pml4_set_dirty(thread_current()->pml4, page->va, 0);
	page->frame = NULL;

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	
	if(page->frame){
		/* corresponding physical memory will be freed at process_clean_up */
		/* no need : palloc_free_page(page->frame->kva) */
		list_remove(&page->frame->frame_elem);
		free(page->frame);
	}	
	else{
		bitmap_set(swap_table, anon_page->swap_index, 0);
	}
}
