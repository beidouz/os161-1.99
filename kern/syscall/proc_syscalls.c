#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <limits.h>
#include <mips/trapframe.h>


static volatile pid_t pid_counter = PID_MIN;

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *ret_val)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  if (!curproc) return 1;
  *ret_val = curproc->pid;
  return 0;
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}


int sys_fork(struct trapframe *tf, pid_t *ret_val) {

  //CREATE process structure for child process
  struct proc *child_proc = proc_create_runprogram(curproc->p_name); // create the process structure
  //check if child process is created
  if (child_proc == NULL) return ENOMEM;
  
  //CREATE and copy address space (and data) from parent to child, give address space to new proc
  int as_copy_err = as_copy(curproc_getas(), &(child_proc->p_addrspace));
  //check if address space is copied successfully
  if(as_copy_err) {
    proc_destroy(child_proc);
  }
  
  //Assign PID to child process
  int temp_pid = generate_pid(child_proc);
  if (temp_pid == -1) {
    as_destroy(child_proc->p_addrspace);
    proc_destroy(child_proc);
    return ENPROC;
  }
  child_proc->pid = temp_pid;

  //CREATE the parent & child relationship
  child_proc->parent = curproc;
  array_add(curproc->children_pids, &(child_proc->pid), NULL); //add child pid to array of all children pids

  //CREATE trapframe
  struct trapframe *new_tf = kmalloc(sizeof(struct trapframe));
  if (new_tf == NULL) {
    as_destroy(child_proc->p_addrspace);
    proc_destroy(child_proc);
    return ENOMEM;
  }
  // memcpy(new_tf, tf, sizeof(struct trapframe));
  *new_tf = *tf;

  //CREATE thread for child process
  int thread_fork_err = thread_fork("child_proc", child_proc, enter_forked_process, new_tf, 0);
  if (thread_fork_err) {
    // as_destroy(child_proc->p_addrspace);
    // proc_destroy(child_proc);
    return thread_fork_err;
  }

  if (child_proc == curproc) {
    *ret_val = 0;
  } else {
    *ret_val = child_proc->pid;
  }

  return 0;
}

