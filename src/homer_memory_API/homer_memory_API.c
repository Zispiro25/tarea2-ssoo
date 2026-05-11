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
                        ((uint32_t)tabla[base + 20] << 24) |
                        ((uint32_t)tabla[base + 21] << 16) |
                        ((uint32_t)tabla[base + 22] << 8) |
                        ((uint32_t)tabla[base + 23]);

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