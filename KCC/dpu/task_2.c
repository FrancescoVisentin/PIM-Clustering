#include <stdio.h>
#include <stdint.h>
#include <alloc.h>
#include <mram.h>
#include <defs.h>
#include <barrier.h>
#include <mutex.h>

#include "../support/common.h"

__host struct dpu_arguments_t DPU_INPUT_ARGUMENTS;

BARRIER_INIT(my_barrier_1, NR_TASKLETS);
BARRIER_INIT(my_barrier_2, NR_TASKLETS);
MUTEX_INIT(my_mutex);

//Variabili condivise.
static T* centers_set;
static D max_distance = 0;

static D get_max_distance(T* buffer, uint32_t n_points, uint32_t n_centers, uint32_t dim, D tasklet_max_distance) {
    
    for (unsigned int i = 0; i < n_points; i++) {
        uint32_t point_index = i*dim; //Riduco numero di moltiplicazioni effettuate.
        D min_center_dist = INIT_VAL;
        
        for(unsigned int j = 0; j < n_centers; j++) {
            uint32_t center_index = j*dim;
            D dist = 0;
        
            for (unsigned int k = 0; k < dim; k++) {
                dist += power(buffer[point_index+k], centers_set[center_index+k], dim); //TODO: non gestisce overflow.
            }

            min_center_dist = (dist < min_center_dist) ? dist : min_center_dist;
        }

        if (tasklet_max_distance <= min_center_dist) {
            tasklet_max_distance = min_center_dist;
        }
    }

    return tasklet_max_distance;
}


int main() {
    unsigned int tasklet_id = me();
    uint32_t n_points = DPU_INPUT_ARGUMENTS.n_points_dpu_i;
    uint32_t n_centers = DPU_INPUT_ARGUMENTS.n_centers;
    uint32_t dim = DPU_INPUT_ARGUMENTS.dim;
    uint32_t dpu_mem_size = DPU_INPUT_ARGUMENTS.mem_size;


    if (tasklet_id == 0) {
        mem_reset();
    
        uint32_t centers_set_size = ((n_centers*dim) << T_SHIFT);
        centers_set_size = (centers_set_size % 8) == 0 ? centers_set_size : roundup(centers_set_size, 8);
        centers_set = (T*) mem_alloc(centers_set_size);
        
        //Carico i centri finali precedentemente calcolati.
        mram_read(DPU_MRAM_HEAP_POINTER + dpu_mem_size, centers_set, centers_set_size);
    
        //Resetto varibili globali.
        max_distance = 0;
    }

    barrier_wait(&my_barrier_1);
    
    uint32_t buffer_size = get_block_size(dim << T_SHIFT);
    T* buffer = (T*) mem_alloc(buffer_size);
    
    //Indirizzo del primo blocco per ogni tasklet in MRAM.
    uint32_t points_per_block = buffer_size/(dim << T_SHIFT);
    uint32_t base_block_addr = (uint32_t)(DPU_MRAM_HEAP_POINTER + buffer_size*tasklet_id);
    uint32_t last_point_addr = (uint32_t)(DPU_MRAM_HEAP_POINTER + ((n_points*dim) << T_SHIFT));

    D tasklet_max_distance = 0;
    for(uint32_t point_index = base_block_addr; point_index < last_point_addr; point_index += buffer_size*NR_TASKLETS) {
        
        uint32_t l_size_bytes = buffer_size;
        uint32_t points_block_i = points_per_block;

        if (point_index + buffer_size >= last_point_addr) {
            l_size_bytes = last_point_addr-point_index;
            points_block_i = (last_point_addr - point_index)/(dim << T_SHIFT);
            
            l_size_bytes = (l_size_bytes % 8 == 0) ? l_size_bytes: roundup(l_size_bytes, 8); //Allineo su 8 bytes.
        }

        mram_read((__mram_ptr void const*) point_index, buffer, l_size_bytes);

        tasklet_max_distance = get_max_distance(buffer, points_block_i, n_centers, dim, tasklet_max_distance);
    }

    if (tasklet_id == 0) {
        max_distance = tasklet_max_distance;

        //Resetto il counter della barriera.
        my_barrier_1.count = NR_TASKLETS;
    }

    barrier_wait(&my_barrier_2);

    mutex_lock(my_mutex);
    if (tasklet_max_distance > max_distance) {
        max_distance = tasklet_max_distance;
    }

    if (tasklet_id == 0) {
        my_barrier_2.count = NR_TASKLETS; //Resetto il counter della barriera.
    }
    mutex_unlock(my_mutex);

    barrier_wait(&my_barrier_1);

    //Carico il valore massimo di distanza calcolato in MRAM.
    if (tasklet_id == 0) {
        mram_write(&max_distance, DPU_MRAM_HEAP_POINTER + dpu_mem_size, sizeof(D));
        
        //Resetto barriere per prossime esecuzioni.
        my_barrier_1.count = NR_TASKLETS;
        my_barrier_2.count = NR_TASKLETS;
    }
    
    return 0;
}