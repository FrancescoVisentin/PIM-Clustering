#ifndef _PARAMS_H
#define _PARAMS_H


struct Params {
    uint32_t n_warmup;
    uint32_t n_reps;

    uint32_t n_points;
    uint32_t n_centers;
    uint32_t dim;
    bool rnd_first;
};

static void usage() {
    fprintf(stderr,
            "\nUsage:  ./program [options]"
            "\n"
            "\nGeneral options:"
            "\n    -h        help"
            "\n    -w <W>    # of untimed warmup iterations (default=1)"
            "\n    -e <E>    # of timed repetition iterations (default=3)"
            "\n"    
            "\nBenchmark-specific options:"
            "\n    -n <N>    n_points (default=4000 elements)"
            "\n    -k <K>    n_centers (default=15 elements)"
            "\n    -d <D>    dim (default=2)"
            "\n");
}

struct Params input_params(int argc, char **argv) {
    struct Params p;
    p.n_points = 4000;
    p.n_centers = 15;
    p.dim = 2;

    p.n_warmup = 1;
    p.n_reps = 3;
    p.rnd_first = false;

    int opt;
    while((opt = getopt(argc, argv, "hrw:e:n:k:d:")) != -1) {
        switch (opt) {
            case 'h':
                usage();
                exit(0);
            case 'w': p.n_warmup    = atoi(optarg); break;
            case 'e': p.n_reps      = atoi(optarg); break;
            case 'n': p.n_points    = atoi(optarg); break;
            case 'k': p.n_centers   = atoi(optarg); break;
            case 'd': p.dim         = atoi(optarg); break;
            case 'r': p.rnd_first   = true; break;
            default:
                fprintf(stderr, "\nUrecognized option!\n");
                usage();
                exit(0);
        }
    }

    return p;
}

#endif