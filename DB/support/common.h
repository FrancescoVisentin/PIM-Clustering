#ifndef _COMMON_H
#define _COMMON_H

//Se impostato ad 1 le DPU stampano i centri calcolati.
#define PRINT 0

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

};

#if defined(FLOAT) || defined(DOUBLE)
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
#else
    //Calcola |a-b|^exp usando l'esponenziazione per quadratura.
    D power(T a, T b, uint32_t exp) {
        T base = (a >= b) ? a-b : b-a;
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
#endif

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
                    printf("%f", points_set[i*dim + k]);
                    continue;
                }
                printf(", %f", points_set[i*dim + k]);
            }
            printf("]\n");
        }
}

#endif