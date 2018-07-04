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
#include <vfs.h>
#include <kern/fcntl.h>

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
  p->exitcode = _MKWAIT_EXIT(exitcode);
  
  
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
  int exitstatus;
  int result;
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
  if (target_child->exitcode < 0) {
    //if the retrieved child has not exited, we wait for it
    lock_acquire(target_child->proc_lock);
    cv_wait(target_child->proc_cv, target_child->proc_lock);
    lock_release(target_child->proc_lock);
  }
  
  exitstatus = target_child->exitcode;
  *ret_val = target_child->exitcode;
  result = copyout((void *)&exitstatus, status, sizeof(int));
  
  if (result) {
    return(result);
  }
  proc_destroy(target_child);
  return 0;
  // /* for now, just pretend the exitstatus is 0 */
  // exitstatus = 0;
  // result = copyout((void *)&exitstatus,status,sizeof(int));
  
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




static void free_args(char **args, int len) {
  for (int i = 0; i <= len; ++i) {
    kfree(args[i]);
  }
}


int copyin_args(int arg_count, char ** kern_args, userptr_t *user_args, vaddr_t *stack_ptr) {
  //idea: write a loop that puts all the args(the strings themselves) onto the user stack at index x
  //      and at the same time, at each step after placing the arg onto the stack, we get the pointer
  //      that points to the arg on the stack and place the ptr on user stack at index (x+ size of all arg strings)
  //      size of all arg strings will be calculated at the beginning using a loop (which is shit but idgf)
  //      note: everything is in reverse order
  
  // //////////////
  // int result = 0;
  // vaddr_t stack = *stack_ptr;
  // vaddr_t split_location = stack;
  // char ** string_locations = kmalloc(arg_count * sizeof(char *));
  // for (int i = 0; i < arg_count; ++i) {
  //   split_location -= ROUNDUP(strlen(*(kern_args+i)) + 1, 8);
  //   result = copyoutstr(*(kern_args + arg_count - i - 1), (userptr_t)split_location, strlen(*(kern_args+i)) + 1, NULL);
  //   if (result) {
  //     kfree(string_locations);
  //     return result;
  //   }
    
  //   string_locations[i] = (char *)split_location;
  // }
  // //split_location -= sizeof(void *);
  
  // *(string_locations + arg_count) = NULL;
  // //set NULL
  // //copyout(NULL, (userptr_t)split_location, sizeof(void *));
  
  // for (int i = arg_count - 1; i > -1; --i) {
  //   int temp_len = strlen(kern_args[i]) + 1;
  //   //copyoutstr(kern_args[i], (userptr_t)(stack - ROUNDUP(temp_len, 8)), temp_len, NULL);
  //   split_location -= sizeof(void *);
  //   kprintf("shit %p\n", *(string_locations+i));
  //   result = copyout(string_locations + i, (userptr_t)split_location, sizeof(char *));
  //   if (result) {
  //     kfree(string_locations);
  //     return result;
  //   }
  //   stack -= ROUNDUP(temp_len, 8);
  // }
  // stack = split_location;
  // *user_args = (userptr_t)stack; //modify user_args to contain the actual cur stackptr
  // if (stack % 8 == 0) stack -= 8;
  // else stack -= 4;  //idk wtf this is, but its on hint page
  // //kprintf("shit: %p\n", user_args);
  // *stack_ptr = stack;  //update the stack ptr
  // kfree(string_locations);
  // kprintf("getting here %d\n", result);
  // return result;
  // ////////////////
  
  
  // THE above implementation is having weird memory errors too hard to debug.....
  // trying 2nd approach:
  //                have one for loop that adds the strings onto the stack in reverse order, at the same time
  //                store each corresponding address into an array. Add a NULL after adding all the strings onto
  //                the stack. Then have a second for loop that adds the array of addresses onto the stack.
  int result = 0;
  vaddr_t user_stack = *stack_ptr;
  char ** string_locations = kmalloc((arg_count + 1) * sizeof(char *));
  
  *(string_locations + arg_count) = NULL;  //in between all strinngs and all addresses
  
  for (int i = arg_count-1; i > -1; --i) {
    int arg_len = strlen(*(kern_args + (arg_count - i -1))) + 1;
    user_stack -= ROUNDUP(arg_len, 8);
    result = copyoutstr(*(kern_args + (arg_count - i - 1)), (userptr_t)user_stack, arg_len, NULL);
    if (result) {
      kfree(string_locations);
      return result;
    }
    *(string_locations + (arg_count - i -1)) = (char *)user_stack;
  }
  
  for (int j = arg_count; j > -1; --j) {
    user_stack -= sizeof(char *);
    result = copyout(string_locations + j, (userptr_t)user_stack, sizeof(char *));
    if (result) {
      kfree(string_locations);
      return result;
    }
  }
  
  *stack_ptr = user_stack;
  *user_args = (userptr_t)user_stack;   // this is the argv parameter
  kfree(string_locations);
  
  return result;
}
  
    
int sys_execv(const char *progname, userptr_t args2) {
  
	struct addrspace *new_as;
	struct addrspace *old_as = curproc_getas();
	int result;
  char **args = (char **)args2;
  
  //count the number of arguments
  int arg_count = 0;
  while(args[arg_count] != NULL) {
    if (strlen(args[arg_count]) > 1024) {
      return E2BIG;
    }
    arg_count++;
  }
  if (arg_count > 32) {
    return E2BIG;
  }

  //copy the arguments into the kernel
  char **kern_args = kmalloc((arg_count + 1) * sizeof(char *)); // malloc space for kernel args
  if (!kern_args) return ENOMEM;
  
  for (int i = 0; i < arg_count; ++i) {
    size_t arg_len = strlen(args[i]) + 1;
    kern_args[i] = kmalloc(sizeof(char *) * arg_len);
    if (!kern_args[i]) {
      free_args(kern_args, i);
      kfree(kern_args);
      return ENOMEM;
    }
    //kprintf("target: %s\n", args[i]);
    
    result = copyinstr((userptr_t)args[i], kern_args[i], arg_len, NULL);
    if (result) {
      free_args(kern_args, i);
      kfree(kern_args);
      return result;
    }
  }
  kern_args[arg_count] = NULL;
  
  // kprintf("I'm printing the array of args snake a\n");
  // for (int x = 0; x < arg_count; ++x) {
  //   kprintf("I'm printing the length of the args %d\n", strlen(kern_args[x]));
  //   kprintf("I'm printing the actual arg: %s; \n", kern_args[x]);
  // }
  
  
// Copy the program path into the kernel (program path is the parameter "program"

	// allocate space for program_path
	char *new_prog = kmalloc((strlen(progname) + 1) * sizeof(char));
	if (!new_prog) {
	   free_args(kern_args, arg_count - 1);
	   kfree(kern_args);
	   return ENOMEM;
 	}

	//copy the program path into the allocated kernel space
	result = copyinstr((const_userptr_t)progname, new_prog, strlen(progname) + 1, NULL);
	if (result) {
	  //if there is an error
	  free_args(kern_args, arg_count - 1);
	  kfree(kern_args);
	  kfree(new_prog);
	  return result;
	}
	
	struct vnode *v;
	vaddr_t entrypoint;
	//int result;

	/* Open the file. */
	result = vfs_open(new_prog, O_RDONLY, 0, &v);
	if (result) {
	  free_args(kern_args, arg_count - 1);
	  kfree(kern_args);
	  kfree(new_prog);
		return result;
	}

	/* We should be a new process. */
	//KASSERT(curproc_getas() == NULL);
	/* Create a new address space. */
	new_as = as_create();
	if (new_as == NULL) {
		vfs_close(v);
		free_args(kern_args, arg_count - 1);
	  kfree(kern_args);
	  kfree(new_prog);
		return ENOMEM;
	}

	/* Switch to the new address space and activate it. */
  // already made a backup of the old address_space at the begining of the function, when fail, set it back
	curproc_setas(new_as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		free_args(kern_args, arg_count - 1);
	  kfree(kern_args);
	  kfree(new_prog);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);


	/* Define the user STACK in the address space */
	vaddr_t user_stack;
	result = as_define_stack(new_as, &user_stack);   //this puts the newly defined user stack into the user address space
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		free_args(kern_args, arg_count - 1);
	  kfree(kern_args);
	  kfree(new_prog);
		return result;
	}
  
  //copy the arguments into the new address space -> copy the args onto the user stack
  userptr_t user_args;  // trying to give the args to user
  result = copyin_args(arg_count, kern_args, &user_args, &user_stack);
                                                                       //arg_count is the number of args trying to give to user   (this 	kprintf("printing user_args: %p\n", user_args); will be argc)
                                                                       //kern_args is an array of pointers pointing to strings(args) that are on the kernel heap
                                                                       //user_args will be an array of(pointer) pointers pointing to strings(args) that are on the user stack (this will be argv)
                                                                       //user_stack is a pointer pointing to the top of the user_stack
  //kprintf("my life is a lie: %d\n", result);
  if (result) {
    free_args(kern_args, arg_count - 1);
	  kfree(kern_args);
	  kfree(new_prog);
	  curproc_setas(old_as);//if failed -> need to set the current process to the old one
	  as_destroy(new_as);
		return result;
  }
  //destroy the stuff at the end
  as_destroy(old_as);
  kfree(new_prog);
  free_args(kern_args, arg_count - 1);
  kfree(kern_args);
	/* Warp to user mode. */
	enter_new_process(arg_count, user_args, user_stack, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}
