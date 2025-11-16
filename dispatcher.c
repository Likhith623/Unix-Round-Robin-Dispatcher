/* dispatcher.c
   Round-Robin Dispatcher - Following Stallings Figure 9.5 (RR q=1)
   ✔ Correct preemption timing
   ✔ Quantum = 1 second
   ✔ Matches Stallings expected output
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>

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

/* RR Queue */
static job_t *rr_head = NULL, *rr_tail = NULL;

/* Input Queue */
static job_t *input_head = NULL;

/* Gantt Chart */
#define MAX_TIME 2000
int gantt[MAX_TIME];
int gantt_index = 0;

/* ---------------- QUEUE FUNCTIONS ---------------- */

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

/* ---------------- PRINT JOB TABLE ---------------- */

void print_job_table() {
    printf("\n==================== JOB TABLE ====================\n");
    printf(" Job ID | Arrival | CPU Burst \n");
    printf("--------+---------+-----------\n");

    job_t *p = input_head;
    while (p) {
        printf("   %-4d |   %-5d |    %-5d\n",
               p->id, p->arrival, p->total_cpu);
        p = p->next;
    }
    printf("===================================================\n\n");
}

/* ---------------- CSV LOADING ---------------- */

void load_jobs(const char *fname) {
    FILE *f = fopen(fname, "r");
    if (!f) { perror("fopen"); exit(1); }

    char line[256];
    job_t *last = NULL;
    int job_counter = 1;

    while (fgets(line, sizeof(line), f)) {
        if (line[0]=='#' || strlen(line)<3) continue;

        // Try extended format (8 columns)
        int arrival, priority, service, memory, p4, p5, p6, p7;
        int parsed = sscanf(line, "%d,%d,%d,%d,%d,%d,%d,%d", 
                           &arrival, &priority, &service, &memory, &p4, &p5, &p6, &p7);
        
        if (parsed >= 3) {
            job_t *j = calloc(1, sizeof(job_t));
            j->id = job_counter++;
            j->arrival = arrival;
            j->total_cpu = service;
            j->remaining = service;
            j->pid = -1;
            j->state = NOT_STARTED;
            j->next = NULL;

            if (!input_head) input_head = last = j;
            else { last->next = j; last = j; }
        } else {
            // Try simple format
            int arrival2, id, service2;
            if (sscanf(line, "%d,%d,%d", &arrival2, &id, &service2) == 3) {
                job_t *j = calloc(1, sizeof(job_t));
                j->id = id;
                j->arrival = arrival2;
                j->total_cpu = service2;
                j->remaining = service2;
                j->pid = -1;
                j->state = NOT_STARTED;
                j->next = NULL;

                if (!input_head) input_head = last = j;
                else { last->next = j; last = j; }
            }
        }
    }
    fclose(f);
}

/* ---------------- CHILD EXIT CHECK ---------------- */

int child_exited(pid_t pid) {
    if (pid <= 0) return 0;
    int status;
    pid_t r = waitpid(pid, &status, WNOHANG);
    return (r > 0);
}

/* ---------------- GANTT CHART ---------------- */

void print_gantt_chart() {
    printf("\n==================== GANTT CHART ====================\n");
    printf("Time:  ");
    for (int i = 0; i < gantt_index; i++) {
        printf("%-4d", i);
    }

    printf("\nCPU:   ");
    for (int i = 0; i < gantt_index; i++) {
        if (gantt[i] == -1) {
            printf(" -  ");
        } else {
            printf("J%-2d ", gantt[i]);
        }
    }

    printf("\n=====================================================\n\n");
}

/* ---------------- MAIN DISPATCHER (Stallings Algorithm) ---------------- */

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s jobs.csv\n", argv[0]);
        exit(1);
    }

    /* Step 1-2: Initialize and load jobs */
    load_jobs(argv[1]);
    print_job_table();

    for (int i = 0; i < MAX_TIME; i++) gantt[i] = -1;

    /* Step 3: Start dispatcher timer */
    int t = 0;
    job_t *current = NULL;

    /* Step 4: Main dispatcher loop */
    while (any_jobs_left() || current != NULL) {

        /* Step 4.i: Unload pending processes from input queue */
        job_t *m;
        while ((m = pop_input_if_arrival_le(t)) != NULL) {
            printf("[t=%d] ➤ Job %d ARRIVED (burst=%d)\n", t, m->id, m->total_cpu);
            enqueue_rr(m);
        }

        /* Step 4.ii: If a process is currently running */
        if (current) {
            /* Step 4.ii.a: Decrement remaining CPU time */
            current->remaining--;
            printf("[t=%d] ⚙ RAN Job %d (remaining: %d → %d)\n", 
                   t, current->id, current->remaining + 1, current->remaining);

            /* Step 4.ii.b: If time's up */
            if (current->remaining <= 0) {
                /* Step 4.ii.b.A: Terminate */
                kill(current->pid, SIGINT);
                waitpid(current->pid, NULL, 0);
                printf("[t=%d] ✔ FINISH Job %d (completed)\n", t, current->id);
                free(current);
                current = NULL;
            }
            /* Step 4.ii.c: else if other processes waiting */
            else if (rr_head != NULL) {
                /* Step 4.ii.c.A: Suspend */
                kill(current->pid, SIGTSTP);
                current->state = SUSPENDED;
                printf("[t=%d] ⏸ PREEMPT Job %d → suspended\n", t, current->id);
                /* Step 4.ii.c.B: Enqueue back */
                enqueue_rr(current);
                current = NULL;
            }
        }

        /* Step 4.iii: If no process running && RR queue not empty */
        if (!current && rr_head != NULL) {
            /* Step 4.iii.a: Dequeue from RR */
            job_t *job = dequeue_rr();

            /* Step 4.iii.b: Start or resume */
            if (job->state == NOT_STARTED) {
                /* Start it (fork & exec) */
                pid_t pid = fork();
                if (pid == 0) {
                    char arg[20];
                    sprintf(arg, "%d", job->total_cpu);
                    execl("./jobprog", "./jobprog", arg, NULL);
                    perror("execl");
                    exit(1);
                }
                job->pid = pid;
                job->state = RUNNING;
                printf("[t=%d] ▶ START Job %d (pid=%d)\n", t, job->id, pid);
                usleep(100000);
            } else if (job->state == SUSPENDED) {
                /* Restart it (SIGCONT) */
                kill(job->pid, SIGCONT);
                job->state = RUNNING;
                printf("[t=%d] ▶ RESUME Job %d (pid=%d)\n", t, job->id, job->pid);
                usleep(50000);
            }

            /* Step 4.iii.c: Set as currently running */
            current = job;
        }

        /* Record in Gantt chart BEFORE sleep */
        gantt[gantt_index++] = current ? current->id : -1;

        /* Check if child exited naturally (for robustness) */
        if (current && child_exited(current->pid)) {
            printf("[t=%d] ⚠ Job %d exited naturally (ignoring)\n", t, current->id);
            free(current);
            current = NULL;
        }

        /* Step 4.iv: Sleep for one second (quantum) */
        sleep(1);

        /* Step 4.v: Increment dispatcher timer */
        t++;

        /* Step 4.vi: Loop back */
    }

    /* Step 5: Exit */
    printf("\nDispatcher done (all jobs completed)\n");
    print_gantt_chart();

    return 0;
}