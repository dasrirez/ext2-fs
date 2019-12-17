#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"
#include <string.h>
#include <errno.h>
#include "helpers.h"

#define BLOCK_SIZE             (EXT2_BLOCK_SIZE << sb->s_log_block_size)
#define BLOCK_OFFSET(block)    (EXT2_BLOCK_SIZE + (block - 1) * BLOCK_SIZE)
#define INODE_OFFSET(inode_no) ((inode_no - 1) * sizeof(struct ext2_inode))
#define INODES_PER_BLOCK       (BLOCK_SIZE / sizeof(struct ext2_inode))
#define INO_TBL_BLOCKS         (sb->s_inodes_per_group / INODES_PER_BLOCK)

unsigned char *disk;
struct ext2_super_block *sb;
struct ext2_group_desc *gd;

int main(int argc, char **argv) {
  // takes in three arguments, no optional
  // argv[1] is the ext2 formatted disk
  // argv[2] is the path to the file
  // argv[3] is the absolute path the destination

  if (argc != 4) {
      // neither 3 or 4 arguments
      fprintf(stderr, "Usage: ext2_cp <image file name> <source> <destination>\n");
      exit(1);
  }

  int fd = open(argv[1], O_RDWR);

  disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(disk == MAP_FAILED) {
    perror("mmap");
    exit(EXIT_FAILURE);
  }

  /* Super-block struct. Always start at byte offset 1024 from the start of disk.  */
  sb = (struct ext2_super_block *)(disk + 1024);

  /* Group description struct:
    * The block group desciptor follows right after the super block. Since each
    * block has size 1024 bytes, the block group descriptors must starts at byte
    * offset 1024 * 2 = 2048*/
  gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);

  // 1. open file
  FILE *f = fopen(argv[2], "rb");
  if (f == NULL) {
    printf("cannot stat '%s': No such file or directory\n", argv[2]);
    return -ENOENT;
  }

  // get the file name
  const char s[2] = "/";
  char *token;
  char *srcname;
  token = strtok(argv[2], s);
  while(token != NULL) {
    srcname = token;
    token = strtok(NULL, s);
  }

  // 2. get entry of destination
  struct ext2_dir_entry_2 *destination = getdir_from_path(argv[3]);
  if (destination == NULL) {
    printf("cannot create regular file '%s': Not a directory\n", argv[3]);
    return -ENOENT;
  }

  // verify file with same name does not exist at destination
  struct ext2_dir_entry_2 *found = get_entry_from_entry(destination, srcname);
  if (found) {
    printf("File or Directory called %s already exists in destination\n", srcname);
    return -EEXIST;
  }

  // rec len is already allocated
  struct ext2_dir_entry_2 *copyfile = allocate_entry_2(destination, srcname, EXT2_FT_REG_FILE);
  if (copyfile == NULL) {
    return -ENOMEM;
  }
  // // allocate inode
  int inode_num = allocate_inode(EXT2_S_IFREG);
  copyfile->inode = inode_num;
  copyfile->name_len = strlen(srcname);
  copyfile->file_type = EXT2_FT_REG_FILE;
  memcpy(copyfile->name, srcname, strlen(srcname));
  // // now clone this thing
  // get the inode
  struct ext2_inode *inode = (struct ext2_inode *)(disk + BLOCK_OFFSET(gd->bg_inode_table) + INODE_OFFSET(inode_num));
  inode->i_links_count++;
  unsigned char buffer[EXT2_BLOCK_SIZE];
  int i, new_block, bytes_read;
  unsigned char *block;
  for (i=0;i<12;i++) {
    bytes_read = 0;
    // memcpy(buffer,'0', EXT2_BLOCK_SIZE);
    while((bytes_read = fread(buffer, 1,EXT2_BLOCK_SIZE, f)) >0) {
      // new block
      new_block = allocate_block();
      inode->i_blocks = inode->i_blocks + 2;
      block = (unsigned char *)(disk + BLOCK_OFFSET(new_block));
      memcpy(block, buffer, bytes_read);
      inode->i_block[i] = new_block;
      inode->i_size+=bytes_read;
    }
    // break out of loop means can't read anymore

  }
  // indirect
  int x;
  unsigned int *indirect;
  int bound = EXT2_BLOCK_SIZE/(sizeof(unsigned int));
  new_block = allocate_block();
  inode->i_block[12] = new_block;
  // get new block;
  indirect = (unsigned int *)(disk + EXT2_BLOCK_SIZE * inode->i_block[12]);
  for (x=0;x<bound;x++) {
    bytes_read = 0;
    // memcpy(buffer, '\0', EXT2_BLOCK_SIZE);
    while((bytes_read = fread(buffer, 1,EXT2_BLOCK_SIZE, f)) > 0) {
      // new block
      new_block = allocate_block();
      inode->i_blocks = inode->i_blocks + 2;
      block = (unsigned char *)(disk + BLOCK_OFFSET(new_block));
      memcpy(block, buffer, bytes_read);
      indirect[x] = new_block;
      inode->i_size+=bytes_read;
    }
    // break out looop means cant read anymore
  }
  fclose(f);
  return 0;
}
