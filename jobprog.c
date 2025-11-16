/* jobprog.c
   Simple job program: runs for service_time seconds.
   Usage: ./jobprog <service_time>
   It relies on default signal behavior:
     SIGTSTP - stop (suspend)
     SIGCONT - continue (resume)
     SIGINT  - terminate
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <service_time>\n", argv[0]);
        return 1;
    }
    int service_time = atoi(argv[1]);
    if (service_time <= 0) return 0;

    /* Optionally print a startup message (comment out for "clean" runs) */
    printf("[job pid=%d] started, service_time=%d\n", getpid(), service_time);
    fflush(stdout);

    /* Run service_time seconds. We use sleep(1) each loop so a SIGTSTP can stop it. */
    for (int i = 0; i < service_time; ++i) {
        /* Simulate 1 second of CPU-bound work:
           If you want busy CPU instead of sleeping, replace sleep(1) with a busy loop.
           For the assignment, sleep(1) is fine and easier to control.
        */
        sleep(1);
        /* Optionally print progress (comment for quieter output) */
        /* printf("[job pid=%d] progress %d/%d\n", getpid(), i+1, service_time); fflush(stdout); */
    }

    /* Completed normally */
    printf("[job pid=%d] finished normally\n", getpid());
    fflush(stdout);
    return 0;
}





