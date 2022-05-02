#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <dpu.h>


#include "../support/params.h"
#include "../support/common.h"
#include "../support/timer.h"

#ifndef DPU_BINARY
#define DPU_BINARY "./bin/dpu_code"
#endif


//Buffer contenente tutti i punti.
static T* P; 

//Buffer per i centri calcolati dalle dpu.
static T* C; //Buffer centri finali dpu.
static T* R; //Buffer centri calcolati da ogni dpu.

//Buffer per i centri calcolati dall'host per verificare i risultati.
static T* H; //Buffer centri finali host.
static T* M; //Buffer "centri intermedi" host.


//Legge i valori dal database fornito.
static void init_dataset(T* points_buffer, char *path) {
    FILE *db = fopen(path, "r");

    int i = 0;
    char row[100];
    char *num;

    while (feof(db) != true) {
        fgets(row, 100, db);
        num = strtok(row, ",");

        while(num != NULL) {
            points_buffer[i++] = atof(num);
            num = strtok(NULL, ",");
        }

    }
    
    fclose(db);
}

//Estrae "n_points" centri da "points_buffer" inserrendoli in "centers_buffer".
static void get_centers(T* point_buffer, T* centers_buffer, uint32_t n_points, uint32_t n_centers, uint32_t dim) {
    
    //Scelgo "a caso" il primo centro.
    uint32_t n_centers_found = 1;
    for (unsigned int i = 0; i < dim; i++) {
        centers_buffer[i] = point_buffer[i];
    }

    while (n_centers_found < n_centers) {
        D candidate_center_dist = 0;

        for (unsigned int i = 0; i < n_points; i++) {
            uint32_t point_index = i*dim;
            D min_center_dist = UINT32_MAX;
            
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
static void k_clustering_host(uint32_t n_point_dpu, uint32_t n_points_last_dpu, uint32_t n_centers, uint32_t dim) {

    //Calcolo i centri dai vari sottoinsiemi di P.
    uint32_t points_offset = 0;
    uint32_t centers_set_offset = 0;
    for (int i = 0; i < NR_DPUS-1; i++) {
        points_offset = i*n_point_dpu*dim;
        centers_set_offset = i*n_centers*dim;

        get_centers(P + points_offset, M + centers_set_offset, n_point_dpu, n_centers, dim);
    }
    points_offset = (NR_DPUS-1)*n_point_dpu*dim;
    centers_set_offset = (NR_DPUS-1)*n_centers*dim;
    get_centers(P + points_offset, M + centers_set_offset, n_points_last_dpu, n_centers, dim);
    //Calcolo i centri finali
    get_centers(M, H, n_centers*NR_DPUS, n_centers, dim);
}


int main(int argc, char **argv) {

    //Parametri per l'esecuzione del benchmark: #punti, #centri, dimensione dello spazio e #ripetizioni effettuate.
    struct Params p = input_params(argc, argv);
    if (p.n_points == 0) return -1; //Il file non è stato aperto correttamente.

    struct dpu_set_t dpu_set, dpu;

    Timer timer;

    //Alloco le DPU.
    DPU_ASSERT(dpu_alloc(NR_DPUS, NULL, &dpu_set));
    DPU_ASSERT(dpu_load(dpu_set, DPU_BINARY, NULL));

    //Suddivisione punti per ogni DPU.
    //La dimensione del blocco assegnato ad ogni DPU deve essere allineata su 8 bytes.
    uint32_t points_per_dpu = p.n_points/NR_DPUS;
    uint32_t points_per_last_dpu = p.n_points-points_per_dpu*(NR_DPUS-1);
    uint32_t mem_block_per_dpu = points_per_last_dpu*p.dim;
    uint32_t mem_block_per_dpu_8bytes = ((mem_block_per_dpu*sizeof(T) % 8) == 0) ? mem_block_per_dpu : roundup(mem_block_per_dpu, 8);

    //Inizializzo buffer dei punti usato dalle DPU e successivamente dall'host.
    P = malloc(points_per_dpu*p.n_centers*p.dim*sizeof(T)*(NR_DPUS-1) + mem_block_per_dpu_8bytes*sizeof(T));
    init_dataset(P, p.db);
    
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
    for(unsigned int rep = 0; rep < p.n_warmup + p.n_reps; rep++) {

        //Cronometro caricamento dati CPU-DPU.
        if (rep >= p.n_warmup) {
            start(&timer, 0, rep - p.n_warmup);
        }

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
        if(rep >= p.n_warmup) {
            stop(&timer, 0);
            start(&timer, 1, rep - p.n_warmup);
        }

        //Avvio l'esecuzione delle DPU.
        DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));
        
        //Stop timer tempo esecuzione nelle DPU.
        //Cronometro trasferimento DPU-CPU ed calcolo centri finali.
        if(rep >= p.n_warmup) {
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
        get_centers(R, C, p.n_centers*NR_DPUS, p.n_centers, p.dim);

        //Stop timer trasferimento DPU-CPU ed calcolo centri finali.
        //Cronometro esecuzione algoritmo su host.
        if(rep >= p.n_warmup) {
            stop(&timer, 2);
            start(&timer, 3, rep - p.n_warmup);
        }

        //Calcolo dei centri finali da parte dell'host.
        k_clustering_host(points_per_dpu, points_per_last_dpu, p.n_centers, p.dim);

        //Stop timer esecuzione algoritmo su host.
        if(rep >= p.n_warmup) {
            stop(&timer, 3);
        }

        //Verifico i risultati.
        bool status = true;
        for (unsigned int j = 0; j < p.n_centers; j++) {
                for (unsigned int k = 0; k < p.dim; k++){
                    if (H[j*p.dim + k] != C[j*p.dim + k]) status = false;
                }
        }
        if (status) {
            printf("Rep %d: Outputs are equal\n", rep);
        } else {
            printf("Rep %d: Outputs differ!\n", rep);
        }
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

    printf("\n\nCentri finali:\n");
    print_points(C, p.n_centers, p.dim);

    free(H);
    free(M);
    free(C);
    free(R);
    free(P);
    DPU_ASSERT(dpu_free(dpu_set));

    return 0;
}
