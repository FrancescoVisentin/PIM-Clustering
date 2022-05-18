#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <dpu.h>


#include "../support/params.h"
#include "../support/common.h"
#include "../support/timer.h"

#define DPU_BINARY1 "./bin/dpu_code1"
#define DPU_BINARY2 "./bin/dpu_code2"

#if defined(FLOAT) || defined(DOUBLE)
T my_rand(int max) {
        return max*((T)rand()/(T)RAND_MAX);
}
#else
T my_rand(int max) {
        return (T)(rand()%max);
}
#endif

//Buffer contenente tutti i punti.
static T* P; 

//Buffer per i centri calcolati dalle dpu.
static T* C; //Buffer centri finali dpu.
static T* R; //Buffer centri calcolati da ogni dpu.

//Buffer per i centri calcolati dall'host per verificare i risultati.
static T* H; //Buffer centri finali host.
static T* M; //Buffer "centri intermedi" host.


//Crea casualmente le cordinate dei punti.
static void init_dataset(T* points_buffer, uint32_t n, uint32_t d) {
    srand(time(NULL));
    for (unsigned int i = 0; i < n*d; i++) {
        points_buffer[i] = my_rand(1000); //Restringo il range di numeri per evitare overflow.
    }
}

//Estrae "n_points" centri da "points_buffer" inserrendoli in "centers_buffer".
static void get_centers(T* point_buffer, T* centers_buffer, uint32_t n_points, uint32_t n_centers, uint32_t dim, uint32_t first_offset) {
    
    //Scelgo "a caso" il primo centro.
    uint32_t n_centers_found = 1;
    for (unsigned int i = 0; i < dim; i++) {
        centers_buffer[i] = point_buffer[i + first_offset];
    }

    while (n_centers_found < n_centers) {
        D candidate_center_dist = 0;

        for (unsigned int i = 0; i < n_points; i++) {
            uint32_t point_index = i*dim;
            D min_center_dist = INIT_VAL;
            
            for(unsigned int j = 0; j < n_centers_found; j++) {
                uint32_t center_index = j*dim;
                D dist = 0;
            
                for (unsigned int k = 0; k < dim; k++) {
                    dist += power(point_buffer[point_index+k], centers_buffer[center_index+k], dim); //TODO: non gestisce overflow.
                }

                min_center_dist = (dist < min_center_dist) ? dist : min_center_dist;
            }

            if (candidate_center_dist <= min_center_dist) {
                candidate_center_dist = min_center_dist;
                for (unsigned int k = 0; k < dim; k++) {
                    centers_buffer[n_centers_found*dim + k] = point_buffer[point_index+k];
                }
            }
        }

        n_centers_found++;
    }

}

//Calcola i centri, partendo dagli stessi sottoinsiemi di punti assegnati alle DPU.
static void k_clustering_host(uint32_t n_point_dpu, uint32_t n_points_last_dpu, uint32_t n_centers, uint32_t dim, uint32_t first_offset) {

    //Calcolo i centri dai vari sottoinsiemi di P.
    uint32_t points_offset = 0;
    uint32_t centers_set_offset = 0;
    for (int i = 0; i < NR_DPUS-1; i++) {
        points_offset = i*n_point_dpu*dim;
        centers_set_offset = i*n_centers*dim;

        get_centers(P + points_offset, M + centers_set_offset, n_point_dpu, n_centers, dim, first_offset);
    }
    points_offset = (NR_DPUS-1)*n_point_dpu*dim;
    centers_set_offset = (NR_DPUS-1)*n_centers*dim;
    get_centers(P + points_offset, M + centers_set_offset, n_points_last_dpu, n_centers, dim, first_offset);
    
    //Calcolo i centri finali
    get_centers(M, H, n_centers*NR_DPUS, n_centers, dim, 0);
}

//Calcola il costo del clustering effettuato linearmente su tutti i punti.
static D get_linear_cost(uint32_t n_points, uint32_t n_centers, uint32_t dim, uint32_t first_offset) {
    
    T centers_set[n_centers*dim];

    get_centers(P, centers_set, n_points, n_centers, dim, first_offset);

    D clustering_cost = 0;
    for (unsigned int i = 0; i < n_points; i++) {
        uint32_t point_index = i*dim;
        D min_center_dist = INIT_VAL;
        
        for(unsigned int j = 0; j < n_centers; j++) {
            uint32_t center_index = j*dim;
            D dist = 0;
        
            for (unsigned int k = 0; k < dim; k++) {
                dist += power(P[point_index+k], centers_set[center_index+k], dim); //TODO: non gestisce overflow.
            }

            min_center_dist = (dist < min_center_dist) ? dist : min_center_dist;
        }

        if (clustering_cost <= min_center_dist) {
            clustering_cost = min_center_dist;
        }
    }
    
    return clustering_cost;
}


int main(int argc, char **argv) {

    //Parametri per l'esecuzione del benchmark: #punti, #centri, dimensione dello spazio e #ripetizioni effettuate.
    struct Params p = input_params(argc, argv);
    
    struct dpu_set_t dpu_set, dpu;

    Timer timer;

    //Alloco le DPU.
    DPU_ASSERT(dpu_alloc(NR_DPUS, NULL, &dpu_set));

    //Suddivisione punti per ogni DPU.
    //La dimensione del blocco assegnato ad ogni DPU deve essere allineata su 8 bytes.
    uint32_t points_per_dpu = p.n_points/NR_DPUS;
    uint32_t points_per_last_dpu = p.n_points-points_per_dpu*(NR_DPUS-1);
    uint32_t mem_block_per_dpu = points_per_last_dpu*p.dim;
    uint32_t mem_block_per_dpu_8bytes = ((mem_block_per_dpu*sizeof(T) % 8) == 0) ? mem_block_per_dpu : roundup(mem_block_per_dpu, 8);

    //Inizializzo buffer dei punti usato dalle DPU e successivamente dall'host.
    P = malloc(points_per_dpu*p.dim*sizeof(T)*(NR_DPUS-1) + mem_block_per_dpu_8bytes*sizeof(T));
    init_dataset(P, p.n_points, p.dim);
    
    //Indici usati per accedere in MRAM e recuperare i centri intermedi dalle DPU.
    //La dimensione del blocco che vado a copiare deve essere allineata su 8 bytes.
    uint32_t dpu_center_set_addr = mem_block_per_dpu_8bytes*sizeof(T);
    uint32_t data_length = p.n_centers*p.dim;
    uint32_t data_length_8bytes = ((data_length*sizeof(T)) % 8 == 0) ? data_length : roundup(data_length, 8);
    
    //Inizializzo buffer per contenere i centri intermedi ed i centri finali delle DPU.
    R = malloc(data_length_8bytes*sizeof(T)*NR_DPUS);
    C = malloc(data_length*sizeof(T));

    //Inizializzo i buffer per contenere i centri intermedi ed i centri finali dell'host.
    H = malloc(p.n_centers*p.dim*sizeof(T));
    M = malloc(p.n_centers*p.dim*sizeof(T)*NR_DPUS);

    //Parametri per le varie DPU.
    struct dpu_arguments_t  input_arguments[NR_DPUS];
    for (int i=0; i < NR_DPUS-1; i++) {
        input_arguments[i].n_points_dpu_i = points_per_dpu;
        input_arguments[i].n_centers = p.n_centers;
        input_arguments[i].dim = p.dim;
        input_arguments[i].mem_size = mem_block_per_dpu_8bytes*sizeof(T);
    }
    input_arguments[NR_DPUS-1].n_points_dpu_i = points_per_last_dpu;
    input_arguments[NR_DPUS-1].n_centers = p.n_centers;
    input_arguments[NR_DPUS-1].dim = p.dim;
    input_arguments[NR_DPUS-1].mem_size = mem_block_per_dpu_8bytes*sizeof(T);

    //Eseguo più volte il calcolo dei centri misurando il tempo di esecuzione.
    for (unsigned int rep = 0; rep < p.n_warmup + p.n_reps; rep++) {

        //Indice randomico del primo centro di ogni DPU. Forzo un multiplo di 2 per allineamento su 8 bytes. 
        srand(time(NULL));
        uint32_t index = rand()%(points_per_last_dpu/2)*2;
        uint32_t offset = p.rnd_first*index*p.dim;
        for (unsigned int i = 0; i < NR_DPUS; i++) {
            input_arguments[i].first_center_offset = offset*sizeof(T);
        }

        //Cronometro caricamento dati CPU-DPU.
        if (rep >= p.n_warmup) {
            start(&timer, 0, rep - p.n_warmup);
        }

        //Carico il programma per calcolare i centri.
        DPU_ASSERT(dpu_load(dpu_set, DPU_BINARY1, NULL));

        //Carico i parametri di input per ogni DPU.
        unsigned int i = 0;
        DPU_FOREACH(dpu_set, dpu, i) {
            DPU_ASSERT(dpu_prepare_xfer(dpu, &input_arguments[i]));
        }
        DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "DPU_INPUT_ARGUMENTS", 0, sizeof(input_arguments[0]), DPU_XFER_DEFAULT));
        
        //Carico l'insieme di punti per ogni DPU.
        i = 0;
        DPU_FOREACH(dpu_set, dpu, i) {
                DPU_ASSERT(dpu_prepare_xfer(dpu, P + i*points_per_dpu*p.dim));
        }
        DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0, mem_block_per_dpu_8bytes*sizeof(T), DPU_XFER_DEFAULT));

        //Stop timer CPU-DPU
        //Cronometro tempo esecuzione nelle DPU.
        if (rep >= p.n_warmup) {
            stop(&timer, 0);
            start(&timer, 1, rep - p.n_warmup);
        }

        //Avvio l'esecuzione delle DPU.
        DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));
        
        //Stop timer tempo esecuzione nelle DPU.
        //Cronometro trasferimento DPU-CPU ed calcolo centri finali.
        if (rep >= p.n_warmup) {
            stop(&timer, 1);
            start(&timer, 2, rep - p.n_warmup);
        }

        //Recupero i dati.
        i = 0;
        DPU_FOREACH(dpu_set, dpu, i) {
            DPU_ASSERT(dpu_prepare_xfer(dpu, R + i*data_length_8bytes));
        }
        DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME, dpu_center_set_addr, data_length_8bytes*sizeof(T), DPU_XFER_DEFAULT));
        
        if (data_length*sizeof(T) % 8 != 0) {
            //Devo ricompattare il buffer.    
            for (unsigned int i = 0; i < NR_DPUS; i++) {
                for (unsigned int j = 0; j < data_length; j++) {
                    R[i*data_length + j] = R[i*data_length_8bytes + j];
                }
            }
        }

        //Calcolo i centri finali partendo dai centri trovati dalle DPU.
        get_centers(R, C, p.n_centers*NR_DPUS, p.n_centers, p.dim, 0);

        //Stop timer trasferimento DPU-CPU ed calcolo centri finali.
        //Cronometro esecuzione algoritmo su host.
        if (rep >= p.n_warmup) {
            stop(&timer, 2);
            start(&timer, 3, rep - p.n_warmup);
        }

        //Calcolo dei centri finali da parte dell'host.
        k_clustering_host(points_per_dpu, points_per_last_dpu, p.n_centers, p.dim, offset);

        //Stop timer esecuzione algoritmo su host.
        if (rep >= p.n_warmup) {
            stop(&timer, 3);
        }

        //Carico il programma per calcolare il costo del clustering.
        DPU_ASSERT(dpu_load(dpu_set, DPU_BINARY2, NULL));
        
        //Carico i parametri.
        i=0;
        DPU_FOREACH(dpu_set, dpu, i) {
            DPU_ASSERT(dpu_prepare_xfer(dpu, &input_arguments[i]));
        }
        DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "DPU_INPUT_ARGUMENTS", 0, sizeof(input_arguments[0]), DPU_XFER_DEFAULT));

        //Carico i centri finali calcolati.
        i = 0;
        uint32_t center_set_size = (p.n_centers*p.dim*sizeof(T) % 8) == 0 ? p.n_centers*p.dim*sizeof(T) : roundup(p.n_centers*p.dim*sizeof(T), 8);
        DPU_FOREACH(dpu_set, dpu, i) {
                DPU_ASSERT(dpu_prepare_xfer(dpu, C));
        }
        DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, dpu_center_set_addr, center_set_size, DPU_XFER_DEFAULT));
        
        //Avvio l'esecuzione delle DPU.
        DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));
        
        //Recupero i costi calcolati.
        i = 0;
        D costs[NR_DPUS];
        DPU_FOREACH(dpu_set, dpu, i) {
            DPU_ASSERT(dpu_prepare_xfer(dpu, &costs[i]));
        }
        DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME, dpu_center_set_addr, sizeof(D), DPU_XFER_DEFAULT));

        //Calcolo costo del clustering.
        D dpu_cost = 0;
        for (int j = 0; j < NR_DPUS; j++) {
            if (costs[j] > dpu_cost)
                dpu_cost = costs[j];
        }

        //Calcolo il costo del clustering effettuato linearmente su tutti i punti.
        D cpu_cost = get_linear_cost(p.n_points, p.n_centers, p.dim, (rand()%p.n_points)*p.rnd_first);

        //Verifico i risultati.
        bool status = true;
        for (unsigned int j = 0; j < p.n_centers; j++) {
                for (unsigned int k = 0; k < p.dim; k++){
                    if (H[j*p.dim + k] != C[j*p.dim + k]) status = false;
                }
        }
        
        print_res(status, rep, dpu_cost, cpu_cost);
    }

    //Stampo media tempi di esecuzione
    printf("\n\nTempi medi:\n");
    printf("CPU-DPU: ");
    print(&timer, 0, p.n_reps);
    printf("\tDPU Kernel: ");
    print(&timer, 1, p.n_reps);
    printf("\tDPU-CPU e centri finali: ");
    print(&timer, 2, p.n_reps);
    printf("\tCPU: ");
    print(&timer, 3, p.n_reps);

    free(H);
    free(M);
    free(C);
    free(R);
    free(P);
    DPU_ASSERT(dpu_free(dpu_set));

    return 0;
}
