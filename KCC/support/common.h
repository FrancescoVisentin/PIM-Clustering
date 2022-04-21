#ifndef _COMMON_H
#define _COMMON_H

//TODO: Define usati per testing da pulire
/*
#define T uint32_t
#define NR_DPUS 4
#define NR_TASKLETS 8
#define BLOCK_SIZE 256
*/

//Definisco i vari tipi.
#ifdef UINT32
#define T uint32_t
#elif UINT64
#define T uint64_t
#elif INT32
#define T int32_t
#elif INT64
#define T int64_t
#elif FLOAT
#define T float
#elif DOUBLE
#define T double
#elif CHAR
#define T char
#elif SHORT
#define T short
#endif

//Arrotonda "n" al successivo multiplo di "m".
#define roundup(n,m) ((n/m)*m + m)

//Struttura per passaggio parametri tra host e le DPU.
struct dpu_arguments_t {
    uint32_t n_points_dpu_i;
    uint32_t n_centers;
    uint32_t dim;
    uint32_t mem_size;

};

//Calcola |a-b|^exp usando l'esponenziazione per quadratura.
uint32_t power(uint32_t a, uint32_t b, uint32_t exp) {
    uint32_t base = (a >= b) ? a-b : b-a;
    uint32_t res = 1;
    while (exp > 0) {
        if (exp & 1) {
            res *= base;
        }
        exp >>= 1;
        base *= base;
    }

    return res;
}

//Ritorna il multiplo di point_dim più vicino ad BLOCK_SIZE ed divisibile per 8.
uint32_t get_block_size(uint32_t point_dim) {
    
    if ((BLOCK_SIZE % point_dim) == 0) {
        return BLOCK_SIZE;
    }
    
    uint32_t lcm = (point_dim > 8) ? point_dim : 8;

    while (true) {
        if ((lcm % 8 == 0) && (lcm % point_dim == 0)) break;

        lcm++;
    }
    
    for(uint32_t tmp=lcm; lcm < BLOCK_SIZE; lcm+=tmp);
    
    return lcm;
}

void print_points(T* points_set, uint32_t n_centers, uint32_t dim) {
    for (unsigned int i=0; i < n_centers; i++) {
            printf("Centro: %d [", i);
            for (unsigned int k = 0; k < dim; k++) {
                if(k == 0) {
                    printf("%d", points_set[i*dim + k]);
                    continue;
                }
                printf(", %d", points_set[i*dim + k]);
            }
            printf("]\n");
        }
}

#endif