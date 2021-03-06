#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/disk.h"
#include "filesys/filesys.h"

struct bitmap;

void inode_init (void);
bool inode_create (disk_sector_t, off_t, enum file_type);
struct inode *inode_open (disk_sector_t);
struct inode *inode_reopen (struct inode *);
disk_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);

/* Helper function */
off_t inode_length (const struct inode *);
enum file_type inode_get_type (struct inode *inode);
bool inode_removed(struct inode *inode);
disk_sector_t inode_set_psector(struct inode *inode, disk_sector_t psector);
disk_sector_t inode_get_psector(struct inode *inode);
struct inode_disk * inode_get_inode_disk(struct inode *inode);

#endif /* filesys/inode.h */
