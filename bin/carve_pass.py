#!/usr/bin/python3

import sys, os
import subprocess as sp
from pathlib import Path
import tools.utils as utils

if len(sys.argv) <= 1:
  print("usage : {} <input.bc> <func_args|complex_types> [<compile args> ...]".format(sys.argv[0]))
  exit()

inputbc = sys.argv[1]
mode = sys.argv[2]
compile_args = sys.argv[3:]

if mode == "func_args":
  so_name = "carve_func_args_pass.so"
elif mode == "complex_types":
  so_name = "carve_type_pass.so"
else:
  print("mode must be func_args or complex_types")
  exit()

if utils.check_given_bitcode(inputbc) == False:
  exit()

ld_path = utils.get_ld_path()

#get carve_func_args_pass.so filepath
source_path = Path(__file__).resolve()
source_dir = str(source_path.parent.parent)

so_path = source_dir + "/lib/" + so_name

outname = ".".join(inputbc.split(".")[:-1]) + ".carv"

if len(compile_args) == 0:
  link_args = utils.get_link_option(inputbc)
  compile_args = link_args

clang_ver = utils.get_clang_version()
if clang_ver == 15:
  force_legacy_pm_flag = "-flegacy-pass-manager"
else:
  force_legacy_pm_flag = "-fno-experimental-new-pass-manager"

cmd = ["clang++", "--ld-path=" + ld_path
  , force_legacy_pm_flag , "-ggdb", "-O0"
#  , "-fsanitize=address"
#  , "-O2"
  , "-Xclang", "-load", "-Xclang", so_path, "-fPIC"
  , "-I", source_dir + "/include", "-o", outname
  , "-L", source_dir + "/lib", inputbc, "-l:fa_carver.a" ] + compile_args

#opt  -enable-new-pm=0  -load ../../lib/carve_func_args_pass.so --carve < main.bc -o out.bc

env=os.environ.copy()
#env["DUMP_IR"] = "1"

print(" ".join(cmd))

process= sp.Popen(cmd, env=env, stdout=sp.PIPE, stderr=sp.STDOUT, encoding="utf-8")

while True:
  line = process.stdout.readline()
  if line == '' and process.poll() != None:
    break

  print(line.strip(), flush=True)
