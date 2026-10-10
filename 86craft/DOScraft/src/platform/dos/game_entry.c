/* Real pinned kmain; ordinary DJGPP CRT replaces the Towns boot entry. */
#include <stdio.h>
void kmain(void);
void dos_game_options(int argc,char **argv);
int main(int argc,char **argv)
{
    dos_game_options(argc,argv);
    puts("DOScraft direct port: silent audio checkpoint, no paging.");
    kmain();
    return 0;
}
