#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

// mlfq simulator addition
#include "mlfq_simulator/mlfqtrace.h"

struct {
    struct spinlock lock;
    struct proc proc[NPROC];
} ptable;

static struct proc* initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void* chan);

// MLFQ Simulator functions
void mlfq_trace_init(void);
void mlfq_ev(int, struct proc*, int);

void
pinit(void)
{
    initlock(&ptable.lock, "ptable");
    mlfq_trace_init(); // mlfq simulator initialization
}

// Must be called with interrupts disabled
int
cpuid() {
    return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
    mycpu(void)
{
    int apicid, i;

    if (readeflags() & FL_IF)
        panic("mycpu called with interrupts enabled\n");

    apicid = lapicid();
    // APIC IDs are not guaranteed to be contiguous. Maybe we should have
    // a reverse map, or reserve a register to store &cpus[i].
    for (i = 0; i < ncpu; ++i) {
        if (cpus[i].apicid == apicid)
            return &cpus[i];
    }
    panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
    myproc(void) {
    struct cpu* c;
    struct proc* p;
    pushcli();
    c = mycpu();
    p = c->proc;
    popcli();
    return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
    struct proc* p;
    char* sp;

    acquire(&ptable.lock);

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        if (p->state == UNUSED)
            goto found;

    release(&ptable.lock);
    return 0;

found:
    p->state = EMBRYO;
    p->pid = nextpid++;
    p->priority = 0; // initializing to queue Q0
    p->ticks = 0;
    p->total_ticks = 0;
    p->cpu_ticks = 0;
    p->io_until = 0;

    release(&ptable.lock);

    // Allocate kernel stack.
    if ((p->kstack = kalloc()) == 0) {
        p->state = UNUSED;
        return 0;
    }
    sp = p->kstack + KSTACKSIZE;

    // Leave room for trap frame.
    sp -= sizeof * p->tf;
    p->tf = (struct trapframe*)sp;

    // Set up new context to start executing at forkret,
    // which returns to trapret.
    sp -= 4;
    *(uint*)sp = (uint)trapret;

    sp -= sizeof * p->context;
    p->context = (struct context*)sp;
    memset(p->context, 0, sizeof * p->context);
    p->context->eip = (uint)forkret;

    return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
    struct proc* p;
    extern char _binary_initcode_start[], _binary_initcode_size[];

    p = allocproc();

    initproc = p;
    if ((p->pgdir = setupkvm()) == 0)
        panic("userinit: out of memory?");
    inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
    p->sz = PGSIZE;
    memset(p->tf, 0, sizeof(*p->tf));
    p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
    p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
    p->tf->es = p->tf->ds;
    p->tf->ss = p->tf->ds;
    p->tf->eflags = FL_IF;
    p->tf->esp = PGSIZE;
    p->tf->eip = 0;  // beginning of initcode.S

    safestrcpy(p->name, "initcode", sizeof(p->name));
    p->cwd = namei("/");

    // this assignment to p->state lets other cores
    // run this process. the acquire forces the above
    // writes to be visible, and the lock is also needed
    // because the assignment might not be atomic.
    acquire(&ptable.lock);

    p->state = RUNNABLE;

    release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
    uint sz;
    struct proc* curproc = myproc();

    sz = curproc->sz;
    if (n > 0) {
        if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
            return -1;
    }
    else if (n < 0) {
        if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
            return -1;
    }
    curproc->sz = sz;
    switchuvm(curproc);
    return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
    int i, pid;
    struct proc* np;
    struct proc* curproc = myproc();

    // Allocate process.
    if ((np = allocproc()) == 0) {
        return -1;
    }

    // Copy process state from proc.
    if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0) {
        kfree(np->kstack);
        np->kstack = 0;
        np->state = UNUSED;
        return -1;
    }
    np->sz = curproc->sz;
    np->parent = curproc;
    *np->tf = *curproc->tf;

    // Clear %eax so that fork returns 0 in the child.
    np->tf->eax = 0;

    for (i = 0; i < NOFILE; i++)
        if (curproc->ofile[i])
            np->ofile[i] = filedup(curproc->ofile[i]);
    np->cwd = idup(curproc->cwd);

    safestrcpy(np->name, curproc->name, sizeof(curproc->name));

    pid = np->pid;

    acquire(&ptable.lock);

    np->state = RUNNABLE;

    release(&ptable.lock);

    return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
    struct proc* curproc = myproc();
    struct proc* p;
    int fd;

    if (curproc == initproc)
        panic("init exiting");

    // Close all open files.
    for (fd = 0; fd < NOFILE; fd++) {
        if (curproc->ofile[fd]) {
            fileclose(curproc->ofile[fd]);
            curproc->ofile[fd] = 0;
        }
    }

    begin_op();
    iput(curproc->cwd);
    end_op();
    curproc->cwd = 0;

    acquire(&ptable.lock);

    // Parent might be sleeping in wait().
    wakeup1(curproc->parent);

    // Pass abandoned children to init.
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->parent == curproc) {
            p->parent = initproc;
            if (p->state == ZOMBIE)
                wakeup1(initproc);
        }
    }

    // Jump into the scheduler, never to return.
    curproc->state = ZOMBIE;
    sched();
    panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
    struct proc* p;
    int havekids, pid;
    struct proc* curproc = myproc();

    acquire(&ptable.lock);
    for (;;) {
        // Scan through table looking for exited children.
        havekids = 0;
        for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
            if (p->parent != curproc)
                continue;
            havekids = 1;
            if (p->state == ZOMBIE) {
                // Found one.
                pid = p->pid;
                kfree(p->kstack);
                p->kstack = 0;
                freevm(p->pgdir);
                p->pid = 0;
                p->parent = 0;
                p->name[0] = 0;
                p->killed = 0;
                p->state = UNUSED;
                release(&ptable.lock);
                return pid;
            }
        }

        // No point waiting if we don't have any children.
        if (!havekids || curproc->killed) {
            release(&ptable.lock);
            return -1;
        }

        // Wait for children to exit.  (See wakeup1 call in proc_exit.)
        sleep(curproc, &ptable.lock);  //DOC: wait-sleep
    }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
    struct proc* p;
    struct cpu* c = mycpu();
    c->proc = 0;

    // Tracking round-robin starting position for each queue level
    // to implement RR behaviour
    static struct proc* last_proc[4] = { 0, 0, 0, 0 };

    for (;;) {
        // Enable interrupts on this processor.
        sti();

        // Loop over process table looking for process to run.
        acquire(&ptable.lock);

        int found = 0;
        for (int q = 0; q < 4; ++q) {
            if (found == 1) break;

            // so now we see the last process, and continue execution from there, if not available we continue searching from table
            struct proc* start = last_proc[q] ? last_proc[q] + 1 : ptable.proc;

            // so we search from last position to end 
            for (p = start; p < &ptable.proc[NPROC]; p++) {
                if (p->state == RUNNABLE && p->priority == q) {
                    found = 1;
                    last_proc[q] = p;
                    goto run_proc; // so now we schedule this process
                }
            }

            // in case we didnt find any process in the end, it is possible they are in the starting. so search there
            for (p = ptable.proc; p < start && p < &ptable.proc[NPROC]; p++) {
                if (p->state == RUNNABLE && p->priority == q) {
                    found = 1;
                    last_proc[q] = p;
                    goto run_proc;
                }
            }

        run_proc:
            if (found) {
                // Switch to chosen process.  It is the process's job
             // to release ptable.lock and then reacquire it
             // before jumping back to us.
                c->proc = p;
                found = 1;
                switchuvm(p);
                p->state = RUNNING;
                mlfq_ev(EV_RUN, p, p->priority); // MLFQ event capturing for simulator
                swtch(&(c->scheduler), p->context);
                switchkvm();

                // Process is done running for now.
                // It should have changed its p->state before coming back.
                c->proc = 0;
                break;
            }
        }
        release(&ptable.lock);
    }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
    int intena;
    struct proc* p = myproc();

    if (!holding(&ptable.lock))
        panic("sched ptable.lock");
    if (mycpu()->ncli != 1)
        panic("sched locks");
    if (p->state == RUNNING)
        panic("sched running");
    if (readeflags() & FL_IF)
        panic("sched interruptible");
    intena = mycpu()->intena;
    p->ticks = 0;

    // mlfq simulator
    if (p->state == RUNNABLE)
        mlfq_ev(EV_YIELD, p, p->priority);
    else if (p->state == SLEEPING)
        mlfq_ev(EV_SLEEP, p, p->priority);
    else if (p->state == ZOMBIE)
        mlfq_ev(EV_EXIT, p, p->priority);
    swtch(&p->context, mycpu()->scheduler);
    mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
    acquire(&ptable.lock);  //DOC: yieldlock
    myproc()->state = RUNNABLE;
    sched();
    release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
    static int first = 1;
    // Still holding ptable.lock from scheduler.
    release(&ptable.lock);

    if (first) {
        // Some initialization functions must be run in the context
        // of a regular process (e.g., they call sleep), and thus cannot
        // be run from main().
        first = 0;
        iinit(ROOTDEV);
        initlog(ROOTDEV);
    }

    // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void* chan, struct spinlock* lk)
{
    struct proc* p = myproc();

    if (p == 0)
        panic("sleep");

    if (lk == 0)
        panic("sleep without lk");

    // Must acquire ptable.lock in order to
    // change p->state and then call sched.
    // Once we hold ptable.lock, we can be
    // guaranteed that we won't miss any wakeup
    // (wakeup runs with ptable.lock locked),
    // so it's okay to release lk.
    if (lk != &ptable.lock) {  //DOC: sleeplock0
        acquire(&ptable.lock);  //DOC: sleeplock1
        release(lk);
    }
    // Go to sleep.
    p->chan = chan;
    p->state = SLEEPING;

    sched();

    // Tidy up.
    p->chan = 0;

    // Reacquire original lock.
    if (lk != &ptable.lock) {  //DOC: sleeplock2
        release(&ptable.lock);
        acquire(lk);
    }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void* chan)
{
    struct proc* p;

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->state == SLEEPING && p->chan == chan) {
            p->state = RUNNABLE;
            mlfq_ev(EV_WAKE, p, p->priority);
        }
    }
}
// Wake up all processes sleeping on chan.
void
wakeup(void* chan)
{
    acquire(&ptable.lock);
    wakeup1(chan);
    release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
    struct proc* p;

    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->pid == pid) {
            p->killed = 1;
            // Wake process from sleep if necessary.
            if (p->state == SLEEPING)
                p->state = RUNNABLE;
            release(&ptable.lock);
            return 0;
        }
    }
    release(&ptable.lock);
    return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
    static char* states[] = {
    [UNUSED] "unused",
    [EMBRYO]    "embryo",
    [SLEEPING]  "sleep ",
    [RUNNABLE]  "runble",
    [RUNNING]   "run   ",
    [ZOMBIE]    "zombie"
    };
    int i;
    struct proc* p;
    char* state;
    uint pc[10];

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->state == UNUSED)
            continue;
        if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
            state = states[p->state];
        else
            state = "???";
        cprintf("%d %s %s", p->pid, state, p->name);
        if (p->state == SLEEPING) {
            getcallerpcs((uint*)p->context->ebp + 2, pc);
            for (i = 0; i < 10 && pc[i] != 0; i++)
                cprintf(" %p", pc[i]);
        }
        cprintf("\n");
    }
}


//-----------------------------MLFQ Helper Function --------------------------------------//

int quantum_for_priority(int q) {
    if (q == 0) {
        return 6;
    }
    else if (q == 1) {
        return 10;
    }
    else if (q == 2) {
        return 16;
    }
    else {
        return 24;
    }
}

int allotment_for_priority(int q) {
    // so let cumulative limit be *5 
    // (can change if want)
    return quantum_for_priority(q) * 5;
}

void account_tick(struct proc* p) {
    p->ticks++;
    p->total_ticks++;
    p->cpu_ticks++;
}

int
demote_on_limit(struct proc* p)
{
    if (p->priority != 3 &&
        p->total_ticks >= allotment_for_priority(p->priority)) {
        p->priority++;
        p->ticks = 0;
        p->total_ticks = 0;

        mlfq_ev(EV_DEMOTE, p, p->priority);
        return 1;
    }
    return 0;
}

int quantum_expired(struct proc* p) {
    // if the time quanta expired, signal
    if (p->ticks >= quantum_for_priority(p->priority)) {
        return 1;
    }
    return 0;
}

int
priority_boost(void)
{
    struct proc* p;
    int moved = 0;

    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; ++p) {
        if (p->state == UNUSED || p->state == ZOMBIE)
            continue;
        if (p->priority != 0) {
            moved++;
            if (!mlfq_tracing()) {
                cprintf("[MLFQ] BOOST: pid %d (%s) -> Q0\n",
                        p->pid, p->name);
            }
        }
        p->priority = 0;
        p->ticks = 0;
        p->total_ticks = 0;
    }
    mlfq_ev(EV_BOOST, 0, moved);
    release(&ptable.lock);
    return moved;
}

// Adding getprio to kernel
int
getprio(int pid)
{
    struct proc* p;
    int r = -1;

    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->pid == pid && p->state != UNUSED) {
            r = p->priority;
            break;
        }
    }
    release(&ptable.lock);

    return r;
}

int
get_cpu_ticks(void)
{
    struct proc *p = myproc();

    if (p == 0)
        return -1;

    return p->cpu_ticks;
}

//----------------------------- MLFQ Trace Recorder -----------------------------

#define EVN 1024

static struct mlfqev evbuf[EVN];
static uint evhead;
static uint evtail;
static int evon;
static struct spinlock evlock;

void
mlfq_trace_init(void)
{
    initlock(&evlock, "mlfqev");
}

int
mlfq_tracing(void)
{
    int on;

    acquire(&evlock);
    on = evon;
    release(&evlock);

    return on;
}

void
mlfq_ev(int type, struct proc* p, int q)
{
    acquire(&evlock);

    if (evon) {
        if (evhead - evtail == EVN)
            evtail++;

        evbuf[evhead % EVN].tick = ticks;
        evbuf[evhead % EVN].type = type;
        evbuf[evhead % EVN].pid = p ? p->pid : 0;
        evbuf[evhead % EVN].q = q;

        evhead++;
    }

    release(&evlock);
}

void
mlfq_trace_set(int on)
{
    acquire(&evlock);

    evon = on;

    if (on)
        evhead = evtail = 0;

    release(&evlock);
}

int
mlfq_drain(struct mlfqev* out, int max)
{
    int n = 0;

    if (max <= 0)
        return -1;

    acquire(&evlock);

    while (evtail != evhead && n < max) {
        out[n++] = evbuf[evtail % EVN];
        evtail++;
    }

    release(&evlock);

    return n;
}

int
mlfq_iowait(int n)
{
    struct proc *p = myproc();

    if (p == 0 || n <= 0)
        return -1;

    acquire(&ptable.lock);

    p->io_until = ticks + n;

    sleep(p, &ptable.lock);

    release(&ptable.lock);

    return 0;
}

void
iowake(void)
{
    struct proc *p;

    acquire(&ptable.lock);

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->state == SLEEPING &&
            p->io_until != 0 &&
            ticks >= p->io_until) {

            p->io_until = 0;
            p->state = RUNNABLE;

            mlfq_ev(EV_WAKE, p, p->priority);
        }
    }

    release(&ptable.lock);
}