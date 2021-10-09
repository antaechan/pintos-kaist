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

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

static void check_user_memory(void *uaddr);


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
			break;

		case SYS_EXIT:                   
			sys_exit(arg1);
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
	/* 1. store exit_stat  */
	cur->exit_stat = status;

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
	// file_deny_write....?
	lock_release(&filesys_lock);

	return fd;
}

/* Obtain a file's size. */
int sys_filesize (int fd){
	struct file *file;
	int length;

	lock_acquire(&filesys_lock);
	file = convert_fd2file(fd, thread_current());
	if(file == NULL){
		lock_release(&filesys_lock);
		return -1;
	}
	length = file_length(file);
	lock_release(&filesys_lock);
	
	return length;
}

/* Read from a file. */
int sys_read (int fd, void *buffer, unsigned length){
	
	struct file *file;
	char *ptr = (char *)buffer;
	int cnt;

	check_user_memory(ptr);
	check_user_memory(ptr + length - 1);

	lock_acquire(&filesys_lock);

	switch(fd)
	{
		case STDIN_FILENO:
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
			break;

		default:
			file = convert_fd2file(fd, thread_current());
			if(file == NULL){
				cnt = -1;
				break;
			}
				
			cnt = file_read(file, buffer, length);
			break;
	}

	lock_release(&filesys_lock);
	return cnt;
}

/* Write to a file. */
int sys_write (int fd, const void *buffer, unsigned length){
	
	struct file *file;
	int cnt = 0;

	check_user_memory(buffer);
	check_user_memory(buffer + length - 1);

	lock_acquire(&filesys_lock);
	switch(fd){
		case STDOUT_FILENO:
			putbuf((const char *)buffer, length);
			cnt = length;
			break;
		
		default:
			file = convert_fd2file(fd, thread_current());
			if(file == NULL){
				cnt = -1;
				break;
			}
			cnt = file_write(fd, buffer, length);
			break;
	}

	lock_release(&filesys_lock);
	return cnt;
}

/* Change position in a file. */
void sys_seek (int fd, unsigned position){

	struct file* file;

	lock_acquire(&filesys_lock);
	file = convert_fd2file(fd, thread_current());
	if(file != NULL) file_seek(file, position);
	lock_release(&filesys_lock);

}

/* Report current position in a file. */
unsigned sys_tell (int fd){
	unsigned position;
	struct file* file;
	
	lock_acquire(&filesys_lock);
	file = convert_fd2file(fd, thread_current());
	if(file != NULL){
		position = file_tell(file);
	}
	else{
		position = -1;
	}

	lock_release(&filesys_lock);
	return position;
}

/* Close a file. */
void sys_close (int fd){
	
	struct file *file;
	lock_acquire(&filesys_lock);
	
	file = convert_fd2file(fd, thread_current());
	if (file == NULL)
	{
		lock_release(&filesys_lock);
		return;
	}
		
	file_close(file);
	// delete file to list
	delete_file2list(fd, thread_current());
	lock_release(&filesys_lock);
}

/* return opened file which has file descriptor fd,
 if not exist, then return NULL */
struct file *convert_fd2file(int fd, struct thread *thread){
	struct thread *t = thread;
	struct list_elem *e;
	struct fd_t *fd_t;

	if((fd <= 2) || (fd >= t->next_fd)) return NULL;

	for(e = list_begin(&t->fd_list); e != list_end(&t->fd_list); e = list_next(e))
	{
		fd_t = list_entry(e, struct fd_t, elem);
		if(fd_t->fd == fd) return fd_t->file;
	}
	return NULL;
}


/* convert fd to fd_t with fd_list*/
struct fd_t *convert_fd2fd_t(int fd, struct thread *thread){
	struct thread *t = thread;
	struct list_elem *e;
	struct fd_t *fd_t;

	if(fd < 2 || fd >= t->next_fd) return NULL;

	for(e=list_begin(&t->fd_list); e!=list_end(&t->fd_list); e=list_next(e)){
		fd_t = list_entry(e, struct fd_t, elem);
		if(fd_t->fd == fd) return fd_t;
	}
}

/* insert file to fd_list and increase next_fd */
int insert_file2list(struct file *file, struct thread *thread){
	struct thread *t = thread;
	int fd;

	struct fd_t *fd_t = (struct fd_t *)palloc_get_page(PAL_USER);
	if(fd_t == NULL)
		return -1;
	
	fd = t->next_fd++;
	fd_t->fd = fd;
	fd_t->file = file;
	list_push_back(&t->fd_list, &fd_t->elem);
	return fd;
}

/* delete file to fd_list and decrease next_fd */
void delete_file2list(int fd, struct thread *thread){
	struct thread *t = thread;
	struct list_elem *e;
	struct fd_t *fd_t;

	for(e = list_begin(&t->fd_list); e != list_end(&t->fd_list); e = list_next(e))
	{
		fd_t = list_entry(e, struct fd_t, elem);
		if(fd_t->fd == fd)
		{
			list_remove(e);
			palloc_free_page(fd_t);
			break;
		}
	}

	return;
}

/* compare file f1(with fd1) with f2(with fd2), and return true if same, otherwise return false */
bool is_same_file(int fd1, int fd2){
	struct file *f1 = convert_fd2file(fd1, thread_current());
	struct file *f2 = convert_fd2file(fd2, thread_current());
	bool inode = (&f1->inode == &f2->inode);
	bool pos = (f1->pos == f2->pos);
	bool deny_write = (f1->deny_write == f2->deny_write);

	return inode & pos & deny_write;
}


/* ----------------- Extra Credit -------------------------- */
/* Duplicate the file descriptor */
int sys_dup2(int oldfd, int newfd){
	struct thread *t = thread_current();
	struct file *old_file;
	// struct file *new_file;
	// struct fd_t *old_fd_t;
	struct fd_t *new_fd_t;
	struct fd_t *fd_t;

	/* check oldfd */
	if(oldfd < 2 || oldfd >= t->next_fd) return -1;

	old_file = convert_fd2file(oldfd, thread_current());
	if(old_file == NULL) return -1;

	/* if oldfd and newfd have same value, then return newfd */
	if((oldfd == newfd) && is_same_file(oldfd, newfd)) return newfd;

	/* if newfd is opened, then close it */
	new_fd_t = convert_fd2fd_t(newfd, thread_current());
	if(new_fd_t != NULL) sys_close(newfd);

	/* if newfd is bigger than t->next_fd, then change it to newfd */
	if(newfd > t->next_fd) t->next_fd = newfd;
	else delete_file2list(newfd, thread_current());

	/* duplicate it */
	fd_t = (struct fd_t*)malloc(sizeof(struct fd_t*));
	fd_t->fd = newfd;
	fd_t->file = file_duplicate(old_file);
	list_push_back(&t->fd_list, &fd_t->elem);

	return newfd;
}
/* Projects 2 and later. ----------------------------------- */
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            

/* check the virtual address validity which provided by user process */
static void check_user_memory(void *uaddr)
{
	bool is_valid = true;
	
	/* invalid pointer */	
	if(uaddr == NULL)
		is_valid = false;

	/* a pointer into kernel virtual address space */
	else if(is_kernel_vaddr(uaddr))
		is_valid = false;

	/* //a virtual address not mapped to physical address
	else if(pml4e_walk(thread_current()->pml4, uaddr, false) == NULL)
		is_valid = false; */
	
	/* handle these cases by terminating the user process 1*/
	if(!is_valid)
		sys_exit(-1);
	
	return;
}