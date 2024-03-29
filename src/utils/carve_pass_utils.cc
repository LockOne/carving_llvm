
#include "carving/carve_pass.hpp"
#include "llvm/Demangle/Demangle.h"

FunctionCallee mem_allocated_probe;
FunctionCallee remove_probe;

FunctionCallee record_func_ptr;
FunctionCallee add_no_stub_func;
FunctionCallee is_no_stub;

FunctionCallee argv_modifier;
FunctionCallee __carv_fini;
FunctionCallee carv_char_func;
FunctionCallee carv_short_func;
FunctionCallee carv_int_func;
FunctionCallee carv_long_func;
FunctionCallee carv_longlong_func;
FunctionCallee carv_float_func;
FunctionCallee carv_double_func;
FunctionCallee carv_ptr_func;
FunctionCallee carv_func_ptr;
FunctionCallee carv_func_call;
FunctionCallee carv_func_ret;
FunctionCallee update_carved_ptr_idx;
FunctionCallee keep_class_info;
FunctionCallee class_carver;
FunctionCallee record_func_ptr_index;
FunctionCallee insert_obj_info;
FunctionCallee record_ofstream;

Constant *global_carve_ready = NULL;
Constant *global_cur_class_idx = NULL;
Constant *global_cur_class_size = NULL;

extern std::set<std::string> custom_carvers;

// Insert the probes in the module symbol table
void get_carving_func_callees_and_globals(bool carv_func_name) {
  mem_allocated_probe = Mod->getOrInsertFunction(
      "__mem_allocated_probe", VoidTy, Int8PtrTy, Int32Ty, Int8PtrTy);
  remove_probe = Mod->getOrInsertFunction("__remove_mem_allocated_probe",
                                          VoidTy, Int8PtrTy);

  record_func_ptr = Mod->getOrInsertFunction("__record_func_ptr", VoidTy,
                                             Int8PtrTy, Int8PtrTy);
  add_no_stub_func =
      Mod->getOrInsertFunction("__add_no_stub_func", VoidTy, Int8PtrTy);
  is_no_stub = Mod->getOrInsertFunction("__is_no_stub_func", Int8Ty, Int8PtrTy);

  argv_modifier = Mod->getOrInsertFunction("__carver_argv_modifier", VoidTy,
                                           Int32PtrTy, Int8PtrPtrPtrTy);
  __carv_fini = Mod->getOrInsertFunction("__carv_FINI", VoidTy);
  carv_char_func = Mod->getOrInsertFunction("Carv_char", VoidTy, Int8Ty);
  carv_short_func = Mod->getOrInsertFunction("Carv_short", VoidTy, Int16Ty);
  carv_int_func = Mod->getOrInsertFunction("Carv_int", VoidTy, Int32Ty);
  carv_long_func = Mod->getOrInsertFunction("Carv_longtype", VoidTy, Int64Ty);
  carv_longlong_func =
      Mod->getOrInsertFunction("Carv_longlong", VoidTy, Int128Ty);
  carv_float_func = Mod->getOrInsertFunction("Carv_float", VoidTy, FloatTy);
  carv_double_func = Mod->getOrInsertFunction("Carv_double", VoidTy, DoubleTy);
  carv_ptr_func = Mod->getOrInsertFunction("Carv_pointer", Int32Ty, Int8PtrTy,
                                           Int8PtrTy, Int32Ty, Int32Ty);

  if (carv_func_name) {
    carv_func_ptr =
        Mod->getOrInsertFunction("__Carv_func_ptr_name", VoidTy, Int8PtrTy);
  } else {
    carv_func_ptr =
        Mod->getOrInsertFunction("__Carv_func_ptr_index", VoidTy, Int8PtrTy);
  }

  carv_func_call =
      Mod->getOrInsertFunction("__carv_func_call_probe", VoidTy, Int32Ty);
  carv_func_ret = Mod->getOrInsertFunction("__carv_func_ret_probe", VoidTy,
                                           Int8PtrTy, Int32Ty);

  update_carved_ptr_idx =
      Mod->getOrInsertFunction("__update_carved_ptr_idx", VoidTy);
  keep_class_info = Mod->getOrInsertFunction("__keep_class_info", VoidTy,
                                             Int8PtrTy, Int32Ty, Int32Ty);

  // Constructs global variables to global symbol table.
  global_carve_ready = Mod->getOrInsertGlobal("__carv_ready", Int8Ty);
  global_cur_class_idx =
      Mod->getOrInsertGlobal("__carv_cur_class_index", Int32Ty);
  global_cur_class_size =
      Mod->getOrInsertGlobal("__carv_cur_class_size", Int32Ty);

  record_func_ptr_index = Mod->getOrInsertFunction("__record_func_ptr_index",
                                                   VoidTy, Int8PtrTy, Int32Ty);

  insert_obj_info = Mod->getOrInsertFunction("__insert_obj_info", VoidTy,
                                             Int8PtrTy, Int8PtrTy);
  record_ofstream = Mod->getOrInsertFunction("__record_ofstream", VoidTy,
                                             Int8PtrTy, Int8PtrTy);
}

std::vector<AllocaInst *> tracking_allocas;

// Insert probes to track alloca instrution memory allcation
void Insert_alloca_probe(BasicBlock &entry_block) {
  std::vector<AllocaInst *> alloc_instrs;
  AllocaInst *last_alloc_instr = NULL;

  // Collect all alloca instructions in block
  for (llvm::Instruction &IN : entry_block) {
    if (isa<AllocaInst>(&IN)) {
      last_alloc_instr = dyn_cast<AllocaInst>(&IN);
      alloc_instrs.push_back(last_alloc_instr);
    }
  }

  if (last_alloc_instr != NULL) {
    IRB->SetInsertPoint(last_alloc_instr->getNextNonDebugInstruction());
    for (llvm::AllocaInst *alloc_instr : alloc_instrs) {
      // allocated_type is the type pointed by alloc_instr_type.
      Type *allocated_type = alloc_instr->getAllocatedType();
      Type *alloc_instr_type = alloc_instr->getType();

      unsigned int size = DL->getTypeAllocSize(allocated_type);

      Value *casted_ptr = alloc_instr;
      if (alloc_instr_type != Int8PtrTy) {
        casted_ptr = IRB->CreateCast(Instruction::CastOps::BitCast, alloc_instr,
                                     Int8PtrTy);
      }

      Value *size_const = ConstantInt::get(Int32Ty, size);

      Constant *type_name_const = Constant::getNullValue(Int8PtrTy);

      if (allocated_type->isStructTy()) {
        std::string typestr = allocated_type->getStructName().str();
        type_name_const = gen_new_string_constant(typestr, IRB);
      }

      IRB->CreateCall(mem_allocated_probe,
                      {casted_ptr, size_const, type_name_const});
      tracking_allocas.push_back(alloc_instr);
    }
  }
}

static Constant *get_mem_alloc_type(Instruction *call_inst) {
  if (call_inst == NULL) {
    return Constant::getNullValue(Int8PtrTy);
  }

  auto cur_insertpoint = IRB->GetInsertPoint();

  CastInst *cast_instr;
  if ((cast_instr = dyn_cast<CastInst>(cur_insertpoint))) {
    Type *cast_type = cast_instr->getType();
    if (isa<PointerType>(cast_type)) {
      PointerType *cast_ptr_type = dyn_cast<PointerType>(cast_type);
      Type *pointee_type = cast_ptr_type->getPointerElementType();
      if (pointee_type->isStructTy()) {
        std::string typestr = pointee_type->getStructName().str();
        Constant *typename_const = gen_new_string_constant(typestr, IRB);
        return typename_const;
      }
    }
  }

  return Constant::getNullValue(Int8PtrTy);
}

bool Insert_mem_func_call_probe(Instruction *call_inst,
                                std::string callee_name) {
  // IRB->SetInsertPoint(call_inst->getNextNonDebugInstruction());

  if (callee_name == "malloc") {
    // Track malloc
    Constant *type_name_const = get_mem_alloc_type(call_inst);

    Value *size = call_inst->getOperand(0);
    if (size->getType() == Int64Ty) {
      size = IRB->CreateCast(Instruction::CastOps::Trunc, size, Int32Ty);
    }
    IRB->CreateCall(mem_allocated_probe, {call_inst, size, type_name_const});
    return true;
  } else if (callee_name == "realloc") {
    // Track realloc
    Constant *type_name_const = get_mem_alloc_type(call_inst);
    IRB->CreateCall(remove_probe, {call_inst->getOperand(0)});
    Value *size = call_inst->getOperand(1);
    if (size->getType() == Int64Ty) {
      size = IRB->CreateCast(Instruction::CastOps::Trunc, size, Int32Ty);
    }
    IRB->CreateCall(mem_allocated_probe, {call_inst, size, type_name_const});
    return true;
  } else if (callee_name == "free") {
    IRB->CreateCall(remove_probe, {call_inst->getOperand(0)});
    return true;
  } else if (callee_name == "llvm.memcpy.p0i8.p0i8.i64") {
    // Get some hint from memory related functions
    //  Value * size = IN.getOperand(2);
    //  if (size->getType() == Int64Ty) {
    //    size = IRB->CreateCast(Instruction::CastOps::Trunc, size, Int32Ty);
    //  }
    //  std::vector<Value *> args {IN.getOperand(0), size};
    //  IRB->CreateCall(mem_allocated_probe, args);
  } else if (callee_name == "llvm.memmove.p0i8.p0i8.i64") {
    // Value * size = IN.getOperand(2);
    // if (size->getType() == Int64Ty) {
    //   size = IRB->CreateCast(Instruction::CastOps::Trunc, size, Int32Ty);
    // }
    // std::vector<Value *> args {IN.getOperand(0), size};
    // IRB->CreateCall(mem_allocated_probe, args);
  } else if (callee_name == "strlen") {
    // Value * add_one = IRB->CreateAdd(&IN, ConstantInt::get(Int64Ty, 1));
    // Value * size = IRB->CreateCast(Instruction::CastOps::Trunc, add_one,
    // Int32Ty); std::vector<Value *> args {IN.getOperand(0), size};
    // IRB->CreateCall(mem_allocated_probe, args);
  } else if (callee_name == "strncpy") {
    // Value * size = IN.getOperand(2);
    // if (size->getType() == Int64Ty) {
    //   size = IRB->CreateCast(Instruction::CastOps::Trunc, size, Int32Ty);
    // }
    // std::vector<Value *> args {IN.getOperand(0), size};
    // IRB->CreateCall(mem_allocated_probe, args);
  } else if (callee_name == "strcpy") {
    // std::vector<Value *> strlen_args;
    // strlen_args.push_back(IN.getOperand(0));
    // Value * strlen_result = IRB->CreateCall(strlen_callee, strlen_args);
    // Value * add_one = IRB->CreateAdd(strlen_result,
    // ConstantInt::get(Int64Ty, 1)); std::vector<Value *> args
    // {IN.getOperand(0), add_one}; IRB->CreateCall(mem_allocated_probe,
    // args);
  } else if ((callee_name == "_Znwm") || (callee_name == "_Znam")) {
    // new operator
    Constant *type_name_const = get_mem_alloc_type(call_inst);

    Value *size = call_inst->getOperand(0);
    if (size->getType() == Int64Ty) {
      size = IRB->CreateCast(Instruction::CastOps::Trunc, size, Int32Ty);
    }
    IRB->CreateCall(mem_allocated_probe, {call_inst, size, type_name_const});
    return true;
  } else if ((callee_name == "_ZdlPv") || (callee_name == "_ZdaPv")) {
    // delete operator
    IRB->CreateCall(remove_probe, {call_inst->getOperand(0)});
    return true;
  }

  return false;
}

static void Insert_glob_mem_alloc_probe(GlobalVariable *gv) {
  Value *casted_ptr =
      IRB->CreateCast(Instruction::CastOps::BitCast, gv, Int8PtrTy);
  Type *gv_type = gv->getValueType();
  unsigned int size = DL->getTypeAllocSize(gv_type);

  Constant *type_name_const = Constant::getNullValue(Int8PtrTy);

  if (gv_type->isStructTy() && dyn_cast<StructType>(gv_type)->hasName()) {
    std::string type_name = gv_type->getStructName().str();
    type_name_const = gen_new_string_constant(type_name, IRB);
  }

  Value *size_const = ConstantInt::get(Int32Ty, size);
  IRB->CreateCall(mem_allocated_probe,
                  {casted_ptr, size_const, type_name_const});
}

static bool instrumented = false;
void Insert_carving_main_probe(BasicBlock *entry_block, Function *F,
                               std::vector<Function *> *func_list) {
  if (instrumented) {
    return;
  }
  instrumented = true;

  IRB->SetInsertPoint(entry_block->getFirstNonPHIOrDbgOrLifetime());

  Value *new_argc = NULL;
  Value *new_argv = NULL;

  size_t num_main_args = F->arg_size();
  assert(num_main_args == 0 || num_main_args == 2);

  std::vector<CallInst *> call_instrs;
  std::vector<ReturnInst *> ret_instrs;

  for (auto &BB : *F) {
    for (auto &I : BB) {
      if (isa<CallInst>(&I)) {
        call_instrs.push_back(dyn_cast<CallInst>(&I));
      } else if (isa<ReturnInst>(&I)) {
        ret_instrs.push_back(dyn_cast<ReturnInst>(&I));
      }
    }
  }

  if (F->arg_size() == 0) {
    F->setName("__old_main");
    // Make another copy of main that has argc and argv...
    FunctionType *new_func_type =
        FunctionType::get(Int32Ty, {Int32Ty, Int8PtrPtrTy}, false);
    Function *new_func =
        Function::Create(new_func_type, Function::ExternalLinkage, "main", Mod);
    BasicBlock *new_func_entry =
        BasicBlock::Create(*Context, "entry", new_func);
    IRB->SetInsertPoint(new_func_entry);
    Instruction *call_old_main = IRB->CreateCall(F, {});
    IRB->CreateRet(call_old_main);

    IRB->SetInsertPoint(call_old_main);
    F = new_func;

    forbid_func_set.insert(new_func);
  }

  Value *argc = F->getArg(0);
  Value *argv = F->getArg(1);
  AllocaInst *argc_ptr = IRB->CreateAlloca(Int32Ty);
  AllocaInst *argv_ptr = IRB->CreateAlloca(Int8PtrPtrTy);

  new_argc = IRB->CreateLoad(Int32Ty, argc_ptr);
  new_argv = IRB->CreateLoad(Int8PtrPtrTy, argv_ptr);

  argc->replaceAllUsesWith(new_argc);
  argv->replaceAllUsesWith(new_argv);

  IRB->SetInsertPoint((Instruction *)new_argc);

  IRB->CreateStore(argc, argc_ptr);
  IRB->CreateStore(argv, argv_ptr);

  IRB->CreateCall(argv_modifier, {argc_ptr, argv_ptr});

  Instruction *new_argv_load_instr = dyn_cast<Instruction>(new_argv);

  IRB->SetInsertPoint(new_argv_load_instr->getNextNonDebugInstruction());

  FunctionCallee vtable_record = Mod->getOrInsertFunction(
      "__record_vtable_ptr", VoidTy, Int8PtrTy, Int8PtrTy);

  auto globals = Mod->global_values();
  // Global variables memory probing
  for (auto global_iter = globals.begin(); global_iter != globals.end();
       global_iter++) {
    if (!isa<GlobalVariable>(*global_iter)) {
      continue;
    }

    std::string name = global_iter->getName().str();
    std::string demangled = llvm::demangle(name);

    GlobalValue *global_v1 = &(*global_iter);

    if (demangled.find("vtable for ") != std::string::npos) {
      Value *cast_ptr =
          IRB->CreateCast(Instruction::CastOps::BitCast, global_v1, Int8PtrTy);
      IRB->CreateCall(vtable_record,
                      {cast_ptr, gen_new_string_constant(demangled, IRB)});
    }

    GlobalVariable *global_v = dyn_cast<GlobalVariable>(global_v1);
    if (global_v->getName().str().find("llvm.") != std::string::npos) {
      continue;
    }

    Insert_glob_mem_alloc_probe(global_v);
  }

  // Record func ptr
  if (func_list == NULL) {
    for (auto &Func : Mod->functions()) {
      bool is_no_stub = false;
      if (Func.isIntrinsic()) {
        continue;
      }
      if (!Func.size()) {
        is_no_stub = true;
      }

      std::string func_name = Func.getName().str();
      Constant *func_name_const = gen_new_string_constant(func_name, IRB);
      Value *cast_val =
          IRB->CreateCast(Instruction::CastOps::BitCast, &Func, Int8PtrTy);
      IRB->CreateCall(record_func_ptr, {cast_val, func_name_const});

      if (func_name.find("_GLOBAL__sub_I_") != std::string::npos) {
        is_no_stub = true;
      }
      if (func_name == "__cxx_global_var_init") {
        is_no_stub = true;
      }
      if (func_name == "main") {
        is_no_stub = true;
      }

      if (!is_no_stub) {
        continue;
      }

      IRB->CreateCall(add_no_stub_func, {cast_val});
    }
  } else {
    int index = 0;
    for (Function *func : *func_list) {
      Value *cast_val =
          IRB->CreateCast(Instruction::CastOps::BitCast, func, Int8PtrTy);
      IRB->CreateCall(record_func_ptr_index,
                      {cast_val, ConstantInt::get(Int32Ty, index)});
      index++;
    }
  }

  // Record class type string constants
  for (auto iter : class_name_map) {
    unsigned int class_size = DL->getTypeAllocSize(iter.first);
    IRB->CreateCall(keep_class_info,
                    {iter.second.second, ConstantInt::get(Int32Ty, class_size),
                     ConstantInt::get(Int32Ty, iter.second.first)});
  }

  for (auto call_instr : call_instrs) {
    Function *callee = call_instr->getCalledFunction();
    if (callee == NULL) {
      continue;
    }
    if (callee->isDebugInfoForProfiling()) {
      continue;
    }

    IRB->SetInsertPoint(call_instr->getNextNonDebugInstruction());
    Insert_mem_func_call_probe(call_instr, callee->getName().str());
  }

  for (auto ret_instr : ret_instrs) {
    IRB->SetInsertPoint(ret_instr);
    IRB->CreateCall(__carv_fini, {});
  }

  return;
}

BasicBlock *insert_array_carve_probe(Value *arr_ptr_val,
                                     BasicBlock *cur_block) {
  Type *arr_ptr_type = arr_ptr_val->getType();
  assert(arr_ptr_type->isPointerTy());
  PointerType *arr_ptr_type_2 = dyn_cast<PointerType>(arr_ptr_type);

  Type *arr_type = arr_ptr_type_2->getPointerElementType();
  assert(arr_type->isArrayTy());

  ArrayType *arr_type_2 = dyn_cast<ArrayType>(arr_type);
  Type *arr_elem_type = arr_type_2->getElementType();

  unsigned int arr_size = arr_type_2->getNumElements();

  // Make loop block
  BasicBlock *loopblock = cur_block->splitBasicBlock(&(*IRB->GetInsertPoint()));
  BasicBlock *const loopblock_start = loopblock;

  IRB->SetInsertPoint(loopblock->getFirstNonPHIOrDbgOrLifetime());
  PHINode *index_phi = IRB->CreatePHI(Int32Ty, 2);
  index_phi->addIncoming(ConstantInt::get(Int32Ty, 0), cur_block);

  Value *getelem_instr = IRB->CreateInBoundsGEP(
      arr_type, arr_ptr_val, {ConstantInt::get(Int32Ty, 0), index_phi});

  loopblock = insert_gep_carve_probe(getelem_instr, loopblock);

  Value *index_update_instr =
      IRB->CreateAdd(index_phi, ConstantInt::get(Int32Ty, 1));
  index_phi->addIncoming(index_update_instr, loopblock);

  Value *cmp_instr2 = IRB->CreateICmpSLT(index_update_instr,
                                         ConstantInt::get(Int32Ty, arr_size));

  Instruction *cur_ip = &(*IRB->GetInsertPoint());

  BasicBlock *endblock = loopblock->splitBasicBlock(cur_ip);

  IRB->SetInsertPoint(cur_ip);

  Instruction *BB_term = IRB->CreateBr(loopblock_start);
  BB_term->removeFromParent();

  // remove old terminator
  Instruction *old_term = cur_block->getTerminator();
  ReplaceInstWithInst(old_term, BB_term);

  IRB->SetInsertPoint(cur_ip);

  Instruction *loopblock_term =
      IRB->CreateCondBr(cmp_instr2, loopblock_start, endblock);

  loopblock_term->removeFromParent();

  // remove old terminator
  old_term = loopblock->getTerminator();
  ReplaceInstWithInst(old_term, loopblock_term);

  IRB->SetInsertPoint(endblock->getFirstNonPHIOrDbgOrLifetime());

  return endblock;
}

BasicBlock *insert_gep_carve_probe(Value *gep_val, BasicBlock *cur_block) {
  PointerType *gep_type = dyn_cast<PointerType>(gep_val->getType());
  Type *gep_pointee_type = gep_type->getPointerElementType();

  if (gep_pointee_type->isStructTy()) {
    insert_struct_carve_probe(gep_val, gep_pointee_type);
  } else if (gep_pointee_type->isArrayTy()) {
    cur_block = insert_array_carve_probe(gep_val, cur_block);
  } else if (is_func_ptr_type(gep_pointee_type)) {
    Value *load_ptr = IRB->CreateLoad(gep_pointee_type, gep_val);
    Value *cast_ptr =
        IRB->CreateCast(Instruction::CastOps::BitCast, load_ptr, Int8PtrTy);
    IRB->CreateCall(carv_func_ptr, {cast_ptr});
  } else {
    Value *loadval = IRB->CreateLoad(gep_pointee_type, gep_val);
    cur_block = insert_carve_probe(loadval, cur_block);
  }

  return cur_block;
}

BasicBlock *insert_carve_probe(Value *val, BasicBlock *BB) {
  Type *val_type = val->getType();
  BasicBlock *cur_block = BB;

  if (val_type == Int1Ty) {
    Value *cast_val = IRB->CreateZExt(val, Int8Ty);
    IRB->CreateCall(carv_char_func, {cast_val});
  } else if (val_type == Int8Ty) {
    IRB->CreateCall(carv_char_func, {val});
  } else if (val_type == Int16Ty) {
    IRB->CreateCall(carv_short_func, {val});
  } else if (val_type == Int32Ty) {
    IRB->CreateCall(carv_int_func, {val});
  } else if (val_type == Int64Ty) {
    IRB->CreateCall(carv_long_func, {val});
  } else if (val_type == Int128Ty) {
    IRB->CreateCall(carv_longlong_func, {val});
  } else if (val_type->isIntegerTy()) {
    Value *cast_val = IRB->CreateZExt(val, Int128Ty);
    IRB->CreateCall(carv_longlong_func, {cast_val});
  } else if (val_type == FloatTy) {
    IRB->CreateCall(carv_float_func, {val});
  } else if (val_type == DoubleTy) {
    IRB->CreateCall(carv_double_func, {val});
  } else if (val_type->isX86_FP80Ty()) {
    Value *cast_val = IRB->CreateFPCast(val, DoubleTy);
    IRB->CreateCall(carv_double_func, {cast_val});
  } else if (val_type->isStructTy()) {
    // Sould be very simple tiny struct...
    StructType *struct_type = dyn_cast<StructType>(val_type);
    const StructLayout *SL = DL->getStructLayout(struct_type);

    auto memberoffsets = SL->getMemberOffsets();
    int idx = 0;
    for (auto _iter : memberoffsets) {
      Value *extracted_val = IRB->CreateExtractValue(val, idx);
      cur_block = insert_carve_probe(extracted_val, cur_block);
      idx++;
    }

  } else if (is_func_ptr_type(val_type)) {
    Value *ptrval =
        IRB->CreateCast(Instruction::CastOps::BitCast, val, Int8PtrTy);
    IRB->CreateCall(carv_func_ptr, {ptrval});
  } else if (val_type->isFunctionTy() || val_type->isArrayTy()) {
    // Is it possible to reach here?
  } else if (val_type->isPointerTy()) {
    PointerType *ptrtype = dyn_cast<PointerType>(val_type);
    // type that we don't know.
    if (ptrtype->isOpaque() || ptrtype->isOpaquePointerTy()) {
      return cur_block;
    }
    Type *pointee_type = ptrtype->getPointerElementType();

    if (isa<StructType>(pointee_type)) {
      StructType *tmptype = dyn_cast<StructType>(pointee_type);
      if (tmptype->isOpaque()) {
        return cur_block;
      }
    }

    unsigned int pointee_size = DL->getTypeAllocSize(pointee_type);
    if (pointee_size == 0) {
      return cur_block;
    }

    Value *ptrval = val;
    if (val_type != Int8PtrTy) {
      ptrval = IRB->CreateCast(Instruction::CastOps::BitCast, val, Int8PtrTy);
    }

    bool is_class_type = false;
    Value *default_class_idx = ConstantInt::get(Int32Ty, -1);
    if (pointee_type->isStructTy()) {
      StructType *struct_type = dyn_cast<StructType>(pointee_type);
      auto search = class_name_map.find(struct_type);
      if (search != class_name_map.end()) {
        is_class_type = true;
        default_class_idx = ConstantInt::get(Int32Ty, search->second.first);
      }
    } else if (pointee_type == Int8Ty) {
      // check
      is_class_type = true;
      default_class_idx = ConstantInt::get(Int32Ty, num_class_name_const);
    }

    Value *pointee_size_val = ConstantInt::get(Int32Ty, pointee_size);

    std::string typestr = get_type_str(pointee_type);
    Constant *typestr_const = gen_new_string_constant(typestr, IRB);

    // Call Carv_pointer
    Value *end_size = IRB->CreateCall(
        carv_ptr_func,
        {ptrval, typestr_const, default_class_idx, pointee_size_val});

    Value *class_idx = NULL;
    if (is_class_type) {
      pointee_size_val = IRB->CreateLoad(Int32Ty, global_cur_class_size);
      class_idx = IRB->CreateLoad(Int32Ty, global_cur_class_idx);
    }

    Value *pointer_size = IRB->CreateSDiv(end_size, pointee_size_val);

    Value *cmp_instr1 =
        IRB->CreateICmpEQ(pointer_size, ConstantInt::get(Int32Ty, 0));

    Instruction *split_point = &(*IRB->GetInsertPoint());

    // Make loop block
    BasicBlock *loopblock = BB->splitBasicBlock(split_point);
    BasicBlock *const loopblock_start = loopblock;

    IRB->SetInsertPoint(loopblock->getFirstNonPHI());
    PHINode *index_phi = IRB->CreatePHI(Int32Ty, 2);
    index_phi->addIncoming(ConstantInt::get(Int32Ty, 0), BB);

    Value *elem_ptr = IRB->CreateInBoundsGEP(pointee_type, val, index_phi);

    if (is_class_type) {
      Value *casted_elem_ptr =
          IRB->CreateCast(Instruction::CastOps::BitCast, elem_ptr, Int8PtrTy);
      IRB->CreateCall(class_carver, {casted_elem_ptr, class_idx});
    } else {
      loopblock = insert_gep_carve_probe(elem_ptr, loopblock);
    }

    Value *index_update_instr =
        IRB->CreateAdd(index_phi, ConstantInt::get(Int32Ty, 1));
    index_phi->addIncoming(index_update_instr, loopblock);

    Value *cmp_instr2 = IRB->CreateICmpSLT(index_update_instr, pointer_size);

    BasicBlock *endblock =
        loopblock->splitBasicBlock(&(*IRB->GetInsertPoint()));

    IRB->SetInsertPoint(BB->getTerminator());

    Instruction *BB_term =
        IRB->CreateCondBr(cmp_instr1, endblock, loopblock_start);
    BB_term->removeFromParent();

    // remove old terminator
    Instruction *old_term = BB->getTerminator();
    ReplaceInstWithInst(old_term, BB_term);

    IRB->SetInsertPoint(loopblock->getTerminator());

    Instruction *loopblock_term =
        IRB->CreateCondBr(cmp_instr2, loopblock_start, endblock);

    loopblock_term->removeFromParent();

    // remove old terminator
    old_term = loopblock->getTerminator();
    ReplaceInstWithInst(old_term, loopblock_term);

    IRB->SetInsertPoint(endblock->getFirstNonPHIOrDbgOrLifetime());

    cur_block = endblock;
  } else {
    DEBUG0("Unknown type input : \n");
    DEBUGDUMP(val);
  }

  return cur_block;
}

std::set<std::string> struct_carvers;
void insert_struct_carve_probe(Value *struct_ptr, Type *type) {
  StructType *struct_type = dyn_cast<StructType>(type);

  const std::string class_name = struct_type->getName().str();
  const std::string symbol_name = convert_symbol_str(class_name);

  if (custom_carvers.find(symbol_name) != custom_carvers.end()) {
    Type *class_ptr_ty = PointerType::get(type, 0);
    llvm::errs() << "Found symbol : " << symbol_name << "\n";
    FunctionCallee custom_carver = Mod->getOrInsertFunction(
        "__Carv_custom_" + symbol_name, VoidTy, class_ptr_ty);
    IRB->CreateCall(custom_carver, {struct_ptr});
    IRB->CreateRetVoid();
    return;
  }

  auto search2 = class_name_map.find(struct_type);
  if (search2 == class_name_map.end()) {
    insert_struct_carve_probe_inner(struct_ptr, type);
  } else {
    Value *casted =
        IRB->CreateCast(Instruction::CastOps::BitCast, struct_ptr, Int8PtrTy);
    IRB->CreateCall(class_carver,
                    {casted, ConstantInt::get(Int32Ty, search2->second.first)});
  }

  return;
}

static std::map<std::string, DIType *> struct_ditype_map;

void construct_ditype_map() {
  for (auto iter : DbgFinder.types()) {
    std::string type_name = iter->getName().str();
    DIType *dit = iter;
    struct_ditype_map[type_name] = dit;
  }

  for (auto DIsubprog : DbgFinder.subprograms()) {
    std::string type_name = DIsubprog->getName().str();
    DIType *DISubroutType = DIsubprog->getType();
    struct_ditype_map[type_name] = DISubroutType;
  }
}

void insert_struct_carve_probe_inner(Value *struct_ptr, Type *type) {
  IRBuilderBase::InsertPoint cur_ip = IRB->saveIP();
  StructType *struct_type = dyn_cast<StructType>(type);
  const StructLayout *SL = DL->getStructLayout(struct_type);

  std::string struct_name = struct_type->getName().str();
  struct_name = struct_name.substr(struct_name.find('.') + 1);
  if (struct_name.find("::") != std::string::npos) {
    struct_name = struct_name.substr(struct_name.find("::") + 2);
  }

  llvm::errs() << "Inserting class carver : " << struct_name << "\n";

  std::string struct_carver_name = "__Carv_" + struct_name;
  auto search = struct_carvers.find(struct_carver_name);
  FunctionCallee struct_carver = Mod->getOrInsertFunction(
      struct_carver_name, VoidTy, struct_ptr->getType());

  if (search == struct_carvers.end()) {
    struct_carvers.insert(struct_carver_name);

    // Define struct carver
    Function *struct_carv_func = dyn_cast<Function>(struct_carver.getCallee());

    BasicBlock *entry_BB =
        BasicBlock::Create(*Context, "entry", struct_carv_func);

    IRB->SetInsertPoint(entry_BB);
    Instruction *ret_void_instr = IRB->CreateRetVoid();
    IRB->SetInsertPoint(entry_BB->getFirstNonPHIOrDbgOrLifetime());

    // Get field names
    std::vector<std::string> elem_names;

    auto search = struct_ditype_map.find(struct_name);
    if (search != struct_ditype_map.end()) {
      DIType *dit = search->second;
      get_struct_field_names_from_DIT(dit, &elem_names);
    }

    auto memberoffsets = SL->getMemberOffsets();

    if (elem_names.size() > memberoffsets.size()) {
      elem_names.clear();
    }

    while (elem_names.size() < memberoffsets.size()) {
      // Can't get field names, just put simple name
      int field_idx = elem_names.size();
      elem_names.push_back("field" + std::to_string(field_idx));
    }

    if (elem_names.size() == 0) {
      // struct types with zero fields?
      IRB->restoreIP(cur_ip);
      return;
    }

    Value *carver_param = struct_carv_func->getArg(0);

    // depth check
    Constant *depth_check_const =
        Mod->getOrInsertGlobal("__carv_depth", Int8Ty);

    Value *depth_check_val = IRB->CreateLoad(Int8Ty, depth_check_const);
    Value *depth_check_cmp = IRB->CreateICmpSGT(
        depth_check_val, ConstantInt::get(Int8Ty, STRUCT_CARV_DEPTH));
    BasicBlock *depth_check_BB =
        BasicBlock::Create(*Context, "depth_check", struct_carv_func);
    BasicBlock *do_carving_BB =
        BasicBlock::Create(*Context, "do_carving", struct_carv_func);
    BranchInst *depth_check_br =
        IRB->CreateCondBr(depth_check_cmp, depth_check_BB, do_carving_BB);
    depth_check_br->removeFromParent();
    ReplaceInstWithInst(ret_void_instr, depth_check_br);

    IRB->SetInsertPoint(depth_check_BB);

    IRB->CreateRetVoid();

    IRB->SetInsertPoint(do_carving_BB);

    BasicBlock *cur_block = do_carving_BB;

    Value *add_one_depth =
        IRB->CreateAdd(depth_check_val, ConstantInt::get(Int8Ty, 1));
    IRB->CreateStore(add_one_depth, depth_check_const);

    Instruction *depth_store_instr2 =
        IRB->CreateStore(depth_check_val, depth_check_const);

    IRB->CreateRetVoid();

    IRB->SetInsertPoint(depth_store_instr2);

    int elem_idx = 0;
    for (auto iter : elem_names) {
      Value *gep = IRB->CreateStructGEP(struct_type, carver_param, elem_idx);

      cur_block = insert_gep_carve_probe(gep, cur_block);
      elem_idx++;
    }
  }

  IRB->restoreIP(cur_ip);
  IRB->CreateCall(struct_carver, {struct_ptr});
  return;
}

void insert_check_carve_ready() {
  BasicBlock *cur_block = IRB->GetInsertBlock();

  BasicBlock *new_end_block =
      cur_block->splitBasicBlock(&(*IRB->GetInsertPoint()));

  BasicBlock *carve_block = BasicBlock::Create(
      *Context, "carve_block", cur_block->getParent(), new_end_block);

  IRB->SetInsertPoint(cur_block->getTerminator());

  Instruction *ready_load_instr = IRB->CreateLoad(Int8Ty, global_carve_ready);
  Value *ready_cmp =
      IRB->CreateICmpEQ(ready_load_instr, ConstantInt::get(Int8Ty, 1));

  Instruction *ready_br =
      IRB->CreateCondBr(ready_cmp, carve_block, new_end_block);
  ready_br->removeFromParent();
  ReplaceInstWithInst(cur_block->getTerminator(), ready_br);

  IRB->SetInsertPoint(carve_block);

  Instruction *br_instr = IRB->CreateBr(new_end_block);

  IRB->SetInsertPoint(new_end_block->getFirstNonPHIOrDbgOrLifetime());
  // IRB->CreateStore(ConstantInt::get(Int8Ty, 1), global_carve_ready);

  IRB->SetInsertPoint(br_instr);
}

// Remove alloca (local variable) memory tracking info.
void insert_dealloc_probes() {
  for (auto alloc_instr : tracking_allocas) {
    Value *casted_ptr =
        IRB->CreateCast(Instruction::CastOps::BitCast, alloc_instr, Int8PtrTy);
    IRB->CreateCall(remove_probe, {casted_ptr});
  }
}

std::set<std::string> no_carve_funcs{
    "_start_c",          "__init_libc",
    "__libc_csu_init",   "__libc_csu_fini",
    "__libc_start_main", "__libc_start_main_stage2",
    "libc_start_init",   "exit",
    "libc_exit_fini",    "_Exit",
};

void gen_class_carver() {
  class_carver =
      Mod->getOrInsertFunction("__class_carver", VoidTy, Int8PtrTy, Int32Ty);
  Function *class_carver_func = dyn_cast<Function>(class_carver.getCallee());

  BasicBlock *entry_BB =
      BasicBlock::Create(*Context, "entry", class_carver_func);

  BasicBlock *default_BB =
      BasicBlock::Create(*Context, "default", class_carver_func);

  // Put return void ad default block
  IRB->SetInsertPoint(default_BB);
  IRB->CreateRetVoid();

  IRB->SetInsertPoint(entry_BB);

  Value *carving_ptr = class_carver_func->getArg(0);  // *int8
  Value *class_idx = class_carver_func->getArg(1);    // int32

  SwitchInst *switch_inst =
      IRB->CreateSwitch(class_idx, default_BB, num_class_name_const + 1);

  for (auto class_type : class_name_map) {
    int case_id = class_type.second.first;
    BasicBlock *case_block = BasicBlock::Create(
        *Context, std::to_string(case_id), class_carver_func);
    switch_inst->addCase(ConstantInt::get(Int32Ty, case_id), case_block);
    IRB->SetInsertPoint(case_block);

    StructType *class_type_ptr = class_type.first;

    Type *class_ptr_ty = PointerType::get(class_type_ptr, 0);

    Value *casted_var = IRB->CreateCast(Instruction::CastOps::BitCast,
                                        carving_ptr, class_ptr_ty);

    const std::string class_name = class_type_ptr->getName().str();
    const std::string symbol_name = convert_symbol_str(class_name);

    llvm::errs() << "class name : " << class_name << "\n";
    llvm::errs() << "symbol name : " << symbol_name << "\n";

    if (custom_carvers.find(symbol_name) != custom_carvers.end()) {
      llvm::errs() << "Found symbol : " << symbol_name << "\n";
      FunctionCallee custom_carver = Mod->getOrInsertFunction(
          "__Carv_custom_" + symbol_name, VoidTy, class_ptr_ty);
      IRB->CreateCall(custom_carver, {casted_var});
      IRB->CreateRetVoid();
      continue;
    }

    insert_struct_carve_probe_inner(casted_var, class_type_ptr);
    IRB->CreateRetVoid();
  }

  // default is char *
  int case_id = num_class_name_const;
  BasicBlock *case_block =
      BasicBlock::Create(*Context, std::to_string(case_id), class_carver_func);
  switch_inst->addCase(ConstantInt::get(Int32Ty, case_id), case_block);
  IRB->SetInsertPoint(case_block);

  Value *load_val = IRB->CreateLoad(Int8Ty, carving_ptr);
  IRB->CreateCall(carv_char_func, {load_val});
  IRB->CreateRetVoid();

  switch_inst->setDefaultDest(case_block);

  default_BB->eraseFromParent();

  return;
}