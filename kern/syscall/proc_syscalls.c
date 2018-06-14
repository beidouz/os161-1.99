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
#include <synch.h>

//using self define myarray for  children_pids, using a builtin C array for pmanager procs array!!!!!!


static volatile pid_t pid_counter = PID_MIN;

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {
  // im a child -> if parent is dead, kill myself
  //            -> if parent is not dead, delete everything else but keep pid,exitcode, parent and children
  // im a parent -> if child is dead, kill the child
  //             -> if child is not dead, tell him that im dying
  
  struct addrspace *as;
  struct proc *p = curproc;
  p->exitcode = exitcode;
  
  
  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);
  KASSERT(curproc->p_addrspace != NULL);
  
  lock_acquire(pmanager_lock);
  for (int i = 0; i < p->children_pids->len; ++i) {
    int temp_pid = p->children_pids->arr[i];

    //look up the child process in the pmanager array
    struct proc * cur_child = pmanager->procs[temp_pid];
    if (cur_child == NULL) {
      //child is already dead, do nothing and continue
      continue;
    } else {
      //if the child still exists, set parent reference to null cuz im killing myself
      cur_child->parent = NULL;
      if (cur_child->exitcode >= 0) {
        //the child has exited, just kill it all
        lock_release(pmanager_lock);   // release the pmanager_lock here because proc_destroy needs this lock
        proc_destroy(cur_child);
        lock_acquire(pmanager_lock);
      }
    }
  }
  lock_release(pmanager_lock);
  //no longer need this array of child PIDs
  myarray_delete(p->children_pids);
  
  
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

  threadarray_cleanup(&p->p_threads);
  spinlock_cleanup(&p->p_lock);
  
  lock_acquire(p->proc_lock);
  cv_broadcast(p->proc_cv, p->proc_lock);
  lock_release(p->proc_lock);
  
  //if parent is dead, or parent has exited or parent is the kernel i'll just kill myself :)
  if (!(p->parent) || p->parent->exitcode >= 0 || p->parent == kproc) {
    proc_destroy(p);
  }
  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  //proc_destroy(p);
  
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
sys_waitpid(pid_t pid, userptr_t status, int options, pid_t *ret_val) {
  // int exitstatus;
  // int result;
  (void)status;

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
  
  //check if the pid is valid -> between PID_MIN and PID_MAX
  if (pid < PID_MIN || pid > PID_MAX) {
   return ESRCH; //search error
  }
  //try to grab the child from the pmanager array using PID
  struct proc * target_child = pmanager->procs[pid];
  //check if the child retrieved is a valid child
  if (!(target_child)) return ECHILD; //not a child
  if (target_child->exitcode >= 0) {
    //if the retrieved child has already exited
    *ret_val = target_child->exitcode;
    return 0;
  }
  
  //when the child has not exited, we wait for it
  lock_acquire(target_child->proc_lock);
  cv_wait(target_child->proc_cv, target_child->proc_lock);
  lock_release(target_child->proc_lock);
  *ret_val = target_child->exitcode;
  proc_destroy(target_child);
  return 0;
  // /* for now, just pretend the exitstatus is 0 */
  // exitstatus = 0;
  // result = copyout((void *)&exitstatus,status,sizeof(int));
  // if (result) {
  //   return(result);
  // }
  // *retval = pid;
  // return(0);
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
  myarray_insert(curproc->children_pids, child_proc->pid);//add child pid to array of all children pids

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

