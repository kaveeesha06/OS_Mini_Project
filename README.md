# Heatwave Intelligence – Advanced Single-File C Project

## Requirements
- Linux or Ubuntu on WSL2
- GCC / build-essential

## Compile
```bash
gcc -std=c11 -Wall -Wextra -O2 heatwave.c -o heatwave
```

## Run
```bash
./heatwave
```

## Menu
1. Complete pipeline
2. Experiment 1: Linux system information
3. Experiment 2: file handling
4. Experiment 4: multi-process regional prediction
5. Experiment 6: SRTF / Round Robin scheduling simulation
6. View forecast
0. Exit

## Suggested demonstration inputs
- Complete pipeline: choose `1`, then choose `1` for SRTF.
- Repeat with Round Robin: choose `1`, then `2`, then time quantum `2`.
- Experiment 4 only: choose `4`, then `1` for SRTF or `2` and quantum `2` for RR.
- Experiment 6 only: choose `5`, then `1` or `2`, then quantum `2` for RR.

## Output files
- `processed/history.csv` – copied source data
- `outputs/forecasts.csv` – one next-day prediction per region
- `outputs/heatwave_advisory.txt` – severity-based educational advisory
- `logs/prediction.log` – child-process prediction log
- `logs/mutual_exclusion_demo.txt` – protected concurrent writes

## Architecture notes and limitations
- Experiment 4 forks one child per region. Each child re-executes this program in `--region-child` mode, predicts its region, and appends its result.
- `waitpid()` collects each child’s exit status.
- `flock(LOCK_EX)` protects each append to shared forecast and log files.
- Experiment 6 simulates preemptive SJF (SRTF) and Round Robin with Gantt timelines and waiting/turnaround metrics.
- Arrival ticks are illustrative: a region's arrival tick is the region's first-appearance order (0, 1, 2, ...) in the processed CSV. Burst length is the number of valid observations for that region. These are scheduling simulation units, not measured CPU times.
- The simulation does not change Linux's kernel scheduler. Linux continues to schedule the actual child processes.
- Linear Regression is fitted separately for each region using all available records; it predicts one day after the latest recorded day. No holdout accuracy score is calculated.
- Dataset is synthetic and severity thresholds (35, 40, 45 °C) are illustrative. This is not an official meteorological forecast or public warning system.

## Tested run sequence
```text
3       # prepare processed/history.csv
4       # run multi-process regional prediction
1       # choose SRTF
```
For Round Robin:
```text
3
4
2
2       # time quantum
```
For mutual exclusion:
```text
7
```

With the included synthetic CSV, the expected predictions are:
- Mumbai: 39.00 C (MODERATE)
- Pune: 37.00 C (MODERATE)
- Nashik: 38.00 C (MODERATE)
- Thane: 40.00 C (HIGH)

The included sample has equal observation counts per region, so the SRTF timeline for this particular input may not visibly interrupt a running job. The implementation is preemptive and will switch when a newly ready job has a strictly shorter remaining burst. For a visible preemption demonstration, use an input CSV with different observation counts and keep at least two observations per region for regression.
