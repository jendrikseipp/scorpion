Bootstrap: docker
From: ubuntu:20.04

%files
    `pwd` /planner

%post
    ## Install all necessary dependencies.
    apt-get update
    apt-get -y install --no-install-recommends cmake g++ make python3

    ## Clear build directory.
    rm -rf /planner/builds

    ## Build planner.
    cd /planner
    ./build.py

    ## Strip binaries.
    strip --strip-all /planner/builds/release/bin/downward /planner/builds/release/bin/preprocess-h2

    ## Remove packages unneeded for running the planner.
    apt-get -y autoremove cmake g++ make
    rm -rf /var/lib/apt/lists/*

    ## Only keep essential binaries.
    mkdir -p /compiled-planner/builds/release
    mv /planner/driver /planner/fast-downward.py /compiled-planner
    mv /planner/builds/release/bin /compiled-planner/builds/release
    rm -rf /planner
    mv /compiled-planner /planner

%runscript
    ## The runscript is called whenever the container is used to solve
    ## an instance.

    DOMAINFILE="$1"
    PROBLEMFILE="$2"
    PLANFILE="$3"

    ## Call planner.
    /planner/fast-downward.py \
        --plan-file "$PLANFILE" \
        --transform-task preprocess-h2 \
        --alias scorpion \
        "$DOMAINFILE" \
        "$PROBLEMFILE"

%labels
    Name        Scorpion
    Description Saturated cost partitioning over abstraction heuristics
    Authors     Jendrik Seipp <jendrik.seipp@liu.se>
