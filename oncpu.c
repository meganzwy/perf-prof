#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <monitor.h>
#include <tep.h>


static profiler oncpu;

struct perins_cpumap {
    int nr;
    int map[0];
};

static struct oncpu_ctx {
    int nr_ins;
    int nr_cpus;
    int size_perins_cpumap;
    struct perins_cpumap *maps;
    struct perins_cpumap *all_ins;
    struct env *env;
} ctx;

static int oncpu_init(struct perf_evlist *evlist, struct env *env)
{
    struct perf_event_attr attr = {
        .type          = PERF_TYPE_TRACEPOINT,
        .config        = 0,
        .size          = sizeof(struct perf_event_attr),
        .sample_period = 1000000,
        .sample_type   = PERF_SAMPLE_TID | PERF_SAMPLE_CPU,
        .read_format   = 0,
        .pinned        = 1,
        .disabled      = 1,
        .watermark     = 1,
        .wakeup_watermark = (oncpu.pages << 12) / 2,
    };
    struct perf_evsel *evsel;

    if (monitor_instance_oncpu()) {
        fprintf(stderr, "Need to specify -p PID parameter\n");
        return -1;
    }
    if (!env->interval)
        env->interval = 1000;

    ctx.env = env;
    ctx.nr_ins = monitor_nr_instance();
    ctx.nr_cpus = get_present_cpus();
    ctx.size_perins_cpumap = sizeof(struct perins_cpumap) + ctx.nr_cpus * sizeof(int);
    ctx.maps = malloc((ctx.nr_ins + 1) * ctx.size_perins_cpumap);
    if (!ctx.maps)
        return -1;
    ctx.all_ins = (void *)ctx.maps + ctx.nr_ins * ctx.size_perins_cpumap;
    memset(ctx.maps, 0, (ctx.nr_ins + 1) * ctx.size_perins_cpumap);

    attr.config = tep__event_id("sched", "sched_stat_runtime");
    evsel = perf_evsel__new(&attr);
    if (!evsel) {
        return -1;
    }
    perf_evlist__add(evlist, evsel);
    return 0;
}

static void oncpu_exit(struct perf_evlist *evlist)
{
    free(ctx.maps);
}

static void print_cpumap(int ins, struct perins_cpumap *map)
{
    int i;

    if (!map->nr)
        return;

    if (ins >= 0) {
        printf("[%6d] ", monitor_instance_thread(ins));
    }
    for (i = 0; i < ctx.nr_cpus; i++) {
        if (map->map[i] > 0)
            printf("%d ", i);
    }
    printf("\n");
}

static void oncpu_interval(void)
{
    int i;

    print_time(stdout);
    printf("\n");
    if (ctx.env->perins) {
        printf("[THREAD] [CPUS]\n");
        for (i = 0; i < ctx.nr_ins; i++) {
            print_cpumap(i, (void *)ctx.maps + i * ctx.size_perins_cpumap);
        }
    } else {
        print_cpumap(-1, ctx.all_ins);
    }
    memset(ctx.maps, 0, (ctx.nr_ins + 1) * ctx.size_perins_cpumap);
}

static void oncpu_sample(union perf_event *event, int instance)
{
    // in linux/perf_event.h
    // PERF_SAMPLE_TID | PERF_SAMPLE_CPU
    struct sample_type_data {
        struct {
            __u32    pid;
            __u32    tid;
        }    tid_entry;
        struct {
            __u32    cpu;
            __u32    reserved;
        }    cpu_entry;
    } *data = (void *)event->sample.array;
    struct perins_cpumap *map = (void *)ctx.maps + instance * ctx.size_perins_cpumap;
    map->nr++;
    map->map[data->cpu_entry.cpu]++;
    ctx.all_ins->nr++;
    ctx.all_ins->map[data->cpu_entry.cpu]++;
}

static profiler oncpu = {
    .name = "oncpu",
    .pages = 4,
    .init = oncpu_init,
    .deinit = oncpu_exit,
    .interval = oncpu_interval,
    .sample = oncpu_sample,
};
PROFILER_REGISTER(oncpu)


