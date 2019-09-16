#!/bin/bash

FTL_SO_PATH="./ftl-exp/ftl.so"
OUTPUT="./output/${TRACE_NAME}.rec"

if [[ -z "${TRACE_NAME}" ]]; then
    echo "Env TRACE_NAME not set"
    exit 1
fi

rm -f ${OUTPUT}
parallel -j8 -k ./vst-jasmine ../traces-unaligned/${TRACE_NAME}.trace ${FTL_SO_PATH} -i {} -p ::: ${HOME}/mnt/${TRACE_NAME}/*.img | tee -a ${OUTPUT}
