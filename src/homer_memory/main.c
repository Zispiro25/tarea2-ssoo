#include <stdio.h>
#include <stdlib.h>
#include "../homer_memory_API/homer_memory_API.h"

int main(int argc, char const *argv[]) {
  //format_memory((char *)argv[1]);
  mount_memory((char *)argv[1]);

  start_process(10, "ventas");
  start_process(20, "reportes");
  list_processes();

  int pcb_libres = processes_slots();
  printf("PCB free slots: %d\n", pcb_libres);

  int slots_arch = file_table_slots(10);
  printf("File-table free slots (pid=10): %d\n", slots_arch);

  homerFile* f = open_file(10, "dino.jpg", 'w'); 
  write_file(f, "dino.jpg");

  f = open_file(10, "dino.jpg", 'r'); 
  read_file(f, "dino_copia.jpg");
  close_file(f);

  list_files(10);
  frame_bitmap_status();

  delete_file(10, "dino.jpg");
  finish_process(10); 

  int cerrados = clear_all_processes();
  printf("Procesos cerrados por clear_all_processes(): %d\n", cerrados);

  unmount_memory();
  return 0;
}