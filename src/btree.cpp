#include "nbtree_w.h"
#include "config.h"
#include "util.h"
#include "timer.h"
#include "benchmarks.h"
#include <fstream>
#include <thread>
#include <boost/thread/barrier.hpp>
#include <unistd.h>
#include <sys/mman.h>

using namespace std;

extern "C" {

struct BTree;

void* btree_create(void);
void btree_init_for_thread(int thread_id);
int btree_insert(void* tree, entry_key_t key, entry_key_t value);
int btree_remove(void* tree, entry_key_t key);
entry_key_t btree_find(void* tree, entry_key_t key);
}

char *thread_space_start_addr;
__thread char *start_addr;
__thread char *curr_addr;
uint64_t allocate_size = 113ULL * 1024ULL * 1024ULL * 1024ULL;

char *thread_mem_start_addr;
__thread char *start_mem;
__thread char *curr_mem;
uint64_t allocate_mem = 113ULL * 1024ULL * 1024ULL * 1024ULL;

struct BTree {
    btree* inner;
};

void* btree_create(void) {
    int fd = open("/mnt/pmem1/btree", O_RDWR);
    if (fd < 0)
    {
        printf("[NVM MGR]\tfailed to open nvm file\n");
        exit(-1);
    }
    if (ftruncate(fd, allocate_size) < 0)
    {
        printf("[NVM MGR]\tfailed to truncate file\n");
        exit(-1);
    }
    void *pmem = mmap(NULL, allocate_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    memset(pmem, 0, SPACE_OF_MAIN_THREAD);
    start_addr = (char *)pmem;
    curr_addr = start_addr;
    thread_space_start_addr = (char *)pmem + SPACE_OF_MAIN_THREAD;
    void *mem = new char[allocate_mem];
    start_mem = (char *)mem;
    curr_mem = start_mem;
    thread_mem_start_addr = (char *)mem + MEM_OF_MAIN_THREAD;
    
    // Warm-up
    // printf("[COORDINATOR]\tWarm-up..\n");
    // printf("---\n");
    BTree* tree = new BTree;
    tree->inner = new btree();
    // printf("+++\n");
    return (void*)(tree);
}
void btree_init_for_thread(int thread_id) {
    start_addr = thread_space_start_addr + thread_id * SPACE_PER_THREAD;
    curr_addr = start_addr;
    start_mem = thread_mem_start_addr + thread_id * MEM_PER_THREAD;
    curr_mem = start_mem;
}
int btree_insert(void* tree, entry_key_t key, entry_key_t value) {

    // printf("%ld -1\n", key);
    // printf("%ld -2\n", key);
    bool ret = ((BTree*)(tree))->inner->insert(key, (char *)(value));
    // printf("%x insert %x\n", key, (entry_key_t)(curr_addr));

    if(ret == false) {
        printf("%ld insert failed\n", key);
    }
    return ret;
}

entry_key_t btree_find(void* tree, entry_key_t key) {
    // printf("%ld -3\n", key);
    return (entry_key_t)(((BTree*)(tree))->inner->search(key));
}
int btree_remove(void* tree, entry_key_t key) {
    // printf("%ld -3\n", key);
    return (entry_key_t)(((BTree*)(tree))->inner->remove(key));
}

// int main(void) {
//     void* tree = btree_create();
//     printf("create ok\n");
//     btree_insert(tree, 10, 10);
//     printf("%ld\n", btree_find(tree, 10));
//     return 0;
// }