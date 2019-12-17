#define MAX_BLOCKS             13
#define BLOCK_SIZE             (EXT2_BLOCK_SIZE << sb->s_log_block_size)
#define BLOCK_OFFSET(block)    (EXT2_BLOCK_SIZE + (block - 1) * BLOCK_SIZE)
#define INODE_OFFSET(inode_no) ((inode_no - 1) * sizeof(struct ext2_inode))
#define INODES_PER_BLOCK       (BLOCK_SIZE / sizeof(struct ext2_inode))
#define INO_TBL_BLOCKS         (sb->s_inodes_per_group / INODES_PER_BLOCK)

struct ext2_dir_entry_2 *getdir_from_path(char *path);
struct ext2_dir_entry_2 *getdir_from_inode(unsigned int inode_no, char *dir_name);
struct ext2_dir_entry_2 *getdir_from_block (unsigned int block, char *dir_name);

unsigned int allocate_bitmap(unsigned int block, unsigned int bytes);
struct ext2_dir_entry_2 *allocate_space_in_block(unsigned int block, unsigned char file_type, char *name);
struct ext2_dir_entry_2 *allocate_space_in_inode(unsigned int inode_no, unsigned char file_type, char *name);
struct ext2_inode *make_dir_inode(unsigned int inode_no, unsigned int parent_inode_no);

struct ext2_dir_entry_2 *rm_from_inode(unsigned int inode_no, char *dir_name);
struct ext2_dir_entry_2 *rm_from_block(unsigned int block, char *dir_name);
void free_inode(unsigned int inode_no);
void free_bitmap(unsigned int bitmap_block, unsigned int bitmap_len, unsigned int target_bit);

struct ext2_inode *make_symlink(unsigned int inode_no, char *path);
struct ext2_dir_entry_2 *getdir_from_symlink(struct ext2_dir_entry *symlink);

int readbitmap(unsigned int block, unsigned int bytes);
int readinode(unsigned int inode_no);
int readentries(unsigned int inode_no);
int foreach_inode(int (*func)(unsigned int));
int readmeta();

struct ext2_dir_entry_2 *search_block(char *fname, int block_num);
struct ext2_dir_entry_2 *search_inode_directory(struct ext2_inode *cwd, char *fname);
struct ext2_dir_entry_2 *get_entry_from_inode(struct ext2_inode *inode, char *fname);
void create_dir_entry_2(struct ext2_dir_entry_2 *entry, int rec_len, char *fname, int file_type);
int calculate_rec_len(int name_len);
int is_entry_space_available(struct ext2_dir_entry_2 *entry, int name_len);
struct ext2_dir_entry_2 *allocate_entry_in_block_2(char *fname, int block_num, int file_type);
struct ext2_dir_entry_2 *allocate_entry_2(struct ext2_dir_entry_2 *parent_dir, char *new_dir, int file_type);
struct ext2_dir_entry_2 *get_entry_from_entry(struct ext2_dir_entry_2 *entry, char *fname);
int allocate_inode(unsigned short type);
int allocate_block();

unsigned char *disk;
struct ext2_super_block *sb;
struct ext2_group_desc *gd;
