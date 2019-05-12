#!/bin/bash

for f in ./jobs/*
do
    sbatch $f
done
