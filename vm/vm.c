/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/synch.h"
#include "threads/mmu.h"

static struct list frame_list;
static struct lock frame_lock;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_list);
	lock_init(&frame_lock);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* hash structure Helpers */
static unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED);

static bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED);

static void spt_destroy(struct hash_elem *e, void *aux UNUSED);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;
	
	/* TODO: Create the page */
	struct page *page = malloc(sizeof(struct page));
	if(page == NULL)
		goto error;
	
	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: fetch the initialier according to the VM type 
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		switch(VM_TYPE(type))
		{
			case VM_ANON:
				uninit_new(page, upage, init, type, aux, anon_initializer);
				break;

			case VM_FILE:
				uninit_new(page, upage, init, type, aux, file_backed_initializer);
				break;
	
			default:
				goto error;
		}

		page->writable = writable;
		/* TODO: Insert the page into the spt. */
		spt_insert_page(spt, page);
		return true;
	}

error:
	if(page) free(page);
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page page;
	struct hash_elem *e;
	/* TODO: Fill this function. */
	page.va = pg_round_down(va);

	e = hash_find(spt->pages, &page.helem);
	return e != NULL ? hash_entry(e, struct page, helem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */
	if(!hash_insert(spt->pages, &page->helem))
		succ = true;
	return succ;
}

/* remove PAGE in spt and free all resource including memory itself */
void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	
	if(hash_delete(spt->pages, &page->helem))
		vm_dealloc_page (page);
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	struct thread *curr = thread_current();
	 /* TODO: The policy for eviction is up to you. */
	
	if(list_empty(&frame_list))
		PANIC("Impossible, memory leak happens");
		
	struct list_elem *e = list_begin(&frame_list);
	while(true){
		victim = list_entry(e, struct frame, elem);
		if(!pml4_is_accessed(curr->pml4, victim->page->va))
			break;
		pml4_set_accessed(curr->pml4, victim->page->va, false);

		if(e == list_end(&frame_list))
			list_begin(&frame_list);
		else
			list_next(e);
	}
	
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	if(!swap_out(victim->page))
		PANIC("swap memory is full");
	
	/* pml4 connection clear */
	pml4_clear_page(thread_current()->pml4, victim->page->va);

	/* page, frame reference clear */
	victim->page->frame = NULL;
	victim->page = NULL;

	/* frame_list remove */
	list_remove(&victim->elem);

	/* frame physical memory clean up */
	memset(victim->kva, 0, PGSIZE);

	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	/* TODO: Fill this function. */
	struct frame *frame = malloc(sizeof(struct frame));

	frame->page = NULL;
	frame->kva = palloc_get_page(PAL_USER);
	
	if(frame->kva == NULL){
		/* swap case */
		free(frame);
		frame = vm_evict_frame();
	}
	
	list_push_back(&frame_list, &frame->elem);

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	void *stack_end = pg_round_down(addr);
	vm_alloc_and_claim_page(VM_ANON | VM_STACK, stack_end, true);
}

bool is_stack_growth(void *addr, uintptr_t rsp)
{
	bool on_stack = ((USER_STACK - STACK_SIZE_LIMIT) <= addr) && (addr <= USER_STACK);
	bool check_address  = (addr == rsp - 8) || (rsp <= addr);

	return on_stack && check_address;
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
	return false;
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	uintptr_t rsp;

	/* TODO: Validate the fault */
	/* invalid pointer */
	if(addr == NULL)
		return false;

	/* user process try to access kernel virtual address */
	if(is_kernel_vaddr(addr) && user)
		return false;

	rsp = user ? f->rsp : thread_current()->saving_rsp;
	page = spt_find_page(spt, addr);

	/* handle stack growth */
	if (is_stack_growth(addr, rsp) && !page) {
		vm_stack_growth (addr);
		return true;
	}
	
	/* check whether addr refers not present page */
	if(page == NULL)
		return false;

	/* try to write on read-only page */
	if(write && !not_present)
		return vm_handle_wp(page);
	
	/* implement lazy loading */
	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	lock_acquire(&frame_lock);
	destroy (page);
	lock_release(&frame_lock);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct thread *t = thread_current();
	struct page *page = spt_find_page(&t->spt, va);
	/* TODO: Fill this function */
	if(!page) return false;

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct thread *t = thread_current();
	bool swap_succ;

	lock_acquire(&frame_lock);
	struct frame *frame = vm_get_frame();

	bool success;

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	success = pml4_set_page(t->pml4, page->va, frame->kva, page->writable);
	if(!success) return false;
	
	swap_succ = swap_in (page, frame->kva);
	lock_release(&frame_lock);
	return swap_succ;
}

/* allocate and claim page, the actual content in physcial memeory, frame is not initialized */
bool
vm_alloc_and_claim_page (enum vm_type type, void *upage, bool writable)
{
	if(!vm_alloc_page(type, upage, writable))
		return false;

	if(!vm_claim_page(upage))
		return false;

	return true;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	struct hash *pages = malloc(sizeof(struct hash));
	hash_init(pages, page_hash, page_less, NULL);
	spt->pages = pages;
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	
	struct hash_iterator i;
	struct page *page;
	struct loading_datas *aux;
	struct loading_datas *parent_aux;
	hash_first(&i, src->pages);
	while(hash_next(&i))
	{
		page = hash_entry(hash_cur(&i), struct page, helem);
		
		switch(page->operations->type)
		{
			case VM_UNINIT:
				aux = malloc(sizeof(struct loading_datas));
				if(aux == NULL)
					return false;

				switch(page->uninit.type)
				{
					case VM_ANON:
						parent_aux = page->uninit.aux;
						aux->file = file_duplicate(parent_aux->file);
						aux->ofs = parent_aux->ofs;
						aux->read_bytes = parent_aux->read_bytes;
						aux->zero_bytes = parent_aux->zero_bytes;
						
						if(!vm_alloc_page_with_initializer(page->uninit.type, page->va, 
							page->writable, page->uninit.init, aux)){
							if(aux)	free(aux);
							return false;
						}
						break;

					case VM_FILE:
						break;

					default:
						return false;
				}
				break;
				
			case VM_ANON:
				if(!vm_alloc_and_claim_page(page->operations->type, page->va, page->writable))
					return false;

				/* initialize actual content in physical memory, frame */
				struct page *copy_page = spt_find_page(dst, page->va);
				memcpy(copy_page->frame->kva, page->frame->kva, PGSIZE);
				break;
				
			case VM_FILE:
				break;

			default:
				return false;
		}
	}

	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_destroy(spt->pages, spt_destroy);
	free(spt->pages);
}

/* Returns a hash value for page p. */
static unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, helem);
  return hash_bytes (&p->va, sizeof p->va);
}

/* Returns true if page a precedes page b. */
static bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, helem);
  const struct page *b = hash_entry (b_, struct page, helem);

  return a->va < b->va;
}

static void
spt_destroy(struct hash_elem *e, void *aux UNUSED)
{
	struct page *page = hash_entry(e, struct page, helem);
	vm_dealloc_page(page);
}

