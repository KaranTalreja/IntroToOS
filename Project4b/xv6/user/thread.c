#include "types.h"
#include "stat.h"
#include "user.h"
#include "x86.h"

#define PGSIZE (4096)
#define NPROC (64)

typedef struct thread {
  void *stack;
  int pid;
  int parent_pid;
  int in_use;
} thread_t;

thread_t g_threads[NPROC];

thread_t * alloc_thread()
{
  int i = 0;
  for (i = 0; i < NPROC; ++i) {
    if (!g_threads[i].in_use) {
      g_threads[i].in_use = 1;
      g_threads[i].parent_pid = getpid();
      return &g_threads[i];
    }
  }
  return NULL;
}

int thread_create(void (*start_routine) (void*), void *data)
{
  thread_t* curr_thread = alloc_thread();
  if (NULL == curr_thread) return -1;
  void* stack = (void*)malloc(PGSIZE);
  memset(stack, 0, PGSIZE);
  if (stack == NULL) {
    return -1;
  }
  curr_thread->stack = stack;
  curr_thread->pid = clone(start_routine, data, stack);
  return curr_thread->pid;
}

int thread_join()
{
  int retval = 0;
  void *stack;
  retval = join(&stack);
  free(stack);
  return retval;
}

void lock_init(lock_t* lock)
{
  lock->locked = 0;
}

void lock_acquire(lock_t* lock)
{
  while(xchg(&lock->locked, 1) != 0)
    ;
}

void lock_release(lock_t* lock)
{
  lock->locked = 0; 
}
