#ifndef _COMMON_H
#define _COMMON_H

//Definisco i vari tipi.
#ifdef UINT32
#define T uint32_t
#define T_SHIFT 2
#define D uint64_t
#elif UINT64
#define T uint64_t
#define T_SHIFT 3
#define D uint64_t
#elif INT32
#define T int32_t
#define T_SHIFT 2
#define D uint64_t
#elif INT64
#define T int64_t
#define T_SHIFT 3
#define D uint64_t
#elif FLOAT
#define T float
#define T_SHIFT 2
#define D double
#elif DOUBLE
#define T double
#define T_SHIFT 3
#define D double
#elif CHAR
#define T char
#define T_SHIFT 0
#define D uint64_t
#elif SHORT
#define T short
#define T_SHIFT 1
#define D uint64_t
#endif

//Arrotonda "n" al successivo multiplo di "m".
#define roundup(n,m) ((n/m)*m + m)

//Struttura per passaggio parametri tra host e le DPU.
struct dpu_arguments_t {
    uint32_t n_points_dpu_i;
    uint32_t n_centers;
    uint32_t dim;
    uint32_t mem_size;
    uint32_t first_center_offset;

};

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


//Funzioni adattate al tipo run time.
#if defined(FLOAT) || defined(DOUBLE)
    #define INIT_VAL 18446744073709551616.0

    D f_pow(T base, uint32_t exp){
        D temp;
        if(exp == 0) 
            return 1; 
        temp = f_pow(base, exp / 2); 
        if (exp & 1)
            return base * temp * temp;
        else
            return temp * temp;
    }

    //Calcola |a-b|^exp.
    D power(T a, T b, uint32_t exp) {
        D base = (a > b) ? a-b : b-a;
        return f_pow(base, exp);
    }

    void print_points(T* points_set, uint32_t n_centers, uint32_t dim) {
    for (unsigned int i=0; i < n_centers; i++) {
            printf("Centro: %d [", i);
            for (unsigned int k = 0; k < dim; k++) {
                if(k == 0) {
                    printf("%f", points_set[i*dim + k]);
                    continue;
                }
                printf(", %f", points_set[i*dim + k]);
            }
            printf("]\n");
        }
    }

    //Wrapper di printf per stampare i risultati finali.
    void print_res(bool s, unsigned int r, double dpu_cost, double cpu_cost) {
        if(s) printf("Rep %d: Outputs are equal\t DPU cost: %f\t Linear cost: %f\n", r, dpu_cost, cpu_cost);
        else  printf("Rep %d: Outputs are different!\t DPU cost: %f\t Linear cost: %f\n", r, dpu_cost, cpu_cost);
    }
#else
    #define _INIT_VAL_(c) c ## ULL
    #define INIT_VAL (_INIT_VAL_(18446744073709551615))

    //Calcola |a-b|^exp usando l'esponenziazione per quadratura.
    D power(T a, T b, uint32_t exp) {
        D base = (a >= b) ? a-b : b-a;
        D res = 1;
        while (exp > 0) {
            if (exp & 1) {
                res *= base;
            }
            exp >>= 1;
            base *= base;
        }

        return res;
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

    //Wrapper di printf per stampare i risultati finali.
    void print_res(bool s, unsigned int r, uint64_t dpu_cost, uint64_t cpu_cost) {
        if(s) printf("Rep %d: Outputs are equal\t DPU cost: %lu\t Linear cost: %lu\n", r, dpu_cost, cpu_cost);
        else  printf("Rep %d: Outputs are different!\t DPU cost: %lu\t Linear cost: %lu\n", r, dpu_cost, cpu_cost);
    }
#endif

#endif