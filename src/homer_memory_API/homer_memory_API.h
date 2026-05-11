#pragma once
#include "../homer_File/homer_File.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
typedef struct homerFile {
    uint8_t process_id;
    char file_name[15];
    char mode;
    int pcb_index;
    int file_index;
    uint64_t file_size;
    uint32_t virtual_addr;
    bool valid;
} homerFile;

/* ====== FUNCIONES GENERALES ====== */

void mount_memory(char *memory_path);
void unmount_memory(void);

void list_processes(void);
int processes_slots(void);
void list_files(int process_id);
void frame_bitmap_status(void);

int format_memory(char *memory_path);

/* ====== FUNCIONES PARA PROCESOS ====== */

int start_process(int process_id, char *process_name);
int finish_process(int process_id);
int clear_all_processes(void);
int file_table_slots(int process_id);

/* ====== FUNCIONES PARA ARCHIVOS ====== */

homerFile *open_file(int process_id, char *file_name, char mode);
int read_file(homerFile *file_desc, char *dest);
int write_file(homerFile *file_desc, char *src);
void delete_file(int process_id, char *file_name);
void close_file(homerFile *file_desc);