/* jobprog.c - Child process that handles signals properly
   This version ensures the child doesn't exit early and respects dispatcher control
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

volatile sig_atomic_t keep_running = 1;

void sigint_handler(int signo) {
    /* SIGINT = terminate */
    keep_running = 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <service_time>\n", argv[0]);
        exit(1);
    }
    
    int service_time = atoi(argv[1]);
    pid_t pid = getpid();
    
    /* Set up signal handler for SIGINT */
    signal(SIGINT, sigint_handler);
    
    /* SIGTSTP and SIGCONT use default handlers (stop and continue) */
    
    printf("[job pid=%d] started, service_time=%d\n", pid, service_time);
    fflush(stdout);
    
    /* Run indefinitely until dispatcher sends SIGINT
       The dispatcher tracks remaining time, not us */
    while (keep_running) {
        sleep(1);  /* Sleep in small chunks so signals can interrupt */
    }
    
    printf("[job pid=%d] terminating\n", pid);
    fflush(stdout);
    
    return 0;
}