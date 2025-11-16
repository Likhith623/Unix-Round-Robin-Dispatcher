/* dispatcher.c
   Real Round-Robin dispatcher (quantum = 1s).
   Usage: ./dispatcher jobs.csv
   jobs.csv lines: arrival,id,service
   Example:
     0,1,5
     2,2,3
     4,3,2
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <time.h>

typedef enum { NOT_STARTED, RUNNING, SUSPENDED, TERMINATED } state_t;
typedef struct job {
    int id;
    int arrival;
    int total_cpu;
    int remaining;
    pid_t pid;
    state_t state;
    struct job *next;
} job_t;

/* Simple queue implementation for RR */
static job_t *rr_head = NULL, *rr_tail = NULL;
static job_t *input_head = NULL; // sorted by arrival ascending

void enqueue_rr(job_t *j) {
    j->next = NULL;
    if (!rr_tail) rr_head = rr_tail = j;
    else { rr_tail->next = j; rr_tail = j; }
}

job_t *dequeue_rr() {
    job_t *j = rr_head;
    if (!j) return NULL;
    rr_head = rr_head->next;
    if (!rr_head) rr_tail = NULL;
    j->next = NULL;
    return j;
}

/* helper: pop input if arrival <= t (input list sorted ascending) */
job_t *pop_input_if_arrival_le(int t) {
    if (!input_head) return NULL;
    if (input_head->arrival <= t) {
        job_t *j = input_head;
        input_head = input_head->next;
        j->next = NULL;
        return j;
    }
    return NULL;
}

int any_jobs_left() {
    return (input_head != NULL) || (rr_head != NULL);
}

/* Parse job list file. Expect CSV: arrival,id,service */
void load_jobs(const char *fname) {
    FILE *f = fopen(fname, "r");
    if (!f) { perror("fopen"); exit(1); }
    char line[256];
    job_t *last = NULL;
    while (fgets(line, sizeof(line), f)) {
        if (line[0]=='#' || strlen(line)<3) continue;
        int arrival = 0, id = 0, service = 0;
        // tolerant parsing: allow spaces and optional commas
        if (sscanf(line, "%d,%d,%d", &arrival, &id, &service) < 3) continue;
        job_t *j = calloc(1, sizeof(job_t));
        if (!j) { perror("calloc"); exit(1); }
        j->id = id;
        j->arrival = arrival;
        j->total_cpu = service;
        j->remaining = service;
        j->pid = 0; j->state = NOT_STARTED; j->next = NULL;
        if (!input_head) input_head = last = j;
        else { last->next = j; last = j; }
    }
    fclose(f);
}

/* reap any finished children (non-blocking). Returns number reaped. */
int reap_children_nonblock() {
    int reaped = 0;
    int status;
    pid_t r;
    while ((r = waitpid(-1, &status, WNOHANG)) > 0) {
        reaped++;
        // find the job with this pid and mark terminated
        job_t *it = NULL;
        // search rr queue and input queue (unlikely in input). Also check current pointer in main.
        // For simplicity, just print here; main loop also calls waitpid for current child when needed.
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            printf("[reap] child pid=%d exited (status=%d)\n", r, status);
        }
    }
    return reaped;
}

/* Check if a specific child pid has exited (non-blocking). Returns 1 if exited. */
int child_exited(pid_t pid) {
    if (pid <= 0) return 0;
    int status;
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == 0) return 0; // still running
    if (r == -1) {
        // error (ECHILD if no such child)
        return 1;
    }
    // r == pid
    if (WIFEXITED(status) || WIFSIGNALED(status)) return 1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s jobs.csv\n", argv[0]); exit(1); }
    load_jobs(argv[1]);
    int dispatcher_timer = 0;
    job_t *current = NULL;

    /* Main dispatcher loop */
    while ( any_jobs_left() || current != NULL ) {
        /* 1. move arrivals to RR queue */
        job_t *m;
        while ((m = pop_input_if_arrival_le(dispatcher_timer)) != NULL) {
            enqueue_rr(m);
            printf("[t=%d] Job %d arrived (cpu=%d)\n", dispatcher_timer, m->id, m->remaining);
        }

        /* reap any unexpectedly finished children (rare) */
        reap_children_nonblock();

        /* 2. If current running, decrement remaining and handle */
        if (current) {
            /* if child already exited (race or finished earlier), handle */
            if (child_exited(current->pid)) {
                printf("[t=%d] Job %d finished (child exited)\n", dispatcher_timer, current->id);
                current->state = TERMINATED;
                current = NULL;
            } else {
                /* Let it run for this quantum (we assume it is already running) */
                printf("[t=%d] Running Job %d (pid=%d rem=%d)\n", dispatcher_timer, current->id, (int)current->pid, current->remaining);
                current->remaining -= 1;

                if (current->remaining <= 0) {
                    /* job completed: tell child to terminate and wait for it */
                    if (kill(current->pid, SIGINT) == -1) {
                        if (errno != ESRCH) perror("kill(SIGINT)");
                    } else {
                        // give it a small moment to die cleanly
                    }
                    int st;
                    waitpid(current->pid, &st, 0);
                    printf("[t=%d] Job %d terminated by dispatcher (completed)\n", dispatcher_timer, current->id);
                    current->state = TERMINATED;
                    current = NULL;
                } else {
                    /* If there are other waiting jobs, preempt current */
                    if (rr_head != NULL) {
                        if (kill(current->pid, SIGTSTP) == -1) {
                            if (errno != ESRCH) perror("kill(SIGTSTP)");
                        } else {
                            current->state = SUSPENDED;
                            enqueue_rr(current);
                            printf("[t=%d] Job %d suspended and requeued (rem=%d)\n", dispatcher_timer, current->id, current->remaining);
                            current = NULL;
                        }
                    } else {
                        /* no other jobs waiting: let it continue next tick */
                        printf("[t=%d] Job %d continues (no other waiting) rem=%d\n", dispatcher_timer, current->id, current->remaining);
                    }
                }
            }
        }

        /* 3. If nothing running and rr queue not empty -> start/resume */
        if (!current && rr_head != NULL) {
            job_t *job = dequeue_rr();
            if (!job) {
                // nothing to do
            } else if (job->state == NOT_STARTED) {
                /* start new process: fork + exec the job program (jobprog) */
                pid_t pid = fork();
                if (pid < 0) {
                    perror("fork");
                    // put job back
                    enqueue_rr(job);
                } else if (pid == 0) {
                    /* Child: execute the worker program.
                       Arguments: service_time (integer)
                       We use execl with argv[1] = service_time string.
                    */
                    char arg1[32];
                    sprintf(arg1, "%d", job->total_cpu);
                    /* Replace the child with jobprog executable */
                    execl("./jobprog", "./jobprog", arg1, (char*)NULL);
                    /* If execl fails */
                    perror("execl");
                    _exit(1);
                } else {
                    /* Parent: record pid and consider current */
                    job->pid = pid;
                    job->state = RUNNING;
                    current = job;
                    printf("[t=%d] Started Job %d pid=%d\n", dispatcher_timer, job->id, (int)pid);
                    /* Small sleep to reduce race where parent sends signal too early */
                    usleep(100000); // 100ms
                }
            } else if (job->state == SUSPENDED) {
                if (kill(job->pid, SIGCONT) == -1) {
                    if (errno == ESRCH) {
                        // child may have exited meanwhile; mark terminated
                        job->state = TERMINATED;
                        printf("[t=%d] Job %d missing on resume (already exited)\n", dispatcher_timer, job->id);
                        free(job);
                        job = NULL;
                    } else {
                        perror("kill(SIGCONT)");
                        enqueue_rr(job);
                    }
                } else {
                    job->state = RUNNING;
                    current = job;
                    printf("[t=%d] Resumed Job %d pid=%d\n", dispatcher_timer, job->id, (int)job->pid);
                    /* allow child to get scheduled */
                    usleep(50000);
                }
            } else {
                // shouldn't happen
            }
        }

        /* 4. Sleep for one second (quantum) */
        sleep(1);
        dispatcher_timer += 1;
    }

    printf("Dispatcher done (all jobs completed)\n");
    return 0;
}
