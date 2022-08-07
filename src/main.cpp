/*
 *  The Grace Programming Language.
 *
 *  This file contains the main entry point and argument parsing for the Grace interpreter. 
 *  
 *  Copyright (c) 2022 - Present, Ryan Jeffares.
 *  All rights reserved.
 *
 *  For licensing information, see grace.hpp
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include <dynload.h>
#include <dyncall.h>

#include <fmt/core.h>
#include <fmt/color.h>

#include "grace.hpp"
#include "compiler.hpp"

static void Error(const std::string& message)
{
  fmt::print(stderr, fmt::fg(fmt::color::red) | fmt::emphasis::bold, "ERROR: ");
  fmt::print(stderr, "{}\n", message);
}

static void Usage()
{
  fmt::print("Grace {}.{}.{}\n\n", GRACE_MAJOR_VERSION, GRACE_MINOR_VERSION, GRACE_PATCH_NUMBER);
  fmt::print("USAGE:\n");
  fmt::print("  grace [options] file [grace_options]\n\n");
  fmt::print("OPTIONS:\n");
  fmt::print("  -h, --help                    Print help info and exit\n");
  fmt::print("  -V, --version                 Print version info and exit\n");
  fmt::print("  -v, --verbose                 Enable verbose mode - print compilation and run times, print compiler warnings\n");
  fmt::print("  -we, --warnings-error         Show compiler warnings, warnings result in errors\n");
}

void TestLibrary()
{
  DLLib* lib = dlLoadLibrary("../../libs/grace_test/libGraceTest.so");
  if (lib == NULL) {
    std::cout << "Failed to load library" << std::endl;
    return;
  }

  void* sayHello = dlFindSymbol(lib, "SayHello");
  if (sayHello == nullptr) {
    std::cout << "Failed to load symbol" << std::endl;
    dlFreeLibrary(lib);
    return;
  }

  DCCallVM* vm = dcNewCallVM(4096);
  dcMode(vm, DC_CALL_C_DEFAULT);

  dcReset(vm);
  dcCallVoid(vm, sayHello);

  void* printString = dlFindSymbol(lib, "PrintString");
  dcReset(vm);
  dcArgPointer(vm, (DCpointer)"this is a string");
  dcCallVoid(vm, printString);

  void* sqrtFunc = dlFindSymbol(lib, "Sqrt");
  dcReset(vm);
  dcArgDouble(vm, 10.0);
  auto res = dcCallDouble(vm, sqrtFunc);
  std::cout << res << std::endl;

  dlFreeLibrary(lib);
}

int main(int argc, const char* argv[])
{
  TestLibrary();

  if (argc < 2) {
    Usage();
    return 1;
  }

  std::vector<std::string> args;
  args.reserve(argc);
  for (auto i = 0; i < argc; i++) {
    args.emplace_back(argv[i]);
  }

  std::filesystem::path filePath;
  bool verbose = false;
  bool warningsError = false;

  std::vector<std::string> graceMainArgs;
  auto appendToGraceArgs = false;
  for (auto i = 1; i < argc; i++) {
    if (args[i] == "--version" || args[i] == "-V") {
      if (appendToGraceArgs) {
        graceMainArgs.push_back(args[i]);
      } else {
        fmt::print("Grace {}.{}.{}\n", GRACE_MAJOR_VERSION, GRACE_MINOR_VERSION, GRACE_PATCH_NUMBER);
        return 0;
      }
    } else if (args[i] == "--help" || args[i] == "-h") {
      if (appendToGraceArgs) {
        graceMainArgs.push_back(args[i]);
      } else {
        Usage();
        return 0;
      }
    } else if (args[i] == "--verbose" || args[i] == "-v") {
      if (appendToGraceArgs) {
        graceMainArgs.push_back(args[i]);
      } else {
        verbose = true;
      }
    } else if (args[i] == "--warnings-error" || args[i] == "-we") {
      if (appendToGraceArgs) {
        graceMainArgs.push_back(args[i]);
      } else {
        warningsError = true;
      }
    } else if (args[i].ends_with(".gr")) {
      // first .gr file will be used as the file to run
      // any other command line flags for the interpreter, e.g. -v, should be given before the file
      // any args after the first .gr file will be given to the main function in the grace script
      if (appendToGraceArgs) {
        graceMainArgs.push_back(args[i]);
      } else {
        filePath = args[i];
      }
      appendToGraceArgs = true;
    } else {
      if (appendToGraceArgs) {
        graceMainArgs.push_back(args[i]);
      } else {
        Error(fmt::format("Unrecognised argument '{}'\n", args[i]));
        Usage();
        return 1;
      }
    }
  }

  if (filePath.empty()) {
    Error("no '.gr' file given");
    return 1;
  }
  
  if (!std::filesystem::exists(filePath)) {
    Error(fmt::format("provided file '{}' does not exist", filePath.string()));
    return 1;
  }

  std::stringstream inStream;
  try {
    std::ifstream inFile(filePath);
    inStream << inFile.rdbuf();
  } catch (const std::exception& e) {
    Error(e.what());
    return 1;
  }

  return static_cast<int>(
    Grace::Compiler::Compile(
      filePath.string(), inStream.str(), verbose, warningsError, graceMainArgs
    )
  );
}
