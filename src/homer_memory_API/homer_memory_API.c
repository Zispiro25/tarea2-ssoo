#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include "homer_memory_API.h"

#define PCB_OFFSET 0L
#define PCB_COUNT 32
#define PCB_ENTRY_SIZE 256

#define IPT_OFFSET (8L * 1024L)
#define IPT_ENTRIES 65536
#define IPT_ENTRY_SIZE 3

#define BITMAP_OFFSET (PCB_OFFSET + (8L * 1024L) + (192L * 1024L))
#define BITMAP_SIZE (8L * 1024L)

#define DATA_OFFSET (BITMAP_OFFSET + BITMAP_SIZE)

#define FILE_TABLE_OFFSET 16
#define FILE_ENTRY_SIZE 24
#define FILE_ENTRY_COUNT 10

#define FRAME_SIZE (32L * 1024L)

char *path = NULL;
FILE *archivo = NULL;

static int abrir_memoria_global_modo(const char *modo) {
    if (path == NULL) return -1;

    if (archivo != NULL) {
        fclose(archivo);
        archivo = NULL;
    }

    archivo = fopen(path, modo);
    if (archivo == NULL) return -1;

    return 0;
}

static int liberar_paginas_proceso(FILE *f, uint8_t process_id) {
    for (int pfn = 0; pfn < IPT_ENTRIES; pfn++) {
        long entry_offset = IPT_OFFSET + (long)pfn * IPT_ENTRY_SIZE;

        if (fseek(f, entry_offset, SEEK_SET) != 0) return -1;

        unsigned char b[3];
        if (fread(b, 1, 3, f) != 3) return -1;

        uint32_t entry = ((uint32_t)b[0]) |
                         ((uint32_t)b[1] << 8) |
                         ((uint32_t)b[2] << 16);

        uint8_t valid = (entry >> 23) & 0x01;
        uint16_t pid = (entry >> 13) & 0x03FF;

        if (valid == 1 && pid == process_id) {
            entry = 0;

            b[0] = (unsigned char)(entry & 0xFF);
            b[1] = (unsigned char)((entry >> 8) & 0xFF);
            b[2] = (unsigned char)((entry >> 16) & 0xFF);

            if (fseek(f, entry_offset, SEEK_SET) != 0) return -1;
            if (fwrite(b, 1, 3, f) != 3) return -1;
        }
    }

    return 0;
}

static int limpiar_pcb(FILE *f, int pcb_index) {
    unsigned char vacio[PCB_ENTRY_SIZE] = {0};
    long entry_offset = PCB_OFFSET + (long)pcb_index * PCB_ENTRY_SIZE;

    if (fseek(f, entry_offset, SEEK_SET) != 0) return -1;
    if (fwrite(vacio, 1, PCB_ENTRY_SIZE, f) != PCB_ENTRY_SIZE) return -1;

    return 0;
}

static int terminar_pcb_por_indice(FILE *f, int pcb_index) {
    long entry_offset = PCB_OFFSET + (long)pcb_index * PCB_ENTRY_SIZE;

    if (fseek(f, entry_offset, SEEK_SET) != 0) return -1;

    unsigned char estado;
    if (fread(&estado, 1, 1, f) != 1) return -1;
    if (estado != 0x01) return -1;

    if (fseek(f, entry_offset + 15, SEEK_SET) != 0) return -1;

    unsigned char pid;
    if (fread(&pid, 1, 1, f) != 1) return -1;

    if (liberar_paginas_proceso(f, pid) != 0) return -1;
    if (limpiar_pcb(f, pcb_index) != 0) return -1;

    return 0;
}

void mount_memory(char *memory_path) {
    if (archivo != NULL) {
        fclose(archivo);
        archivo = NULL;
    }

    free(path);
    path = NULL;

    archivo = fopen(memory_path, "rb+");
    if (archivo == NULL) {
        perror("fopen");
        return;
    }

    path = malloc(strlen(memory_path) + 1);
    if (path == NULL) {
        perror("malloc");
        fclose(archivo);
        archivo = NULL;
        return;
    }

    strcpy(path, memory_path);
}

void unmount_memory(void) {
    if (archivo != NULL) {
        fclose(archivo);
        archivo = NULL;
    }

    free(path);
    path = NULL;
}

void list_processes(void) {
    if (archivo == NULL) {
        printf("Error: archivo no abierto\n");
        return;
    }

    rewind(archivo);

    for (int i = 0; i < PCB_COUNT; i++) {
        unsigned char estado;
        char nombre[15];
        unsigned char id;
        unsigned char tabla[240];

        if (fread(&estado, 1, 1, archivo) != 1) return;
        if (fread(nombre, 1, 14, archivo) != 14) return;
        nombre[14] = '\0';
        if (fread(&id, 1, 1, archivo) != 1) return;
        if (fread(tabla, 1, 240, archivo) != 240) return;

        if (estado == 0x01) {
            printf("id %u nombre: %s\n", (unsigned int)id, nombre);
        }
    }
}

int processes_slots(void) {
    if (archivo == NULL) {
        printf("Error: archivo no abierto\n");
        return -1;
    }

    rewind(archivo);
    int entradas = 0;

    for (int i = 0; i < PCB_COUNT; i++) {
        unsigned char estado;
        unsigned char resto[255];

        if (fread(&estado, 1, 1, archivo) != 1) return -1;
        if (fread(resto, 1, 255, archivo) != 255) return -1;

        if (estado == 0x00) entradas++;
    }

    return entradas;
}

void list_files(int process_id) {
    if (archivo == NULL) {
        printf("Error: archivo no abierto\n");
        return;
    }

    rewind(archivo);

    for (int i = 0; i < PCB_COUNT; i++) {
        unsigned char estado;
        char nombre[15];
        unsigned char id;
        unsigned char tabla[240];

        if (fread(&estado, 1, 1, archivo) != 1) return;
        if (fread(nombre, 1, 14, archivo) != 14) return;
        nombre[14] = '\0';
        if (fread(&id, 1, 1, archivo) != 1) return;
        if (fread(tabla, 1, 240, archivo) != 240) return;

        if (estado == 0x01 && id == (unsigned char)process_id) {
            for (int x = 0; x < FILE_ENTRY_COUNT; x++) {
                int base = FILE_ENTRY_SIZE * x;

                if (tabla[base] == 0x01) {
                    char nombre_archivo[15];
                    memcpy(nombre_archivo, &tabla[base + 1], 14);
                    nombre_archivo[14] = '\0';

                    uint64_t tamano_archivo =
                        ((uint64_t)tabla[base + 15]) |
                        ((uint64_t)tabla[base + 16] << 8) |
                        ((uint64_t)tabla[base + 17] << 16) |
                        ((uint64_t)tabla[base + 18] << 24) |
                        ((uint64_t)tabla[base + 19] << 32);

                    uint32_t dir_v =
                        ((uint32_t)tabla[base + 20]) |
                        ((uint32_t)tabla[base + 21] << 8) |
                        ((uint32_t)tabla[base + 22] << 16) |
                        ((uint32_t)tabla[base + 23] << 24);

                    uint32_t offset = dir_v & 0x7FFF;
                    uint32_t vpn = (dir_v >> 15) & 0x0FFF;

                    printf("vpn: %" PRIu32
                           " offset: %" PRIu32
                           " tamaño archivo: %" PRIu64
                           " direccion virtual: %" PRIu32
                           " nombre: %s\n",
                           vpn, offset, tamano_archivo, dir_v, nombre_archivo);
                }
            }
            return;
        }
    }
}

void frame_bitmap_status(void) {
    if (archivo == NULL) {
        printf("Error: archivo no abierto\n");
        return;
    }

    if (fseek(archivo, BITMAP_OFFSET, SEEK_SET) != 0) return;

    unsigned int usados = 0;

    for (int i = 0; i < BITMAP_SIZE; i++) {
        unsigned char byte;
        if (fread(&byte, 1, 1, archivo) != 1) return;

        for (int b = 0; b < 8; b++) {
            usados += (byte >> b) & 1U;
        }
    }

    unsigned int libres = 65536U - usados;
    printf("libres: %u usados: %u\n", libres, usados);
}

int format_memory(char *memory_path) {
    unsigned long long pcb_size = 8ULL * 1024ULL;
    unsigned long long tabla_inv_size = 192ULL * 1024ULL;
    unsigned long long bitmap_size = 8ULL * 1024ULL;
    unsigned long long data_size = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    unsigned long long memory_size = pcb_size + tabla_inv_size + bitmap_size + data_size;

    FILE *f = fopen(memory_path, "wb+");
    if (f == NULL) return -1;

    if (fseek(f, (long)(memory_size - 1ULL), SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }

    if (fputc(0, f) == EOF) {
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

int start_process(int process_id, char *process_name) {
    if (path == NULL) return -1;
    if (process_id < 0 || process_id > 255) return -1;
    if (process_name == NULL) return -1;

    FILE *f = fopen(path, "rb+");
    if (!f) return -1;

    for (int i = 0; i < PCB_COUNT; i++) {
        long entry_offset = PCB_OFFSET + (long)i * PCB_ENTRY_SIZE;

        if (fseek(f, entry_offset, SEEK_SET) != 0) {
            fclose(f);
            return -1;
        }

        unsigned char state;
        if (fread(&state, 1, 1, f) != 1) {
            fclose(f);
            return -1;
        }

        if (state == 0x00) {
            if (fseek(f, entry_offset, SEEK_SET) != 0) {
                fclose(f);
                return -1;
            }

            unsigned char state_out = 0x01;
            unsigned char name[14] = {0};
            unsigned char pid = (unsigned char)process_id;
            unsigned char zeros[240] = {0};

            strncpy((char *)name, process_name, 14);

            if (fwrite(&state_out, 1, 1, f) != 1) {
                fclose(f);
                return -1;
            }
            if (fwrite(name, 1, 14, f) != 14) {
                fclose(f);
                return -1;
            }
            if (fwrite(&pid, 1, 1, f) != 1) {
                fclose(f);
                return -1;
            }
            if (fwrite(zeros, 1, 240, f) != 240) {
                fclose(f);
                return -1;
            }

            fclose(f);
            if (abrir_memoria_global_modo("rb+") != 0) return -1;
            return 0;
        }
    }

    fclose(f);
    return -1;
}

int finish_process(int process_id) {
    if (path == NULL) return -1;

    FILE *f = fopen(path, "rb+");
    if (!f) return -1;

    for (int i = 0; i < PCB_COUNT; i++) {
        long entry_offset = PCB_OFFSET + (long)i * PCB_ENTRY_SIZE;

        if (fseek(f, entry_offset, SEEK_SET) != 0) {
            fclose(f);
            return -1;
        }

        unsigned char estado;
        if (fread(&estado, 1, 1, f) != 1) {
            fclose(f);
            return -1;
        }

        if (estado == 0x01) {
            if (fseek(f, entry_offset + 15, SEEK_SET) != 0) {
                fclose(f);
                return -1;
            }

            unsigned char pid;
            if (fread(&pid, 1, 1, f) != 1) {
                fclose(f);
                return -1;
            }

            if (pid == (unsigned char)process_id) {
                if (liberar_paginas_proceso(f, (unsigned char)process_id) != 0) {
                    fclose(f);
                    return -1;
                }

                if (limpiar_pcb(f, i) != 0) {
                    fclose(f);
                    return -1;
                }

                fclose(f);
                if (abrir_memoria_global_modo("rb+") != 0) return -1;
                return 0;
            }
        }
    }

    fclose(f);
    return -1;
}

int clear_all_processes(void) {
    if (path == NULL) return -1;

    FILE *f = fopen(path, "rb+");
    if (!f) return -1;

    int terminados = 0;

    for (int i = 0; i < PCB_COUNT; i++) {
        long entry_offset = PCB_OFFSET + (long)i * PCB_ENTRY_SIZE;

        if (fseek(f, entry_offset, SEEK_SET) != 0) {
            fclose(f);
            return -1;
        }

        unsigned char estado;
        if (fread(&estado, 1, 1, f) != 1) {
            fclose(f);
            return -1;
        }

        if (estado == 0x01) {
            if (terminar_pcb_por_indice(f, i) != 0) {
                fclose(f);
                return -1;
            }
            terminados++;
        }
    }

    fclose(f);
    if (abrir_memoria_global_modo("rb+") != 0) return -1;
    return terminados;
}

int file_table_slots(int process_id) {
    if (path == NULL) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    for (int i = 0; i < PCB_COUNT; i++) {
        long pcb_offset = PCB_OFFSET + (long)i * PCB_ENTRY_SIZE;

        if (fseek(f, pcb_offset, SEEK_SET) != 0) {
            fclose(f);
            return -1;
        }

        unsigned char estado;
        if (fread(&estado, 1, 1, f) != 1) {
            fclose(f);
            return -1;
        }

        if (estado != 0x01) continue;

        if (fseek(f, pcb_offset + 15, SEEK_SET) != 0) {
            fclose(f);
            return -1;
        }

        unsigned char pid;
        if (fread(&pid, 1, 1, f) != 1) {
            fclose(f);
            return -1;
        }

        if (pid == (unsigned char)process_id) {
            int libres = 0;

            for (int j = 0; j < FILE_ENTRY_COUNT; j++) {
                long file_entry_offset = pcb_offset + FILE_TABLE_OFFSET + (long)j * FILE_ENTRY_SIZE;

                if (fseek(f, file_entry_offset, SEEK_SET) != 0) {
                    fclose(f);
                    return -1;
                }

                unsigned char validez;
                if (fread(&validez, 1, 1, f) != 1) {
                    fclose(f);
                    return -1;
                }

                if (validez == 0x00) libres++;
            }

            fclose(f);
            return libres;
        }
    }

    fclose(f);
    return -1;
}

//funciones de escritura
int search_pcb_index(int process_id){
    if (archivo == NULL){
        return -1;
    }
    for (int i = 0; i < PCB_COUNT; i++){
        long pcb_addr = PCB_OFFSET + (i * PCB_ENTRY_SIZE);

        fseek(archivo, pcb_addr, SEEK_SET);
        uint8_t valid;
        fread(&valid, 1, 1, archivo);
        
        fseek(archivo, pcb_addr + 15, SEEK_SET);
        uint8_t id;
        fread(&id, 1, 1, archivo);

        if (valid == 0x01 && id == (uint8_t)process_id){
            return i;
        }
    }
    return -1;
}

int search_file_index(int pcb_idx, char* file_name){
    long file_table_addr = PCB_OFFSET + (pcb_idx * PCB_ENTRY_SIZE) + FILE_TABLE_OFFSET;
    for (int i = 0; i < FILE_ENTRY_COUNT; i++){
        fseek(archivo, file_table_addr + (i * FILE_ENTRY_SIZE), SEEK_SET);
        uint8_t valid;
        fread(&valid, 1, 1, archivo);

        char current_name[15];
        fread(current_name, 1, 14, archivo);
        current_name[14] = '\0';
        if(valid == 0x01 && strcmp(current_name, file_name) == 0){
            return i;
        }
    }
    return -1;
}

void update_ipt_entry(int process_id, uint32_t vpn, uint32_t pfn, bool valid){
    uint32_t new_entry = 0;
    if (valid){
        new_entry |= (1U << 23);
    }
    new_entry |= ((uint32_t)process_id << 13);
    new_entry |= vpn;
    uint8_t b[3];
    b[0] = new_entry & 0xFF;
    b[1] = (new_entry >> 8) & 0xFF;
    b[2] = (new_entry >> 16) & 0xFF;
    fseek(archivo, IPT_OFFSET + (pfn * IPT_ENTRY_SIZE), SEEK_SET);
    fwrite(b, 1, 3, archivo);
}

uint32_t get_physical_address(int process_id, uint32_t virtual_address) {
    uint32_t vpn = virtual_address >> 15 & 0x0FFF;
    uint32_t offset = virtual_address & 0x7FFF;
    for (uint32_t pfn = 0; pfn < IPT_ENTRIES; pfn++){
        long ipt_entry_offset = IPT_OFFSET + (pfn * IPT_ENTRY_SIZE);
        fseek(archivo, ipt_entry_offset, SEEK_SET);
        unsigned char b[3];
        fread(b, 1, 3, archivo);
        uint32_t entry = ((uint32_t)b[0]) | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16);
        uint8_t valid = (entry >> 23) & 0x01;
        uint16_t pid_entry = (entry >> 13) & 0x3ff;
        uint16_t vpn_entry = entry & 0x1FFF;
        if (valid == 1 && pid_entry == (uint16_t)process_id && (vpn_entry & 0x0FFF) == vpn){
            uint32_t paddr_rel = (pfn << 15) | offset;
            uint32_t headers_size = (8 + 192 + 8) * 1024; //Por la tabla de PCBs, tabla invertida y bitmap
            // Asi devolvemos siempre en area de datos
            return headers_size + paddr_rel;
        }
    }
    return 0xFFFFFFFF;
}

homerFile* open_file(int process_id, char* file_name, char mode){
    // Revisar si el archivo existe
    if (archivo == NULL){
        return NULL;
    }

    int pcb_idx = search_pcb_index(process_id);

    // Revisar si el proceso existe
    if (pcb_idx == -1){
        return NULL;
    }

    int found_idx = search_file_index(pcb_idx, file_name);
    long file_table_addr = PCB_OFFSET + (pcb_idx * PCB_ENTRY_SIZE) + FILE_TABLE_OFFSET;
    if (mode == 'r'){
        if (found_idx == -1){
            return NULL;
        }

        // Se salta el bit de validez y de nombre
        fseek(archivo, file_table_addr + (found_idx * FILE_ENTRY_SIZE) + 15, SEEK_SET);
        
        // Se calcula el tamaño del archivo
        uint8_t size_bytes[5];
        fread(size_bytes, 1, 5, archivo);
        uint64_t file_size = 0;
        for (int b = 0; b < 5; b++){
            file_size = file_size | ((uint64_t)size_bytes[b] << (8*b));
        }
        
        // Se lee el vaddr de inicio
        uint32_t file_vaddr;
        fread(&file_vaddr, 4, 1, archivo);

        // Se inicializa y devuelve el homerFile
        homerFile* archivo_actual = malloc(sizeof(homerFile));
        archivo_actual->process_id = process_id;
        strncpy(archivo_actual->file_name, file_name, 14);
        archivo_actual->file_name[14] = '\0';
        archivo_actual->mode = 'r';
        archivo_actual->file_size = file_size;
        archivo_actual->virtual_addr = file_vaddr;
        archivo_actual->pcb_index = pcb_idx;
        archivo_actual->file_index = found_idx;
        archivo_actual->valid = true;
        return archivo_actual;
    } else if(mode == 'w'){
        if (found_idx != -1){
            return NULL;
        }

        // Se busca el primer espacio libre
        int empty_idx = -1;
        for (int i = 0; i < FILE_ENTRY_COUNT; i++){
            fseek(archivo, file_table_addr + (i * FILE_ENTRY_SIZE), SEEK_SET);
            uint8_t valid;
            fread(&valid, 1, 1, archivo);
            if (valid == 0x00){
                empty_idx = i;
                break;
            }
        }
        // Si la tabla esta llena
        if (empty_idx == -1){
            return NULL;
        }
        
        //Se inicializa y devuelve el homerFile
        homerFile* archivo_actual = malloc(sizeof(homerFile));
        archivo_actual->process_id = process_id;
        strncpy(archivo_actual->file_name, file_name, 14);
        archivo_actual->file_name[14] = '\0';
        archivo_actual->mode = 'w';
        archivo_actual->file_size =  0;
        archivo_actual->virtual_addr = 0;
        archivo_actual->pcb_index = pcb_idx;
        archivo_actual->file_index = empty_idx;
        archivo_actual->valid = true;
        return archivo_actual;
    }
    return NULL;
}

int read_file(homerFile* file_desc, char* dest){
    FILE* local_file = fopen(dest, "wb");
    if (local_file == NULL){
        return -1;
    }
    uint64_t total_read_bytes = 0;
    while (total_read_bytes < file_desc->file_size){
        uint32_t vaddr_actual = file_desc->virtual_addr + total_read_bytes;
        uint32_t paddr = get_physical_address(file_desc->process_id, vaddr_actual);
        if (paddr == 0xFFFFFFFF){
            break;
        }

        //Se calcula el espacio restante del frame y del archivo
        uint32_t current_offset = vaddr_actual & 0x7FFF;
        uint32_t remaining_frame = FRAME_SIZE - current_offset;
        uint32_t remaining_file = file_desc->file_size - total_read_bytes;
        uint32_t to_read;
        if(remaining_file < remaining_frame){
            to_read = remaining_file;
        } else {
            to_read = remaining_frame;
        }

        unsigned char* buffer = malloc(to_read);
        fseek(archivo, paddr, SEEK_SET);
        fread(buffer, 1, to_read, archivo);
        fwrite(buffer, 1, to_read, local_file);
        free(buffer);
        total_read_bytes += to_read;
    }
    fclose(local_file);
    return total_read_bytes;
}

int write_file(homerFile* file_desc, char* src){
    FILE* local_file = fopen(src, "rb");
    if (local_file == NULL){
        return -1;
    }
    fseek(local_file, 0, SEEK_END);
    uint64_t to_write = ftell(local_file);
    rewind(local_file);

    int pcb_idx = file_desc->pcb_index;
    if (pcb_idx == -1){
        return -1;
    }
    
    //Se busca la ultima vaddr ocupada para asignar la nueva vaddr al final de esta
    uint32_t start_vaddr = 0;
    long file_table_addr = PCB_OFFSET + (pcb_idx * PCB_ENTRY_SIZE) + FILE_TABLE_OFFSET;
    for (int i = 0; i < FILE_ENTRY_COUNT; i++){
        long pcb_addr = file_table_addr + (i * FILE_ENTRY_SIZE);
        fseek(archivo, pcb_addr, SEEK_SET);
        uint8_t valid;
        fread(&valid, 1, 1, archivo);

        if (valid == 0x01){
            fseek(archivo, 14, SEEK_CUR);
            uint8_t size_bytes[5];
            fread(size_bytes, 1, 5, archivo);
            uint64_t size = 0;
            for (int b = 0; b < 5; b++){
                size = size | ((uint64_t)size_bytes[b] << (8*b));
            }
            uint32_t vaddr;
            fread(&vaddr, 4, 1, archivo);
            if (vaddr + size > start_vaddr){
                start_vaddr = vaddr + size;
            }
        }
    }
    file_desc->virtual_addr = start_vaddr;
    uint64_t total_written = 0;
    uint32_t current_vaddr = start_vaddr;

    //Se escribe el archivo dividiendolo en paginas
    while (total_written < to_write){
        uint32_t vpn = (current_vaddr >> 15) & 0x0FFF;
        uint32_t offset = current_vaddr & 0x7FFF;
        uint32_t paddr = get_physical_address(file_desc->process_id, current_vaddr);
        if (paddr == 0xFFFFFFFF){
            int pfn_found = -1;

            //Se busca el primer frame libre en el bitmap
            fseek(archivo, BITMAP_OFFSET, SEEK_SET);
            for (int i = 0; i < BITMAP_SIZE; i++){
                uint8_t byte;
                fread(&byte, 1, 1, archivo);
                for (int bit = 0; bit < 8; bit++){
                    if(!((byte >> bit) & 1)){
                        pfn_found = i * 8 + bit;
                        byte = byte | (1 << bit);
                        fseek(archivo, -1, SEEK_CUR);
                        fwrite(&byte, 1, 1, archivo);
                        break;
                    }
                }
                if (pfn_found != -1){
                    break;
                }
            }
            if (pfn_found == -1){
                break;
            }
            //Se asigna el frame en la IPT
            update_ipt_entry(file_desc->process_id, vpn, pfn_found, true);
            paddr = (8 + 192 + 8) * 1024 + (pfn_found << 15) + offset;
        }

        //Se calcula el espacio disp en el frame actual
        uint32_t space_in_frame = FRAME_SIZE - offset;
        uint32_t remaining_to_write = to_write - total_written;
        if (remaining_to_write > space_in_frame){
            remaining_to_write = space_in_frame;
        }
        char* buffer = malloc(remaining_to_write);
        fread(buffer, 1, remaining_to_write, local_file);
        fseek(archivo, paddr, SEEK_SET);
        fwrite(buffer, 1, remaining_to_write, archivo);
        free(buffer);
        total_written += remaining_to_write;
        current_vaddr += remaining_to_write;
    }

    //Se guardan los metadatos del archivo
    long target_file_entry_addr = PCB_OFFSET + (pcb_idx * PCB_ENTRY_SIZE) + FILE_TABLE_OFFSET + (file_desc->file_index * FILE_ENTRY_SIZE);
    fseek(archivo, target_file_entry_addr, SEEK_SET);
    uint8_t valid_out = 0x01;
    fwrite(&valid_out, 1, 1, archivo);
    fwrite(file_desc->file_name, 1, 14, archivo);
    uint8_t size_bytes[5];
    for (int b = 0; b < 5; b++){
        size_bytes[b] = (total_written >> (8*b)) & 0xFF;
    }
    fwrite(size_bytes, 1, 5, archivo);
    fwrite(&file_desc->virtual_addr, 4, 1, archivo);

    file_desc->file_size = total_written;
    fclose(local_file);
    return total_written;
}       

void delete_file(int process_id, char* file_name){
    if (archivo == NULL){
        return;
    }
    int pcb_idx = search_pcb_index(process_id);
    if (pcb_idx == -1){
        return;
    }

    uint64_t file_size = 0;
    uint32_t file_vaddr = 0;
    int entry_found_idx = search_file_index(pcb_idx, file_name);
    if (entry_found_idx == -1){
        return;
    }
    long file_table_addr = PCB_OFFSET + (pcb_idx * PCB_ENTRY_SIZE) + FILE_TABLE_OFFSET;

    // Se invalida en la tabla
    fseek(archivo, file_table_addr + (entry_found_idx * FILE_ENTRY_SIZE), SEEK_SET);
    uint8_t invalid = 0x00;
    fwrite(&invalid, 1, 1, archivo);

    uint32_t current_vaddr = file_vaddr;
    uint64_t total_cleared = 0;

    // Se recorren los VPNs del archivo y se liberan si se pueden
    while (total_cleared < file_size){
        uint32_t vpn = (current_vaddr >> 15) & 0x0FFF;
        bool vpn_used = false;

        // Se verifica si otro archivo lo usa
        for (int j = 0; j < FILE_ENTRY_COUNT; j++){
            fseek(archivo, file_table_addr + (j * FILE_ENTRY_SIZE), SEEK_SET);
            uint8_t valid;
            fread(&valid, 1, 1, archivo);
            if (valid == 0x01){
                fseek(archivo, 14, SEEK_CUR);
                uint8_t size_bytes[5];
                fread(size_bytes, 1, 5, archivo);
                uint64_t size = 0;
                for (int b = 0; b < 5; b++){
                    size = size | ((uint64_t)size_bytes[b] << (8*b));
                }
                uint32_t vaddr;
                fread(&vaddr, 4, 1, archivo);
                uint32_t other_vpn_start = (vaddr >> 15) & 0x0FFF;
                uint32_t other_vpn_end = ((vaddr + size -1 ) >> 15) & 0x0FFF;//ver aqui
                if (vpn >= other_vpn_start && vpn <= other_vpn_end){
                    vpn_used = true;
                    break;
                }
            }
        }

        // Si solo este archivo lo usa, se libera
        if (!vpn_used){
            for (int pfn = 0; pfn < IPT_ENTRIES; pfn++){
                fseek(archivo, IPT_OFFSET + (pfn * IPT_ENTRY_SIZE), SEEK_SET);
                uint8_t b[3];
                fread(b, 1, 3, archivo);
                uint32_t entry = ((uint32_t)b[0]) | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16);
                uint8_t valid = (entry >> 23) & 0x01;
                uint16_t pid_entry = (entry >> 13) & 0x3ff;
                uint16_t vpn_entry = entry & 0x1FFF;
                if (valid == 1 && pid_entry == (uint16_t)process_id && (vpn_entry & 0x0FFF) == vpn){
                    // Se invalida en IPT
                    update_ipt_entry(0, 0, pfn, false);

                    // Se libera el PFN en el Bitmap
                    fseek(archivo, BITMAP_OFFSET + (pfn / 8), SEEK_SET);
                    uint8_t byte;
                    fread(&byte, 1, 1, archivo);
                    byte = byte & ~(1 << (pfn % 8));
                    fseek(archivo, -1, SEEK_CUR);
                    fwrite(&byte, 1, 1, archivo);
                    break;
                }
            }
        }
        total_cleared += FRAME_SIZE;
        current_vaddr += FRAME_SIZE;
    }
}

void close_file(homerFile* file_desc){
    if (file_desc != NULL){
        free(file_desc);
    }
}
