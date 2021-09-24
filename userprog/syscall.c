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

void
syscall_init (void) {
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
}
/* Delete a file. */
bool sys_remove (const char *file)
{
	check_user_memory(file);
}
/* Open a file. */
int sys_open (const char *file)
{
	check_user_memory(file);
}
/* Obtain a file's size. */
int sys_filesize (int fd);

/* Read from a file. */
int sys_read (int fd, void *buffer, unsigned length){
	
	// void *ptr = buffer;
	// /* for all buffer bytes, check user memory */
	// while(){
	// 	check_user_memory(ptr);

	// }
	
}
/* Write to a file. */
int sys_write (int fd, const void *buffer, unsigned length){
	// void *ptr = buffer;
	// /* for all buffer bytes, check user memory */
	// while(){
	// 	check_user_memory(ptr);

	// }
}

/* Change position in a file. */
void sys_seek (int fd, unsigned position);

/* Report current position in a file. */
unsigned sys_tell (int fd);

/* Close a file. */
void sys_close (int fd);

/* ----------------- Extra Credit -------------------------- */
/* Duplicate the file descriptor */
int sys_dup2(int oldfd, int newfd);
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

	/* a virtual address not mapped to physical address */
	else if(pml4e_walk(thread_current()->pml4, uaddr, false) == NULL)
		is_valid = false;
	
	/* handle these cases by terminating the user process 1*/
	if(!is_valid)
		sys_exit(-1);
	
	return;
}