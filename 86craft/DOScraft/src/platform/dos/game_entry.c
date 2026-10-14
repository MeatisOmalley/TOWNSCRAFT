/* Real pinned kmain; ordinary DJGPP CRT replaces the Towns boot entry. */
#include <stdio.h>
void kmain(void);
void dos_game_options(int argc,char **argv);
#ifdef DOS_MEM_REPORT
#include <dpmi.h>
unsigned long dos_mem_entry_free_pages;
#endif
int main(int argc,char **argv)
{
#ifdef DOS_MEM_REPORT
    __dpmi_free_mem_info entry;
    if (!__dpmi_get_free_memory_information(&entry)) dos_mem_entry_free_pages = entry.total_number_of_free_pages;
#endif
    dos_game_options(argc,argv);
    puts("DOScraft direct port: silent audio checkpoint, no paging.");
    kmain();
    return 0;
}
