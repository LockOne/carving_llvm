#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>

#include "utils/data_utils.hpp"
#include "utils/ptr_map.hpp"

#define MAX_NUM_FILE 8
#define MINSIZE 3
#define MAXSIZE 24

static char *outdir_name = NULL;

static int carved_index = 0;

// Function pointer names
static map<void *, char *> func_ptrs;

// inputs, work as similar as function call stack
static vector<IVAR *> carved_objs;
static vector<POINTER> carved_ptrs;

// memory info
ptr_map alloced_ptrs;
// map<void *, struct typeinfo> alloced_ptrs;

int __carv_cur_class_index = -1;
int __carv_cur_class_size = -1;

bool __carv_opened = false;
bool __carv_ready = false;
char __carv_depth = 0;

static map<char *, classinfo> class_info;

extern "C" {

void __insert_obj_info(char *name, char *type_name) {
  if (!__carv_opened) {
    return;
  }
  VAR<char *> *inputv = new VAR<char *>(type_name, name, INPUT_TYPE::OBJ_INFO);
  carved_objs.push_back((IVAR *)inputv);
}

void Carv_char(char input) {
  if (!__carv_opened) {
    return;
  }
  VAR<char> *inputv = new VAR<char>(input, 0, INPUT_TYPE::CHAR);
  carved_objs.push_back((IVAR *)inputv);
}

void Carv_short(short input) {
  if (!__carv_opened) {
    return;
  }
  VAR<short> *inputv = new VAR<short>(input, 0, INPUT_TYPE::SHORT);
  carved_objs.push_back((IVAR *)inputv);
}

void Carv_int(int input) {
  if (!__carv_opened) {
    return;
  }
  VAR<int> *inputv = new VAR<int>(input, 0, INPUT_TYPE::INT);
  carved_objs.push_back((IVAR *)inputv);
}

void Carv_longtype(long input) {
  if (!__carv_opened) {
    return;
  }
  VAR<long> *inputv = new VAR<long>(input, 0, INPUT_TYPE::LONG);
  carved_objs.push_back((IVAR *)inputv);
}

void Carv_longlong(long long input) {
  if (!__carv_opened) {
    return;
  }
  VAR<long long> *inputv = new VAR<long long>(input, 0, INPUT_TYPE::LONGLONG);
  carved_objs.push_back((IVAR *)inputv);
}

void Carv_float(float input) {
  if (!__carv_opened) {
    return;
  }
  VAR<float> *inputv = new VAR<float>(input, 0, INPUT_TYPE::FLOAT);
  carved_objs.push_back((IVAR *)inputv);
}

void Carv_double(double input) {
  if (!__carv_opened) {
    return;
  }
  VAR<double> *inputv = new VAR<double>(input, 0, INPUT_TYPE::DOUBLE);
  carved_objs.push_back((IVAR *)inputv);
}

int Carv_pointer(void *ptr, char *type_name, int default_idx,
                 int default_size) {
  if (!__carv_opened) {
    return 0;
  }

  if (ptr == NULL) {
    VAR<void *> *inputv = new VAR<void *>(NULL, 0, INPUT_TYPE::NULLPTR);
    carved_objs.push_back((IVAR *)inputv);
    return 0;
  }

  // Find already carved ptr
  int index = 0;
  int num_carved_ptrs = carved_ptrs.size();

  int end_index = -1;
  int end_offset = -1;
  while (index < num_carved_ptrs) {
    POINTER *carved_ptr = carved_ptrs.get(index);
    char *carved_addr = (char *)carved_ptr->addr;
    int carved_ptr_size = carved_ptr->alloc_size;
    char *carved_addr_end = carved_addr + carved_ptr_size;
    if ((carved_addr <= ptr) && (ptr < carved_addr_end)) {
      int offset = ((char *)ptr) - carved_addr;
      VAR<int> *inputv = new VAR<int>(index, 0, offset, INPUT_TYPE::PTR);
      carved_objs.push_back((IVAR *)inputv);
      // Won't carve again.
      return 0;
    } else if (ptr == carved_addr_end) {
      end_index = index;
      end_offset = carved_ptr_size;
    }
    index++;
  }

  if (end_index != -1) {
    VAR<int> *inputv = new VAR<int>(end_index, 0, end_offset, INPUT_TYPE::PTR);
    carved_objs.push_back((IVAR *)inputv);
    return 0;
  }

  ptr_map::rbtree_node *ptr_node = alloced_ptrs.find(ptr);
  if (ptr_node == NULL) {
    auto search = func_ptrs.find(ptr);
    if (search != NULL) {
      VAR<char *> *inputv = new VAR<char *>(*search, 0, INPUT_TYPE::FUNCPTR);
      carved_objs.push_back((IVAR *)inputv);
      return 0;
    }

    VAR<void *> *inputv =
        new VAR<void *>(ptr, type_name, INPUT_TYPE::UNKNOWN_PTR);
    carved_objs.push_back((IVAR *)inputv);
    return 0;
  }

  char *alloc_ptr = (char *)ptr_node->key_;
  int ptr_alloc_size = ptr_node->alloc_size_;

  int new_carved_ptr_index = carved_ptrs.size();

  __carv_cur_class_index = default_idx;
  __carv_cur_class_size = default_size;

  char *name_ptr = ptr_node->type_name_;
  if (name_ptr != NULL) {
    auto search = class_info.find(name_ptr);
    if ((search != NULL) && ((ptr_alloc_size % search->size) == 0)) {
      __carv_cur_class_index = search->class_index;
      __carv_cur_class_size = search->size;
      type_name = name_ptr;
    }
  }

  carved_ptrs.push_back(POINTER(ptr, type_name, ptr_alloc_size, default_size));

  VAR<int> *inputv = new VAR<int>(new_carved_ptr_index, 0, 0, INPUT_TYPE::PTR);
  carved_objs.push_back((IVAR *)inputv);

  return ptr_alloc_size;
}

void __Carv_func_ptr_name(void *ptr) {
  if (!__carv_opened) {
    return;
  }

  auto search = func_ptrs.find(ptr);
  if ((ptr == NULL) || (search == NULL)) {
    VAR<void *> *inputv = new VAR<void *>(NULL, NULL, INPUT_TYPE::NULLPTR);
    carved_objs.push_back((IVAR *)inputv);
    return;
  }

  VAR<char *> *inputv = new VAR<char *>(*search, NULL, INPUT_TYPE::FUNCPTR);
  carved_objs.push_back((IVAR *)inputv);
  return;
}

void __record_func_ptr(void *ptr, char *name) {
  std::cerr << "Recording func ptr : " << ptr << ", " << name << "\n";
  func_ptrs.insert(ptr, name);
}

void __keep_class_info(char *class_name, int size, int index) {
  classinfo tmp(index, size);
  class_info.insert(class_name, tmp);
}

int __get_class_idx() { return __carv_cur_class_index; }

int __get_class_size() { return __carv_cur_class_size; }

// Save ptr with size and type_name into memory `allocted_ptrs`.
void __mem_allocated_probe(void *ptr, int alloc_size, char *type_name) {
  if (!__carv_ready) {
    return;
  }

  alloced_ptrs.insert(ptr, type_name, alloc_size);
  return;
}

void __remove_mem_allocated_probe(void *ptr) {
  if (!__carv_ready) {
    return;
  }

  alloced_ptrs.remove(ptr);
}

static void carved_ptr_postprocessing(int begin_idx, int end_idx) {
  int idx1, idx2, idx3, idx4, idx5;
  bool changed = true;
  while (changed) {
    changed = false;
    idx1 = begin_idx;
    while (idx1 < end_idx) {
      int idx2 = idx1 + 1;
      POINTER *cur_carved_ptr = carved_ptrs.get(idx1);
      char *addr1 = (char *)cur_carved_ptr->addr;
      int size1 = cur_carved_ptr->alloc_size;
      if (size1 == 0) {
        idx1++;
        continue;
      }
      char *end_addr1 = addr1 + size1;
      const char *type1 = cur_carved_ptr->pointee_type;

      while (idx2 < end_idx) {
        POINTER *cur_carved_ptr2 = carved_ptrs.get(idx2);
        char *addr2 = (char *)cur_carved_ptr2->addr;
        int size2 = cur_carved_ptr2->alloc_size;
        if (size2 == 0) {
          idx2++;
          continue;
        }
        char *end_addr2 = addr2 + size2;
        const char *type2 = cur_carved_ptr2->pointee_type;
        if (type1 != type2) {
          idx2++;
          continue;
        }
        int offset = -1;
        int remove_ptr_idx;
        int replacing_ptr_idx;
        POINTER *remove_ptr;
        if ((addr1 <= addr2) && (addr2 < end_addr1)) {
          offset = addr2 - addr1;
          remove_ptr_idx = idx2;
          replacing_ptr_idx = idx1;
          remove_ptr = cur_carved_ptr2;
        } else if ((addr2 <= addr1) && (addr1 < end_addr2)) {
          offset = addr1 - addr2;
          remove_ptr_idx = idx1;
          replacing_ptr_idx = idx2;
          remove_ptr = cur_carved_ptr;
        }

        if (offset != -1) {
          // remove remove_ptr in inputs;
          int idx3 = 0;
          int num_inputs = carved_objs.size();
          while (idx3 < num_inputs) {
            IVAR *tmp_input = *(carved_objs.get(idx3));
            if (tmp_input->type == INPUT_TYPE::PTR) {
              VAR<int> *tmp_inputt = (VAR<int> *)tmp_input;
              if (tmp_inputt->input == remove_ptr_idx) {
                int old_offset = tmp_inputt->pointer_offset;
                tmp_inputt->input = replacing_ptr_idx;
                tmp_inputt->pointer_offset = offset + old_offset;
                if (old_offset == 0) {
                  // remove element carved results
                  char *var_name = tmp_input->name;
                  size_t var_name_len = strlen(var_name);
                  char *check_name = (char *)malloc(var_name_len + 2);
                  memcpy(check_name, var_name, var_name_len);
                  check_name[var_name_len] = '[';
                  check_name[var_name_len + 1] = 0;
                  int idx4 = idx3 + 1;
                  while (idx4 < num_inputs) {
                    IVAR *next_input = *(carved_objs.get(idx4));
                    if (strncmp(check_name, next_input->name,
                                var_name_len + 1) != 0) {
                      break;
                    }
                    idx4++;
                  }
                  free(check_name);
                  int idx5 = idx3 + 1;
                  while (idx5 < idx4) {
                    delete *(carved_objs.get(idx3 + 1));
                    carved_objs.remove(idx3 + 1);
                    idx5++;
                  }
                  num_inputs = carved_objs.size();
                }
              }
            }
            idx3++;
          }
          remove_ptr->alloc_size = 0;
          changed = true;
          break;
        }
        idx2++;
      }

      if (changed) break;
      idx1++;
    }
  }
  return;
}

static map<char *, char *> file_save_hash_map;
static map<char *, unsigned int> file_save_idx_map;

void __carv_file(char *file_name) {
  if (!__carv_opened) {
    return;
  }

  unsigned int file_idx = 0;
  FILE *target_file = fopen(file_name, "rb");
  if (target_file == NULL) {
    return;
  }

  if (file_save_hash_map.find(file_name) == NULL) {
    char *hash_vec = (char *)malloc(sizeof(char) * 256);
    memset(hash_vec, 0, sizeof(char) * 256);
    file_save_hash_map.insert(file_name, hash_vec);
  }

  // char *hash_vec = *(file_save_hash_map.find(file_name));

  // char file_outdir_name[256];
  // snprintf(file_outdir_name, 256, "%s/carved_file_%s", outdir_name,
  // file_name);

  // mkdir(file_outdir_name, 0777);

  // char outfile_name[256];
  // snprintf(outfile_name, 256, "%s/carved_file_%s/%d", outdir_name, file_name,
  //          file_idx++);

  // FILE *outfile = fopen(outfile_name, "wb");
  // if (outfile == NULL) {
  //   fclose(target_file);
  //   return;
  // }

  // char buf[4096];
  // int read_size;
  // int hash_val = 0;
  // while ((read_size = fread(buf, 1, 4096, target_file)) > 0) {
  //   fwrite(buf, 1, read_size, outfile);
  //   int idx = 0;
  //   while (idx < read_size) {
  //     hash_val += buf[idx++];
  //     hash_val = hash_val % 256;
  //   }
  // }

  // fclose(target_file);
  // fclose(outfile);

  // if (hash_vec[hash_val] == 0) {
  //   hash_vec[hash_val] = 1;
  // } else {
  //   unlink(outfile_name);
  // }

  unsigned int *file_idx_ptr = file_save_idx_map.find(file_name);
  if (file_idx_ptr == NULL) {
    file_save_idx_map.insert(file_name, 0);
  } else {
    (*file_idx_ptr)++;
    file_idx = *file_idx_ptr;
  }

  char outfile_name[256];
  snprintf(outfile_name, 256, "%s/carved_file_%s_%d", outdir_name, file_name,
           file_idx);

  FILE *outfile = fopen(outfile_name, "wb");
  if (outfile == NULL) {
    fclose(target_file);
    return;
  }

  char buf[4096];
  int read_size;
  while ((read_size = fread(buf, 1, 4096, target_file)) > 0) {
    fwrite(buf, 1, read_size, outfile);
  }

  fclose(target_file);
  fclose(outfile);

  VAR<int> *inputv = new VAR<int>(file_idx, file_name, INPUT_TYPE::INPUTFILE);
  carved_objs.push_back((IVAR *)inputv);
  return;
}

static int num_excluded = 0;

void __carver_argv_modifier(int *argcptr, char ***argvptr) {
  int argc = (*argcptr) - 1;
  *argcptr = argc;

  char *tmp_outdir_name = (*argvptr)[argc];

  if (tmp_outdir_name[0] != '/') {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
      std::cerr << "Error: Failed to get current working directory, errno : "
                << strerror(errno) << "\n";
      exit(1);
    } else {
      outdir_name = (char *)malloc(sizeof(char) * 512);
      snprintf(outdir_name, 512, "%s/%s", cwd, tmp_outdir_name);
    }
  } else {
    outdir_name = (char *)malloc(sizeof(char) * 512);
    snprintf(outdir_name, 512, "%s", tmp_outdir_name);
  }

  (*argvptr)[argc] = 0;
  __mem_allocated_probe(*argvptr, sizeof(char *) * argc, 0);
  int idx;
  for (idx = 0; idx < argc; idx++) {
    char *argv_str = (*argvptr)[idx];
    __mem_allocated_probe(argv_str, strlen(argv_str) + 1, 0);
  }

  // Write argc, argv values, TODO

  __carv_ready = true;
  return;
}

void __carv_FINI() {
  char buffer[256];
  free(outdir_name);

  __carv_ready = false;
}

static map<const char *, unsigned int> func_file_counter;

void __carv_open(const char *func_name) {
  fprintf(stderr, "__carv_open called , func_name : %s\n", func_name);
  if (!__carv_ready) {
    return;
  }

  unsigned int *func_count = func_file_counter.find(func_name);
  if (func_count != NULL) {
    if (*func_count > 100) {
      return;
    }
  }

  assert(carved_objs.size() == 0);
  assert(carved_ptrs.size() == 0);
  __carv_opened = true;
  return;
}

// Count # of objs of each type
void __carv_close(const char *func_name) {
  fprintf(stderr, "carv close called , func_name : %s\n", func_name);
  if (!__carv_ready) {
    return;
  }

  __carv_opened = false;

  if (carved_objs.size() == 0) {
    return;
  }

  int idx = 0;

  const int num_objs = carved_objs.size();
  const int num_carved_ptrs = carved_ptrs.size();

  bool skip_write = false;

  // unsigned int cur_type_idx = 0;

  // unsigned int *type_idx_ptr =
  //     type_idx.find(type_name);  // type_idx[type_name];
  // if (type_idx_ptr == NULL) {
  //   cur_type_idx = max_type_idx;
  //   type_idx.insert(type_name, max_type_idx++);
  // } else {
  //   cur_type_idx = *type_idx_ptr;
  // }

  // if (num_objs <= 4) {
  //   skip_write = true;
  // }

  if (skip_write) {
    idx = 0;
    while (idx < num_objs) {
      delete *(carved_objs.get(idx));
      idx++;
    }

    num_excluded += 1;

    carved_objs.clear();
    carved_ptrs.clear();
    return;
  }

  unsigned int cur_cnt = 0;
  unsigned int *func_count = func_file_counter.find(func_name);
  if (func_count == NULL) {
    func_file_counter.insert(func_name, 1);
    cur_cnt = 1;
  } else {
    cur_cnt = *func_count;
    (*func_count)++;
  }

  char outfile_name[256];
  snprintf(outfile_name, 256, "%s/%s_%d", outdir_name, func_name, cur_cnt);
  FILE *outfile = fopen(outfile_name, "w");

  if (outfile == NULL) {
    idx = 0;
    while (idx < num_objs) {
      delete *(carved_objs.get(idx));
      idx++;
    }
    carved_objs.clear();
    carved_ptrs.clear();
    return;
  }

  idx = 0;
  while (idx < num_carved_ptrs) {
    POINTER *carved_ptr = carved_ptrs.get(idx);
    fprintf(outfile, "%d:%p:%d:%s\n", idx, carved_ptr->addr,
            carved_ptr->alloc_size, carved_ptr->pointee_type);
    idx++;
  }

  fprintf(outfile, "####\n");

  idx = 0;
  while (idx < num_objs) {
    IVAR *elem = *(carved_objs.get(idx));
    if (elem->type == INPUT_TYPE::CHAR) {
      fprintf(outfile, "CHAR:%d\n", (int)(((VAR<char> *)elem)->input));
    } else if (elem->type == INPUT_TYPE::SHORT) {
      fprintf(outfile, "SHORT:%d\n", (int)(((VAR<short> *)elem)->input));
    } else if (elem->type == INPUT_TYPE::INT) {
      fprintf(outfile, "INT:%d\n", (int)(((VAR<int> *)elem)->input));
    } else if (elem->type == INPUT_TYPE::LONG) {
      fprintf(outfile, "LONG:%ld\n", ((VAR<long> *)elem)->input);
    } else if (elem->type == INPUT_TYPE::LONGLONG) {
      fprintf(outfile, "LONGLONG:%lld\n", ((VAR<long long> *)elem)->input);
    } else if (elem->type == INPUT_TYPE::FLOAT) {
      fprintf(outfile, "FLOAT:%f\n", ((VAR<float> *)elem)->input);
    } else if (elem->type == INPUT_TYPE::DOUBLE) {
      fprintf(outfile, "DOUBLE:%lf\n", ((VAR<double> *)elem)->input);
    } else if (elem->type == INPUT_TYPE::NULLPTR) {
      fprintf(outfile, "NULLPTR:0\n");
    } else if (elem->type == INPUT_TYPE::PTR) {
      VAR<int> *input = (VAR<int> *)elem;
      fprintf(outfile, "PTR:%d:%d\n", input->input, input->pointer_offset);
    } else if (elem->type == INPUT_TYPE::FUNCPTR) {
      VAR<char *> *input = (VAR<char *> *)elem;
      fprintf(outfile, "FUNCPTR:%s\n", input->input);
    } else if (elem->type == INPUT_TYPE::UNKNOWN_PTR) {
      void *addr = ((VAR<void *> *)elem)->input;

      // address might be the end point of carved pointers
      int carved_idx = 0;
      int offset;
      while (carved_idx < num_carved_ptrs) {
        POINTER *carved_ptr = carved_ptrs.get(carved_idx);
        char *end_addr = (char *)carved_ptr->addr + carved_ptr->alloc_size;
        if (end_addr == addr) {
          offset = carved_ptr->alloc_size;
          break;
        }
        carved_idx++;
      }

      if (carved_idx == num_carved_ptrs) {
        fprintf(outfile, "UNKNOWN_PTR:%p\n", ((VAR<void *> *)elem)->input);
      } else {
        fprintf(outfile, "PTR:%d:%d\n", carved_idx, offset);
      }
    } else if (elem->type == INPUT_TYPE::OBJ_INFO) {
      VAR<char *> *input = (VAR<char *> *)elem;
      fprintf(outfile, "OBJ_INFO:%s:%s\n", elem->name, input->input);
    } else if (elem->type == INPUT_TYPE::INPUTFILE) {
      VAR<int> *input = (VAR<int> *)elem;
      fprintf(outfile, "INPUTFILE:%d:%s\n", input->input, elem->name);
    } else {
      std::cerr << "Warning : unknown element type : " << elem->type << ", "
                << elem->name << "\n";
    }

    delete elem;
    idx++;
  }

  fclose(outfile);
  carved_objs.clear();
  carved_ptrs.clear();
  return;
}
}
