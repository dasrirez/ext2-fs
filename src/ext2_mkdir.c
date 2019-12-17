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
    fprintf(stderr, "Usage: ext2_mkdir <image file name> <file path>\n");
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
  if (token == NULL)
    exit(ENOENT);

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
  struct ext2_dir_entry_2 *parent = (struct ext2_dir_entry_2 *)getdir_from_path(parent_path);
  if (parent == NULL) {
    fprintf(stderr, "No such file or directory\n");
    exit(ENOENT);
  }
  struct ext2_dir_entry_2 *dupe = (struct ext2_dir_entry_2 *)getdir_from_inode(parent->inode, name);
  if (dupe != NULL) {
    fprintf(stderr, "File exists\n");
    exit(EEXIST);
  }

  if (gd->bg_free_inodes_count == 0) {
    fprintf(stderr, "No free inode avilable\n");
    exit(ENOSPC);
  }
  unsigned int inode_no = allocate_bitmap(gd->bg_inode_bitmap, sb->s_inodes_count / 8) + 1;
  gd->bg_free_inodes_count--;

  /* should never get here due to gd check above, leaving for safety */
  if (inode_no < EXT2_GOOD_OLD_FIRST_INO && inode_no != EXT2_ROOT_INO) {
    fprintf(stderr, "No free inode avilable\n");
    exit(ENOSPC);
  }

  struct ext2_inode *inode = make_dir_inode(inode_no, parent->inode);
  if (inode == NULL) {
    free_bitmap(gd->bg_inode_bitmap, sb->s_inodes_count / 8, inode_no - 1);
    fprintf(stderr, "No free blocks avilable\n");
    exit(ENOSPC);
  }

  struct ext2_dir_entry_2 *allocated = allocate_space_in_inode(parent->inode, EXT2_FT_DIR, name);

  if (allocated == NULL) {
    /* Failed to allocate block, wipe the created inode */
    free_bitmap(gd->bg_inode_bitmap, sb->s_inodes_count / 8, inode_no - 1);
    gd->bg_free_inodes_count++;
    fprintf(stderr, "No free blocks avilable\n");
    exit(ENOSPC);
  } else {
    allocated->inode = inode_no;
  }
}
