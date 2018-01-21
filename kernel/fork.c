#include "debug.h"
#include "kernel/task.h"
#include "fs/fdtable.h"
#include "kernel/calls.h"

#define CSIGNAL_ 0x000000ff
#define CLONE_VM_ 0x00000100
#define CLONE_FS_ 0x00000200
#define CLONE_FILES_ 0x00000400
#define CLONE_SIGHAND_ 0x00000800
#define CLONE_PTRACE_ 0x00002000
#define CLONE_VFORK_ 0x00004000
#define CLONE_PARENT_ 0x00008000
#define CLONE_THREAD_ 0x00010000
#define CLONE_NEWNS_ 0x00020000
#define CLONE_SYSVSEM_ 0x00040000
#define CLONE_SETTLS_ 0x00080000
#define CLONE_PARENT_SETTID_ 0x00100000
#define CLONE_CHILD_CLEARTID_ 0x00200000
#define CLONE_DETACHED_ 0x00400000
#define CLONE_UNTRACED_ 0x00800000
#define CLONE_CHILD_SETTID_ 0x01000000
#define CLONE_NEWCGROUP_ 0x02000000
#define CLONE_NEWUTS_ 0x04000000
#define CLONE_NEWIPC_ 0x08000000
#define CLONE_NEWUSER_ 0x10000000
#define CLONE_NEWPID_ 0x20000000
#define CLONE_NEWNET_ 0x40000000
#define CLONE_IO_ 0x80000000

static int copy_memory(struct task *task, int flags) {
    struct mem *mem = task->cpu.mem;
    if (flags & CLONE_VM_) {
        mem_retain(mem);
        return 0;
    }
    task->cpu.mem = mem_new();
    pt_copy_on_write(mem, 0, task->cpu.mem, 0, MEM_PAGES);
    return 0;
}

static int copy_files(struct task *task, int flags) {
    if (flags & CLONE_FILES_) {
        task->files->refcount++;
        return 0;
    }
    task->files = fdtable_copy(task->files);
    if (IS_ERR(task->files))
        return PTR_ERR(task->files);
    return 0;
}

static int copy_task(struct task *task, dword_t flags, addr_t ctid_addr) {
    int err = copy_memory(task, flags);
    if (err < 0)
        return err;
    err = copy_files(task, flags);
    if (err < 0)
        return err;

    task->cpu.eax = 0;
    if (flags & CLONE_CHILD_SETTID_)
        if (user_put_task(task, ctid_addr, task->pid))
            return _EFAULT;
    task_start(task);

    if (flags & CLONE_VFORK_) {
        // jeez why does every wait need a lock
        lock(&task->exit_lock);
        wait_for(&task->vfork_done, &task->exit_lock);
        unlock(&task->exit_lock);
    }

    return 0;
}

// eax = syscall number
// ebx = flags
// ecx = stack
// edx, esi, edi = unimplemented garbage
dword_t sys_clone(dword_t flags, addr_t stack, addr_t ptid, addr_t tls, addr_t ctid) {
    STRACE("clone(%x, 0x%x, blah, blah, blah)", flags, stack);
    if (ptid != 0 || tls != 0) {
        FIXME("clone with ptid or ts not null");
        return _EINVAL;
    }
    if ((flags & CSIGNAL_) != SIGCHLD_) {
        FIXME("clone non sigchld");
        return _EINVAL;
    }

    if (stack != 0)
        TODO("clone with nonzero stack");
        // stack = current->cpu.esp;

    struct task *task = task_create(current);
    if (task == NULL)
        return _ENOMEM;
    int err = copy_task(task, flags, ctid);
    if (err < 0) {
        task_destroy(task);
        return err;
    }
    return task->pid;
}

dword_t sys_fork() {
    return sys_clone(SIGCHLD_, 0, 0, 0, 0);
}

dword_t sys_vfork() {
    return sys_clone(CLONE_VFORK_ | CLONE_VM_ | SIGCHLD_, 0, 0, 0, 0);
}
