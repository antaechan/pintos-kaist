#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "threads/synch.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */

struct lock filesys_lock;

/* Disk used for file system. */
extern struct disk *filesys_disk;

enum file_type {
    _FILE = 0,       /* ordinary file */
    _DIRECTORY = 1,  /* directory */
};

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size, enum file_type type);
void *filesys_open (const char *name, int *type);
bool filesys_remove (const char *name);
bool filesys_chdir(char *dir);
#endif /* filesys/filesys.h */
