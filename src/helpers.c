#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include "ext2.h"
#include "helpers.h"

struct ext2_dir_entry_2 *getdir_from_block (unsigned int block, char *dir_name) {
  struct ext2_dir_entry_2 *dir;
  unsigned int size = 0;
  char name[EXT2_NAME_LEN+1];

  dir = (struct ext2_dir_entry_2 *)(disk + BLOCK_OFFSET(block));
  while (size < EXT2_BLOCK_SIZE && dir->inode) {
    memcpy(name, dir->name, dir->name_len);
    name[dir->name_len] = 0;
    if (strcmp(name, dir_name) == 0)
      return dir;
    size += dir->rec_len;
    dir = (struct ext2_dir_entry_2 *)(disk + BLOCK_OFFSET(block) + size);
  }
  return NULL;
}

struct ext2_dir_entry_2 *getdir_from_inode(unsigned int inode_no, char *dir_name) {
  struct ext2_inode *inode = (struct ext2_inode *)
    (disk + BLOCK_OFFSET(gd->bg_inode_table) + INODE_OFFSET(inode_no));
  struct ext2_dir_entry_2 *dir;

  unsigned int block_no;
  unsigned int ind_block_no;

  for (block_no = 0; block_no < MAX_BLOCKS; block_no++) {
    if (block_no != 12) {
      /* direct */
      dir = getdir_from_block(inode->i_block[block_no], dir_name);
      if (dir != NULL)
        return dir;
    } else {
      /* indirect */
      unsigned int *indirect = (unsigned int *)
        (disk + BLOCK_OFFSET(inode->i_block[block_no]));
      for (ind_block_no = 0;
           ind_block_no < EXT2_BLOCK_SIZE / sizeof(unsigned int);
           ind_block_no++) {
        dir = getdir_from_block(indirect[ind_block_no], dir_name);
        if (dir != NULL)
          return dir;
      }
    }
  }
  return NULL;
}

struct ext2_dir_entry_2 *getdir_from_path(char *path) {
  struct ext2_dir_entry_2 *dir;
  unsigned int inode_no = EXT2_ROOT_INO;

  char *token;
  char *delimiter = "/";
  token = strtok(path, delimiter);
  token = token == NULL ? "." : token; /* ie path is "/" */

  while (token != NULL) {
    dir = getdir_from_inode(inode_no, token);
    if (dir == NULL)
      return NULL;
    inode_no = dir->inode;
    token = strtok(NULL, delimiter);
  }
  return dir;
}

void free_bitmap(unsigned int bitmap_block, unsigned int bitmap_len, unsigned int target_bit) {
  int i, j;
  unsigned char *disk_byte;
  /* for each byte */
  for (i = 0; i < bitmap_len; i++) {
    disk_byte = (unsigned char*)(disk + BLOCK_OFFSET(bitmap_block) + i);
    /* for each bit */
    for (j = 0; j < 8; j++) {
      if (target_bit == (i * 8) + j) {
        *disk_byte &= ~(1 << j);
      }
    }
  }
}

unsigned int allocate_bitmap(unsigned int bitmap_block, unsigned int bitmap_len) {
  unsigned int i, j;
  unsigned char *disk_byte;
  /* for each byte */
  for (i = 0; i < bitmap_len; i++) {
    disk_byte = (unsigned char*)(disk + BLOCK_OFFSET(bitmap_block) + i);
    /* for each bit */
    for (j = 0; j < 8; j++) {
      if ((*disk_byte & (1 << j)) == 0) {
        *disk_byte |= (1 << j);
        return (i * 8) + j;
      }
    }
  }
  return 0;
}

struct ext2_dir_entry_2 *allocate_space_in_block(unsigned int block, unsigned char file_type, char *name) {
  struct ext2_dir_entry_2 *dir;
  struct ext2_dir_entry_2 *allocated;
  unsigned int size = 0;
  unsigned int name_len = strlen(name);
  unsigned int real_len;
  unsigned int free_len;

  unsigned int needed_space = sizeof(struct ext2_dir_entry_2) + name_len;
  /* rec len is always a multiple of 4... go figure */
  while (needed_space % 4 != 0)
    needed_space++;

  dir = (struct ext2_dir_entry_2 *)(disk + BLOCK_OFFSET(block));
  while (size < EXT2_BLOCK_SIZE && dir->inode) {
    real_len = sizeof(struct ext2_dir_entry_2) + dir->name_len;
    while (real_len % 4 != 0)
      real_len++;
    free_len = dir->rec_len - real_len;
    if (name_len + sizeof(struct ext2_dir_entry_2) <= free_len) {
      allocated = (struct ext2_dir_entry_2 *)(disk + BLOCK_OFFSET(block) + size + real_len);
      allocated->rec_len = free_len;
      allocated->file_type = file_type;
      allocated->name_len = name_len;
      strncpy(allocated->name, name, name_len);
      dir->rec_len = real_len;
      return allocated;
    }
    size += dir->rec_len;
    dir = (struct ext2_dir_entry_2 *)(disk + BLOCK_OFFSET(block) + size);
  }
  return NULL;
}

struct ext2_dir_entry_2 *allocate_in_new_block(unsigned int block, unsigned char file_type, char *name) {
  struct ext2_dir_entry_2 *allocated;
  unsigned int name_len = strlen(name);

  allocated = (struct ext2_dir_entry_2 *)(disk + BLOCK_OFFSET(block));
  allocated->rec_len = EXT2_BLOCK_SIZE;
  allocated->file_type = file_type;
  allocated->name_len = name_len;
  return allocated;
}

struct ext2_dir_entry_2 *allocate_space_in_inode(unsigned int inode_no, unsigned char file_type, char *name) {
  struct ext2_inode *inode = (struct ext2_inode *)
    (disk + BLOCK_OFFSET(gd->bg_inode_table) + INODE_OFFSET(inode_no));
  struct ext2_dir_entry_2 *allocated = NULL;

  unsigned int block_no;
  unsigned int ind_block_no;

  for (block_no = 0; block_no < MAX_BLOCKS; block_no++) {
    if (block_no != 12) {
      /* direct */
      /* stumbled across an empty block, claim it */
      if (!inode->i_block[block_no] && gd->bg_free_blocks_count) {
        inode->i_block[block_no] = allocate_bitmap(gd->bg_block_bitmap, sb->s_blocks_count / 8) + 1;
        /* safety check, probably don't need */
        if (inode->i_block[block_no]) {
          inode->i_blocks += 2;
          gd->bg_free_blocks_count--;
          allocated = allocate_in_new_block(inode->i_block[block_no], file_type, name);
        }
      } else {
        allocated = allocate_space_in_block(inode->i_block[block_no], file_type, name);
      }
      if (allocated != NULL)
        return allocated;
    } else {
      /* indirect */
      unsigned int *indirect = (unsigned int *)
        (disk + BLOCK_OFFSET(inode->i_block[block_no]));
      for (ind_block_no = 0; ind_block_no < EXT2_BLOCK_SIZE / sizeof(unsigned int); ind_block_no++) {
        if (!indirect[ind_block_no] && gd->bg_free_blocks_count) {
          indirect[ind_block_no] = allocate_bitmap(gd->bg_block_bitmap, sb->s_blocks_count / 8) + 1;
        /* safety check, probably don't need */
          if (indirect[ind_block_no])
            inode->i_blocks += 2;
            gd->bg_free_blocks_count--;
            allocated = allocate_in_new_block(indirect[ind_block_no], file_type, name);
        } else {
          allocated = allocate_space_in_block(indirect[ind_block_no], file_type, name);
        }
        if (allocated != NULL)
          return allocated;
        }
    }
  }
  return NULL;
}

struct ext2_dir_entry_2 *getdir_from_symlink(struct ext2_dir_entry *symlink) {
  struct ext2_inode *symlink_inode = (struct ext2_inode *)(disk + BLOCK_OFFSET(gd->bg_inode_table) + INODE_OFFSET(symlink->inode));
  /* should already be null terminated */
  char *path = (char *)(disk + BLOCK_OFFSET(symlink_inode->i_block[0]));

  return getdir_from_path(path);
}

struct ext2_inode *make_symlink(unsigned int inode_no, char *path) {
  struct ext2_inode *inode = (struct ext2_inode *)(disk + BLOCK_OFFSET(gd->bg_inode_table) + INODE_OFFSET(inode_no));
  memset(inode, 0, sizeof(struct ext2_inode));
  inode->i_block[0] = allocate_bitmap(gd->bg_block_bitmap, sb->s_blocks_count / 8) + 1;
  if (!inode->i_block[0])
    return NULL;
  gd->bg_free_blocks_count--;
  char *dst = (char *)(disk + BLOCK_OFFSET(inode->i_block[0]));
  strncpy(dst, path, strlen(path) + 1);

  inode->i_mode |= EXT2_S_IFLNK;
  inode->i_blocks = 2;
  inode->i_size = strlen(path) + 1;
  inode->i_ctime = (unsigned int)time;
  inode->i_links_count = 1;
  return inode;
}

struct ext2_inode *make_dir_inode(unsigned int inode_no, unsigned int parent_inode_no) {
  struct ext2_inode *inode = (struct ext2_inode *)(disk + BLOCK_OFFSET(gd->bg_inode_table) + INODE_OFFSET(inode_no));
  memset(inode, 0, sizeof(struct ext2_inode));
  inode->i_block[0] = allocate_bitmap(gd->bg_block_bitmap, sb->s_blocks_count / 8) + 1;
  if (!inode->i_block[0])
    return NULL;
  else
    gd->bg_free_blocks_count--;

  inode->i_mode |= EXT2_S_IFDIR;
  inode->i_blocks = 2;
  inode->i_size = EXT2_BLOCK_SIZE;
  inode->i_ctime = (unsigned int)time;
  inode->i_links_count = 1;

  struct ext2_dir_entry_2 *self = (struct ext2_dir_entry_2 *)(disk + BLOCK_OFFSET(inode->i_block[0]));
  self->inode = inode_no;
  self->file_type = EXT2_FT_DIR;
  strcpy(self->name, ".");
  self->name_len = 1;
  self->rec_len = 12;
  // self->rec_len = sizeof(struct ext2_dir_entry_2) + self->name_len;
  // while (self->rec_len % 4 != 0)
  //   self->rec_len++;

  struct ext2_dir_entry_2 *parent = (struct ext2_dir_entry_2 *)(disk + BLOCK_OFFSET(inode->i_block[0]) + self->rec_len);
  parent->inode = parent_inode_no;
  parent->file_type = EXT2_FT_DIR;
  strcpy(parent->name, "..");
  parent->name_len = 2;
  parent->rec_len = EXT2_BLOCK_SIZE - 12;
//  parent->rec_len = EXT2_BLOCK_SIZE - 2 * sizeof(struct ext2_dir_entry_2) - self->name_len - parent->name_len;
  // while (parent->rec_len % 4 != 0)
  //   parent->rec_len++;
  
  gd->bg_used_dirs_count++;
  return inode;
}

struct ext2_dir_entry_2 *rm_from_inode(unsigned int inode_no, char *dir_name) {
  struct ext2_inode *inode = (struct ext2_inode *)
    (disk + BLOCK_OFFSET(gd->bg_inode_table) + INODE_OFFSET(inode_no));
  struct ext2_dir_entry_2 *dir;

  unsigned int block_no;
  unsigned int ind_block_no;

  for (block_no = 0; block_no < MAX_BLOCKS; block_no++) {
    if (block_no != 12) {
      /* direct */
      dir = rm_from_block(inode->i_block[block_no], dir_name);
      if (dir != NULL)
        return dir;
    } else {
      /* indirect */
      unsigned int *indirect = (unsigned int *)
        (disk + BLOCK_OFFSET(inode->i_block[block_no]));
      for (ind_block_no = 0;
           ind_block_no < EXT2_BLOCK_SIZE / sizeof(unsigned int);
           ind_block_no++) {
        dir = rm_from_block(indirect[ind_block_no], dir_name);
        if (dir != NULL)
          return dir;
      }
    }
  }
  return NULL;
}

struct ext2_dir_entry_2 *rm_from_block(unsigned int block, char *dir_name) {
  struct ext2_inode *inode;
  struct ext2_dir_entry_2 *prev_dir;
  struct ext2_dir_entry_2 *dir;
  unsigned int size = 0;
  char name[EXT2_NAME_LEN+1];

  prev_dir = NULL;
  dir = (struct ext2_dir_entry_2 *)(disk + BLOCK_OFFSET(block));
  while (size < EXT2_BLOCK_SIZE && dir->inode) {
    memcpy(name, dir->name, dir->name_len);
    name[dir->name_len] = 0;
    if (strcmp(name, dir_name) == 0) {
      if (dir->file_type == EXT2_FT_DIR) {
        fprintf(stderr, "%s: is a directory", name);
        exit(EINVAL);
      }
      if (prev_dir != NULL) {
        prev_dir->rec_len = prev_dir->rec_len + dir->rec_len;
      } else {
        free_bitmap(gd->bg_block_bitmap, sb->s_blocks_count / 8, block - 1);
        gd->bg_free_blocks_count++;
      }

      inode = (struct ext2_inode *)(disk + BLOCK_OFFSET(gd->bg_inode_table) + INODE_OFFSET(dir->inode));
      inode->i_links_count--;
      if (inode->i_links_count == 0) {
        free_inode(dir->inode);
      }
      return dir;
    }
    size += dir->rec_len;
    prev_dir = dir;
    dir = (struct ext2_dir_entry_2 *)(disk + BLOCK_OFFSET(block) + size);
  }
  return NULL;
}

void free_inode(unsigned int inode_no) {
  struct ext2_inode *inode = (struct ext2_inode *)(disk + BLOCK_OFFSET(gd->bg_inode_table) + INODE_OFFSET(inode_no));
  unsigned int block_no;
  free_bitmap(gd->bg_inode_bitmap, sb->s_inodes_count / 8, inode_no - 1);
  gd->bg_free_inodes_count++;
  for (block_no = 0; block_no < MAX_BLOCKS; block_no++) {
    if (inode->i_block[block_no]) {
      free_bitmap(gd->bg_block_bitmap, sb->s_blocks_count / 8, inode->i_block[block_no] - 1);
      gd->bg_free_blocks_count++;
    }
  }
  if (inode->i_mode & EXT2_S_IFDIR)
    gd->bg_used_dirs_count--;

  inode->i_size = 0;
  inode->i_dtime = (unsigned int)time;
}

int readbitmap(unsigned int block, unsigned int bytes) {
  int i, j;
  unsigned char *disk_byte;
  for (i = 0; i < bytes; i++) {
    disk_byte = (unsigned char*)(disk + BLOCK_OFFSET(block) + i);
    for (j = 0; j < 8; j++) {
      printf("%d", (*disk_byte & (1 << j)) == (1 << j));
    }
    printf(" ");
  }
  printf("\n");
  return 0;
}



int readinode(unsigned int inode_no) {
  if (inode_no == EXT2_GOOD_OLD_FIRST_INO)
    return 1;
  struct ext2_inode *inode =
    (struct ext2_inode *)(disk + BLOCK_OFFSET(gd->bg_inode_table) + INODE_OFFSET(inode_no));
  printf("[%d] type: %c size: %d links: %d blocks: %d\n",
    inode_no,
    inode->i_mode & EXT2_FT_REG_FILE ? 'f' : 'd', /* TODO all other types */
    inode->i_size,
    inode->i_links_count,
    inode->i_blocks);
  printf("[%d] Blocks: %u\n",
    inode_no,
    inode->i_block[0]); /* discovered this by accident wtf */
  return 0;
}

int readentries(unsigned int inode_no) {
  struct ext2_inode *inode = (struct ext2_inode *)
    (disk + BLOCK_OFFSET(gd->bg_inode_table) + INODE_OFFSET(inode_no));

  if (inode_no == EXT2_GOOD_OLD_FIRST_INO)
    return 1;
  if (!(inode->i_mode & EXT2_FT_DIR))
    return 2;

  unsigned int block_no;
  unsigned int size = 0;
  char name[EXT2_NAME_LEN+1];
  struct ext2_dir_entry_2 *entry;
  for(block_no = 0; block_no < MAX_BLOCKS; block_no++) {
    entry = (struct ext2_dir_entry_2 *)
      (disk + BLOCK_OFFSET(inode->i_block[block_no]));
    if (entry->inode)
      printf("   DIR BLOCK NUM: %d (for inode %d)\n", inode->i_block[block_no], inode_no);
    while(size < inode->i_size && entry->inode) {
      if (entry->name_len) {
        memcpy(name, entry->name, entry->name_len);
        name[entry->name_len] = 0;
        printf("Inode: %d rec_len: %d name_len: %d type= %c name=%s\n",
          entry->inode,
          entry->rec_len,
          entry->name_len,
          entry->file_type == EXT2_FT_DIR ? 'd' : 'f', /* TODO implement other FT */
          name);
      }
      size += entry->rec_len;
      entry = (struct ext2_dir_entry_2 *)
        (disk + BLOCK_OFFSET(inode->i_block[block_no]) + size);
    }
    size = 0;
  }
  return 0;
}

/* Loop non reserved inodes and process them if they have blocks */
int foreach_inode(int (*func)(unsigned int)) {
  int i;
  int inode_no;
  struct ext2_inode *inode;
  for (i = -1; i < (signed) INO_TBL_BLOCKS; i++) {
    if (i == -1)
      inode_no = EXT2_ROOT_INO;
    else
      inode_no = EXT2_GOOD_OLD_FIRST_INO + i;
    inode = (struct ext2_inode *)
      (disk + BLOCK_OFFSET(gd->bg_inode_table) + INODE_OFFSET(inode_no));
    if (inode->i_blocks)
      func(inode_no);
  }
  return 0;
}


int readmeta() {
  printf("Inodes: %d\n", sb->s_inodes_count);
  printf("Blocks: %d\n", sb->s_blocks_count);
  printf("Block group:\n");
  printf("\tblock bitmap: %d\n", gd->bg_block_bitmap);
  printf("\tinode bitmap: %d\n", gd->bg_inode_bitmap);
  printf("\tinode table: %d\n", gd->bg_inode_table);
  printf("\tfree blocks: %d\n", gd->bg_free_blocks_count);
  printf("\tfree inodes: %d\n", gd->bg_free_inodes_count);
  printf("\tused dirs: %d\n", gd->bg_used_dirs_count);
  return 0;
}

/**
 * This method reads a block to search for fname
 * will return a block with dir->name == fname
 * */
struct ext2_dir_entry_2 *search_block(char *fname, int block_num) {
  struct ext2_dir_entry_2 *dir = (struct ext2_dir_entry_2 *) (disk + EXT2_BLOCK_SIZE * block_num);
  int curr_pos = 0;

  // total size of the directories in a block cannot exceed a block size
  while(curr_pos < EXT2_BLOCK_SIZE) {
    // getting the name
    char *print_name = malloc(sizeof(char) * dir->name_len + 1);
    int u;
    int match =1;
    for (u=0;u<dir->name_len;u++) {
      print_name[u] = dir->name[u];
      if(fname[u] != print_name[u]) {
        match = 0;
      }
    }
    print_name[dir->name_len] = '\0';
    if (fname[dir->name_len] != '\0') {
      match = 0;
    }
    // check if matches fname
    if (match) {
      free(print_name);
      // return the inode
      return dir;
    }
    free(print_name);
    // move to next directory
    curr_pos = curr_pos + dir->rec_len;
    dir = (void *) dir + dir->rec_len;
    }
    return NULL;
}

/*
 * Given a directory inode, try to find fname in it by looking at the i blocks (first 12 direct, then indirects)
 */
struct ext2_dir_entry_2 *search_inode_directory(struct ext2_inode *cwd, char *fname) {
  if (cwd->i_mode & EXT2_S_IFDIR) {
    struct ext2_dir_entry_2 *entry;
    int block_num;
    // must be a directory
    int i;
    for (i=0;i<12;i++) {
      if(cwd->i_block[i]) {
        // get the block number where the directories are stored
        // does not equal zero => i_block is allocated
        block_num = cwd->i_block[i];
        // pass to helper function block num and fname
        // given back an inode
        entry = search_block(fname, block_num);
        if (entry == NULL) {
        } else {
          return entry;
        }
      } else {
        // i_block = 0 => unallocated
        // exit b/c it is not to be found here
        return NULL;
      }

    }
    int x;
    unsigned int *indirect;
    int bound = EXT2_BLOCK_SIZE/ sizeof(unsigned int);
    indirect = (unsigned int *) (disk + EXT2_BLOCK_SIZE * cwd->i_block[12]);  // perhaps use BLOCK_OFFSET here?
    for (x=0;x<bound; x++) {  // layer 1
      if (indirect[x]) {
        //indirect[x] is a block number
        entry = search_block(fname, indirect[x]);
        if (entry != NULL) {
          return entry;
        }

      } else {
        // unallocated space
        return NULL;
      }
    }
    // unable to find, return NULL
    return NULL;
  } else {
    exit(1);
  }
}

/*
 * Get the directory (ext2_dir_entry_2) with the name fname from the given inode
 */
struct ext2_dir_entry_2 *get_entry_from_inode(struct ext2_inode *inode, char *fname) {
  struct ext2_dir_entry_2 *entry;
  if (inode->i_size > 0) {
    // we do not want to search in empty inode
    // check type of file
    if (inode->i_mode & EXT2_S_IFDIR) {
      entry = search_inode_directory(inode, fname);
      return entry;
    }
    else {
      return (struct ext2_dir_entry_2 *)-1; // TODO: make macro for error codes such as these, also would casting this
    }
  }
  else {
    return (struct ext2_dir_entry_2 *)-2; // REVIEW: casting int to a struct to shut compiler up, would this introduce errors?
  }
  return NULL;
}

/*
 * create a new ext2_dir_entry_2 entry with the following information
 * file_type can be EXT2_FT_REG_FILE, EXT2_FT_DIR, EXT2_FT_SYMLINK
 */
void create_dir_entry_2(struct ext2_dir_entry_2 *entry, int rec_len, char *fname, int file_type) {
  // add properties
  entry->rec_len = rec_len;
  entry->name_len = strlen(fname);
  entry->file_type = file_type;
  strncpy(entry->name, fname, strlen(fname)); // change to memcpy?

}

/*
 * Given the length of the name, calculate the minimum amount of rec_len required for the ext2_dir_entry_2
 */
int calculate_rec_len(int name_len) {
  // 8 bytes required for metadata
  int reclen = 8 + name_len;
  // calculate padding to align to 4 bytes (divisible by 4 bytes)
  reclen = reclen + (4 - (reclen %4));
  return reclen;
}

/*
 * Given an entry, calculate if there is room to place a new ext2_dir_entry_2 with name of length name_len
 * Return 1 if available, -1 if not
 */
int is_entry_space_available(struct ext2_dir_entry_2 *entry, int name_len) {
  // check if there if a disreprency between entry's rec_len and the actual rec_len it requires
  // if there is, that means this is last entry in the block and it has extra padding/space
  int entry_real_rec_len = calculate_rec_len(entry->name_len);
  if (entry_real_rec_len != entry->rec_len) {
    // available space
    int available_space = (entry->rec_len) - entry_real_rec_len;
    // if available_space is >= rec_len required by name_len, then we can place our new entry here
    int new_dir_required_rec_len = calculate_rec_len(name_len);
    if (new_dir_required_rec_len <= available_space) {
      // we can fit!
      // return the rec_len required by the new entry in order to fit in this box and still point to the next_block
      // i.e. all rec_len entries summed in the block would equal EXT2_BLOCK_SIZE
      return available_space;
    }
    else {
      // there is available space, however it is not big enough to fit our new entry
      return -1;
    }
  }
  else {
    // no available space, return -1
    return -1;
  }
}

/*
 * Given a block number and a filename of the new entry, we return an entry if we could allocate it
 * in the block, NULL otherwise
 */
struct ext2_dir_entry_2 *allocate_entry_in_block_2(char *fname, int block_num, int file_type) {
  // get the entry associated with this block_num
  struct ext2_dir_entry_2 *dir = (struct ext2_dir_entry_2 *)(disk + BLOCK_OFFSET(block_num));
  // calculate the rec_len of new entry with name fname
  int curr_pos = 0;
  // loop through the block
  while (curr_pos < EXT2_BLOCK_SIZE) {
    // check if space available
    int is_space_available =  is_entry_space_available(dir, dir->name_len);
    if (is_space_available >=12) {  // min rec_len is 12
      // space is available
      // attempt allocation
      struct ext2_dir_entry_2 *curr;  // our entry we want to insert
      // reduce dir's rec_len
      dir->rec_len = calculate_rec_len(dir->name_len);
      curr_pos = curr_pos + dir->rec_len;
      curr = (struct ext2_dir_entry_2 *) (disk + BLOCK_OFFSET(block_num) + curr_pos);
      // create new entry
      create_dir_entry_2(curr, is_space_available, fname, file_type);
      return curr;
    }
    // move to next directory
    curr_pos = curr_pos + dir->rec_len;
    dir = (void *) dir + dir->rec_len;
  }
return NULL;
}

/*
 * Insert a new entry with name new_dir into parent_dir
 */
struct ext2_dir_entry_2 *allocate_entry_2(struct ext2_dir_entry_2 *parent_dir, char *new_dir, int file_type) {
  struct ext2_inode *parent_inode = (struct ext2_inode *) (disk + BLOCK_OFFSET(gd->bg_inode_table) + INODE_OFFSET(parent_dir->inode));
  // loop through the i_blocks to find a free/available block
  int i;
  int block_num;
  int first_empty_block = -1;
  struct ext2_dir_entry_2 *newDir;
  // search direct block
  for (i=0;i<12;i++) {
    if (parent_inode->i_block[i]) {
      // populate allocated blocks first
      block_num = parent_inode->i_block[i];
      // attempt an allocation in this block
      newDir = allocate_entry_in_block_2(new_dir, block_num, file_type);
      if (newDir) {
        // successfully allocated
        return newDir;
      }
    }
    else {
      // block is unallocated
      // we store this i, so if we have to create a new block we know where to start
      // since max length of filename is 255, then at max the size of actual reclen is (8 + 255) (4 - (8+255) %4) = 263 bytes, so we only need one block
      if (first_empty_block == -1) {
        first_empty_block = parent_inode->i_block[i];
      }
    }
  }

  // no space in first 12 blocks, check indirect blocks
  int x;
    unsigned int *indirect;
    int bound = EXT2_BLOCK_SIZE/ sizeof(unsigned int);
    indirect = (unsigned int *) (disk + EXT2_BLOCK_SIZE * parent_inode->i_block[12]);  // perhaps use BLOCK_OFFSET here?
    for (x=0;x<bound; x++) {    // layer 1
        if (indirect[x]) {
            //indirect[x] is a block number
            newDir = allocate_entry_in_block_2(new_dir, indirect[x], file_type);
            if (newDir != NULL) {
                return newDir;
            }

        } else {
          if (first_empty_block == -1) {
            first_empty_block = indirect[x];
          }
        }
    }


  if (first_empty_block != -1) {
    // try creating a new block
    int new_block = allocate_block();
    // set block 
    parent_inode->i_block[first_empty_block] = new_block;

    // 3. create a new dir and add it in this block
    // attempt an allocation in this block
    newDir = allocate_entry_in_block_2(new_dir, first_empty_block, file_type);
    if (newDir) {
      // successfully allocated
      return newDir;
    }
  }
    // no memory to place
    return NULL;

}

/**
Given an entry of type ext2_dir_entry_2, search within in to find a directory (ext2_dir_entry_2) with name fname
*/
struct ext2_dir_entry_2 *get_entry_from_entry(struct ext2_dir_entry_2 *entry, char *fname) {
  // an ext2_dir_entry_2 contains an indode
  int inode_num = entry->inode;
  struct ext2_inode *inode = (struct ext2_inode *) (disk + BLOCK_OFFSET(gd->bg_inode_table) + INODE_OFFSET(inode_num));
  return get_entry_from_inode(inode, fname);
}

/*
 * Allocate an inode and return the inode number
 */
int allocate_inode(unsigned short type) {
  int i, k;
  /* Get the inode bit map position from group descriptor. */
  int inode_bit_pos = gd->bg_inode_bitmap;

  /* Pointer pointing to inode bit map block. */
  unsigned char *inode_bit_map = disk + EXT2_BLOCK_SIZE * inode_bit_pos;

  /* Number of inodes == number of bits in inode_bitmap. */
  int num_inode_bit = sb->s_inodes_count;
  for (i = 0; i < num_inode_bit/(sizeof(unsigned char) * 8); i++) {
    /* Looping through each bit in a byte and print the least
      * significant bit first. That is reverse the order of the bit. */
    for (k = 0; k < 8; k++) {
      if (((inode_bit_map[i] >> k) & 1) == 0){
        // this means the inode is free
        int inode_num = i*(sizeof(unsigned char) *8) + k + 1;
        struct ext2_inode *freenode = (struct ext2_inode *) (disk + BLOCK_OFFSET(gd->bg_inode_table) + INODE_OFFSET(inode_num));    // add 1 offset since inodes start at 1
        // first wipe the node with 0
        memset(freenode, 0, sizeof(struct ext2_inode));
        // get time which is 64 bit
        // inode stores time as unsigned int, so 4,294,967,295 is max value which is ~136 years.
        // this might break after jan 1, 1970 + 136 years
        time_t seconds;
        seconds = time(NULL);
        // set properties
        freenode->i_mode |= type;     // could be EXT2_S_IFLNK, EXT2_S_IFREG,  EXT2_S_IFDIR
        freenode->i_ctime = seconds;  // creation time
        freenode->i_atime = seconds;  // access time
        freenode->i_mtime = seconds;  // modify time
        freenode->i_size = EXT2_BLOCK_SIZE;
        // indicate inode is in use
        gd->bg_free_inodes_count--;
        sb->s_free_inodes_count--;
        if (type == EXT2_FT_DIR || type == EXT2_S_IFDIR) {
          gd->bg_used_dirs_count++;
        }
        //  set the bitmap to 1 to indicate allocated/1
        inode_bit_map[i] |= 1 << k;
        return inode_num;
      }
    }
  }
  return -1;
}

/*
 * Allocate a free block and return the block num
 */
int allocate_block() {
  /* Group descriptor tells where the block bit map is. */
  int bit_pos = gd->bg_block_bitmap;

  /* Pointer pointing to the bitmap. */
  unsigned char *bit_map_block = disk + EXT2_BLOCK_SIZE * bit_pos;

  /* Number of blocks == number of bits in the bit map. */
  int num_bit = sb->s_blocks_count;

  int i;
  int k;

  /* Looping through the bit map block byte by byte. */
  for (i = 0; i < num_bit/(sizeof(unsigned char) * 8); i++) {
    /* Looping through each bit a byte. */
    for (k = 0; k < 8; k++) {
      if (((bit_map_block[i] >> k) & 1) == 0) {
        // block is free
        int block_num = i*(sizeof(unsigned char) *8) + k + 1;   // add 1 offset since blocks start at 1
        unsigned char *block = disk + BLOCK_OFFSET(block_num);
        // wipe the block
        memset(block, 0, EXT2_BLOCK_SIZE);
        // decrement num of available blocks
        gd->bg_free_blocks_count--;
        sb->s_free_blocks_count--;
        // set the bitmap to 1 to indicated allocated/1
        bit_map_block[i] |= 1 << k;
        return block_num;
      }
    }
  }
  return -1;
}
