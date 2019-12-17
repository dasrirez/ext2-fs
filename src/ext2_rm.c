#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"
#include "helpers.h"

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "Usage: ext2_rm <image file name> <file path>\n");
    exit(1);
  }
  int fd = open(argv[1], O_RDWR);

  /* read (128 blocks) * (1024 bytes per block) */
  disk = mmap
    (NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (disk == MAP_FAILED) {
    perror("mmap");
    exit(1);
  }
  sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
  gd = (struct ext2_group_desc *)(disk + (EXT2_BLOCK_SIZE * 2));

  unsigned int path_len = strlen(argv[2]);
  char path[path_len];
  strcpy(path, argv[2]);

  char parent_path[path_len];
  parent_path[0] = 0;
  char *name;
  char *token = strtok(path, "/");
  /* deleting root */
  if (token == NULL) {
    fprintf(stderr, "/: is a directory\n");
    exit(EINVAL);
  }

  name = token;
  while (token != NULL) {
    token = strtok(NULL, "/");
    if (token != NULL) {
      strcat(parent_path, name);
      strcat(parent_path, "/");
      name = token;
    }
  }
  if (parent_path[0] == 0) {
    strcpy(parent_path, ".");
  }

  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
    fprintf(stderr, "rm: \".\" and \"..\" may not be removed\n");
    exit(EINVAL);
  }

  /* Things needed to be done
   * Update rec_len of previous directory in parent node to point to rec_len of the dir being deleted
   *  Set corresponding block bitmap bit of the parent directory to 0 if previous directory is NULL
   *  Also set parents blocks to one less
   *   Update the free blocks count in gd
   * Now get the inode from the directory entry, iterate over its blocks and free each one in the block bitmap
   * Update the used dir count to be one less
   * Clear the corresponding inode bitmap bit for the inode
   * Set the inode dtime to now
   */
  struct ext2_dir_entry_2 *parent = (struct ext2_dir_entry_2 *)getdir_from_path(parent_path);
  if (parent == NULL) {
    fprintf(stderr, "No such file or directory\n");
    exit(ENOENT);
  }
  
  struct ext2_dir_entry_2 *removed_dir = rm_from_inode(parent->inode, name);
  if (removed_dir == NULL) {
    fprintf(stderr, "No such file or directory\n");
    exit(ENOENT);
  }
  // printf("Removed %s\n", removed_dir->name);
  return 0;
}
