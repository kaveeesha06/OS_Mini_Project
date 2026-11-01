#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <time.h>

#define MAX_REGIONS 30
#define MAX_ROWS 2000
#define LINE_SIZE 512
#define NAME_SIZE 64

typedef struct {
    char name[NAME_SIZE];
    int burst;
    int remaining;
    int arrival;
    int completion;
    int waiting;
    int turnaround;
} Job;

typedef struct {
    char region[NAME_SIZE];
    double day;
    double temp;
} Row;

static void ensure_directories(void) {
    const char *dirs[] = {"processed", "outputs", "logs"};
    for (int i = 0; i < 3; ++i) {
        if (mkdir(dirs[i], 0755) < 0 && errno != EEXIST) {
            perror("mkdir");
        }
    }
}

static int write_all(int fd, const char *buf, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fd, buf + done, len - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += (size_t)n;
    }
    return 0;
}

/* EXPERIMENT 1: Linux system information through system calls. */
static void experiment1(void) {
    struct utsname u;
    struct sysinfo si;
    struct statvfs fs;

    printf("\n[Experiment 1] Linux system information\n");
    if (uname(&u) == 0)
        printf("OS: %s | Kernel: %s | Host: %s\n", u.sysname, u.release, u.nodename);
    else perror("uname");

    if (sysinfo(&si) == 0) {
        double unit = (double)si.mem_unit;
        printf("RAM: %.1f MB total | %.1f MB free | Uptime: %ld seconds\n",
               si.totalram * unit / 1048576.0,
               si.freeram * unit / 1048576.0, si.uptime);
    } else perror("sysinfo");

    if (statvfs(".", &fs) == 0)
        printf("Available disk space: %.2f GB\n",
               (double)fs.f_bavail * (double)fs.f_frsize / 1e9);
    else perror("statvfs");

    printf("Related commands: uname -a, who, uptime, free -h, df -h\n");
}

/* EXPERIMENT 2: create project folders and copy the source CSV with open/read/write. */
static int experiment2(void) {
    char buffer[1024];
    ssize_t n;
    int in_fd, out_fd;

    printf("\n[Experiment 2] Climate dataset file handling\n");
    ensure_directories();

    in_fd = open("data/temperature_history.csv", O_RDONLY);
    if (in_fd < 0) {
        perror("open data/temperature_history.csv");
        printf("Place the supplied CSV in the data/ folder and try again.\n");
        return -1;
    }

    out_fd = open("processed/history.csv", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        perror("open processed/history.csv");
        close(in_fd);
        return -1;
    }

    while ((n = read(in_fd, buffer, sizeof(buffer))) > 0) {
        if (write_all(out_fd, buffer, (size_t)n) < 0) {
            perror("write processed/history.csv");
            close(in_fd);
            close(out_fd);
            return -1;
        }
    }
    if (n < 0) {
        perror("read data/temperature_history.csv");
        close(in_fd);
        close(out_fd);
        return -1;
    }
    close(in_fd);
    close(out_fd);

    printf("Copied data/temperature_history.csv -> processed/history.csv\n");
    printf("Directories ready: processed/, outputs/, logs/\n");
    printf("Related commands: pwd, ls -l, cat, wc -l, cp, mv, chmod\n");
    return 0;
}

static int parse_double(const char *s, double *value) {
    char *end = NULL;
    errno = 0;
    double v = strtod(s, &end);
    if (errno || end == s) return -1;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
    if (*end != '\0') return -1;
    *value = v;
    return 0;
}

static int load_dataset(Row rows[], int *row_count, Job jobs[], int *job_count) {
    FILE *fp = fopen("processed/history.csv", "r");
    char line[LINE_SIZE];
    int sequence = 0;

    *row_count = 0;
    *job_count = 0;
    if (!fp) {
        perror("fopen processed/history.csv");
        printf("Run Experiment 2 first.\n");
        return -1;
    }

    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        fprintf(stderr, "Dataset is empty.\n");
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *save = NULL;
        char *region = strtok_r(line, ",", &save);
        char *day_text = strtok_r(NULL, ",", &save);
        char *temp_text = strtok_r(NULL, ",\r\n", &save);
        double day, temp;
        int idx = -1;

        if (!region || !day_text || !temp_text) continue;
        region[strcspn(region, "\r\n")] = '\0';
        if (strlen(region) == 0 || strlen(region) >= NAME_SIZE ||
            parse_double(day_text, &day) < 0 ||
            parse_double(temp_text, &temp) < 0 ||
            day < 0 || temp < -50 || temp > 70) {
            fprintf(stderr, "Skipping invalid CSV row: %s", line);
            continue;
        }
        if (*row_count >= MAX_ROWS) {
            fprintf(stderr, "Maximum row limit (%d) reached.\n", MAX_ROWS);
            break;
        }

        for (int j = 0; j < *job_count; ++j)
            if (strcmp(jobs[j].name, region) == 0) idx = j;

        if (idx < 0) {
            if (*job_count >= MAX_REGIONS) {
                fprintf(stderr, "Maximum region limit (%d) reached.\n", MAX_REGIONS);
                fclose(fp);
                return -1;
            }
            idx = (*job_count)++;
            memset(&jobs[idx], 0, sizeof(jobs[idx]));
            snprintf(jobs[idx].name, sizeof(jobs[idx].name), "%s", region);
            /* Arrival tick is the region's first-appearance order (0, 1, 2, ...).
               This is an illustrative application-level arrival model. */
            jobs[idx].arrival = idx;
        }

        snprintf(rows[*row_count].region, sizeof(rows[*row_count].region), "%s", region);
        rows[*row_count].day = day;
        rows[*row_count].temp = temp;
        (*row_count)++;
        jobs[idx].burst++;
        sequence++;
    }
    fclose(fp);

    for (int j = 0; j < *job_count; ++j)
        jobs[j].remaining = jobs[j].burst;

    if (*row_count == 0 || *job_count == 0) {
        fprintf(stderr, "No valid climate observations were found.\n");
        return -1;
    }
    return 0;
}

/* Calculate per-region simple linear regression and predict one day beyond its latest day. */
static int predict_region(const char *region, double *prediction, int *observations) {
    FILE *fp = fopen("processed/history.csv", "r");
    char line[LINE_SIZE];
    double sx = 0, sy = 0, sxx = 0, sxy = 0, latest = -1e100;
    int count = 0;

    if (!fp) return -1;
    (void)fgets(line, sizeof(line), fp); /* CSV header */

    while (fgets(line, sizeof(line), fp)) {
        char *save = NULL;
        char *name = strtok_r(line, ",", &save);
        char *day_text = strtok_r(NULL, ",", &save);
        char *temp_text = strtok_r(NULL, ",\r\n", &save);
        double day, temp;
        if (!name || !day_text || !temp_text || strcmp(name, region) != 0) continue;
        if (parse_double(day_text, &day) < 0 || parse_double(temp_text, &temp) < 0) continue;
        sx += day; sy += temp; sxx += day * day; sxy += day * temp;
        if (day > latest) latest = day;
        count++;
    }
    fclose(fp);

    if (count == 0) return -1;
    double slope = 0.0;
    double denominator = count * sxx - sx * sx;
    if (count > 1 && denominator != 0.0)
        slope = (count * sxy - sx * sy) / denominator;
    double intercept = (sy - slope * sx) / count;
    *prediction = intercept + slope * (latest + 1.0);
    *observations = count;
    return 0;
}

static const char *severity_for(double t) {
    if (t >= 45.0) return "SEVERE";
    if (t >= 40.0) return "HIGH";
    if (t >= 35.0) return "MODERATE";
    return "NORMAL";
}

/* EXPERIMENT 6: Preemptive SJF (SRTF), including Gantt timeline and metrics.
   Arrivals are illustrative ticks based on each region's first CSV appearance. */
static int schedule_srtf(Job jobs[], int n, int order[]) {
    int done = 0, time = 0, last = -1, segment_start = 0;
    int gantt_job[10000], gantt_start[10000], gantt_end[10000], segments = 0;
    for (int i = 0; i < n; ++i) {
        jobs[i].remaining = jobs[i].burst;
        jobs[i].completion = jobs[i].waiting = jobs[i].turnaround = 0;
    }

    printf("\n[Experiment 6] Preemptive SJF / SRTF scheduling\n");
    while (done < n && time < 10000) {
        int best = -1;
        for (int i = 0; i < n; ++i) {
            if (jobs[i].arrival <= time && jobs[i].remaining > 0 &&
                (best < 0 || jobs[i].remaining < jobs[best].remaining))
                best = i;
        }
        if (best < 0) {
            if (last >= 0 && segments < 10000) {
                gantt_job[segments] = last; gantt_start[segments] = segment_start;
                gantt_end[segments++] = time; last = -1;
            }
            time++;
            continue;
        }
        if (best != last) {
            if (last >= 0 && segments < 10000) {
                gantt_job[segments] = last; gantt_start[segments] = segment_start;
                gantt_end[segments++] = time;
            }
            last = best;
            segment_start = time;
        }
        jobs[best].remaining--;
        time++;
        if (jobs[best].remaining == 0) {
            jobs[best].completion = time;
            jobs[best].turnaround = time - jobs[best].arrival;
            jobs[best].waiting = jobs[best].turnaround - jobs[best].burst;
            done++;
            if (last >= 0 && segments < 10000) {
                gantt_job[segments] = last;
                gantt_start[segments] = segment_start;
                gantt_end[segments++] = time;
            }
            last = -1;
        }
    }
    if (last >= 0 && segments < 10000) {
        gantt_job[segments] = last; gantt_start[segments] = segment_start;
        gantt_end[segments++] = time;
    }

    printf("Gantt timeline: ");
    for (int s = 0; s < segments; ++s)
        printf("| %s %d-%d ", jobs[gantt_job[s]].name, gantt_start[s], gantt_end[s]);
    printf("|\n");

    for (int k = 0; k < n; ++k) {
        int best = -1;
        for (int i = 0; i < n; ++i) {
            int used = 0;
            for (int x = 0; x < k; ++x) if (order[x] == i) used = 1;
            if (!used && (best < 0 || jobs[i].completion < jobs[best].completion)) best = i;
        }
        order[k] = best;
    }
    return done == n ? 0 : -1;
}

/* EXPERIMENT 6: Round Robin with arrivals, Gantt timeline and metrics. */
static int schedule_rr(Job jobs[], int n, int quantum, int order[]) {
    int queue[10000], head = 0, tail = 0, queued[MAX_REGIONS] = {0};
    int done = 0, time = 0, completion_order = 0;
    int gantt_job[10000], gantt_start[10000], gantt_end[10000], segments = 0;

    if (quantum < 1) return -1;
    for (int i = 0; i < n; ++i) {
        jobs[i].remaining = jobs[i].burst;
        jobs[i].completion = jobs[i].waiting = jobs[i].turnaround = 0;
    }

    printf("\n[Experiment 6] Round Robin scheduling (quantum = %d)\n", quantum);
    while (done < n && time < 10000) {
        for (int i = 0; i < n; ++i) {
            if (!queued[i] && jobs[i].remaining > 0 && jobs[i].arrival <= time) {
                queue[tail++] = i;
                queued[i] = 1;
            }
        }
        if (head == tail) {
            time++;
            continue;
        }

        int current = queue[head++];
        queued[current] = 0;
        int slice = jobs[current].remaining < quantum ? jobs[current].remaining : quantum;
        int start = time;
        jobs[current].remaining -= slice;
        time += slice;
        if (segments < 10000) {
            gantt_job[segments] = current;
            gantt_start[segments] = start;
            gantt_end[segments++] = time;
        }

        for (int i = 0; i < n; ++i) {
            if (i != current && !queued[i] && jobs[i].remaining > 0 && jobs[i].arrival <= time) {
                queue[tail++] = i;
                queued[i] = 1;
            }
        }

        if (jobs[current].remaining == 0) {
            jobs[current].completion = time;
            jobs[current].turnaround = time - jobs[current].arrival;
            jobs[current].waiting = jobs[current].turnaround - jobs[current].burst;
            order[completion_order++] = current;
            done++;
        } else {
            queue[tail++] = current;
            queued[current] = 1;
        }
    }

    printf("Gantt timeline: ");
    for (int s = 0; s < segments; ++s)
        printf("| %s %d-%d ", jobs[gantt_job[s]].name, gantt_start[s], gantt_end[s]);
    printf("|\n");
    return done == n ? 0 : -1;
}

static void print_metrics(Job jobs[], int n) {
    double avg_wait = 0, avg_turn = 0;
    printf("\nScheduling metrics:\n");
    printf("%-14s %-8s %-8s %-12s %-12s %-12s\n",
           "Region", "Arrival", "Burst", "Completion", "Waiting", "Turnaround");
    for (int i = 0; i < n; ++i) {
        printf("%-14s %-8d %-8d %-12d %-12d %-12d\n",
               jobs[i].name, jobs[i].arrival, jobs[i].burst,
               jobs[i].completion, jobs[i].waiting, jobs[i].turnaround);
        avg_wait += jobs[i].waiting;
        avg_turn += jobs[i].turnaround;
    }
    printf("Average waiting time: %.2f units\n", avg_wait / n);
    printf("Average turnaround time: %.2f units\n", avg_turn / n);
    printf("Note: arrival and burst values are illustrative scheduling units, not measured CPU times.\n");
}

/* Append one complete output record. */
static int append_text(const char *path, const char *text) {
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return -1;

    size_t len = strlen(text);
    ssize_t n;
    do {
        n = write(fd, text, len);
    } while (n < 0 && errno == EINTR);

    int rc = (n == (ssize_t)len) ? 0 : -1;
    close(fd);
    return rc;
}

/* A child handles one region and appends its own prediction and log record. */
static int regional_child(const char *region) {
    double predicted;
    int observations;
    char row[256], logline[512];
    if (predict_region(region, &predicted, &observations) < 0) {
        fprintf(stderr, "Child %ld: prediction failed for %s\n", (long)getpid(), region);
        return 1;
    }

    snprintf(row, sizeof(row), "%s,%.2f,%s,%d\n",
             region, predicted, severity_for(predicted), observations);
    if (append_text("outputs/forecasts.csv", row) < 0) {
        perror("forecast append");
        return 1;
    }

    snprintf(logline, sizeof(logline),
             "PID=%ld region=%s observations=%d prediction=%.2f severity=%s\n",
             (long)getpid(), region, observations, predicted, severity_for(predicted));
    if (append_text("logs/prediction.log", logline) < 0) {
        perror("log append");
        return 1;
    }

    printf("[Child PID %ld] %s: %.2f C -> %s (%d observations)\n",
           (long)getpid(), region, predicted, severity_for(predicted), observations);
    return 0;
}

/* EXPERIMENT 4: fork one child per region; exec re-enters this program in child mode.
   Parent collects every child with waitpid(). */
static int experiment4(int algorithm, int quantum) {
    Row rows[MAX_ROWS];
    Job jobs[MAX_REGIONS];
    int row_count, job_count, order[MAX_REGIONS];
    pid_t pids[MAX_REGIONS];
    int status_by_job[MAX_REGIONS];

    printf("\n[Experiment 4] Multi-process regional prediction\n");
    if (load_dataset(rows, &row_count, jobs, &job_count) < 0) return -1;

    if (algorithm == 1) {
        if (schedule_srtf(jobs, job_count, order) < 0) return -1;
    } else {
        if (schedule_rr(jobs, job_count, quantum, order) < 0) return -1;
    }
    print_metrics(jobs, job_count);

    /* Parent initializes the shared output file before children append results. */
    int out = open("outputs/forecasts.csv", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { perror("create forecasts.csv"); return -1; }
    const char *header = "region,next_day_temperature_c,severity,observations\n";
    if (write_all(out, header, strlen(header)) < 0) {
        perror("write forecast header"); close(out); return -1;
    }
    close(out);

    int logfd = open("logs/prediction.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (logfd >= 0) close(logfd);

    printf("\n[Experiment 4] Starting one child process per region:\n");
    for (int k = 0; k < job_count; ++k) {
        int idx = order[k];
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            pids[idx] = -1;
            status_by_job[idx] = 1;
            continue;
        }
        if (pid == 0) {
            execl("/proc/self/exe", "heatwave", "--region-child", jobs[idx].name, (char *)NULL);
            perror("execl");
            _exit(127);
        }
        pids[idx] = pid;
        printf("Parent: launched %s as PID %ld\n", jobs[idx].name, (long)pid);
    }

    int failures = 0;
    for (int i = 0; i < job_count; ++i) {
        if (pids[i] < 0) { failures++; continue; }
        int status;
        if (waitpid(pids[i], &status, 0) < 0) {
            perror("waitpid"); failures++; continue;
        }
        if (WIFEXITED(status)) {
            status_by_job[i] = WEXITSTATUS(status);
            printf("Parent: %s child PID %ld exited with status %d\n",
                   jobs[i].name, (long)pids[i], status_by_job[i]);
            if (status_by_job[i] != 0) failures++;
        } else {
            printf("Parent: %s child did not exit normally.\n", jobs[i].name);
            failures++;
        }
    }
    if (failures) {
        fprintf(stderr, "%d regional child process(es) failed.\n", failures);
        return -1;
    }

    /* Parent generates advisory after all children finish. */
    FILE *forecast = fopen("outputs/forecasts.csv", "r");
    FILE *advisory = fopen("outputs/heatwave_advisory.txt", "w");
    if (!forecast || !advisory) {
        perror("open final output");
        if (forecast) fclose(forecast);
        if (advisory) fclose(advisory);
        return -1;
    }
    fprintf(advisory, "HEATWAVE INTELLIGENCE - EDUCATIONAL ADVISORY\n");
    fprintf(advisory, "Based on synthetic sample data and illustrative thresholds.\n\n");
    char line[LINE_SIZE], name[NAME_SIZE], level[16];
    double temp;
    int observations;
    (void)fgets(line, sizeof(line), forecast);
    while (fgets(line, sizeof(line), forecast)) {
        if (sscanf(line, "%63[^,],%lf,%15[^,],%d", name, &temp, level, &observations) == 4) {
            if (strcmp(level, "HIGH") == 0 || strcmp(level, "SEVERE") == 0)
                fprintf(advisory, "%s: %.2f C (%s). Stay hydrated, avoid strenuous outdoor activity and follow local official alerts.\n",
                        name, temp, level);
            else
                fprintf(advisory, "%s: %.2f C (%s). Continue monitoring local conditions.\n",
                        name, temp, level);
        }
    }
    fclose(forecast);
    fclose(advisory);

    printf("\nAll regional child processes completed.\n");
    printf("Created outputs/forecasts.csv, outputs/heatwave_advisory.txt and logs/prediction.log\n");
    printf("Parent process PID: %ld\n", (long)getpid());
    printf("Note: SRTF/RR is an application-level scheduling simulation; Linux still schedules the real processes.\n");
    printf("Synthetic data and illustrative thresholds; this is not an official weather forecast.\n");
    return 0;
}

static int run_scheduler_menu(void) {
    Row rows[MAX_ROWS];
    Job jobs[MAX_REGIONS];
    int row_count, job_count, order[MAX_REGIONS];
    int algorithm, quantum = 2;

    if (load_dataset(rows, &row_count, jobs, &job_count) < 0) return -1;
    printf("\n[Experiment 6] CPU scheduling simulation for regional prediction jobs\n");
    printf("1. Preemptive SJF (SRTF)\n2. Round Robin\nChoice: ");
    if (scanf("%d", &algorithm) != 1 || (algorithm != 1 && algorithm != 2)) {
        printf("Invalid algorithm choice.\n"); return -1;
    }
    if (algorithm == 2) {
        printf("Enter time quantum (positive integer): ");
        if (scanf("%d", &quantum) != 1 || quantum < 1) {
            printf("Invalid time quantum.\n"); return -1;
        }
        if (schedule_rr(jobs, job_count, quantum, order) < 0) return -1;
    } else {
        if (schedule_srtf(jobs, job_count, order) < 0) return -1;
    }
    print_metrics(jobs, job_count);
    return 0;
}

static void view_previous_forecast(void) {
    FILE *fp = fopen("outputs/forecasts.csv", "r");
    char line[LINE_SIZE];
    if (!fp) {
        printf("No forecast found. Run Experiment 4 first.\n");
        return;
    }
    printf("\nPrevious forecast:\n");
    while (fgets(line, sizeof(line), fp)) fputs(line, stdout);
    fclose(fp);
}

static int ask_algorithm(int *algorithm, int *quantum) {
    printf("Choose scheduling algorithm:\n1. Preemptive SJF (SRTF)\n2. Round Robin\nChoice: ");
    if (scanf("%d", algorithm) != 1 || (*algorithm != 1 && *algorithm != 2)) {
        printf("Invalid choice.\n"); return -1;
    }
    if (*algorithm == 2) {
        printf("Enter time quantum: ");
        if (scanf("%d", quantum) != 1 || *quantum < 1) {
            printf("Time quantum must be a positive integer.\n"); return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);

    if (argc == 3 && strcmp(argv[1], "--region-child") == 0)
        return regional_child(argv[2]);

    ensure_directories();
    int choice, algorithm, quantum = 2;
    for (;;) {
        printf("\nHEATWAVE INTELLIGENCE\n");
        printf("1. Run complete heatwave prediction\n");
        printf("2. Experiment 1 - Linux system information\n");
        printf("3. Experiment 2 - Dataset file handling\n");
        printf("4. Experiment 4 - Multi-process prediction\n");
        printf("5. Experiment 6 - SRTF / Round Robin scheduling\n");
        printf("6. View previous forecast\n");
        printf("0. Exit\nChoice: ");

        if (scanf("%d", &choice) != 1) {
            printf("Invalid input. Enter a menu number.\n");
            int ch; while ((ch = getchar()) != '\n' && ch != EOF) {}
            continue;
        }

        switch (choice) {
            case 1:
                experiment1();
                if (experiment2() == 0 && ask_algorithm(&algorithm, &quantum) == 0)
                    experiment4(algorithm, quantum);
                break;
            case 2:
                experiment1();
                break;
            case 3:
                experiment2();
                break;
            case 4:
                if (access("processed/history.csv", R_OK) != 0) {
                    printf("Processed dataset not found. Run option 3 first.\n");
                } else if (ask_algorithm(&algorithm, &quantum) == 0) {
                    experiment4(algorithm, quantum);
                }
                break;
            case 5:
                if (access("processed/history.csv", R_OK) != 0) {
                    printf("Processed dataset not found. Run option 3 first.\n");
                } else {
                    run_scheduler_menu();
                }
                break;
            case 6:
                view_previous_forecast();
                break;
            case 0:
                printf("Exiting Heatwave Intelligence.\n");
                return 0;
            default:
                printf("Invalid option. Select 0 to 7.\n");
        }
    }
}
