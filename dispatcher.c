/* dispatcher.c
   Improved Round-Robin Dispatcher with:
   ✔ Clean event logs
   ✔ Job table at start
   ✔ CORRECTED Gantt chart at end
   ✔ Round Robin quantum = 1 sec
   ✔ Proper job ID support (not just 0-9)
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

/* Gantt Chart - Changed to int array to store full job IDs */
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

    while (fgets(line, sizeof(line), f)) {
        if (line[0]=='#' || strlen(line)<3) continue;

        int arrival, id, service;
        if (sscanf(line, "%d,%d,%d", &arrival, &id, &service) < 3)
            continue;

        job_t *j = calloc(1, sizeof(job_t));
        j->id = id;
        j->arrival = arrival;
        j->total_cpu = service;
        j->remaining = service;
        j->pid = -1;
        j->state = NOT_STARTED;
        j->next = NULL;

        if (!input_head) input_head = last = j;
        else { last->next = j; last = j; }
    }
    fclose(f);
}

/* ---------------- CHILD EXIT CHECK ---------------- */

int child_exited(pid_t pid) {
    if (pid <= 0) return 0;
    int status;
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == 0) return 0;
    return 1;
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

/* ---------------- MAIN DISPATCHER ---------------- */

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s jobs.csv\n", argv[0]);
        exit(1);
    }

    load_jobs(argv[1]);
    print_job_table();

    /* Initialize Gantt chart with -1 (idle) */
    for (int i = 0; i < MAX_TIME; i++) {
        gantt[i] = -1;
    }

    int t = 0;
    job_t *current = NULL;

    while (any_jobs_left() || current != NULL) {

        /* STEP 1: ARRIVALS - Move jobs from input queue to RR queue */
        job_t *m;
        while ((m = pop_input_if_arrival_le(t)) != NULL) {
            printf("[t=%d] ➤ Job %d ARRIVED (burst=%d)\n",
                   t, m->id, m->total_cpu);
            enqueue_rr(m);
        }

        /* STEP 2: START or RESUME a job if nothing is currently running */
        if (!current && rr_head != NULL) {
            job_t *job = dequeue_rr();

            if (job->state == NOT_STARTED) {
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
                current = job;
                printf("[t=%d] ▶ START Job %d (pid=%d)\n", t, job->id, pid);
                usleep(100000);

            } else if (job->state == SUSPENDED) {
                kill(job->pid, SIGCONT);
                job->state = RUNNING;
                current = job;
                printf("[t=%d] ▶ RESUME Job %d (pid=%d)\n", t, job->id, job->pid);
                usleep(50000);
            }
        }

        /* STEP 3: GANTT UPDATE - Record what will run during this quantum
           CRITICAL: This must happen BEFORE the job runs, not after! */
        if (current)
            gantt[gantt_index] = current->id;
        else
            gantt[gantt_index] = -1;
        gantt_index++;

        /* STEP 4: RUN the current job for 1 quantum */
        if (current) {
            /* Let the job run for 1 second (the quantum) */
            sleep(1);
            
            /* After running, check if child exited naturally during execution */
            if (child_exited(current->pid)) {
                printf("[t=%d] ✔ FINISH Job %d (exited naturally)\n", t, current->id);
                current = NULL;
            } else {
                printf("[t=%d] ⚙ RAN Job %d (remaining: %d → %d)\n",
                       t, current->id, current->remaining, current->remaining - 1);
                current->remaining--;

                /* Check if it's done or needs preemption */
                if (current->remaining <= 0) {
                    kill(current->pid, SIGINT);
                    waitpid(current->pid, NULL, 0);
                    printf("[t=%d] ✔ FINISH Job %d (completed)\n", t, current->id);
                    current = NULL;
                } else if (rr_head != NULL) {
                    /* Quantum expired and other jobs waiting - preempt */
                    kill(current->pid, SIGTSTP);
                    current->state = SUSPENDED;
                    enqueue_rr(current);
                    printf("[t=%d] ⏸ PREEMPT Job %d → suspended\n", t, current->id);
                    current = NULL;
                }
                /* else: no other jobs waiting, let it continue next quantum */
            }
        } else {
            /* No job running - idle time */
            sleep(1);
        }

        t++;
    }

    printf("\nDispatcher done (all jobs completed)\n");
    print_gantt_chart();

    return 0;
}