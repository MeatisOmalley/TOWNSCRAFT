/* DOS entry points that DOScraft's native tests call, mapped to the N64.
 * The world test's own file routing (world_test_open, __dpmi_int) stays
 * compiled but unused: terrain comes from src/n64/rom_hdd.c. */
void n64_hdd_shutdown(void);

void dos_hdd_shutdown(void) { n64_hdd_shutdown(); }

/* DOS commit, referenced by the test's unused __dpmi_int substitute. */
int _commit(int fd) { (void)fd; return 0; }
