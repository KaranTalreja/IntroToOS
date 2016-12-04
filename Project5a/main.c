#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define stat xv6_stat  // avoid clash with host struct stat
#define dirent xv6_dirent  // avoid clash with host struct stat
#include "include/types.h"
#include "include/fs.h"
#include "include/stat.h"
#undef stat
#undef dirent
#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0') 
char output[1000] = {0};
#define BLOCK_SIZE (512)

int nblocks = 995;
int ninodes = 200;
int size = 1024;
#define fscheck_printf(format, ...) \
  sprintf(output, format, ##__VA_ARGS__); \
  if (write(STDOUT_FILENO, output, strlen(output)) == -1) \
    perror("Error in writing to STDOUT\n");

#define fscheck_perror(format, ...) \
  sprintf(output, format, ##__VA_ARGS__); \
  if (write(STDERR_FILENO, output, strlen(output)) == -1) \
    perror("Error in writing to STDERR\n");

// @brief Function to print out usage of this program in case of invalid args
void usage_and_exit() {
  fscheck_printf("Usage: fscheck file_system_image\n");
  exit(1);
}

void* rsect (uint sect, void* img_ptr) {
  return img_ptr + (sect*BSIZE);
}

void check_balloced(uint block, void* img_ptr) {
  char* bitmap = rsect(BBLOCK(block, ninodes), img_ptr);
  uint off = block / 8;
  uint bit_off = block % 8;
  if (0x01 != ((bitmap[off] >> (bit_off)) & 0x01)) {
    fscheck_perror("ERROR: address used by inode but marked free in bitmap.\n");
    exit(1);
  }
  //printf ("bl:%d, bp:"BYTE_TO_BINARY_PATTERN"\n", block, BYTE_TO_BINARY(bitmap[off]));
  return;
}

int seen_root = 0;
void check_dir_inode (uint inode_num, struct dinode* dip, void* img_ptr) {
  uint file_size = dip->size;
  uint n_blocks = file_size / 512;
  int i = 0;
  char *buf;
  uint* indirect = NULL;
  if (n_blocks > NDIRECT) indirect = rsect(dip->addrs[NDIRECT], img_ptr);
  int seen_self_dir = 0, seen_parent_dir = 0;
  for (i=0; i< n_blocks; i++) {
    if (i < NDIRECT) {
      buf = rsect(dip->addrs[i], img_ptr);
      struct xv6_dirent *dir_entry = (struct xv6_dirent*)buf;
      while (0 != dir_entry->inum) {
        if (0 == seen_self_dir && (0 == strcmp(dir_entry->name, "."))) seen_self_dir = 1;
        if (0 == seen_parent_dir && (0 == strcmp(dir_entry->name, ".."))) {
          seen_parent_dir = 1;
          if (0 == seen_root && dir_entry->inum == inode_num && inode_num == 1) seen_root = 1;
        }
        //printf ("%d,%s,%d\n",inode_num, dir_entry->name, dir_entry->inum);
        dir_entry++;
      }
    }
    else {
      uint curr_addr = indirect[i-NDIRECT];
      buf = rsect(curr_addr, img_ptr);
      struct xv6_dirent *dir_entry = (struct xv6_dirent*)buf;
      while (0 != dir_entry->inum) {
        if (0 == seen_self_dir && (0 == strcmp(dir_entry->name, "."))) seen_self_dir = 1;
        if (0 == seen_parent_dir && (0 == strcmp(dir_entry->name, ".."))) {
          seen_parent_dir = 1;
          if (0 == seen_root && dir_entry->inum == inode_num && inode_num == 1) seen_root = 1;
        }
        //printf ("%d,%s,%d\n",inode_num, dir_entry->name, dir_entry->inum);
        dir_entry++;
      }
    }
  }
  if (!(seen_self_dir & seen_parent_dir)) {
    fscheck_perror("directory not properly formatted.\n");
    exit(1);
  }
}

void check_inode_addrs(struct dinode* dip, void* img_ptr) {
  uint file_size = dip->size;
  uint n_blocks = file_size / 512;
  int i;
  char *buf; 
  uint* indirect = NULL;
  if (n_blocks > NDIRECT) {
    uint curr_addr = dip->addrs[NDIRECT];
    //printf ("D->I Addr:%d\n", curr_addr);
    if (curr_addr < 29 || curr_addr > 1024) {
      fscheck_perror("ERROR: bad address in inode.\n");
      exit(1);
    }
    check_balloced(curr_addr, img_ptr);
    indirect = rsect(curr_addr, img_ptr);
  }
  for (i=0; i< n_blocks; i++) {
    if (i < NDIRECT) {
      //printf ("D Addr:%d\n", dip->addrs[i]);
      uint curr_addr = dip->addrs[i];
      if (curr_addr < 29 || curr_addr > 1024) {
        fscheck_perror("ERROR: bad address in inode.\n");
        exit(1);
      }
      check_balloced(curr_addr, img_ptr);
    }
    else {
      //printf ("I Addr%d\n", indirect[i-NDIRECT]);
      uint curr_addr = indirect[i-NDIRECT];
      if (curr_addr < 29 || curr_addr > 1024) {
        fscheck_perror("ERROR: bad address in inode.\n");
        exit(1);
      }
      check_balloced(curr_addr, img_ptr);
    }
  }
}

// unused | superblock | inode table | bitmap (data) | data blocks

int
main(int argc, char* argv[]) {
  if (argc > 2) usage_and_exit();
  char* img_file = argv[1];
  int fd = open(img_file, O_RDONLY);
  if (fd < 0) {
    fscheck_perror("image not found.\n");
    exit(1);
  }

  int rc;
  struct stat sbuf;
  rc = fstat(fd, &sbuf);
  assert(rc == 0);

  void* img_ptr = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  assert(img_ptr != MAP_FAILED);

  struct superblock* sb = (struct superblock*)rsect(1, img_ptr);
  //fscheck_perror("ERROR: bad inode.\n");
  //exit(1);
  int i;
  struct dinode *dip = (struct dinode*)rsect(2, img_ptr);
  for (i = 0; i< sb->ninodes; i++) {
    switch(dip->type) {
      case 0:
        break;
      case T_DEV:
      case T_FILE: {
        //printf ("File addresses\n");
        check_inode_addrs(dip, img_ptr);
        //printf ("File addresses end\n");
        break;
      }
      case T_DIR: {
        //printf ("Dir addresses\n");
        check_inode_addrs(dip, img_ptr);
        //printf ("Dir addresses end\n");
        check_dir_inode (i, dip, img_ptr);
        break;
      }
      default:
      fscheck_perror("ERROR: bad inode.\n");
      exit(1);
    }
    dip++;
  }
  if (0 == seen_root) {
    fscheck_perror("ERROR: root directory does not exist.\n");
    exit(1);
  }
  return 0;
}