#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/fixed-point.h"

#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */

	/* implement alarm_clock */
	int64_t wakeup_ticks;				/* tick to wake up */

	/* implement advanced scheduler */
	struct list_elem allelem;			/* use to traverse all threads */

	int nice;
	fixed_point	recent_cpu;
	
	/* implement priority donation */
	int original_priority;
	struct list donor_list;
	struct list_elem donor_elem;
	struct lock *wait_for_what_lock;

	struct list locks;

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */

	int next_fd;						/* next fd to insert */
	struct list fd_list;				/* list of fd*/
	struct file *running_file;			/* file that runs currently */
	
	struct process_data_bank *data_bank;	/* process important information store */
	struct list child_list;

	/* Implement dup2 part: Extended File Descirptor(Extra) -----------------------*/
	struct list stdin_list;
	struct list stdout_list;
	/* Implement dup2 part: Extended File Descirptor(Extra) -----------------------*/

#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;

	uintptr_t saving_rsp;
#endif
#ifdef EFILESYS
	struct dir *cwd;					/* current working directory */
	struct list dir_list;				/* hold directory_desc as element */
#endif
	
	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* fd of thread */
struct fd_t{
	struct file *file;
	struct list dup2_list;
	struct list_elem elem;
};

struct fd{
	int fd;
	struct list_elem elem;
};

struct dir_desc
{
	int fd;
	struct dir *dir;
	struct list_elem elem;
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

void thread_sleep_until(int64_t wakeup_ticks);
void thread_wakeup(int64_t ticks);
bool thread_wakeup_judge(int64_t ticks);
bool thread_wakeup_ticks_less(const struct list_elem* a,
							  const struct list_elem* b,
							  void * aux UNUSED);

bool thread_priority_more(const struct list_elem *a,
						  const struct list_elem *b,
						  void *aux UNUSED);
void max_priority_compare(void);

void thread_increment_recent_cpu(void);
void thread_calculate_load_avg(void);
void thread_recalculate_recent_cpu(void);
void thread_recalculate_priority(void);

//void priority_donate(struct thread *thread);
void priority_donate(struct thread *thread);

void priority_update(struct thread *thread);
void update_donor_lock(struct lock *lock);

#endif /* threads/thread.h */
