#ifndef __FAT_H__
#define __FAT_H__
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <ctype.h>
#include <list>
#include <vector>
#include <cstddef>
using namespace std;

#define SECTOR_SIZE 512

// tablica FAT
#define EOC 0xFFFF
#define NOT_USE 0x0000
#define DAMAGED 0xFFF7
#define EOCC 0xFFF8

// pierwszy bajt nazwy/ status alokacji
#define FREE_OR_LAST_CLUSTER 0x00
#define DELETED 0xE5

// bajt atrybutów
#define READ_ONLY_FILE 0x01
#define HIDDEN_FILE 0x02
#define SYSTEM_FILE 0x04
#define VOLUME_LABEL 0x04
#define DIRECTORY 0x10
#define ARCHIVED 0x20
#define LFN 0x0F
#define LINE_LENGTH 100

int seconds(uint16_t time){ return (time & 0X001F) * 2; }
int minutes(uint16_t time) { return ((time & 0X07E0) >> 5); }
int hours(uint16_t time) { return ((time & 0XF800) >> 11); }
int month_day(uint16_t time) { return (time & 0X001F); }
int month(uint16_t time) { return (time & 0X01E0) >> 5; }
int year(uint16_t time) { return 1980 + ((time & 0XFE00) >> 9); }

struct BPB {
    int16_t bytes_per_sector; // Liczba bajtów w sektorze
    uint8_t sectors_per_cluster; // Liczba sektorów w klastrze
    uint16_t reserved_sectors; // Liczba zarezerwowanych sektorów
    uint8_t fat_count; // Liczba tabel alokacji plików (FAT)
    uint16_t root_dir_capacity; // Liczba wpisów w katalogu głównym woluminu (
    // root directory ) ; Liczba * sizeof DirEntry )
    // musi być
    // wielokrotnością długości sektora.
    uint16_t logical_sectors16; // Całkowita liczba sektorów w woluminie
    uint8_t media_type; // Typ nośnika
    uint16_t sectors_per_fat; // Liczba sektorów na jedną tablicę alokacji
    uint16_t CHS_geometry_Ns; // liczba sektorów na ścieżce
    uint16_t CHS_geometry_Nh; // Liczba ścieżek w cylindrze
    uint32_t FAT_volume_offset; // Liczba ukrytych sektorów
    uint32_t logical_sectors32; // : Całkowita liczba sektorów w woluminie
} __attribute__((packed));

struct E_BPB {
    uint8_t disk_number; // Oznaczenie numeru dysku
    uint8_t head_number; // Zarezerwowane
    uint8_t extended_BPB_signature; // Sygnatura rozszerzonego rekordu ładującego
    uint32_t volume_number; // Unikalny numer seryjny woluminu
    uint8_t volume_attribute[11]; // Nazwa woluminu ;  wpis w katalogu główny FAT
    // (atrybut Volume )
    uint64_t FAT_id; // Identyfikator systemu plików
} __attribute__((packed));

struct boot_sector {
    uint8_t jump_to_boot_loader[3];
    uint8_t OEM_name[8];
    struct BPB BIOS_Parameter_Block;
    struct E_BPB Extended_BPB;
    uint8_t boot_loader_code[448];
    uint16_t bootsector_end_tag;
} __attribute__((packed));

struct directory_entry {
    char filename[8];
    char filename_extension[3];
    uint8_t attribute_byte;
    uint8_t reserved_1;
    uint8_t creation_ms; // ms
    uint16_t creation_time;
    uint16_t creation_data;
    uint16_t last_access_data;
    uint16_t reserved_2;
    uint16_t last_write_time;
    uint16_t last_write_data;
    uint16_t starting_cluster;
    uint32_t file_size;
} __attribute__((packed));

typedef uint64_t lba_t;
typedef struct directory_entry MDIR;
typedef struct directory_handler DIR_HANDLER;

struct directory_handler {
    list<MDIR> directory_list;
    int id;
};

struct file_handler {
    int pos;
    int ID;
    int current_cluster;
    MDIR file;
};

typedef struct file_handler MFILE;

#endif