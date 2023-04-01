# JobsManager

For years the performance of processors has increased steadily, and games and other programs have reaped the benefits of this increasing power without having
to do anything special.

The rules have changed. The performance of single processor cores is now increasing very slowly, if at all. However, the computing power available in a typical computer or console continues to grow. The difference is that most of this performance gain now comes from having multiple processor cores in a single machine, often in a single chip.

The increases in available processing power are just as dramatic as in the past, but now developers have to write multi-threaded code in order to use this power.

## Core idea

The two core concepts are:

- Decompose processing task as "jobs" that are performed by "Worker" threads.
- Avoid delegating synchronization to the operating system: Do it yourself with atomic operations.

## Building blocks

The system relies on three items:

- Jobs
- Workers
- Synchronization

A "Job" is exactly what one would expect:

```cpp
struct job_t
{
    void (*jobRun_t)(void *data);
    void *data;
};
```

A "Worker" is a thread that will remain idle waiting for a signal. When it is awoken it tries to find jobs. The workers try to avoid synchronization by using atomic operations while trying to fetch a job from a JobList.

"Synchronization" is performed via three primitives: Signals, Mutexes and Atomic operations. The latter are favored since they allow the engine to retain CPU focus. 

## Architecture

The brain of that SubSystem is the JobsManager. It is responsible for spawning the workers threads and creating queues where Jobs are stored.

That is the first way synchronization is avoided: Divide the engine job posting system into multiple sections that are accessed by one thread only and therefore require no synchronization. In the engine, queues are called JobsList.

### Jobs consumption

A "Worker" runs continuously and tries to "find a job". This process requires no mutexes or monitors: An atomically incremented integer distribute jobs with no overlaps.

### Usage

Since jobs are segregated into sections accessed by only one thread, there is no synchronization required for adding a job. However, submitting a job to the worker system does involve a mutex. Here is a example where the renderer tries to find which lights are generating interactions :

```cpp
    //tr.frontEndJobList is a JobsList

    for ( viewLight_t * vLight = tr.viewDef->viewLights; vLight != NULL; vLight = vLight->next )
    {
        tr.frontEndJobList->AddJob( (jobRun_t)R_AddSingleLight, vLight );
    }
    
    tr.frontEndJobList->Submit();
    tr.frontEndJobList->Wait();
```
 
Three parts:

1. *AddJob* : No synchronization necessary, job is added to a vector.
2. *Submit* : Mutex synchonization, each worker threads add the JobList to their own local ringer buffer list of JobLists.
3. *Wait*   : Signal synchonization (delegated to OS). Let the Worker threads complete.

### How a "Worker" works

Workers are infinite loops. Each iteration the loop check if more JobList have been added to the ring buffer and if so copies the reference to the local stack.

Local stack : The thread stack is used to store JobLists addresses as an anti-stalling mechanism. If a thread fails to "lock" a JobList, it falls in RUN_STALLED mode. This stall can be recovered from by navigating the local stack JobLists list in order to find an other jobList to visit. This way, "Yielding" is avoided.

The interesting part is that everything is is done with no Mutexes mechanisms: Only atomic operations are used.

Note: Avoiding Mutexes is pushed far: Sometimes they are not used even though they should have been for "correctness". Example: The copy from heap to stack uses lastJobList and firstJobList with no mutex. This means that the copy can omit a JobList added concurrently on the ring buffer. It is wrong but it is OK: The worker will just stall and wait for a signal when the ring buffer operation is completed.

