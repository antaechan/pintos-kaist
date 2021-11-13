/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include <string.h>

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);
static bool lazy_load_file (struct page *page, void *aux);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
	struct loading_datas *datas = (struct loading_datas *)page->uninit.aux;
	file_page->file = datas->file;
	file_page->ofs = datas->ofs;
	file_page->length = datas->total_length;
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;

	struct file *file = file_page->file;
	size_t read_bytes = file_page->read_bytes;
	off_t ofs = file_page->ofs;

	if(read_bytes != file_read_at(file, kva, read_bytes, ofs))
		return false;
	
	if(read_bytes < PGSIZE)
		memset(kva + read_bytes, 0, PGSIZE - read_bytes);

	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	struct thread *curr = thread_current();

	struct file *file = file_page->file;
	size_t read_bytes = file_page->read_bytes;
	off_t ofs = file_page->ofs;

	/* write back */
	if(pml4_is_dirty(curr->pml4, page->va))
	{
		file_write_at(file, page->va, read_bytes, ofs);
		pml4_set_dirty(curr->pml4, page->va, false);
	}
	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;

	struct file *file = file_page->file;
	size_t read_bytes = file_page->read_bytes;
	off_t ofs = file_page->ofs;

	/* write back */
	if(pml4_is_dirty(thread_current()->pml4, page->va))
		file_write_at(file, page->va, read_bytes, ofs);

	file_close(file_page->file);

	if(page->frame){
		/* corresponding physical memory will be freed at process_clean_up */
		/* no need : palloc_free_page(page->frame->kva) */
		list_remove(&page->frame->elem);
		free(page->frame);
	}

}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
			
	uint32_t read_bytes = length;

	off_t start_ofs = offset;
	void *start_addr = addr;
	
	while (read_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		struct loading_datas *aux = malloc(sizeof(struct loading_datas));
		aux->file = file_reopen(file);
		aux->ofs = start_ofs;
		aux->read_bytes = page_read_bytes;
		aux->total_length = (start_addr == addr) ? length : -1;

		if (!vm_alloc_page_with_initializer (VM_FILE, start_addr,
					writable, lazy_load_file, aux))
		{
			file_close(aux->file);
			free(aux);

			/* munmap page from addr to start_addr */
			struct page *page = spt_find_page(&thread_current()->spt, addr);
			if(page){
				page->file.length = (start_addr - addr);
				do_munmap(addr);
			}
			return NULL;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		start_addr += PGSIZE;
		start_ofs += PGSIZE;
	}
	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	
	struct page *page = spt_find_page(&thread_current()->spt, addr);

	if(page == NULL)
		return;

	struct file_page *file_page = &page->file;
	int length = file_page->length;

	void *start_addr = addr;
	if(page_get_type(page) == VM_FILE && length > 0)
	{
		while(start_addr < (addr + length)){
			page = spt_find_page(&thread_current()->spt, start_addr);
			spt_remove_page(&thread_current()->spt, page);
			start_addr += PGSIZE;
		}
	}
}

static bool
lazy_load_file (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	struct loading_datas *datas = (struct loading_datas *)aux;
	struct file *file = datas->file;
	size_t read_bytes = datas->read_bytes;
	size_t zero_bytes = PGSIZE - read_bytes;
	off_t ofs = datas->ofs;
	void *pa = page->frame->kva;
	bool success = false;
	
	page->file.read_bytes = file_read_at(file, pa, read_bytes, ofs);
	
	if(page->file.read_bytes < PGSIZE)
		memset(pa + read_bytes, 0, PGSIZE - page->file.read_bytes);
	
	pml4_set_dirty(thread_current()->pml4, page->va, false);
	success = true;

	done:
		free(datas);
		return success;
}
