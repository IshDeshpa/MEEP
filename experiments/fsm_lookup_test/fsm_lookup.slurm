#!/bin/bash
#SBATCH -J fsm_lookup
#SBATCH -o fsm_lookup.o%j
#SBATCH -e fsm_lookup.e%j
#SBATCH -p gpu-a100-small
#SBATCH -N 1
#SBATCH -n 1
#SBATCH -t 00:15:00
#SBATCH -A TRA25006

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
module load gcc/13.2.0
module load cuda/12.8
hostname
date -Is
nvcc --version
nvidia-smi --query-gpu=index,name,memory.total,driver_version --format=csv
nvcc -O3 -std=c++17 -lineinfo -Xptxas=-v \
    -gencode arch=compute_80,code=sm_80 \
    -gencode arch=compute_90,code=sm_90 \
    fsm_lookup_bench.cu -o fsm_lookup_bench
make hostcheck
# Tiny run checks the face/edge kernels. Full cube allocation is still 16 GiB,
# even with a small iteration count, so check it once in the main run below.
./fsm_lookup_bench edges 10 2 1
./fsm_lookup_bench faces 10 2 1
./fsm_lookup_bench all 1000 512 5 both
# H100 alternative submission: sbatch -p gpu-h100 fsm_lookup.slurm
