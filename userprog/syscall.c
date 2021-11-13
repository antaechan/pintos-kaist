#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "threads/init.h"
#include "userprog/process.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "vm/vm.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

static void check_user_memory(void *uaddr);
static void check_addr_writable(void *uaddr);
static bool check_fd(int fd);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

void
syscall_init (void) {
	
	lock_init(&filesys_lock);
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.

	/* when page fault occurs from kernel mode, f->rsp has undefined value.
		should save rsp in current thread */
	/* e.g. page fault occurs in implementing read, write system call */
	thread_current()->saving_rsp = f->rsp;

	uint64_t arg1 = f->R.rdi;
	uint64_t arg2 = f->R.rsi;
	uint64_t arg3 = f->R.rdx;
	uint64_t arg4 = f->R.r10;
	uint64_t arg5 = f->R.r8;
	uint64_t arg6 = f->R.r9;
	
	switch (f->R.rax)
	{
		case SYS_HALT:              	 
			sys_halt();
			NOT_REACHED();
			break;

		case SYS_EXIT:                   
			sys_exit(arg1);
			NOT_REACHED();
			break;

		case SYS_FORK:                   
			f->R.rax = sys_fork((const char *)arg1, f);
			break;

		case SYS_EXEC:                   
			f->R.rax = sys_exec((const char *)arg1);
			break;

		case SYS_WAIT:                   
			f->R.rax = sys_wait((tid_t)arg1);
			break;

		case SYS_CREATE:                 
			f->R.rax = sys_create((const char *)arg1, (unsigned)arg2);
			break;

		case SYS_REMOVE:                 
			f->R.rax = sys_remove((const char *)arg1);
			break;

		case SYS_OPEN:                   
			f->R.rax = sys_open((const char *)arg1);
			break;

		case SYS_FILESIZE:               
			f->R.rax = sys_filesize((int)arg1);
			break;

		case SYS_READ:                   
			f->R.rax = sys_read((int)arg1, (void *)arg2, (unsigned)arg3);
			break;

		case SYS_WRITE:                  
			f->R.rax = sys_write((int)arg1, (const void *)arg2, (unsigned)arg3);
			break;

		case SYS_SEEK:                   
			sys_seek((int)arg1, (unsigned)arg2);
			break;

		case SYS_TELL:                   
			f->R.rax = sys_tell((int)arg1);
			break;

		case SYS_CLOSE:
			sys_close((int)arg1);
			break;

		case SYS_DUP2:                   
			f->R.rax = sys_dup2((int)arg1, (int)arg2);
			break;

		case SYS_MMAP:
			f->R.rax = sys_mmap((void *)arg1, (size_t)arg2, (int)arg3, (int)arg4, (off_t)arg5);
			break;

		case SYS_MUNMAP:
			sys_munmap((void *)arg1);
			break;

		default:
			thread_exit();
			break;
	}

}


/* Projects 2 and later. ----------------------------------- */
/* ----------------- process syscall ----------------------- */

/* Halt the operating system. */
void sys_halt (void){
	power_off();
}

/* Terminate this process. */
void sys_exit (int status)
{
	struct thread * cur = thread_current();
	/* 1. store exit_stat in bank */
	
  	if(cur->data_bank != NULL) {
    	cur->data_bank->exit_stat = status;
  	}

	/* 2. termination message */
	printf ("%s: exit(%d)\n", cur->name, status);

	/* 3. call thread_exit function */
	thread_exit();
}

/* Clone current process. */
tid_t sys_fork (const char *thread_name, struct intr_frame *f)
{
	check_user_memory(thread_name);
	return process_fork(thread_name, f);
}

/* Switch current process. */
int sys_exec (const char *cmdline){
	check_user_memory(cmdline);

	/* lock synchronization exist in process_exec */
	tid_t tid = process_exec(cmdline);

	/* if program cannot load or run for any reason, terminate */
	if(tid == -1){
		sys_exit(-1);
	}
}

/* Wait for a child process to die. */
int sys_wait (tid_t child_id){
	return process_wait(child_id);
}

/* --------------- file system syscall --------------------- */

/* Create a file. */
bool sys_create (const char *file, unsigned initial_size)
{
	check_user_memory(file);
	bool success;

	lock_acquire(&filesys_lock);
	success = filesys_create(file, initial_size);
	lock_release(&filesys_lock);

	return success;
}
/* Delete a file. */
bool sys_remove (const char *file)
{
	check_user_memory(file);

	bool success;

	lock_acquire(&filesys_lock);
	success = filesys_remove(file);
	lock_release(&filesys_lock);

	return success;
}
/* Open a file. */
int sys_open (const char *file)
{
	check_user_memory(file);
	struct file *open_file;
	int fd;

	lock_acquire(&filesys_lock);
	open_file = filesys_open(file); 
	if(open_file == NULL){
		lock_release(&filesys_lock);
		return -1;
	}
	fd = insert_file2list(open_file, thread_current());
	lock_release(&filesys_lock);

	return fd;
}

/* Obtain a file's size. */
int sys_filesize (int fd){

	struct thread *t = thread_current();
	struct fd_t *fd_t;
	int length;

	if(!check_fd(fd)) return -1;

	lock_acquire(&filesys_lock);
	fd_t = search_fd_t_double_list(fd, &t->fd_list);
	if(fd_t == NULL){
		lock_release(&filesys_lock);
		return -1;
	}

	length = file_length(fd_t->file);
	lock_release(&filesys_lock);
	return length;
}

/* Read from a file. */
int sys_read (int fd, void *buffer, unsigned length){
	
	struct fd_t *fd_t;
	struct thread *t = thread_current();
	char *ptr = (char *)buffer;
	int cnt;

	check_user_memory(ptr);
	check_user_memory(ptr + length - 1);

#ifdef VM
	check_addr_writable(ptr);
	check_addr_writable(ptr + length - 1);
#endif

	if(!check_fd(fd)) return -1;

	lock_acquire(&filesys_lock);

	struct list_elem *e;
	struct fd *fd_num;

	if(fd_num = search_fd_single_list(fd, &t->stdin_list))
		goto stdin_read;

	fd_t = search_fd_t_double_list(fd, &t->fd_list);
	if(fd_t == NULL){
		cnt = -1;
		goto done;
	}
	
	/* read file data and write at buffer */
	cnt = file_read(fd_t->file, buffer, length);

	done:
		lock_release(&filesys_lock);
		return cnt;

	stdin_read:
		/* read from stdin file */
		for (cnt = 0; cnt < length; cnt++)
		{
			*(ptr + cnt) = input_getc();
			if( *(ptr + cnt) == '\n')
			{
				*(ptr + cnt) = NULL;
				break;
			}
		}
		check_user_memory(ptr + length);
		*(ptr + cnt) = NULL;
		goto done;

}

/* Write to a file. */
int sys_write (int fd, const void *buffer, unsigned length){
	
	struct fd_t *fd_t;
	struct thread *t = thread_current();
	int cnt;

	check_user_memory(buffer);
	check_user_memory(buffer + length - 1);

	if(!check_fd(fd)) return -1;

	lock_acquire(&filesys_lock);

	struct list_elem *e;
	struct fd *fd_num;

	if(fd_num = search_fd_single_list(fd, &t->stdout_list))
		goto stdout_write;

	fd_t = search_fd_t_double_list(fd, &t->fd_list);
	if(fd_t == NULL){
		cnt = -1;
		goto done;
	}

	/* write data in buffer to file */
	cnt = file_write(fd_t->file, buffer, length);
	
	done:
		lock_release(&filesys_lock);
		return cnt;

	stdout_write:
		/* write at STDOUT file */
		putbuf((const char *)buffer, length);
		cnt = length;
		goto done;
}

/* Change position in a file. */
void sys_seek (int fd, unsigned position){

	struct thread *t = thread_current();
	struct fd_t* fd_t;
	if(!check_fd(fd)) return;

	lock_acquire(&filesys_lock);

	fd_t = search_fd_t_double_list(fd, &t->fd_list);
	if(fd_t != NULL) file_seek(fd_t->file, position);
	lock_release(&filesys_lock);

}

/* Report current position in a file. */
unsigned sys_tell (int fd){

	struct thread *t = thread_current();
	unsigned position;
	struct fd_t *fd_t;

	if(!check_fd(fd)) return -1;

	lock_acquire(&filesys_lock);

	fd_t = search_fd_t_double_list(fd, &t->fd_list);
	if(fd_t != NULL){
		position = file_tell(fd_t->file);
	}
	else{
		position = -1;
	}

	lock_release(&filesys_lock);
	return position;
}

/* Close a file. */
void sys_close (int fd){
	
	struct fd_t *fd_t;
	struct fd *fd_num;
	struct list_elem *e;
	struct thread *t = thread_current();

	if(!check_fd(fd))	return;

	lock_acquire(&filesys_lock);

	if(fd_num = search_fd_single_list(fd, &t->stdin_list)){
		list_remove(&fd_num->elem);
		palloc_free_page(fd_num);
		goto done;
	}

	if(fd_num = search_fd_single_list(fd, &t->stdout_list)){
		list_remove(&fd_num->elem);
		palloc_free_page(fd_num);
		goto done;
	}

	fd_t = search_fd_t_double_list(fd, &t->fd_list);
	fd_num = search_fd_double_list(fd, &t->fd_list);

	if (fd_num == NULL)
		goto done;

	list_remove(&fd_num->elem);
	palloc_free_page(fd_num);

	/* should close file */
	if(list_empty(&fd_t->dup2_list))
	{
		file_close(fd_t->file);
		list_remove(&fd_t->elem);
		palloc_free_page(fd_t);
	}

	done:
		lock_release(&filesys_lock);
		return;
}

struct fd *search_fd_single_list(int fd, struct list *list){
	struct list_elem *e;
	struct fd *fd_num;

	if(list_empty(list)) return NULL;
	for(e = list_begin(list); e != list_end(list); e = list_next(e))
	{
		fd_num = list_entry(e, struct fd, elem);
		if(fd_num->fd == fd)
			return fd_num;
	}
	return NULL;
}

struct fd *search_fd_double_list(int fd, struct list *list){
	struct list_elem *e;
	struct fd_t *fd_t;
	struct fd *fd_num;

	if(list_empty(list))
		return NULL;
	
	for(e = list_begin(list); e != list_end(list); e = list_next(e))
	{
		fd_t = list_entry(e, struct fd_t, elem);
		fd_num = search_fd_single_list(fd, &fd_t->dup2_list);
		if(fd_num)	return fd_num;
	}
	return NULL;
}

struct fd_t *search_fd_t_double_list(int fd, struct list *list){
	struct list_elem *e;
	struct fd_t *fd_t;
	struct fd *fd_num;

	if(list_empty(list))
		return NULL;
	
	for(e = list_begin(list); e != list_end(list); e = list_next(e))
	{
		fd_t = list_entry(e, struct fd_t, elem);
		fd_num = search_fd_single_list(fd, &fd_t->dup2_list);
		if(fd_num)	return fd_t;
	}
	return NULL;
}

/* insert file to fd_list and increase next_fd */
int insert_file2list(struct file *file, struct thread *thread){
	struct thread *t = thread;
	int fd;

	struct fd_t *fd_t = (struct fd_t *)palloc_get_page(0);
	if(fd_t == NULL)
		goto error;

	list_init(&fd_t->dup2_list);

	struct fd *fd_num = (struct fd *)palloc_get_page(0);
	if(fd_num == NULL)
		goto error;
	
	fd = t->next_fd++;

	fd_num->fd = fd;
	fd_t->file = file;

	list_push_back(&t->fd_list, &fd_t->elem);
	list_push_back(&fd_t->dup2_list, &fd_num->elem);
	return fd;

	error:
		if(fd_t) palloc_free_page(fd_t);
		if(fd_num) palloc_free_page(fd_num);
		return -1;
}

/* ----------------- Extra Credit -------------------------- */
/* Duplicate the file descriptor */
int sys_dup2(int oldfd, int newfd){
	struct thread *t = thread_current();
	struct fd_t *old_fd_t;
	struct fd_t *new_fd_t;
	struct fd_t *fd_t;
	struct fd *fd_num0;
	struct list *push_list;

	bool oldfd_exist = false;
	bool newfd_exist = false;

	/* check oldfd */
	if(!check_fd(oldfd)) return -1;

	struct list_elem *e;
	struct fd *fd_num;

	if(fd_num = search_fd_single_list(oldfd, &t->stdin_list)){
		oldfd_exist = true;
		push_list = &t->stdin_list;
		goto oldfd_valid;
	}

	if(fd_num = search_fd_single_list(oldfd, &t->stdout_list)){
		oldfd_exist = true;
		push_list = &t->stdout_list;
		goto oldfd_valid;
	}

	if(old_fd_t = search_fd_t_double_list(oldfd, &t->fd_list)){
		push_list = &old_fd_t->dup2_list;
		oldfd_exist = true;
	}

	oldfd_valid:
		if(!oldfd_exist) return -1;

	/* if oldfd and newfd have same value, then return newfd */
	if(oldfd == newfd) return newfd;

	/* if newfd is opened, then close it */
	if (fd_num = search_fd_single_list(newfd, &t->stdin_list)){
		newfd_exist = true;
		goto newfd_valid;
	}

	if (fd_num = search_fd_single_list(newfd, &t->stdout_list)){
		newfd_exist = true;
		goto newfd_valid;
	}

	if(new_fd_t = search_fd_t_double_list(oldfd, &t->fd_list))
		newfd_exist = true;
		
	newfd_valid:
		if(newfd_exist)
			sys_close(newfd);

	/* duplicate it */
	fd_num0 = palloc_get_page(0);
	if(fd_num0 == NULL) return -1;
	
	fd_num0->fd = newfd;
	list_push_back(push_list, &fd_num0->elem);

	/* if newfd is bigger than t->next_fd, then change it to newfd */
	if(newfd >= t->next_fd) t->next_fd = newfd + 1;
	return newfd;
}
/* Projects 2 and later. ----------------------------------- */                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       

/* Project 3 */
void *sys_mmap(void *addr, size_t length, int writable, int fd, off_t offset)
{
	
	struct thread *t = thread_current();
	struct fd_t *fd_t = search_fd_t_double_list(fd, &t->fd_list);

	/* handle error case */
	/* file_descriptors which is invalid, or console input and output should not mappable */
	if(!fd_t)	goto error;
	struct file *file = fd_t->file;

	if(length == 0)
		goto error;

	if(pg_ofs(addr) != 0)
		goto error;

	if(pg_ofs(offset) != 0)	/* offset should be aligned to PGSIZE, testcase: mmap-bad-off */
		goto error;

	if(addr == NULL)
		goto error;

	if(is_kernel_vaddr(addr) || is_kernel_vaddr(addr + length))
		goto error;
	
	/* include return NULL when some page in the middle is allocated already */
	return do_mmap(addr, length, writable, file, offset);

	error:
		return NULL;
}

void sys_munmap(void *addr){
	/* don't need to check address invalidity */
	lock_acquire(&filesys_lock);
	do_munmap(addr);
	lock_release(&filesys_lock);

}



/* check the virtual address validity which provided by user process */
static void check_user_memory(void *uaddr)
{
	bool is_valid = false;
	
	/* invalid pointer */	
	if(uaddr == NULL)
		goto done;

	/* a pointer into kernel virtual address space */
	if(is_kernel_vaddr(uaddr))
		goto done;

#ifdef VM
	struct page *page = spt_find_page(&thread_current()->spt, uaddr);
	if (is_stack_growth(uaddr, thread_current()->saving_rsp) && !page){
		is_valid = true;
		goto done;
	}

	if(!page)
		goto done;

#else
	/* a virtual address not mapped to physical address */
	if(pml4e_walk(thread_current()->pml4, uaddr, false) == NULL)
		goto done;
#endif

	is_valid = true;
	done:
		/* check whether terminates the user process */
		if(!is_valid)
			sys_exit(-1);

}

/* check whether virtual address is writable which provided by user process */
static void check_addr_writable(void *uaddr)
{
	bool is_valid = false;

	if (is_stack_growth(uaddr, thread_current()->saving_rsp)){
		is_valid = true;
		goto done;
	}
	
	struct page *page = spt_find_page(&thread_current()->spt, uaddr);
	if(!page)
		goto done;

	if(!page->writable)
		goto done;
	
	is_valid = true;
	done:
		if(!is_valid)
			sys_exit(-1);
}


static bool check_fd(int fd){
	struct thread *t = thread_current();
	if((fd < 0) || (fd >= t->next_fd)) return false;
	return true;
}

bool stdio_init(struct thread *t)
{
	struct fd *stdin = palloc_get_page(0);
	if(stdin == NULL)
		goto error;

	stdin->fd = STDIN_FILENO;
	list_push_back(&t->stdin_list, &stdin->elem);
		
	struct fd *stdout = palloc_get_page(0);
	if(stdout == NULL)
		goto error;

	stdout->fd = STDOUT_FILENO;
	list_push_back(&t->stdout_list, &stdout->elem);
	return true;

	error:
		if(stdin) palloc_free_page(stdin);
		if(stdout) palloc_free_page(stdout);
		return false;
}