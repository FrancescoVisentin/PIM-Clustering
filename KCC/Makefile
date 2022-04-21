DPU_DIR := dpu
HOST_DIR := host
BUILDDIR := bin
OUTPUTDIR := profile
NR_DPUS ?= 4
NR_TASKLETS ?= 16
BL ?= 128
TYPE ?= UINT32

HOST_TARGET := ${BUILDDIR}/host_app
DPU_TARGET := ${BUILDDIR}/dpu_code

COMMON_INCLUDES := support
HOST_SOURCES := $(wildcard ${HOST_DIR}/*.c)
DPU_SOURCES := $(wildcard ${DPU_DIR}/*.c)

COMMON_FLAGS := -Wall -Wextra -g -I${COMMON_INCLUDES}
HOST_FLAGS := ${COMMON_FLAGS} -std=c11 -O3 `dpu-pkg-config --cflags --libs dpu` -DNR_TASKLETS=${NR_TASKLETS} -DNR_DPUS=${NR_DPUS} -DBLOCK_SIZE=${BL} -D${TYPE}
DPU_FLAGS := ${COMMON_FLAGS} -O2 -DNR_TASKLETS=${NR_TASKLETS} -DBLOCK_SIZE=${BL} -D${TYPE}

.PHONY: all clean

all: ${HOST_TARGET} ${DPU_TARGET}

${HOST_TARGET}: ${HOST_SOURCES} ${COMMON_INCLUDES}
	$(CC) -o $@ ${HOST_SOURCES} ${HOST_FLAGS}

${DPU_TARGET}: ${DPU_SOURCES} ${COMMON_INCLUDES}
	dpu-upmem-dpurte-clang ${DPU_FLAGS} -o $@ ${DPU_SOURCES}


clean:
	$(RM) -r ${BUILDDIR}
	$(RM) -r ${OUTPUTDIR}
