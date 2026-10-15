/* Runs an unchanged Towns host test as an N64 ROM.
 *
 * The test keeps its own main(); the link wraps main and exit
 * (-Wl,--wrap=main,--wrap=exit) so this file can capture stdout and the exit
 * code. Results go to a fixed record in uncached RDRAM, which
 * tools/run_tests.py reads through ares's GDB server. */
#include <libdragon.h>
#include <system.h>     /* hook_stdio_calls; not part of libdragon.h */
#include <stdio.h>
#include <string.h>

#define LOG_CAPACITY (128 * 1024)

enum { STATE_IDLE = 0, STATE_RUNNING = 1, STATE_DONE = 2 };

/* Field order is read by tools/run_tests.py; keep in sync. */
typedef struct {
    uint32_t magic;          /* 'T64T' once the harness has started */
    int32_t state;
    int32_t exit_code;
    uint32_t log_len;        /* may exceed LOG_CAPACITY; extra bytes are dropped */
    uint32_t start_ticks;
    uint32_t end_ticks;
    uint32_t memory_bytes;
    uint32_t log_capacity;
    char log[LOG_CAPACITY];
} test_result_t;

test_result_t g_test_result __attribute__((aligned(16)));

static volatile test_result_t *result(void)
{
    return (volatile test_result_t *)UncachedAddr(&g_test_result);
}

static int capture(char *data, unsigned int len)
{
    volatile test_result_t *r = result();
    for (unsigned int i = 0; i < len; i++) {
        uint32_t at = r->log_len + i;
        if (at < LOG_CAPACITY)
            r->log[at] = data[i];
    }
    r->log_len += len;
    return len;
}

static void finish(int code)
{
    fflush(stdout);
    fflush(stderr);
    volatile test_result_t *r = result();
    r->end_ticks = get_ticks();
    r->exit_code = code;
    r->state = STATE_DONE;
    for (;;) {}
}

/* Tests that take a case name (argv[1]) get it from -DTEST_ARG="name". */
int __real_main(int argc, char **argv);
#ifdef TEST_ARG
static char *test_argv[] = { "test", TEST_ARG, 0 };
#else
static char *test_argv[] = { "test", 0 };
#endif

int __wrap_main(void)
{
    volatile test_result_t *r = result();
    r->log_len = 0;
    r->log_capacity = LOG_CAPACITY;
    r->memory_bytes = get_memory_size();
    r->exit_code = -1;
    r->start_ticks = get_ticks();
    r->state = STATE_RUNNING;
    r->magic = 0x54363454;
    hook_stdio_calls(&(stdio_t){ NULL, capture, capture });
    finish(__real_main(sizeof(test_argv) / sizeof(test_argv[0]) - 1, test_argv));
    return 0;
}

void __wrap_exit(int code)
{
    finish(code);
}
