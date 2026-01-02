# Mach-O Symbol Stripper

A professional toolset for stripping specific exported symbols from ARM64 Mach-O binaries (dylibs) while maintaining binary validity and the functionality of other exports.

## Features

- **Precision Stripping**: Removes specific symbols from the Symbol Table (`LC_SYMTAB`) and Export Trie (`LC_DYLD_INFO` / `LC_DYLD_EXPORTS_TRIE`).
- **Code Signing**: Automatically removes invalid code signatures (`LC_CODE_SIGNATURE`).
- **Zero Dependencies (C++)**: The C++ implementation depends only on standard system headers.
- **Cross-Platform (Python)**: The Python implementation uses `lief` for easy scripting.

## Structure

- `src/`: C++ source code (`strip_symbol.cpp`)
- `python/`: Python script (`strip_symbol.py`)
- `tests/`: Test resources and source code
- `samples/`: Sample binaries for testing

## C++ Tool

### Build

```bash
make
```

### Usage

```bash
./strip_symbol_cpp -i <input_dylib> [-o <output_dylib>] [-s <symbol>] [-f <file>]
```

- `-i`: Input dylib path
- `-o`: Output dylib path (default: `*_stripped.dylib`)
- `-s`: Symbol to strip (can be used multiple times)
- `-f`: File containing list of symbols to strip (one per line)

## Python Tool

### Requirements

```bash
pip install lief
```

### Usage

```bash
python3 python/strip_symbol.py -i <input_dylib> [-o <output_dylib>] [-s <symbol>] [-f <file>]
```

## Testing

To build and run the test suite:

```bash
make test
```

This will compile a test dylib, run the C++ stripper on it, and verify that the target symbol is hidden while others remain accessible.
