#include "inode_manager.h"

// disk layer -----------------------------------------

disk::disk()
{
  bzero(blocks, sizeof(blocks));
}

void
disk::read_block(blockid_t id, char *buf)
{
  if (!buf) return;
  memcpy(buf, blocks[id], BLOCK_SIZE);
}

void
disk::write_block(blockid_t id, const char *buf)
{
  if (!buf) return;
  memcpy(blocks[id], buf, BLOCK_SIZE);
}

// block layer -----------------------------------------

// Allocate a free disk block.
blockid_t
block_manager::alloc_block()
{
  /*
   * your code goes here.
   * note: you should mark the corresponding bit in block bitmap when alloc.
   * you need to think about which block you can start to be allocated.
   */
  const int data_block_start =
                  IBLOCK(sb.ninodes, sb.nblocks) + 1;
  
  for (uint32_t i = data_block_start; i < sb.nblocks; ++i) {
    if (!using_blocks.count(i) || !using_blocks[i]) {
      using_blocks[i] = 1;
      return i;
    }
  }
  return 0;
}

void
block_manager::free_block(uint32_t id)
{
  /* 
   * your code goes here.
   * note: you should unmark the corresponding bit in the block bitmap when free.
   */
  if (!using_blocks[id]) {
    return;
  }
  using_blocks[id] = 0;
  return;
}

// The layout of disk should be like this:
// |<-sb->|<-free block bitmap->|<-inode table->|<-data->|
block_manager::block_manager()
{
  d = new disk();

  // format the disk
  sb.size = BLOCK_SIZE * BLOCK_NUM;
  sb.nblocks = BLOCK_NUM;
  sb.ninodes = INODE_NUM;

}

void
block_manager::read_block(uint32_t id, char *buf)
{
  d->read_block(id, buf);
}

void
block_manager::write_block(uint32_t id, const char *buf)
{
  d->write_block(id, buf);
}

// inode layer -----------------------------------------

inode_manager::inode_manager()
{
  bm = new block_manager();
  uint32_t root_dir = alloc_inode(extent_protocol::T_DIR);
  if (root_dir != 1) {
    printf("\tim: error! alloc first inode %d, should be 1\n", root_dir);
    exit(0);
  }
}

/* Create a new file.
 * Return its inum. */
uint32_t
inode_manager::alloc_inode(uint32_t type)
{
  /* 
   * your code goes here.
   * note: the normal inode block should begin from the 2nd inode block.
   * the 1st is used for root_dir, see inode_manager::inode_manager().
   */
  inode *inode_disk;
  char block[BLOCK_SIZE];
  uint32_t i = (type == extent_protocol::T_FILE) ? 2 : 1;

  for (; i < bm->sb.ninodes; ++i) {
    bm->read_block(IBLOCK(i, bm->sb.nblocks), block);
    inode_disk = (struct inode*)block + i%IPB;
    if (inode_disk->type == 0) {
      inode_disk->type = type;
      inode_disk->size = 0;
      put_inode(i, inode_disk);
      return i;
    }
  }

  return 1;
}

void
inode_manager::free_inode(uint32_t inum)
{
  /* 
   * your code goes here.
   * note: you need to check if the inode is already a freed one;
   * if not, clear it, and remember to write back to disk.
   */
  struct inode *ino;

  if ((ino = get_inode(inum)) == NULL) {
    return;
  }

  if (ino->size > NDIRECT * BLOCK_SIZE) {
    bm->free_block(ino->blocks[NDIRECT]);
  }

  memset(ino, 0, sizeof(struct inode));

  bm->write_block(IBLOCK(inum, bm->sb.nblocks), (char *)ino);
  free(ino);
  return;
}


/* Return an inode structure by inum, NULL otherwise.
 * Caller should release the memory. */
struct inode* 
inode_manager::get_inode(uint32_t inum)
{
  struct inode *ino, *ino_disk;
  char buf[BLOCK_SIZE];

  printf("\tim: get_inode %d\n", inum);

  if (inum < 0 || inum >= INODE_NUM) {
    printf("\tim: inum out of range\n");
    return NULL;
  }

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  // printf("%s:%d\n", __FILE__, __LINE__);

  ino_disk = (struct inode*)buf + inum%IPB;
  if (ino_disk->type == 0) {
    printf("\tim: inode not exist\n");
    return NULL;
  }

  ino = (struct inode*)malloc(sizeof(struct inode));
  *ino = *ino_disk;

  return ino;
}

void
inode_manager::put_inode(uint32_t inum, struct inode *ino)
{
  char buf[BLOCK_SIZE];
  struct inode *ino_disk;

  printf("\tim: put_inode %d\n", inum);
  if (ino == NULL)
    return;

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  ino_disk = (struct inode*)buf + inum%IPB;
  *ino_disk = *ino;
  bm->write_block(IBLOCK(inum, bm->sb.nblocks), buf);
}

#define MIN(a,b) ((a)<(b) ? (a) : (b))

/* Get all the data of a file by inum. 
 * Return alloced data, should be freed by caller. */
void
inode_manager::read_file(uint32_t inum, char **buf_out, int *size)
{
  /*
   * your code goes here.
   * note: read blocks related to inode number inum,
   * and copy them to buf_Out
   */
  char *buf, *buf_p;
  uint32_t block[BLOCK_SIZE];
  struct inode* ino;
  unsigned int file_len;
  unsigned int read_so_far = 0;

  if ((ino = get_inode(inum)) == NULL) {
    *buf_out = NULL;
    return;
  }

  file_len = ino->size;

  if (!file_len) {
    buf = (char *)malloc(1);
    buf[0] = '\0';
    return;
  }

  buf = (char *)malloc(file_len);
  buf_p = buf;
  for (unsigned int b = 0; read_so_far < file_len && b < NDIRECT;
       read_so_far += BLOCK_SIZE, ++b) {
    if (file_len - read_so_far > BLOCK_SIZE) {
      bm->read_block(ino->blocks[b], buf_p);
      buf_p += BLOCK_SIZE;
    } else {
      bm->read_block(ino->blocks[b], (char *)block);
      memcpy(buf_p, block, file_len - read_so_far);
    }
  }

  // handle indirect block
  if (read_so_far < file_len) {
    bm->read_block(ino->blocks[NDIRECT], (char *)block);
    for (uint32_t* b = block; read_so_far < file_len;
         read_so_far += BLOCK_SIZE, ++b) {
      if (file_len - read_so_far > BLOCK_SIZE) {
        bm->read_block(*b, buf_p);
        buf_p += BLOCK_SIZE;
      } else {
        bm->read_block(*b, (char *)block);
        memcpy(buf_p, block, file_len - read_so_far);
      }
    }
  }

  *size = file_len;
  *buf_out = buf;
  free(ino);
  return;
}

/* alloc/free blocks if needed */
void
inode_manager::write_file(uint32_t inum, const char *buf, int size)
{
  /*
   * your code goes here.
   * note: write buf to blocks of inode inum.
   * you need to consider the situation when the size of buf 
   * is larger or smaller than the size of original inode
   */

  struct inode* ino;
  uint32_t block[BLOCK_SIZE / sizeof(uint32_t)];
  char padding_block[BLOCK_SIZE];
  unsigned int cur_block;
  unsigned int write_so_far = 0;

  if ((ino = get_inode(inum)) == NULL) {
    return;
  }

  unsigned int block_need = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
  unsigned int block_had  = (ino->size + BLOCK_SIZE - 1) / BLOCK_SIZE;

  for (cur_block = 0; cur_block < MIN(block_need, MIN(block_had, NDIRECT));
       ++cur_block) {
    if (size - write_so_far > BLOCK_SIZE) {
      bm->write_block(ino->blocks[cur_block], buf);
      buf += BLOCK_SIZE;
    } else {
      memcpy(padding_block, buf, size - write_so_far);
      bm->write_block(ino->blocks[cur_block], padding_block);
      buf += BLOCK_SIZE;
    }
    write_so_far += BLOCK_SIZE;
  }

  if (cur_block < NDIRECT) {
    // block_need < NDIRECT or block_had < NDIRECT
    for (; cur_block < block_had; ++cur_block) {
      if (cur_block < NDIRECT) {
        bm->free_block(ino->blocks[cur_block]);
      } else {
        bm->read_block(ino->blocks[NDIRECT], (char *)block);
        for (; cur_block < block_had; ++cur_block) {
          bm->free_block(ino->blocks[block[cur_block - NDIRECT]]);
        }
      }
    }
  
    int alloc_b;
    for (; cur_block < block_need; ++cur_block) {
      if (cur_block < NDIRECT) {
        alloc_b = bm->alloc_block();
        ino->blocks[cur_block] = alloc_b;
        if (size - write_so_far > BLOCK_SIZE) {
          bm->write_block(alloc_b, buf);
        } else {
          memcpy(padding_block, buf, size - write_so_far);
          bm->write_block(alloc_b, padding_block);
        }
        write_so_far += BLOCK_SIZE;
        buf += BLOCK_SIZE;
      } else {
        alloc_b = bm->alloc_block();
        ino->blocks[NDIRECT] = alloc_b;
        for (; cur_block < block_had; ++cur_block) {
          alloc_b = block[cur_block - NDIRECT] = bm->alloc_block();
          if (size - write_so_far > BLOCK_SIZE) {
            bm->write_block(alloc_b, buf);
          } else {
            memcpy(padding_block, buf, size - write_so_far);
            bm->write_block(alloc_b, padding_block);
          }
          write_so_far += BLOCK_SIZE;
          buf += BLOCK_SIZE;
        }
        bm->write_block(ino->blocks[NDIRECT], (char *)block);
      }
    }
  } else {
    // block_need = NDIRECT and block_had = NDIDRECT
    bm->read_block(ino->blocks[NDIRECT], (char *)block);

    for (; cur_block < MIN(block_need, block_had); ++cur_block) {
      if (size - write_so_far > BLOCK_SIZE) {
        bm->write_block(block[cur_block - NDIRECT], buf);
      } else {
        memcpy(padding_block, buf, size - write_so_far);
        bm->write_block(block[cur_block - NDIRECT], padding_block);
      }
      write_so_far += BLOCK_SIZE;
      buf += BLOCK_SIZE;
    }
    for (; cur_block < block_had; ++cur_block) {
      bm->free_block(block[cur_block - NDIRECT]);
    }
    for (; cur_block < block_need; ++cur_block) {
      int alloc_b = bm->alloc_block();
      block[cur_block - NDIRECT] = alloc_b;
      if (size - write_so_far > BLOCK_SIZE) {
        bm->write_block(alloc_b, buf);
      } else {
        memcpy(padding_block, buf, size - write_so_far);
        bm->write_block(alloc_b, padding_block);
      }
      write_so_far += BLOCK_SIZE;
      buf += BLOCK_SIZE;
      bm->write_block(ino->blocks[NDIRECT], (char *)block);
    }
  } 

  ino->size = size;
  put_inode(inum, ino);
  free(ino);
  return;
}

void
inode_manager::getattr(uint32_t inum, extent_protocol::attr &a)
{
  /*
   * your code goes here.
   * note: get the attributes of inode inum.
   * you can refer to "struct attr" in extent_protocol.h
   */
  inode* inode;
  if ((inode = get_inode(inum)) == NULL) {
    return;
  }
  a.atime = inode->atime;
  a.ctime = inode->ctime;
  a.mtime = inode->mtime;
  a.size  = inode->size;
  a.type  = inode->type;
  free(inode);
  return;
}

void
inode_manager::remove_file(uint32_t inum)
{
  /*
   * your code goes here
   * note: you need to consider about both the data block and inode of the file
   */

  struct inode *ino;
  unsigned int freed_so_far;
  uint32_t indirect_block[BLOCK_SIZE / sizeof(uint32_t)];

  if ((ino = get_inode(inum)) == NULL) {
    return;
  }

  for (freed_so_far = 0;
       freed_so_far < ino->size && freed_so_far < NDIRECT * BLOCK_SIZE;
       freed_so_far += BLOCK_SIZE) {
    bm->free_block(ino->blocks[freed_so_far/BLOCK_SIZE]);
  }

  if (freed_so_far < ino->size) {
    bm->read_block(ino->blocks[NDIRECT], (char *)indirect_block);
    uint32_t *bp = indirect_block;
    for (; freed_so_far < ino->size; freed_so_far += BLOCK_SIZE, ++bp) {
      bm->free_block(*bp);
    }
  }

  free(ino);
  free_inode(inum);
  return;
}
