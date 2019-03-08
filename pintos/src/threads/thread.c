#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif


/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

fixed_point_t load_avg;

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

void thread_get_lock (struct lock *);
void thread_rm_lock (struct lock *);
void thread_update_priority (struct thread *);
bool priority_less(const struct list_elem *e1, const struct list_elem *e2, void *aux);

void increace_recent_cpu_by1 (void);
void refresh_load_avg (void);
void refresh_recent_cpu (void);
void refresh_priority_MLFQS (struct thread *t);

/* Addition */

/* List of sleeping threads */
static struct list sleep_list;

/* Check if threads have to be wakened for all threads in sleep_list */
void thread_sleep_foreach (int64_t ticks);

/* Comparator to sort sleep_list depending on wakeTick, uses list_insert_ordered */
bool thread_sleeper_less(const struct list_elem *e1, const struct list_elem *e2, void *aux UNUSED);






/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void)
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);

  /* Modify: init sleep_list */
  list_init (&sleep_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();

  initial_thread->nice = NICE_DEFAUT;
  initial_thread->recent_cpu = fix_int(0);
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void)
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* initialize load average */
  load_avg = fix_int(0);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void)
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void)
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux)
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* set wake_tick = -1 to distinguish from init progress when wake_tick = -2 */
  t->wake_tick = -1;

  /* Add to run queue. */
  thread_unblock (t);

  thread_yield();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void)
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  enum intr_level old_level = intr_disable ();
  /* Insert thread into sleep_list with thread_sleeper_more, because we want to put small elements in the front */
  list_insert_ordered (&sleep_list, &thread_current()->sleep_elem,
                       thread_sleeper_less, NULL);
  intr_set_level (old_level);
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t)
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_remove (&t->sleep_elem);
  list_push_back (&ready_list, &t->elem);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void)
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void)
{
  struct thread *t = running_thread ();

  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void)
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void)
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void)
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;

  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread)
    list_push_back (&ready_list, &cur->elem);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority)
{
  if (thread_mlfqs) {
    return;
  }

  enum intr_level old_level = intr_disable();
  struct thread *current_thread = thread_current();
  int old_priority = current_thread->priority_effective;
  current_thread->priority_ori = new_priority;

  if (new_priority > old_priority || list_empty(&current_thread->locks)) {
    current_thread->priority_effective = new_priority;
    thread_yield();
  }

  intr_set_level(old_level);
}

/* Returns the current thread's priority. */
int
thread_get_priority (void)
{
  return thread_current ()->priority_effective;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED)
{
  /* Not yet implemented. */
  if (nice > NICE_MAX) {
    nice = NICE_MAX;
  }
  if (nice < NICE_MIN) {
    nice = NICE_MIN;
  }
  thread_current ()->nice = nice;
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void)
{
  /* Not yet implemented. */
  return thread_current ()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void)
{
  /* Not yet implemented. */
  return fix_round(fix_mul(load_avg, fix_int(100)));
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void)
{
  /* Not yet implemented. */
  return fix_round(fix_mul(thread_current () ->recent_cpu, fix_int(100)));
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED)
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;)
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux)
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void)
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  old_level = intr_disable ();

  /* Add new variables */
  t->wake_tick = -2;        /* initialize wake_tick of each thread as -2 while in init process*/
  t->priority_ori = priority;
  list_init(&t->locks);
  t->waiting_lock = NULL;

  /* add it to sleep_list */
  list_insert_ordered (&sleep_list, &t->sleep_elem,
                       thread_sleeper_less, NULL);
  intr_set_level (old_level);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  if (!thread_mlfqs) {
    t->priority_effective = priority;
  }
  t->magic = THREAD_MAGIC;

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size)
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void)
{
  if (list_empty (&ready_list)) {
    return idle_thread;
  }
  struct list_elem *thread_elem = list_max (&ready_list, priority_less, NULL);
  enum intr_level old_level = intr_disable ();
  list_remove(thread_elem);
  intr_set_level (old_level);
  return list_entry (thread_elem, struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();

  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread)
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void)
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}



/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void)
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);



/* tast 1 implementation goes here */

/* Apply func for all threads in sleep_list */
void thread_sleep_foreach (int64_t ticks)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&sleep_list); e != list_end (&sleep_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, sleep_elem);
      if (t->status == THREAD_BLOCKED && t->wake_tick <= ticks && t->wake_tick >= -1)
      {
        thread_unblock(t);
      } else if (t->wake_tick > ticks) {
        return;
      }
    }
}

/* Comparator to sort sleep_list depending on wakeTick, uses list_insert_ordered */
bool thread_sleeper_less(const struct list_elem *e1, const struct list_elem *e2, void *aux UNUSED) {
  return list_entry(e1,struct thread, sleep_elem)->wake_tick < list_entry(e2, struct thread, sleep_elem)->wake_tick;
}



/* task 2 implementation goes here */

/* Thread gets a lock */
void
thread_get_lock(struct lock *lock)
{
   enum intr_level old_level = intr_disable ();
   struct thread *current_thread = thread_current();
   list_push_back (&current_thread->locks, &lock->elem);
   if (lock->max_priority > current_thread->priority_effective)
   {
     current_thread->priority_effective = lock->max_priority;
     thread_yield ();
   }

   intr_set_level (old_level);
}

/* Thread removes a lock. */
void
thread_rm_lock (struct lock *lock)
{
  enum intr_level old_level = intr_disable ();
  list_remove (&lock->elem);
  thread_update_priority (thread_current ());
  intr_set_level (old_level);
}

/* Update priority, merged with thread_donate_priority. */
void
thread_update_priority (struct thread *t)
{
  enum intr_level old_level = intr_disable ();
  int max_priority = t->priority_ori;
  int lock_priority;

  if (!list_empty (&t->locks))
  {
    lock_priority = list_entry (list_max (&t->locks, lock_priority_less, NULL), struct lock, elem)->max_priority;
    if (lock_priority > max_priority)
      max_priority = lock_priority;
  }

  t->priority_effective = max_priority;
  intr_set_level (old_level);
}



bool priority_less(const struct list_elem *e1, const struct list_elem *e2, void *aux UNUSED) {
  return list_entry(e1,struct thread, elem)->priority_effective < list_entry(e2, struct thread, elem)->priority_effective;
}


/* task 3 implementation goes here */

void
increace_recent_cpu_by1 (void)
{
  ASSERT(thread_mlfqs);
  if (thread_current () != idle_thread) {
    thread_current ()->recent_cpu = fix_add(thread_current ()->recent_cpu, fix_int(1));
  }
}

void
refresh_load_avg (void)
{
  ASSERT(thread_mlfqs);
  int num_ready_threads = list_size(&ready_list);
  if (thread_current () != idle_thread) {
    num_ready_threads += 1;
  }

  load_avg = fix_add(fix_mul(fix_div(fix_int(59), fix_int(60)), load_avg), \
      fix_unscale(fix_int(num_ready_threads), 60));
}

void
refresh_recent_cpu (void) {
  ASSERT(thread_mlfqs);
  struct list_elem * ele;
  fixed_point_t factor = fix_div (fix_scale (load_avg, 2), \
    fix_add(fix_scale(load_avg, 2), fix_int(1)));

  for (ele = list_begin (&all_list); ele != list_end (&all_list); ele = list_next(ele)) {
    struct thread  *t_entry = list_entry(ele, struct thread, allelem);
    if (t_entry != idle_thread) {
      t_entry->recent_cpu = fix_add (fix_mul (factor, t_entry->recent_cpu), fix_int (t_entry->nice));
    }
  }
}

void
refresh_priority_MLFQS (struct thread *t) {

  /* To save computation, we only update all threads' priority iff multiple of a second. */
  ASSERT(thread_mlfqs);
  if (t != idle_thread) {
    /* truncated priority */
    fixed_point_t subtracted = fix_add(fix_unscale(t->recent_cpu, 4), fix_int(t->nice * 2));
    int pri = fix_trunc(fix_sub(fix_int(PRI_MAX), subtracted));
    if (pri > PRI_MAX) {
      pri = PRI_MAX;
    }
    if (pri < PRI_MIN) {
      pri = PRI_MIN;
    }
    t->priority_effective = pri;
    thread_yield ();
  }
}
