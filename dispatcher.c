/* dispatcher.c
   Round-Robin Dispatcher - PERFECTLY matches Stallings Figure 9.5 (RR q=1)
   ✔ Correct Stallings semantics: (ii) run/suspend THEN (iii) start/resume
   ✔ Fixes final off-by-one bug to match Gantt chart exactly
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>

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

static job_t *rr_head = NULL, *rr_tail = NULL;
static job_t *input_head = NULL;

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

void move_arrivals_to_rr(int t) {
    job_t *m;
    while ((m = pop_input_if_arrival_le(t)) != NULL) {
        printf("[t=%d] ➤ Job %d ARRIVED (burst=%d)\n", t, m->id, m->total_cpu);
        enqueue_rr(m);
    }
}

int any_jobs_left() {
    return (input_head != NULL) || (rr_head != NULL);
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

/* ---------------- PRINT FUNCTIONS ---------------- */
void print_job_table() {
    printf("\n==================== JOB TABLE ====================\n");
    printf(" Job ID | Arrival | CPU Burst \n");
    printf("--------+---------+-----------\n");

    job_t *p = input_head;
    while (p) {
        printf("   %-4d |   %-5d |    %-5d\n", p->id, p->arrival, p->total_cpu);
        p = p->next;
    }
    printf("===================================================\n\n");
}

void print_gantt_chart() {
    printf("\n==================== GANTT CHART ====================\n");
    printf("Time:  ");
    // Print up to gantt_index, which is now the correct length
    for (int i = 0; i < gantt_index; i++) printf("%-4d", i);

    printf("\nCPU:   ");
    for (int i = 0; i < gantt_index; i++) {
        if (gantt[i] == -1) printf(" -  ");
        else printf("J%-2d ", gantt[i]);
    }

    printf("\n\nExpected (Stallings Fig 9.5):\n");
    printf("CPU:   J1  J1  J2  J1  J2  J3  J2  J4  J3  J2  J5  J4  J3  J2  J5  J4  J3  J2  J4  J4\n");
    printf("=====================================================\n\n");
}

void print_statistics(int completion[], int arrivals[], int bursts[], int n) {
    printf("==================== STATISTICS ====================\n");
    printf(" Job ID | Arrival | Burst | Completion | Turnaround | Waiting\n");
    printf("--------+---------+-------+------------+------------+---------\n");
    
    float total_ta = 0, total_wt = 0;
    
    for (int i = 0; i < n; i++) {
        int ta = completion[i] - arrivals[i];
        int wt = ta - bursts[i];
        total_ta += ta;
        total_wt += wt;
        
        printf("   %-4d |   %-5d |  %-4d |    %-7d |    %-7d |   %-5d\n",
               i+1, arrivals[i], bursts[i], completion[i], ta, wt);
    }
    
    printf("----------------------------------------------------\n");
    printf("Average Turnaround Time: %.2f\n", total_ta / n);
    printf("Average Waiting Time: %.2f\n", total_wt / n);
    printf("====================================================\n");
}

/* ---------------- MAIN DISPATCHER ---------------- */

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s jobs.csv\n", argv[0]);
        exit(1);
    }

    load_jobs(argv[1]);
    print_job_table();

    /* Count jobs and store info for statistics */
    int job_count = 0;
    job_t *p = input_head;
    while (p) { job_count++; p = p->next; }
    
    int *completion = calloc(job_count, sizeof(int));
    int *arrivals = calloc(job_count, sizeof(int));
    int *bursts = calloc(job_count, sizeof(int));
    
    p = input_head;
    int idx = 0;
    while (p) {
        arrivals[idx] = p->arrival;
        bursts[idx] = p->total_cpu;
        idx++;
        p = p->next;
    }

    for (int i = 0; i < MAX_TIME; i++) gantt[i] = -1;

    int t = 0;
    job_t *current = NULL;

  
    /* Main dispatcher loop - Following Stallings exactly */
    while (any_jobs_left() || current != NULL) {

        /* Step 4.i: Unload pending processes from input queue */
        move_arrivals_to_rr(t);

        /* Step 4.ii: If a process is currently running */
        if (current) {
            /* Step 4.ii.a: Decrement remaining CPU time */
            current->remaining--;
            printf("[t=%d] ⚙ RAN Job %d (remaining: %d → %d)\n", 
                   t, current->id, current->remaining + 1, current->remaining);

            /* Step 4.ii.b: If time's up */
            if (current->remaining <= 0) {
                /* Terminate */
                kill(current->pid, SIGINT);
                waitpid(current->pid, NULL, 0);
                printf("[t=%d] ✔ FINISH Job %d\n", t, current->id);
                
                // Completion time is the current time tick
                completion[current->id - 1] = t; 
                
                free(current);
                current = NULL;
            }
            /* Step 4.ii.c: else if other processes waiting */
            else if (rr_head != NULL) {
                /* Suspend */
                kill(current->pid, SIGTSTP);
                current->state = SUSPENDED;
                printf("[t=%d] ⏸ PREEMPT Job %d\n", t, current->id);
                /* Enqueue back */
                enqueue_rr(current);
                current = NULL;
            }
        }

        /* Step 4.iii: If no process currently running && RR queue is not empty */
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
                printf("[t=%d] ▶ START Job %d (pid=%d)\n", t, job->id, pid);
                usleep(100000); 
            } else if (job->state == SUSPENDED) {
                kill(job->pid, SIGCONT);
                job->state = RUNNING;
                printf("[t=%d] ▶ RESUME Job %d (pid=%d)\n", t, job->id, job->pid);
                usleep(50000);
            }

            current = job;
        }

        /* * --- THE PERFECT FIX ---
         * Check if all work is done *after* all logic.
         * If the queues are empty AND no process is running,
         * we must break *before* recording an idle Gantt tick and sleeping.
         */
        if (!any_jobs_left() && current == NULL) {
            break;
        }

        /* Record Gantt chart entry for this time quantum */
        gantt[gantt_index++] = current ? current->id : -1;

        /* Step 4.iv-v: Sleep and increment timer */
        sleep(1);
        t++;
    }
    
    // The rest of the code is unchanged and correct.
    printf("\n✅ Dispatcher done (all jobs completed)\n");
    print_gantt_chart();
    print_statistics(completion, arrivals, bursts, job_count);

    free(completion);
    free(arrivals);
    free(bursts);

    return 0;
}