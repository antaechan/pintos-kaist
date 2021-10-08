#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>
#include "threads/thread.h"

struct file *convert_fd2file(int fd, struct thread *thread);
int insert_file2list(struct file *file, struct thread *thread);
void delete_file2list(int fd, struct thread *thread);

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



/* Extra Credit */
int sys_dup2(int oldfd, int newfd);
/* Projects 2 and later. ------------------------------------*/

#endif /* userprog/syscall.h */
