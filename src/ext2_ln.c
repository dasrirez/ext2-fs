#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"
#include "helpers.h"

int main(int argc, char **argv) {
  if (argc < 4 || argc > 5) {
    fprintf(stderr, "Usage: ext2_ln <image file name> [-s] <source path> <target path>\n");
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

  /* Things that need doing
   * Grab the source using its full path
   *  If it is not a regular file, exit with EINVAL
   * Grab the parent dir of the target_name
   *  Search for a directory with the same name as the target_name, exit with EEXIST if found
   * IF HARD LINK
   *  Make a link entry with the same inode as the target_name
   *  Update the target_name link count
   * IF SYMLINK
   *  Allocate bitmap
   *  Use the returned inode # to create a new entry
   *  Allocate block
   *    Similar to the one in mkdir, but instead of a new dir we put a char array (name of the source) in the block
   */

  /* argv indices */
  unsigned char flag;
  int isource;
  int itarget;
  if (argc == 4) {
    isource = 2;
    itarget = 3;
  } else {
    flag = strcmp(argv[2], "-s") == 0;
    isource = 3;
    itarget = 4;
  }

  /* 
   * target_name: the name of the target_name entry
   * target_abs_path: the abs path to the target_name entry
   * target_parent_path: the abs path to the target_name entry's parent
   * 
   * source_abs_path: the abs path to the source entry
   */
  unsigned int source_path_len = strlen(argv[isource]);
  char source_abs_path[1024];
  strncpy(source_abs_path, argv[isource], source_path_len);
  source_abs_path[source_path_len] = 0;

  unsigned int target_path_len = strlen(argv[itarget]);
  char target_abs_path[1024];
  strncpy(target_abs_path, argv[itarget], target_path_len);
  target_abs_path[target_path_len] = 0;

  char target_parent_path[target_path_len];
  target_parent_path[0] = 0;

  char *target_name;
  char *token = strtok(target_abs_path, "/");
  if (token == NULL) {
    fprintf(stderr, "No such file or directory\n");
    exit(ENOENT);
  }

  target_name = token;

  while (token != NULL) {
    token = strtok(NULL, "/");
    if (token != NULL) {
      strcat(target_parent_path, target_name);
      strcat(target_parent_path, "/");
      target_name = token;
    }
  }

  if (target_parent_path[0] == 0) {
    strcpy(target_parent_path, ".");
  }

  // printf("source_abs_path=%s target_abs_path=%s target_parent_path=%s target_name=%s\n", source_abs_path, target_abs_path, target_parent_path, target_name);

  struct ext2_dir_entry_2 *source = (struct ext2_dir_entry_2 *)getdir_from_path(source_abs_path);
  if (source == NULL) {
    fprintf(stderr, "No such file or directory\n");
    exit(ENOENT);
  } else if (source->file_type != EXT2_FT_REG_FILE) {
    fprintf(stderr, "Not a file %s\n", source->name);
    exit(EINVAL);
  }

  struct ext2_dir_entry_2 *target_parent = (struct ext2_dir_entry_2 *)getdir_from_path(target_parent_path);
  if (target_parent == NULL) {
    fprintf(stderr, "No such file or directory\n");
    exit(ENOENT);
  }

  struct ext2_dir_entry_2 *dupe = (struct ext2_dir_entry_2 *)getdir_from_inode(target_parent->inode, target_name);
  if (dupe != NULL) {
    fprintf(stderr, "File exists\n");
    exit(EEXIST);
  }

  unsigned int target_inode_no;
  struct ext2_dir_entry_2 *target;
  struct ext2_inode *target_inode;
  if (flag) {
    if (gd->bg_free_inodes_count == 0) {
      fprintf(stderr, "No free inode avilable\n");
      exit(ENOSPC);
    }
    target_inode_no = allocate_bitmap(gd->bg_inode_bitmap, sb->s_inodes_count / 8) + 1;
    if (target_inode_no < EXT2_GOOD_OLD_FIRST_INO && target_inode_no != EXT2_ROOT_INO) {
      fprintf(stderr, "No free inode avilable\n");
      exit(ENOSPC);
    }
    gd->bg_free_inodes_count--;
    target = (struct ext2_dir_entry_2 *)allocate_space_in_inode(target_parent->inode, EXT2_FT_SYMLINK, target_name);
    if (target == NULL) {
      /* Failed to allocate block, wipe the created inode */
      free_bitmap(gd->bg_inode_bitmap, sb->s_inodes_count / 8, target_inode_no - 1);
      gd->bg_free_inodes_count++;
      fprintf(stderr, "No free blocks avilable\n");
      exit(ENOSPC);
    }
    target->inode = target_inode_no;
    target->file_type = EXT2_FT_SYMLINK;
    target_inode = make_symlink(target_inode_no, source_abs_path);
    target_inode->i_mode |= EXT2_S_IFLNK;
    /* checking that target path gets written */
    // strncpy(target_abs_path, argv[itarget], target_path_len);
    // target_abs_path[target_path_len] = 0;
    // struct ext2_inode *in = (struct ext2_inode *)(disk + BLOCK_OFFSET(gd->bg_inode_table) + INODE_OFFSET(target->inode));
    // printf("%s\n", (char *)(disk + BLOCK_OFFSET(in->i_block[0])));
  } else {
    target = (struct ext2_dir_entry_2 *)allocate_space_in_inode(target_parent->inode, EXT2_FT_REG_FILE, target_name);
    target->inode = source->inode;
    target->file_type = EXT2_FT_REG_FILE;
    struct ext2_inode *source_inode = (struct ext2_inode *)(disk + BLOCK_OFFSET(gd->bg_inode_table) + INODE_OFFSET(source->inode));
    source_inode->i_links_count++;
  }
  
}
