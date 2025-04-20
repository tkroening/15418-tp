#include <stdio.h>
#include <getopt.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <trace.h>
#include <processor.h>
#include <unistd.h>
#include <libgen.h>

extern "C" {
    #include "config.h"
}

#include "engine.h"

int CADSS_VERBOSE = 0;
int processorCount = 1;

void printHelp(char* prog)
{
    printf("%s \n", prog);
    printf("  -h          \t Help message\n");
    printf("  -v          \t Verbose\n");
    printf("  -n <num>    \t Number of processors to simulate\n");
    printf("  -p <file>   \t Processor simulator\n");
    // printf("  -m <file>   \t Memory simulator\n");
    printf("  -t <file>   \t Trace file / directory\n");
    // printf("  -s <file>   \t Setting / configuration file\n");
}

//
// loadSim (name, type)
//    Attempts to load "name/libname.so"
//
struct sim* loadSim(char* name, char* type)
{
    char* baseName = NULL;
    char fullName[SIM_NAME_LIMIT] = {0};
    ssize_t len;

    // TODO
    //  - Support for alternate naming schemes
    //  - if debug == 1, try loading a -debug.so
    baseName = basename(name);

    len = snprintf(fullName, SIM_NAME_LIMIT, "%s/lib%s.so", name, baseName);
    if (len == SIM_NAME_LIMIT || len < 0)
    {
        fprintf(stderr,
                "Failed to generate so name for %s component using %s\n", type,
                name);

        return NULL;
    }

    void* handle = dlopen(fullName, RTLD_LAZY);
    if (handle == NULL)
    {
        fprintf(stderr, "Failed to load %s component using %s: %s\n", type,
                fullName, dlerror());
        return NULL;
    }

    struct sim* s = (struct sim *) malloc(sizeof(struct sim));
    if (s == NULL)
    {
        dlclose(handle);
        fprintf(stderr, "Failed to allocate space for %s component\n", type);
        return NULL;
    }

    s->handle = handle;
    s->init = (void *(*)(void *))dlsym(handle, "init");
    s->tick = (int (*)())dlsym(handle, "tick");
    s->finish = (int (*)(int))dlsym(handle, "finish");
    s->destroy = (int (*)())dlsym(handle, "destroy");
    s->CADSS_VERBOSE = (int*)dlsym(handle, "CADSS_VERBOSE");
    if (s->CADSS_VERBOSE != NULL)
    {
        *(s->CADSS_VERBOSE) = CADSS_VERBOSE;
    }

    int* pCount = (int *) dlsym(handle, "processorCount");
    if (pCount != NULL)
    {
        *pCount = processorCount;
    }

    if (s->init == NULL || s->tick == NULL || s->finish == NULL
        || s->destroy == NULL)
    {
        dlclose(handle);
        free(s);
        fprintf(stderr, "Failed to load interface for %s component\n", type);
        return NULL;
    }
    return s;
}

int main(int argc, char** argv)
{
    int opt;
    struct sim* csim = NULL;
    struct sim* psim = NULL;
    struct sim* bsim = NULL;
    struct sim* isim = NULL;
    struct sim* osim = NULL;
    struct sim* trace = NULL;
    struct sim* msim = NULL;
    char* settingFile = NULL;
    char* cacheName = NULL;
    char* branchName = NULL;
    char* procName = NULL;
    char* coherName = NULL;
    char* interName = NULL;
    char* memName = NULL;

    // TODO - switch to getopt_long that accepts -- arguments
    while ((opt = getopt(argc, argv, ":hvc:p:o:n:i:b:t:s:m:d:")) != -1)
    {
        switch (opt)
        {
            case 'h':
                printHelp(argv[0]);
                return 0;
            case 'p':
                procName = optarg;
                break;
            case 'v':
                CADSS_VERBOSE = 1;
                break;
            case 's':
                settingFile = optarg;
                break;
            case 'n':
                processorCount = atoi(optarg);
                break;
        }
    }

    trace = loadSim("trace", "trace");
    trace_sim_args tsa;
    tsa.arg_count = argc;
    tsa.arg_list = argv;
    optind = 1;
    trace_reader *tr = (trace_reader *) trace->init(&tsa);

    // TODO: Config files are disabled for now
    // if (settingFile == NULL)
    // {
    //     fprintf(stderr, "No setting file specified, using default.config\n");
    //     settingFile = "default.config";
    // }
    // if (openSettings(settingFile) != 0)
    // {
    //     fprintf(stderr, "Failed to open setting file - %s\n", settingFile);
    //     return 0;
    // }

    if (procName == NULL)
    {
        psim = loadSim("processor", "processor");
    }
    else
    {
        psim = loadSim(procName, "processor");
        if (psim == NULL)
        {
            return 0;
        }
    }

    // B.N. - set optind to 1 before calling init on any component
    //  that resets getopt() so the component can use it safely on its arguments
    int argCount = 0;
    char** arg = NULL;

    processor* proc_sim = NULL;

    optind = 1;
    // arg = getSettings("processor", &argCount);
    if (arg == NULL) {}

    processor_sim_args psa;
    // psa.arg_count = argCount;
    // psa.arg_list = arg;
    psa.tr = tr;
    // psa.cache_sim = cache_sim;
    // psa.branch_sim = branch_sim;
    if ((proc_sim = (processor *) psim->init(&psa)) == NULL)
    {
        printf("Failed to initialize processor!\n");
        assert(0);
    }

    // Main sim loop
    int progress = 0;
    int dbgHalt;
    int64_t dbgTickCount = 0;

    do
    {
        // Processor requests trace ops as needed.
        progress = psim->tick();
        dbgTickCount++;
    } while (progress);

    psim->finish(STDOUT_FILENO);
    psim->destroy();
    trace->destroy();

    dlclose(psim->handle);
    free(psim);
    dlclose(trace->handle);
    free(trace);

    return 0;
}
