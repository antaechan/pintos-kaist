#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "threads/thread.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);
static void filesys_parse_path(const char *, char *, char *);

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) {
	filesys_disk = disk_get (0, 1);
	if (filesys_disk == NULL)
		PANIC ("hd0:1 (hdb) not present, file system initialization failed");

	inode_init ();

#ifdef EFILESYS
	fat_init ();

	if (format)
		do_format ();

	fat_open ();
#else
	/* Original FS */
	free_map_init ();

	if (format)
		do_format ();

	free_map_open ();
#endif
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void
filesys_done (void) {
	/* Original FS */
#ifdef EFILESYS
	fat_close ();
#else
	free_map_close ();
#endif
}

static void
filesys_parse_path(const char *file_path, char *directory, char *file_name)
{

	size_t l = strlen(file_path) + 1;
	char path_copy[l];
	strlcpy(path_copy, file_path, l);
	int count = 0;
	int i, delim;
	
	for(i = l - 1; i >= 0; i--){
		if(path_copy[i] == '/'){
			if(i != 0 && count == 0)
			{
				delim = i;
				path_copy[i] = '\0';
			}
			count++;
		}
	}

	if(count == 0)
	{
		/* relative path */
		*directory = '\0';
		strlcpy(file_name, path_copy, l);
	}
	else if((count == 1) && (path_copy[0] == '/'))
	{
		/* / or /a */
		directory[0] = '/';
		directory[1] = '\0';
		strlcpy(file_name, path_copy + 1, l);
	}
	else
	{
		strlcpy(directory, path_copy, l);
		strlcpy(file_name, (path_copy + delim + 1), l);
	}
}


/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool
filesys_create (const char *file_path, off_t initial_size, enum file_type type) {
	disk_sector_t inode_sector = 0;

	/* parse file_path */
	char directory[strlen(file_path) + 1];
	char file_name[strlen(file_path) + 1];

	filesys_parse_path(file_path, directory, file_name);
	
	/* open directory which file will be stored in */
	struct dir *dir = dir_open_path(directory);
	
	/* TODO: handle case filesys_create("/")
			 return false at current state	 */
	bool success = (dir != NULL
			&& fat_allocate (1, &inode_sector)
			&& inode_create (inode_sector, initial_size, type)
			&& dir_add (dir, file_name, inode_sector));

	if (!success && inode_sector != 0)
		fat_remove_chain (inode_sector, 0);

	dir_close (dir);
	return success;
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
void *
filesys_open (const char *file_path, int *type) {

	/* empty file open handling */
	if(strlen(file_path) == 0)	return NULL;

	/* parse file_path */
	char directory[strlen(file_path) + 1];
	char file_name[strlen(file_path) + 1];
	filesys_parse_path(file_path, directory, file_name);
	
	struct dir *dir = dir_open_path(directory);
	/* 1. directory "" is ok, open relative path */
		
	/* 2. file name "", open root directory */
	if(file_name[0] == '\0'){
		*type = _DIRECTORY;
		return (void *)dir;
	}

	struct inode *inode = NULL;
	void *ret = NULL;
	
	ASSERT(strlen(file_name) > 0);
	if (dir != NULL && dir_lookup (dir, file_name, &inode))
	{
		if(inode == NULL || inode_removed(inode))
			return NULL;

		*type = inode_get_type(inode);

		if(*type == _FILE)
			ret = (void *)file_open(inode);

		else if(*type == _DIRECTORY)
			ret = (void *)dir_open(inode);
	}	
	
	/* clean up open directory */
	if(dir) dir_close (dir);
	return ret;
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) {

	char directory[strlen(name) + 1];
	char file_name[strlen(name) + 1];

	filesys_parse_path(name, directory, file_name);

	/* 1. directory "" is ok, relative path */	
	
	/* 2. file name "", remove root directory */
	if(file_name[0] == '\0')
		return false;

	struct dir *dir = dir_open_path(directory);

	bool success = (dir != NULL && dir_remove (dir, file_name));
	dir_close (dir);

	return success;
}

bool
filesys_chdir(char *dir_name)
{	
	struct thread *t = thread_current();
	struct dir *dir = dir_open_path(dir_name);

	if(dir == NULL)
		return false;

	dir_close(t->cwd);
	t->cwd = dir;

	return true;
}

/* Formats the file system. */
static void
do_format (void) {
	printf ("Formatting file system...");

#ifdef EFILESYS
	/* Create FAT and save it to the disk. */
	fat_create ();
	fat_close ();
#else
	free_map_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");
	free_map_close ();
#endif

	printf ("done.\n");
}
