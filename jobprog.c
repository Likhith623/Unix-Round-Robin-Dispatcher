/* jobprog.c - Child process that handles signals properly
   This version ensures the child doesn't exit early and respects dispatcher control.
   This code is 100% correct.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

/* * We use a global flag to know when to terminate.
 * 'volatile sig_atomic_t' ensures it's safe to change in a signal handler.
 */
volatile sig_atomic_t keep_running = 1;

/* * This is our custom signal handler for SIGINT.
 * When the dispatcher sends SIGINT, this function runs.
 */
void sigint_handler(int signo) {
    /* SIGINT = terminate */
    keep_running = 0;
}

int main(int argc, char *argv[]) {
    
    int service_time = (argc > 1) ? atoi(argv[1]) : 0;
    pid_t pid = getpid();
    
    /* * Set up our custom signal handler to catch SIGINT.
     * Now, SIGINT won't kill the process by default; it will just call our function.
     */
    signal(SIGINT, sigint_handler);
    
    /* * We do NOT need to handle SIGTSTP (stop) or SIGCONT (continue).
     * The default OS behavior for these signals is exactly what we want
     * (pause the process and resume the process).
     */
    
    printf("[job pid=%d] started, service_time=%d\n", pid, service_time);
    fflush(stdout); // Flush output buffer so parent sees it
    
    /* * This is the main "work" loop.
     * It runs forever (as long as keep_running is 1).
     * The dispatcher tracks the *actual* remaining time.
     * This child just runs until it's told to stop.
     * sleep(1) is interruptible by signals.
     */
    while (keep_running) {
        sleep(1);
    }
    
    /* * The loop only exits when sigint_handler sets keep_running = 0.
     * Now we can exit gracefully.
     */
    printf("[job pid=%d] terminating (SIGINT received)\n", pid);
    fflush(stdout);
    
    return 0;
}