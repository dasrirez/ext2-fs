#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"
#include "ext2_ls.h"
#include "helpers.h"

void printdirs_from_block(unsigned int block, unsigned int max_size, char flag) {

  struct ext2_dir_entry_2 *dir;
  unsigned int size = 0;
  char name[EXT2_NAME_LEN+1];

  dir = (struct ext2_dir_entry_2 *)(disk + BLOCK_OFFSET(block));
  while (size < max_size && dir->inode) {
    memcpy(name, dir->name, dir->name_len);
    name[dir->name_len] = 0;
    if (flag || name[0] != '.')
      printf("%s\n", name);
    size += dir->rec_len;
    dir = (struct ext2_dir_entry_2 *)(disk + BLOCK_OFFSET(block) + size);
  }
}

void printdirs_from_inode(unsigned int inode_no, char flag) {
  struct ext2_inode *inode = (struct ext2_inode *)
    (disk + BLOCK_OFFSET(gd->bg_inode_table) + INODE_OFFSET(inode_no));
  unsigned int ind_block_no;
  unsigned int block_no;

  for (block_no = 0; block_no < MAX_BLOCKS; block_no++) {
    if (block_no != 12) {
      /* direct */
      printdirs_from_block(inode->i_block[block_no], inode->i_size, flag);
    } else {
      /* indirect */
      unsigned int *indirect = (unsigned int *)
        (disk + BLOCK_OFFSET(inode->i_block[block_no]));
      for (ind_block_no = 0;
           ind_block_no < EXT2_BLOCK_SIZE / sizeof(unsigned int);
           ind_block_no++)
        printdirs_from_block(indirect[ind_block_no], inode->i_size, flag);
    }
  }
}

int main(int argc, char **argv) {
  if (argc < 3 || argc > 4) {
    fprintf(stderr, "Usage: ext2_ls <image file name> [-a] <file path>\n");
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

  unsigned int path_len;
  unsigned int ipath;
  char flag = 0;
  unsigned char slash_suffix = 0;
  struct ext2_dir_entry_2 *dir;
  if (argc < 4) {
    ipath = 2;
  } else {
    ipath = 3;
    flag = (strcmp(argv[2], "-a") == 0);
  }
  path_len = strlen(argv[ipath]);
  char path[path_len];
  strcpy(path, argv[ipath]);

  slash_suffix = path[path_len - 1] == '/';

  dir = getdir_from_path(path);
  if (dir == NULL) {
    fprintf(stderr, "No such directory\n");
    exit(ENOENT);
  } else if (dir->file_type == EXT2_FT_DIR) {
    printdirs_from_inode(dir->inode, flag);
  } else {
    if (slash_suffix) {
      fprintf(stderr, "Not a directory\n");
      exit(ENOENT);
    } else {
      char name[EXT2_NAME_LEN];
      strncpy(name, dir->name, dir->name_len);
      name[dir->name_len] = 0;
      printf("%s\n", name);
    }
  }
  //} else if (dir->file_type == EXT2_FT_SYMLINK) {
  //  dir = getdir_from_symlink(dir);
  //}

}
