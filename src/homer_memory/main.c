#include "../homer_memory_API/homer_memory_API.h"

int main(int argc, char const *argv[]) {
  // montar la memoria

//printf("%s\n", argv[1]);
 mount_memory(argv[1]);

list_processes();
int slots = processes_slots();
printf("li %d\n",slots);
list_files(25);
  return 0;
}