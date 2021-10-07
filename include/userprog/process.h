#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"

struct process_data_bank {
    
    tid_t tid;
    int exit_stat;
    char *cmdline;

    /* information to use in fork */
	struct thread *parent;
	struct intr_frame *parent_if;

    /* porcess state information mark */
	bool init_mark;
	bool fork_succ;
	bool exit_mark;
	bool wait_mark;
    bool orphan;

	/* synchronization */
    struct semaphore sema_init;
    struct semaphore sema_fork;
    struct semaphore sema_wait;

    struct list_elem elem;
};


tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

#endif /* userprog/process.h */
