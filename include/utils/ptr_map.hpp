#ifndef __PTR_MAP_HPP
#define __PTR_MAP_HPP

#define MAX_CACHE_ENTRY (1 << 16)
#define CACHE_HASH(key) ((((unsigned long)key) >> 16) & 0xffff)

#define ROOT_ENTRY (1 << 16)
#define ROOT_HASH(key) ((((unsigned long)key) >> 16) & 0xffff)

// A red-black tree with caching for fast memory allocation tracking.
// Inspired from FuZZan

class ptr_map {
 public:
  ptr_map();

  ~ptr_map();

  ptr_map(ptr_map &other) = delete;
  ptr_map(ptr_map &&other) = delete;

  ptr_map &operator=(ptr_map &other) = delete;
  ptr_map &operator=(ptr_map &&other) = delete;

  enum rbtree_node_color { RED, BLACK };

  class rbtree_node {
   public:
    void *key_ = nullptr;
    char *type_name_ = nullptr;
    int alloc_size_ = 0;
    rbtree_node *left_ = nullptr;
    rbtree_node *right_ = nullptr;
    rbtree_node *parent_ = nullptr;
    enum rbtree_node_color color_ = RED;

    rbtree_node(void *key, char *type_name, int alloc_size);
    ~rbtree_node();

    rbtree_node(rbtree_node &other) = delete;
    rbtree_node(rbtree_node &&other) = delete;

    rbtree_node &operator=(rbtree_node &other) = delete;
    rbtree_node &operator=(rbtree_node &&other) = delete;

    rbtree_node *get_uncle();
    rbtree_node *get_grandparent();
  };

  typedef struct CacheEntry_t {
    rbtree_node *node_;
    bool availability_;
  } CacheEntry;

  CacheEntry cache[MAX_CACHE_ENTRY];

  rbtree_node *roots[ROOT_ENTRY];

  void insert(void *key, char *type_name, int alloc_size);

  rbtree_node *find(void *key);

  void remove(void *key);

  void print_tree(rbtree_node *n, unsigned int);

 private:
  void insert_case1(rbtree_node *n);
  void insert_case2(rbtree_node *n);
  void insert_case3(rbtree_node *n);
  void insert_case4(rbtree_node *n);
  void rotate_left(rbtree_node *n);
  void rotate_right(rbtree_node *n);

  void delete_case1(rbtree_node *n);
  void delete_case2(rbtree_node *n);
  void delete_case3(rbtree_node *n);
  void delete_case4(rbtree_node *n);
  void delete_case5(rbtree_node *n);
  void delete_case6(rbtree_node *n);

  void print_cache();
  void print_tree_sub(rbtree_node *n, unsigned int depth);
};

#endif