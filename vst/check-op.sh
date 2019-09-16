#!/bin/bash

echo "Check functional correctness"
for fpath_trace in ./traces/*.csv; do
    fname_trace=$(basename ${fpath_trace})
    echo Running trace: ${fname_trace}

    dname_crash="./output-order/${fname_trace}.d"
    rm -rf ${dname_crash}
    mkdir -p ${dname_crash}/img
    mkdir -p ${dname_crash}/stdout
    mkdir -p ${dname_crash}/stderr

    ./vst-jasmine ${fpath_trace} ./ftl.so -a 1>/dev/null
done
echo ""

N_JOBS=8

echo "Check prefix semantics and request atomicity"
for fpath_trace in ./traces/*.csv; do
    fname_trace=$(basename ${fpath_trace})
    echo Running trace: ${fname_trace}

    dname_crash="./output-order/${fname_trace}.d"
    rm -rf ${dname_crash}
    mkdir -p ${dname_crash}/img
    mkdir -p ${dname_crash}/stdout
    mkdir -p ${dname_crash}/stderr

    ./vst-jasmine ${fpath_trace} ./ftl.so -a -s -j ${N_JOBS} -d ./${dname_crash} 1>/dev/null
done
echo ""

echo "Check prefix semantics, request atomicity and flush semantics"
for fpath_trace in ./traces/*.csv; do
    fname_trace=$(basename ${fpath_trace})
    echo Running trace: ${fname_trace}

    dname_crash="./output-flush/${fname_trace}.d"
    rm -rf ${dname_crash}
    mkdir -p ${dname_crash}/img
    mkdir -p ${dname_crash}/stdout
    mkdir -p ${dname_crash}/stderr

    ./vst-jasmine ${fpath_trace} ./ftl.so -a -s -j ${N_JOBS} -d ./${dname_crash} -f 1000 1>/dev/null
done
echo ""
