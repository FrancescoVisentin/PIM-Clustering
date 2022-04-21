#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <dpu.h>


#include "../support/params.h"
#include "../support/common.h"

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


//Crea casualmente le cordinate dei punti.
static void init_dataset(T* points_buffer, uint32_t n, uint32_t d) {
    srand(time(NULL));
    for (unsigned int i = 0; i < n*d; i++) {
        points_buffer[i] = (T) rand()%1000; //Restringo il range di numeri per evitare overflow.
    }
}

//Estrae "n_points" centri da "points_buffer" inserrendoli in "centers_buffer".
static void get_centers(T* point_buffer, T* centers_buffer, uint32_t n_points, uint32_t n_centers, uint32_t dim) {
    
    //Scelgo "a caso" il primo centro.
    uint32_t n_centers_found = 1;
    for (unsigned int i = 0; i < dim; i++) {
        centers_buffer[i] = point_buffer[i];
    }

    while (n_centers_found < n_centers){
        uint64_t candidate_center_dist = 0;

        for (unsigned int i = 0; i < n_points; i++) {
            uint32_t point_index = i*dim;
            uint64_t min_center_dist = UINT64_MAX;
            
            for(unsigned int j = 0; j < n_centers_found; j++) {
                uint32_t center_index = j*dim;
                uint64_t dist = 0;
            
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

    //Inizializzo i buffer usati dall' host.
    H = malloc(n_centers*dim*sizeof(T));
    M = malloc(n_centers*dim*sizeof(T)*NR_DPUS);

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

    //Parametri per l'esecuzione del benchmark: #punti, #centri e dimensione dello spazio.
    struct Params p = input_params(argc, argv);
    
    struct dpu_set_t dpu_set, dpu;

    //Alloco le DPU.
    DPU_ASSERT(dpu_alloc(NR_DPUS, NULL, &dpu_set));
    DPU_ASSERT(dpu_load(dpu_set, DPU_BINARY, NULL));

    //Suddivisione punti per ogni DPU.
    //La dimensione del blocco assegnato ad ogni DPU deve essere allineato su 8 bytes.
    uint32_t points_per_dpu = p.n_points/NR_DPUS;
    uint32_t points_per_last_dpu = p.n_points-points_per_dpu*(NR_DPUS-1);
    uint32_t mem_block_per_dpu = points_per_last_dpu*p.dim;
    uint32_t mem_block_per_dpu_8bytes = ((mem_block_per_dpu*sizeof(T) % 8) == 0) ? mem_block_per_dpu : roundup(mem_block_per_dpu, 8);

    //Inizializzo buffer dei punti usato dalle DPU e successivamente dall'host.
    P = malloc(points_per_dpu*p.n_centers*p.dim*sizeof(T)*(NR_DPUS-1) + mem_block_per_dpu_8bytes*sizeof(T));
    init_dataset(P, p.n_points, p.dim);

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

    //Avvio l'esecuzione delle DPU.
    DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));
    
    
    //Recupero i centri calcolati dalle DPU.
    //TODO: (NOTA SUGLI INDICI)
    //È necessario che il numero di byte copiati dalle DPU sia multiplo di 8, questo può richiedere un arrotondamento in eccesso dei byte copiati; 
    //Copiando i dati dalle DPU tutti sullo stesso buffer R è necessario che: "indirizzo_inizio_copia_dpu_i" + "byte_copiati_da_DPU" < "indirizzo_inizio_copia_dpu_i+1"
    //Se questo vincolo non viene rispettato si incorre in un undefined behavior e, dai test effettuati, il primo valore del successivo gruppo di byte copiati viene
    //corrotto randomicamente.
    //Per evitare questo problema copio i dati sul mio buffer lasciando, qunado necessario, del padding tra un gruppo di dati e l'altro.
    //Questo richiede poi di dover ricompattare i dati nel mio buffer ma al momento non ho trovato soluzione migliore...
    i = 0;
    uint32_t dpu_center_set_addr = mem_block_per_dpu_8bytes*sizeof(T);
    uint32_t data_length = p.n_centers*p.dim;
    uint32_t data_length_8bytes = ((data_length*sizeof(T)) % 8 == 0) ? data_length : roundup(data_length, 8);
    
    //Inizializzo buffer per contenere i centri intermedi ed i centri finali delle DPU
    R = malloc(data_length_8bytes*sizeof(T)*NR_DPUS);
    C = malloc(data_length*sizeof(T));

    //Recupero i dati.
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

    //Calcolo dei centri finali da parte dell'host.
    k_clustering_host(points_per_dpu, points_per_last_dpu, p.n_centers, p.dim);

    //Verifico i risultati.
    //TODO: rimuovere stampe provvisorie usate per verificare confrontare risultati
    bool status = true;
    for (unsigned int i = 0; i < p.n_centers*NR_DPUS; i++) {
            for (unsigned int k = 0; k < p.dim; k++){
                if (M[i*p.dim + k] != R[i*p.dim + k]) status = false;
            }
    }
    if (status) {
        printf("ORDERED middle: Outputs are equal\n");
    } else {
        printf("ORDERED middle: Outputs differ!\n");
    }

    status = true;
    for (unsigned int i = 0; i < p.n_centers; i++) {
            for (unsigned int k = 0; k < p.dim; k++){
                if (H[i*p.dim + k] != C[i*p.dim + k]) status = false;
            }
    }
    if (status) {
        printf("ORDERED final: Outputs are equal\n\n");
    } else {
        printf("ORDERED final: Outputs differ!\n\n");
    }


    int tot = p.n_centers*NR_DPUS;
    for (unsigned int i = 0; i < p.n_centers*NR_DPUS; i++) {
        for (unsigned int j = 0; j < p.n_centers*NR_DPUS; j++) {
            int eq = p.dim;
            for (unsigned int k = 0; k < p.dim; k++) {
                if (R[i*p.dim + k] == M[j*p.dim + k]) eq--;
            }

            if(!eq) tot--;
        }
    }
    if (tot <= 0) {
        printf("UNORDERED middle: Outputs are equal\n");
    } else {
        printf("UNORDERED middle: Outputs differ! val:%d\n", tot);
    }

    tot = p.n_centers;
    for (unsigned int i = 0; i < p.n_centers; i++) {
        for (unsigned int j = 0; j < p.n_centers; j++) {
            int eq = p.dim;
            for (unsigned int k = 0; k < p.dim; k++) {
                if (C[i*p.dim + k] == H[j*p.dim + k]) eq--;
            }

            if(!eq) tot--;
        }
    }
    if (tot <= 0) {
        printf("UNORDERED final: Outputs are equal\n\n");
    } else {
        printf("UNORDERED final: Outputs differ! val:%d\n\n", tot);
    }


    //Stampo per verifica i vari centri calcolati dalle varie DPU. 
    printf("Host intermediate centers\n");
    print_points(M, p.n_centers*NR_DPUS, p.dim);

    printf("\nDPUs intermediate centers:\n");
    print_points(R, p.n_centers*NR_DPUS, p.dim);

    printf("\nHost final centers:\n");
    print_points(H, p.n_centers, p.dim);

    printf("\nDPUs final centers:\n");
    print_points(C, p.n_centers, p.dim);

    printf("\nCenters from each DPU:\n");
    DPU_FOREACH(dpu_set, dpu) {
        DPU_ASSERT(dpu_log_read(dpu, stdout));
    }
    
    free(H);
    free(M);
    free(C);
    free(R);
    free(P);
    DPU_ASSERT(dpu_free(dpu_set));

    return status ? 0 : -1;
}
