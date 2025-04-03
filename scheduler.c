#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>

//Ece Nil Kombak 820220330 24.03.2025

#define MAX_JOBS 100

typedef struct {
    char name[50];
    int arrival;
    int priority;
    int exec_time;
    int remaining_time;
    pid_t pid;
    int forked;
    int finished;
    int order;     
    int preempted; 
} Job;

Job jobs[MAX_JOBS];
int job_count = 0;
int time_slice;

FILE *log_file;


void capitalize(char *dest, const char *src) {
    strcpy(dest, src);
    if (dest[0] != '\0')
        dest[0] = toupper(dest[0]);
}

//logging function for printing events with a timestamp
void log_event(const char *format, ...) {
    char buffer[256];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestr[64];
    strftime(timestr, sizeof(timestr), "[%Y-%m-%d %H:%M:%S]", t);

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    fprintf(log_file, "%s [INFO] %s\n", timestr, buffer);
    fflush(log_file);
}


int all_jobs_finished() {
    for (int i = 0; i < job_count; i++) {
        if (!jobs[i].finished)
            return 0;
    }
    return 1;
}

//job simulation function for when the program is run with "job" argument
void run_job(const char *jobName) {
    printf("Job %s started (PID: %d)\n", jobName, getpid());
    while (1) {
        sleep(1);
    }
}

int main(int argc, char *argv[]) {
    //if given "job" then run in job mode
    if (argc >= 3 && strcmp(argv[1], "job") == 0) {
        run_job(argv[2]);
        return 0; 
    }

    //scheduler mode
    FILE *fp = fopen("jobs.txt", "r");
    if (!fp) {
        perror("Error opening jobs.txt");
        exit(EXIT_FAILURE);
    }

    
    char keyword[20];
    if (fscanf(fp, "%s %d", keyword, &time_slice) != 2) {
        fprintf(stderr, "Error reading time slice from jobs.txt\n");
        exit(EXIT_FAILURE);
    }

    //read each job from jobs.txt
    while (fscanf(fp, "%s %d %d %d", jobs[job_count].name,
                  &jobs[job_count].arrival,
                  &jobs[job_count].priority,
                  &jobs[job_count].exec_time) == 4) {
        jobs[job_count].remaining_time = jobs[job_count].exec_time;
        jobs[job_count].forked = 0;
        jobs[job_count].finished = 0;
        jobs[job_count].preempted = 0;
        jobs[job_count].order = job_count;  
        job_count++;
    }
    fclose(fp);

    //open the log file for writing
    log_file = fopen("scheduler.log", "w");
    if (!log_file) {
        perror("Error opening scheduler.log");
        exit(EXIT_FAILURE);
    }

    int current_time = 0;
    int current_job_index = -1; 

    //main scheduling loop
    while (!all_jobs_finished()) {
        int ready_indices[MAX_JOBS];
        int ready_count = 0;
        for (int i = 0; i < job_count; i++) {
            if (jobs[i].arrival <= current_time && !jobs[i].finished) {
                ready_indices[ready_count++] = i;
            }
        }

        if (ready_count == 0) {
            sleep(1);
            current_time++;
            continue;
        }

        //select the next job
        int next_job_index = -1;
        if (ready_count > 1 && current_job_index != -1) {
            int candidate = -1;
            for (int j = 0; j < ready_count; j++) {
                int i = ready_indices[j];
                if (i == current_job_index)
                    continue;
                if (candidate == -1) {
                    candidate = i;
                } else {
                    //comparison: lower priority value, then earlier arrival, then smaller remaining time, then order
                    if ((jobs[i].priority < jobs[candidate].priority) ||
                        (jobs[i].priority == jobs[candidate].priority && jobs[i].arrival < jobs[candidate].arrival) ||
                        (jobs[i].priority == jobs[candidate].priority && jobs[i].arrival == jobs[candidate].arrival &&
                         jobs[i].remaining_time < jobs[candidate].remaining_time) ||
                        (jobs[i].priority == jobs[candidate].priority && jobs[i].arrival == jobs[candidate].arrival &&
                         jobs[i].remaining_time == jobs[candidate].remaining_time && jobs[i].order < jobs[candidate].order))
                    {
                        candidate = i;
                    }
                }
            }
            next_job_index = (candidate == -1) ? current_job_index : candidate;
        } else {
            //only one ready job or no previous job: choose the best candidate.
            int candidate = ready_indices[0];
            for (int j = 1; j < ready_count; j++) {
                int i = ready_indices[j];
                if ((jobs[i].priority < jobs[candidate].priority) ||
                    (jobs[i].priority == jobs[candidate].priority && jobs[i].arrival < jobs[candidate].arrival) ||
                    (jobs[i].priority == jobs[candidate].priority && jobs[i].arrival == jobs[candidate].arrival &&
                     jobs[i].remaining_time < jobs[candidate].remaining_time) ||
                    (jobs[i].priority == jobs[candidate].priority && jobs[i].arrival == jobs[candidate].arrival &&
                     jobs[i].remaining_time == jobs[candidate].remaining_time && jobs[i].order < jobs[candidate].order))
                {
                    candidate = i;
                }
            }
            next_job_index = candidate;
        }

        
        if (next_job_index != current_job_index && jobs[next_job_index].forked) {
            if (jobs[next_job_index].preempted) {
                kill(jobs[next_job_index].pid, SIGCONT);
                log_event("Resuming %s (PID: %d) - SIGCONT", jobs[next_job_index].name, jobs[next_job_index].pid);
                jobs[next_job_index].preempted = 0;
            }
        }

     
        if (!jobs[next_job_index].forked) {
            pid_t pid = fork();
            if (pid < 0) {
                perror("fork error");
                exit(EXIT_FAILURE);
            } else if (pid == 0) {
                execlp(argv[0], argv[0], "job", jobs[next_job_index].name, (char *)NULL);
                perror("execlp failed");
                exit(EXIT_FAILURE);
            } else {
                jobs[next_job_index].pid = pid;
                jobs[next_job_index].forked = 1;
                log_event("Forking new process for %s", jobs[next_job_index].name);
                log_event("Executing %s (PID: %d) using exec", jobs[next_job_index].name, pid);
            }
        }

        
        int run_duration = (jobs[next_job_index].remaining_time < time_slice) ?
                           jobs[next_job_index].remaining_time : time_slice;

        sleep(run_duration);          //simulate the job running
        current_time += run_duration;
        jobs[next_job_index].remaining_time -= run_duration;

       
        char capName[50];
        capitalize(capName, jobs[next_job_index].name);

       
        if (jobs[next_job_index].remaining_time <= 0) {
            log_event("%s completed execution. Terminating (PID: %d)", capName, jobs[next_job_index].pid);
            kill(jobs[next_job_index].pid, SIGTERM);
            waitpid(jobs[next_job_index].pid, NULL, 0);
            jobs[next_job_index].finished = 1;
        } else {
            //time slice expired – preempt the job
            log_event("%s ran for %d seconds. Time slice expired - Sending SIGSTOP", capName, run_duration);
            kill(jobs[next_job_index].pid, SIGSTOP);
            jobs[next_job_index].preempted = 1;
        }

        
        current_job_index = next_job_index;
    }

    fclose(log_file);
    return 0;
}
