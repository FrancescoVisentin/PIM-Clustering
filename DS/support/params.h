#ifndef _PARAMS_H
#define _PARAMS_H


struct Params {
    char *ds;

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
            "\n    -k <K>    n_centers (default=15 elements)"
            "\n    -p <P>    full path to database"
            "\n");
}

struct Params input_params(int argc, char **argv) {
    struct Params p;
    p.n_points = 0;
    p.n_centers = 10;
    p.dim = 0;

    p.n_warmup = 1;
    p.n_reps = 3;
    p.rnd_first = false;

    int opt;
    while((opt = getopt(argc, argv, "hrw:e:k:p:")) != -1) {
        switch (opt) {
            case 'h':
                usage();
                exit(0);
            case 'w': p.n_warmup    = atoi(optarg); break;
            case 'e': p.n_reps      = atoi(optarg); break;
            case 'k': p.n_centers   = atoi(optarg); break;
            case 'p': p.ds          = optarg; break;
            case 'r': p.rnd_first   = true; break;
            default:
                fprintf(stderr, "\nUrecognized option!\n");
                usage();
                exit(0);
        }
    }

    if (p.n_points == 0){
        FILE *ds = fopen(p.ds, "r");

        if (!ds) {
        fprintf(stderr, "Unable to open file.\n");
        return p;
        }

        int nun_lines = 0;
        int dim = 1;
        for (char c = fgetc(ds); c != EOF; c = fgetc(ds)) {
            if (c == '\n') nun_lines++;
            if (nun_lines == 0 && c == ',') dim++;
        }

        p.n_points = nun_lines;
        p.dim = dim;
        fclose(ds);
    }

    return p;
}

#endif