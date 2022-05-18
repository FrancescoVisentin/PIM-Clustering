#include <stdio.h>
#include <stdint.h>
#include <alloc.h>
#include <mram.h>
#include <defs.h>
#include <barrier.h>
#include <mutex.h>

#include "../support/common.h"

//Parametri passati dall'host.
__host struct dpu_arguments_t DPU_INPUT_ARGUMENTS;

BARRIER_INIT(my_barrier_1, NR_TASKLETS);
BARRIER_INIT(my_barrier_2, NR_TASKLETS);
MUTEX_INIT(my_mutex);

//Variabili condivise.
static T* centers_set;
static uint32_t n_centers_found = 0;
static D max_distance = 0;

static D get_furthest_point(T* buffer, uint32_t n_points, uint32_t dim, T* candidate_center, D candidate_center_dist) {
    
    for (unsigned int i = 0; i < n_points; i++) {
        uint32_t point_index = i*dim; //Riduco numero di moltiplicazioni effettuate.
        D min_center_dist = INIT_VAL;
        
        for(unsigned int j = 0; j < n_centers_found; j++) {
            uint32_t center_index = j*dim;
            D dist = 0;
        
            for (unsigned int k = 0; k < dim; k++) {
                dist += power(buffer[point_index+k], centers_set[center_index+k], dim); //TODO: non gestisce overflow.
            }

            min_center_dist = (dist < min_center_dist) ? dist : min_center_dist;
        }

        if (candidate_center_dist <= min_center_dist) {
            candidate_center_dist = min_center_dist;
            for (unsigned int k = 0; k < dim; k++) {
                candidate_center[k] = buffer[point_index+k];
            }
        }
    }

    return candidate_center_dist;
}


int main() {
    unsigned int tasklet_id = me();
    uint32_t n_points = DPU_INPUT_ARGUMENTS.n_points_dpu_i;
    uint32_t n_centers = DPU_INPUT_ARGUMENTS.n_centers;
    uint32_t dim = DPU_INPUT_ARGUMENTS.dim;
    uint32_t dpu_mem_size = DPU_INPUT_ARGUMENTS.mem_size;
    uint32_t first_offset = DPU_INPUT_ARGUMENTS.first_center_offset;


    if (tasklet_id == 0) {        
        mem_reset();
        centers_set = (T*) mem_alloc((n_centers*dim) << T_SHIFT);

        //Il primo punto è scelto come primo centro.
        mram_read(DPU_MRAM_HEAP_POINTER + first_offset, centers_set, roundup((dim << T_SHIFT), 8));

        //Resetto varibili globali.
        n_centers_found = 1;
        max_distance = 0;
    }

    barrier_wait(&my_barrier_1);
    
    //TODO:
    //Sia l'indirizzo in MRAM dove vado a leggere, che l'indirizzo WRAM dove carico i dati per elaborarli, devono essere allineati su 8 byte.
    //
    //Vorrei leggere la memoria MRAM assegnata ad ogni DPU assegnando ad ogni tasklet blocchi di dimensione fissa: BLOCK_SIZE = 2^N dove N è definito
    //in compilazione in base al numero di tasklet istanziati, così da non rischiare di saturare la WRAM. (stesso metodo usato nei benchmark di SAFARI ETH)
    //
    //Non sempre però un blocco contiene un numero intero di punti. (dipende dal valore di dim*sizeof(T))
    //Per garantire l'allineamento su 8 byte carico blocchi del multiplo di (dim*sizeof(T)) più vicino a BLOCK_SIZE e divisibile per 8.
    //Se dim ha un valore abbastanza grande e BLOCK_SIZE è piccolo (ho molti tasklet) non funziona benissimo (gap troppo grande tra BLOCK_SIZE ed buffer_size)
    //Al momento non ho trovato soluzioni migliori...
    uint32_t buffer_size = get_block_size(dim << T_SHIFT);
    T* buffer = (T*) mem_alloc(buffer_size);
    
    //Indirizzo del primo blocco per ogni tasklet in MRAM.
    uint32_t points_per_block = buffer_size/(dim << T_SHIFT);
    uint32_t base_block_addr = (uint32_t)(DPU_MRAM_HEAP_POINTER + buffer_size*tasklet_id);
    uint32_t last_point_addr = (uint32_t)(DPU_MRAM_HEAP_POINTER + ((n_points*dim) << T_SHIFT));

    //Buffer per contenere il candidato centro di ogni tasklet.
    T* candidate_center = (T*) mem_alloc(dim << T_SHIFT);

    while (n_centers_found < n_centers) {
        D candidate_center_distance = 0;

        for(uint32_t point_index = base_block_addr; point_index < last_point_addr; point_index += buffer_size*NR_TASKLETS) {
            
            uint32_t l_size_bytes = buffer_size;
            uint32_t points_block_i = points_per_block;

            if (point_index + buffer_size >= last_point_addr) {
                l_size_bytes = last_point_addr-point_index;
                points_block_i = (last_point_addr - point_index)/(dim << T_SHIFT);
                
                l_size_bytes = (l_size_bytes % 8 == 0) ? l_size_bytes: roundup(l_size_bytes, 8); //Allineo su 8 bytes.
            }

            mram_read((__mram_ptr void const*) point_index, buffer, l_size_bytes);

            candidate_center_distance = get_furthest_point(buffer, points_block_i, dim, candidate_center, candidate_center_distance);
        }

        uint32_t centers_offset = n_centers_found*dim; //Lo imposto ora prima che n_centers_found venga aggiornato.
        
        if (tasklet_id == 0) {
            max_distance = candidate_center_distance;
            for (unsigned int i = 0; i < dim; i++) {
                centers_set[centers_offset + i] = candidate_center[i];
            }

            //Resetto il counter della barriera.
            my_barrier_1.count = NR_TASKLETS;
        }

        barrier_wait(&my_barrier_2);

        mutex_lock(my_mutex);
        if (candidate_center_distance > max_distance) {
            max_distance = candidate_center_distance;
            for (unsigned int i = 0; i < dim; i++) {
                centers_set[centers_offset + i] = candidate_center[i];
            }
        }

        if (tasklet_id == 0) {
            n_centers_found++; //Aggiorno il numero di centri trovati.
            my_barrier_2.count = NR_TASKLETS; //Resetto il counter della barriera.
        }
        mutex_unlock(my_mutex);
    
        barrier_wait(&my_barrier_1);
    }

    //Carico i centri calcolati in MRAM.
    if (tasklet_id == 0) {
        mram_write(centers_set, DPU_MRAM_HEAP_POINTER + dpu_mem_size, roundup(((n_centers*dim) << T_SHIFT), 8)); //TODO: mram_write può caricare 2048 bytes al massimo.        
        
        //Resetto barriere per prossime esecuzioni.
        my_barrier_1.count = NR_TASKLETS;
        my_barrier_2.count = NR_TASKLETS;
    }
    
    return 0;
}