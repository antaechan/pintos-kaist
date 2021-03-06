#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>
#include "threads/thread.h"
#include "threads/interrupt.h"
#include "filesys/file.h"

int insert_file2list(struct file *file, struct thread *thread);
int insert_dir2list(struct dir *dir, struct thread *curr);
struct fd *search_fd_single_list(int fd, struct list *list);
struct fd *search_fd_double_list(int fd, struct list *list);
struct fd_t *search_fd_t_double_list(int fd, struct list *list);
struct dir_desc *search_dir_list(int fd, struct list *list);

bool stdio_init(struct thread *t);

void syscall_init (void);

/* Projects 2 and later. ------------------------------------*/
void sys_halt (void);
void sys_exit (int status);
tid_t sys_fork (const char *thread_name, struct intr_frame *f);
int sys_exec (const char *cmdline);
int sys_wait (tid_t child_id);
bool sys_create (const char *file, unsigned initial_size);
bool sys_remove (const char *file);
int sys_open (const char *file);
int sys_filesize (int fd);
int sys_read (int fd, void *buffer, unsigned length);
int sys_write (int fd, const void *buffer, unsigned length);
void sys_seek (int fd, unsigned position);
unsigned sys_tell (int fd);
void sys_close (int fd);
bool sys_chdir (const char *dir);
bool sys_mkdir (const char *dir);
bool sys_readdir (int fd, char *name);
bool sys_isdir (int fd);
int sys_inumber (int fd);
	
/* Extra Credit */
int sys_dup2(int oldfd, int newfd);
/* Projects 2 and later. ------------------------------------*/

/* project 3 ------------------------------------------------*/
void *sys_mmap(void *addr, size_t length, int writable, int fd, off_t offset);
void sys_munmap(void *addr);


#endif /* userprog/syscall.h */
