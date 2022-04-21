#ifndef _PARAMS_H
#define _PARAMS_H


struct Params {
    uint32_t n_points;
    uint32_t n_centers;
    uint32_t dim;
};

static void usage() {
    fprintf(stderr,
            "\nUsage:  ./program [options]"
            "\n"
            "\nBenchmark-specific options:"
            "\n    -n <I>    n_points (default=1000 elements)"
            "\n    -k <I>    n_centers (default=15 elements)"
            "\n    -d <I>    dim (default=2)"
            "\n");
}

struct Params input_params(int argc, char **argv) {
    struct Params p;
    p.n_points = 4000;
    p.n_centers = 15;
    p.dim = 2;

    int opt;
    while((opt = getopt(argc, argv, "hn:k:d:")) != -1) {
        switch (opt) {
            case 'h':
                usage();
                exit(0);
            case 'n': p.n_points    = atoi(optarg); break;
            case 'k': p.n_centers   = atoi(optarg); break;
            case 'd': p.dim         = atoi(optarg); break;
            default:
                fprintf(stderr, "\nUrecognized option!\n");
                usage();
                exit(0);
        }
    }

    return p;
}

#endif