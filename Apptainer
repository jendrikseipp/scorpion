# Stage 1: Compile the planner
Bootstrap: docker
From: ubuntu:22.04
Stage: build

%files
    . /planner

%post
    ## Install all necessary dependencies.
    apt-get update
    apt-get -y install --no-install-recommends cmake g++ make python3.11

    ## Clear build directory.
    rm -rf /planner/builds

    ## Build planner.
    cd /planner
    python3.11 build.py

    ## Strip binaries.
    strip --strip-all /planner/builds/release/bin/downward /planner/builds/release/bin/preprocess-h2

# Stage 2: Run the planner
Bootstrap: docker
From: ubuntu:22.04
Stage: run

%files from build
    /planner/driver
    /planner/fast-downward.py
    /planner/builds/release/bin

%post
    apt-get update
    apt-get -y install --no-install-recommends python3.11
    rm -rf /var/lib/apt/lists/*

%runscript
    #!/bin/bash

    python3.11 /planner/fast-downward.py "$@"

%labels
    Name        Scorpion
    Description Classical planning system with state-of-the-art cost partitioning algorithms
    Authors     Jendrik Seipp <jendrik.seipp@liu.se>
