#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "userprog/syscall.h"
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif

#define MAX_ARGC		128		/* implement argument passing */
#define MAX_ARGU_LEN	128
#define WSIZE			8

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

static void
construct_stack(struct intr_frame *if_, int argc, char ** argv);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *cmdline) {
	char *cmdline_copy = NULL;
	char *file_name = NULL;
	char *save_ptr = NULL;
	struct process_data_bank *child_bank = NULL;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	cmdline_copy = palloc_get_page (0);
	if (cmdline_copy == NULL)
		goto error;
	
	strlcpy (cmdline_copy, cmdline, PGSIZE);
	file_name = strtok_r(cmdline, " ", &save_ptr);
	
	/* make process_memory_block */
	child_bank = palloc_get_page(PAL_USER);
	if(child_bank == NULL)
		goto error;
	
	/* initialization */
	child_bank->tid = 0;	/* don't know yet, set right value after thread_create  */
    child_bank->exit_stat = -1;
	child_bank->cmdline = cmdline_copy;

	child_bank->init_mark = true;
	child_bank->fork_succ = false;
	child_bank->exit_mark = false;
	child_bank->wait_mark = false;
    child_bank->orphan = false;

    sema_init(&child_bank->sema_init, 0);
    sema_init(&child_bank->sema_fork, 0);
    sema_init(&child_bank->sema_wait, 0);

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (file_name, PRI_DEFAULT, initd, child_bank);
	if (tid == TID_ERROR)
		goto error;

	/* after copy cmdline_copy data in process_exec(), freed it */
	sema_down(&child_bank->sema_init);
	if(cmdline_copy) palloc_free_page(cmdline_copy);
	
	list_push_back(&thread_current()->child_list, &child_bank->elem);
	return tid;

	error:
		/* freed all allocated resource */
		if(cmdline_copy)	palloc_free_page (cmdline_copy);
		if(child_bank)		palloc_free_page (child_bank);
		return TID_ERROR;
}

/* A thread function that launches first user process. */
static void
initd (void *aux) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif
	struct thread *child = thread_current();
	struct process_data_bank *child_bank = (struct process_data_bank *)aux;

	/* 1. update tid of child_bank */
	child_bank->tid = child->tid;

	/* 2. store memory block data into child thread */
	child->data_bank = child_bank;
	
	if (process_exec (child_bank->cmdline) < 0)	
		PANIC("Fail to launch initd\n");

	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/
	struct thread *parent = thread_current();
		
	/* make process_memory_block */
	struct process_data_bank *child_bank = NULL;
	child_bank = palloc_get_page(PAL_USER);
	if(child_bank == NULL)
		goto error;

	/* initialization */
	child_bank->tid = 0;	/* don't know yet, set right value after thread_create  */
    child_bank->exit_stat = -1;
	child_bank->cmdline = NULL;

	child_bank->parent = parent;
	child_bank->parent_if = if_;

	child_bank->init_mark = false;
	child_bank->fork_succ = false;
	child_bank->exit_mark = false;
	child_bank->wait_mark = false;
    child_bank->orphan = false;

    sema_init(&child_bank->sema_init, 0);
    sema_init(&child_bank->sema_fork, 0);
    sema_init(&child_bank->sema_wait, 0);

	tid_t child_tid = thread_create (name, PRI_DEFAULT, __do_fork, child_bank);
	
	/* thread memory allocate failed */
	if(child_tid == TID_ERROR)
		goto error;
	
	sema_down(&child_bank->sema_fork);
	
	/* fork failed */
	if(!child_bank->fork_succ){	
		goto error;
	}

	/* forked successfully */
	list_push_back(&parent->child_list, &child_bank->elem);
	return child_tid;

	error:
		if(child_bank) palloc_free_page(child_bank);
		return TID_ERROR;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if(is_kernel_vaddr(va)) return false;
	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER);
	if(!newpage) return false;

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		palloc_free_page(newpage);
		return false;

	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *current = thread_current ();
	struct process_data_bank *child_bank = (struct process_data_bank *)aux;
	struct thread *parent = (struct thread *)child_bank->parent;

	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if = (struct intr_frame *)child_bank->parent_if;

	/* 1. update rest information of child_bank */
	child_bank->tid = current->tid;

	/* 2. store memory block data into child thread */
	current->data_bank = child_bank;
	
	/* 3. store parent into child thread ?? */

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/
	struct list_elem *e;
	struct fd_t *parent_fd_t;

	if(!list_empty(&parent->fd_list))
	{
		for(e = list_front(&parent->fd_list); e != list_end(&parent->fd_list); e = list_next(e))
		{
			parent_fd_t = list_entry(e, struct fd_t, elem);
			struct fd_t *curr_fd_t = palloc_get_page(PAL_USER);
			if(curr_fd_t == NULL)
				goto error;
				
			curr_fd_t->file = file_duplicate(parent_fd_t->file);
			curr_fd_t->fd = parent_fd_t->fd;

			list_push_back(&current->fd_list, &curr_fd_t->elem);
		}
	}
	current->next_fd = parent->next_fd;
	/* Concern: running_file duplicate ?? */
	
	/* Finally, switch to the newly created process. */
	child_bank->fork_succ = true;
	sema_up(&child_bank->sema_fork);
	if_.R.rax = 0;
	do_iret (&if_);
	
error:
	child_bank->fork_succ = false;
	sema_up(&child_bank->sema_fork);
	thread_exit ();		/* free all resource of above stage */
}

static void 
construct_stack(struct intr_frame *if_, int argc, char ** argv)
{
	char *addrs[MAX_ARGC];
	int length;
	int i;
	void * rsp;
	rsp = (void *)if_->rsp;

	for(i = argc - 1; i >= 0 ; i--){
		length = strlen(argv[i])+1;
		rsp = rsp - length;
		memcpy(rsp, argv[i], length);
		addrs[i] = rsp;
	}

	// word_align
	while((uintptr_t)rsp % WSIZE != 0){
		rsp--;
		*(uint8_t *)(rsp) = 0;
	}
	
	// argv[argc] == NULL
	rsp = rsp - WSIZE;
	*(uint64_t *)rsp = 0;

	for(i = argc - 1; i >= 0 ; i--){
		rsp = rsp - WSIZE;
		*(char **)rsp = addrs[i];
	}

	if_->R.rsi = (uintptr_t)rsp;
	if_->R.rdi = argc;

	// return address
	rsp = rsp - WSIZE;
	*(uint64_t *)rsp = 0;
	if_->rsp = (uintptr_t)rsp;
		
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {

	char cmdline[MAX_ARGU_LEN];
	bool success;
	char * save_ptr;
	char *argv[MAX_ARGC];
	int argc = 0;
	char * file_name;
	struct process_data_bank *cur_bank = thread_current()->data_bank;

	/* parse command line */
	strlcpy(cmdline, f_name, PGSIZE);

	/* after copying data into cmdline, 
		we can free cmdline_copy in process_create_initd */
	if(cur_bank->init_mark)
		sema_up(&cur_bank->sema_init);

	argv[0] = strtok_r(cmdline, " ", &save_ptr);
	while(argv[argc] != NULL){
		argv[++argc] = strtok_r(NULL, " ", &save_ptr);
	}

	file_name = argv[0];	

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup ();

	/* load the binary */
	success = load (file_name, &_if);

	/* If load failed, quit. */
	/* palloc_free_page (f_name); */

	if (!success)
		return -1;

	construct_stack(&_if, argc, argv);

	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */

	int exit_stat;
	struct thread *parent = thread_current();
	struct list *child_list = &parent->child_list;
	struct list_elem *e;

	struct process_data_bank *child_bank = NULL;
	struct process_data_bank *bank;

	if(!list_empty(child_list))
	{
		for(e = list_front(child_list); e = list_end(child_list); e = list_next(e))
		{
			bank = list_entry(e, struct process_data_bank, elem);
			if(bank->tid == child_tid){
				child_bank = bank;
				break;
			}
		}
	}

	if(child_bank == NULL)
	{
		/* pid does not refer to a direct child of the calling process.  */

		/* pid is a direct child of the calling process if and only if
		 the calling process received pid from a successful fork. */
		return -1;
	}
	
	if(child_bank->wait_mark){	
		/* The process that calls wait has already called wait on pid. */
		return -1;
	}
	else{
		child_bank->wait_mark = true;
	}

	if(child_bank->exit_mark){
		/* child is already terminated, return immediately */
		/* include the case, which child is terminated by kernel */
		goto child_exited;
	}
	
	sema_down(&child_bank->sema_wait);
	
	child_exited:
		ASSERT(child_bank->exit_mark);

		exit_stat = child_bank->exit_stat;

		/* no need to hold child_bank memory, freed it */
		list_remove(&child_bank->elem);
		palloc_free_page(child_bank);

		return exit_stat;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	struct process_data_bank *curr_bank = curr->data_bank;
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */

	/* 1. close all open file */
	struct list *fd_list = &curr->fd_list;
	while(!list_empty(fd_list))
	{
		struct list_elem *e = list_pop_front(fd_list);
		struct fd_t *fd_t = list_entry(e, struct fd_t, elem);
		file_close(fd_t->file);
		palloc_free_page(fd_t);
	}
	
	/* 2. close running file */
	if(curr->running_file != NULL)
	 	/* file_allow_write() located in file_close() */
		file_close(curr->running_file);

	/* 3. clean up process_data_bank of child_list */
	
	/* parent process doesn't wait child process and exit,
	therefore, no need to hold child_list process_data_bank memory */
	struct list_elem *e;
	struct list *child_list = &curr->child_list;
	struct process_data_bank *bank;
	if(!list_empty(child_list))
	{
		e = list_pop_front(child_list);
		bank = list_entry(e, struct process_data_bank, elem);
		
		/* no need to wait parent process, parent is also terminated */
		if(bank->exit_mark){
			palloc_free_page(bank);
		}
		else{
			bank->orphan = true;
		}		
	}

	bool curr_orphan = curr_bank->orphan;
	curr_bank->exit_stat = curr->exit_stat;
	curr_bank->exit_mark = true;
	sema_up(&curr_bank->sema_wait);

	/* 4. if current process has left alone and has no parent, 
		clean up process_data bank itself */
	if(curr_orphan){
		palloc_free_page(curr_bank);
	}

	process_cleanup ();
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	lock_acquire(&filesys_lock);
	/* Open executable file. */
	file = filesys_open (file_name);
	
	if (file == NULL) {
		lock_release(&filesys_lock);
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Deny Write On Excutables */
	t->running_file = file;
	file_deny_write(file);
	lock_release(&filesys_lock);

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */
	
	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	file_close (file);
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */
