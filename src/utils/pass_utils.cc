
#include "utils/pass.hpp"

Module *Mod;
LLVMContext *Context;
const DataLayout *DL;
IRBuilder<> *IRB;
DebugInfoFinder DbgFinder;

/**
 * comment from yang
 * this function: initialize_pass_contexts,
 * initializes module context
 * and insertlocation at main function entry instruction
*/
void initialize_pass_contexts(Module &M) {
  LLVMContext &C = M.getContext();
  const DataLayout &dataLayout = M.getDataLayout();
  Mod = &M;
  Context = &C;
  DL = &dataLayout;
  IRB = new IRBuilder<>(C);

  DbgFinder.processModule(M);

  // Set dummy insertlocation at main function entry instruction
  for (auto &F : Mod->functions()) {
    if (F.isIntrinsic() || !F.size()) {
      continue;
    }

    std::string func_name = F.getName().str();
    if (func_name == "main") {
      IRB->SetInsertPoint(F.getEntryBlock().getFirstNonPHIOrDbgOrLifetime());
      break;
    }
  }
}

static std::map<std::string, Constant *> new_string_globals;
static std::map<std::string, std::string> probe_link_names;

std::string get_type_str(Type *type) {
  if (type->isStructTy() && dyn_cast<StructType>(type)->hasName()) {
    return type->getStructName().str();
  }
  std::string typestr;
  raw_string_ostream typestr_stream(typestr);
  type->print(typestr_stream);
  return typestr;
}

bool is_func_ptr_type(Type *type) {
  if (type->isPointerTy()) {
    PointerType *ptrtype = dyn_cast<PointerType>(type);
    Type *elem_type = ptrtype->getPointerElementType();
    return elem_type->isFunctionTy();
  }
  return false;
}

// Get symbol of probe base name.
std::string get_link_name(std::string base_name) {
  auto search = probe_link_names.find(base_name);
  if (search == probe_link_names.end()) {
    DEBUG0("Can't find probe name : " << base_name << "! Abort.\n");
    std::abort();
  }

  return search->second;
}

Constant *gen_new_string_constant(std::string name, IRBuilder<> *IRB) {
  auto search = new_string_globals.find(name);

  if (search == new_string_globals.end()) {
    Constant *new_global = IRB->CreateGlobalStringPtr(name, "", 0, Mod);
    new_string_globals.insert(std::make_pair(name, new_global));
    return new_global;
  }

  return search->second;
}

std::string find_param_name(Value *param, BasicBlock *BB) {
  Instruction *ptr = NULL;

#if LLVM_MAJOR < 15
  for (auto instr_iter = BB->begin(); instr_iter != BB->end(); instr_iter++) {
    if ((ptr == NULL) && isa<StoreInst>(instr_iter)) {
      StoreInst *store_inst = dyn_cast<StoreInst>(instr_iter);
      if (store_inst->getOperand(0) == param) {
        ptr = (Instruction *)store_inst->getOperand(1);
      }
    } else if (isa<DbgVariableIntrinsic>(instr_iter)) {
      DbgVariableIntrinsic *intrinsic =
          dyn_cast<DbgVariableIntrinsic>(instr_iter);

      Value *valloc = intrinsic->getVariableLocationOp(0);

      if (valloc == ptr) {
        DILocalVariable *var = intrinsic->getVariable();
        return var->getName().str();
      }
    }
  }
#else
#endif

  return "";
}

void get_struct_field_names_from_DIT(DIType *dit,
                                     std::vector<std::string> *elem_names) {
  while ((dit != NULL) && isa<DIDerivedType>(dit)) {
    DIDerivedType *tmptype = dyn_cast<DIDerivedType>(dit);
    dit = tmptype->getBaseType();
  }

  if (dit == NULL) {
    return;
  }

  if (isa<DISubroutineType>(dit)) {
    DISubroutineType *subroutine_type = dyn_cast<DISubroutineType>(dit);
    for (auto subtype : subroutine_type->getTypeArray()) {
      if (subtype == NULL) {
        continue;
      }

      get_struct_field_names_from_DIT(subtype, elem_names);
    }

  } else if (isa<DICompositeType>(dit)) {
    DICompositeType *struct_DIT = dyn_cast<DICompositeType>(dit);
    int field_idx = 0;
    for (auto iter2 : struct_DIT->getElements()) {
      if (isa<DIDerivedType>(iter2)) {
        DIDerivedType *elem_DIT = dyn_cast<DIDerivedType>(iter2);
        dwarf::Tag elem_tag = elem_DIT->getTag();
        std::string elem_name = "";

#if LLVM_MAJOR < 15
        if (elem_tag == dwarf::Tag::DW_TAG_member) {
          elem_name = elem_DIT->getName().str();
        } else if (elem_tag == dwarf::Tag::DW_TAG_inheritance) {
          elem_name = elem_DIT->getBaseType()->getName().str();
        }
#else
#endif

        if (elem_name == "") {
          elem_name = "field" + std::to_string(field_idx);
        }
        elem_names->push_back(elem_name);
        field_idx++;
      } else if (isa<DISubprogram>(iter2)) {
        // methods of classes, skip
        continue;
      }
    }
  } else {
    // TODO
    return;
  }
}

int num_class_name_const = 0;
std::vector<std::pair<Constant *, int>> class_name_consts;
std::map<StructType *, std::pair<int, Constant *>> class_name_map;

// Collect (index, class name) into vector and map.
void get_class_type_info() {
  for (auto struct_type : Mod->getIdentifiedStructTypes()) {
    if (struct_type->isOpaque()) {
      continue;
    }
    std::string name = struct_type->getName().str();
    Constant *name_const = gen_new_string_constant(name, IRB);
    class_name_consts.push_back(
        std::make_pair(name_const, DL->getTypeAllocSize(struct_type)));
    class_name_map.insert(std::make_pair(
        struct_type, std::make_pair(num_class_name_const++, name_const)));
  }
}

std::map<Function *, std::vector<GlobalVariable *>> global_var_uses;
static std::set<Use *> searching_uses;

// Construct `global_var_uses`, which maps function to used global variables.
static void add_global_use(Use &op, std::set<GlobalVariable *> *inserted,
                           Function *F) {
  // Check if this op is already searched
  if (searching_uses.find(&op) != searching_uses.end()) {
    return;
  }

  if (isa<GlobalVariable>(op)) {
    GlobalVariable *global = dyn_cast<GlobalVariable>(op);
    if (inserted->find(global) != inserted->end()) {
      return;
    }
    if (global->isConstant()) {
      return;
    }

    std::string glob_name = global->getName().str();
    if (glob_name.size() == 0) {
      return;
    }
    if (glob_name == "stdout") {
      return;
    }
    if (glob_name == "stderr") {
      return;
    }

    global_var_uses[F].push_back(global);
    inserted->insert(global);
  } else if (isa<ConstantExpr>(op)) {
    ConstantExpr *const_expr = dyn_cast<ConstantExpr>(op);
    Instruction *instr = const_expr->getAsInstruction();
    if (instr == NULL) {
      return;
    }
    searching_uses.insert(&op);
    for (auto &op2 : instr->operands()) {
      add_global_use(op2, inserted, F);
    }
    instr->deleteValue();
  } else if (isa<Instruction>(op)) {
    Instruction *instr = dyn_cast<Instruction>(op);
    searching_uses.insert(&op);
    for (auto &op2 : instr->operands()) {
      add_global_use(op2, inserted, F);
    }
  }

  return;
}

void find_global_var_uses() {
  for (auto &F : Mod->functions()) {
    std::set<GlobalVariable *> inserted;
    global_var_uses.insert(std::make_pair(&F, std::vector<GlobalVariable *>()));
    for (auto &BB : F.getBasicBlockList()) {
      for (auto &Instr : BB.getInstList()) {
        for (auto &op : Instr.operands()) {
          add_global_use(op, &inserted, &F);
        }
      }
    }
    searching_uses.clear();
  }

  // TODO : track callee's uses
}

Type *VoidTy;
IntegerType *Int1Ty;
IntegerType *Int8Ty;
IntegerType *Int16Ty;
IntegerType *Int32Ty;
IntegerType *Int64Ty;
IntegerType *Int128Ty;

Type *FloatTy;
Type *DoubleTy;

PointerType *Int8PtrTy;
PointerType *Int16PtrTy;
PointerType *Int32PtrTy;
PointerType *Int64PtrTy;
PointerType *Int128PtrTy;
PointerType *Int8PtrPtrTy;
PointerType *Int8PtrPtrPtrTy;

PointerType *FloatPtrTy;
PointerType *DoublePtrTy;

// Get pointer types from the LLVM context
void get_llvm_types() {
  VoidTy = Type::getVoidTy(*Context);
  Int1Ty = Type::getInt1Ty(*Context);
  Int8Ty = Type::getInt8Ty(*Context);
  Int16Ty = Type::getInt16Ty(*Context);
  Int32Ty = Type::getInt32Ty(*Context);
  Int64Ty = Type::getInt64Ty(*Context);
  Int128Ty = Type::getInt128Ty(*Context);
  FloatTy = Type::getFloatTy(*Context);
  DoubleTy = Type::getDoubleTy(*Context);
  Int8PtrTy = PointerType::get(Int8Ty, 0);
  Int16PtrTy = PointerType::get(Int16Ty, 0);
  Int32PtrTy = PointerType::get(Int32Ty, 0);
  Int64PtrTy = PointerType::get(Int64Ty, 0);
  Int128PtrTy = PointerType::get(Int128Ty, 0);
  Int8PtrPtrTy = PointerType::get(Int8PtrTy, 0);
  Int8PtrPtrPtrTy = PointerType::get(Int8PtrPtrTy, 0);
  FloatPtrTy = PointerType::get(FloatTy, 0);
  DoublePtrTy = PointerType::get(DoubleTy, 0);
}

std::set<Function *> forbid_func_set;
// Check if the function is not the scope of analysis.
bool is_inst_forbid_func(Function *F) {
  if (F->isIntrinsic() || !F->size()) {
    return true;
  }

  if (forbid_func_set.find(F) != forbid_func_set.end()) {
    return true;
  }

  std::string name = F->getName().str();
  if (name.find("__Carv_") != std::string::npos) {
    return true;
  }
  if (name.find("llvm_gcov") != std::string::npos) {
    return true;
  }
  if (name.find("_GLOBAL__sub_I_") != std::string::npos) {
    return true;
  }
  if (name.find("__cxx_global_var_init") != std::string::npos) {
    return true;
  }
  if (name.find("__class_replay") != std::string::npos) {
    return true;
  }
  if (name.find("__class_carver") != std::string::npos) {
    return true;
  }

  return false;
}

void check_and_dump_module() {
  char *tmp = getenv("DUMP_IR");
  if (tmp == NULL) {
    return;
  }
  DEBUG0("Dumping IR...\n");
  Mod->dump();
}

std::string convert_symbol_str(std::string name) {
  while (name.find(".") != std::string::npos) {
    name.replace(name.find("."), 1, "_");
  }

  while (name.find(":") != std::string::npos) {
    name.replace(name.find(":"), 1, "_");
  }

  return name;
}