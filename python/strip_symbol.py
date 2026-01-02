import lief
import sys
import argparse
import os

def strip_symbols(input_path, output_path, symbols_to_strip):
    print(f"[*] Loading {input_path}...")
    try:
        binary = lief.parse(input_path)
    except lief.lief_errors.parsing_error:
        print("[-] Failed to parse binary")
        return False

    if not binary:
        print("[-] Failed to parse binary")
        return False

    print(f"[*] Stripping {len(symbols_to_strip)} symbols...")
    
    # 1. Remove from Symbol Table
    count = 0
    to_remove = []
    
    for sym in binary.symbols:
        if sym.name in symbols_to_strip:
            to_remove.append(sym)
            
    for sym in to_remove:
        # print(f"  [+] Removing from Symbol Table: {sym.name}")
        binary.remove(sym)
        count += 1
        
    print(f"[*] Removed {count} symbols from Symbol Table")

    # 2. Remove from Exports
    exp_count = 0
    to_remove_exp = []
    for exp in binary.exported_symbols:
        if exp.name in symbols_to_strip:
            to_remove_exp.append(exp)
            
    for exp in to_remove_exp:
        # print(f"  [+] Removing from Exports: {exp.name}")
        binary.remove_symbol(exp.name) 
        exp_count += 1
        
    print(f"[*] Removed {exp_count} exported symbols")

    # 3. Remove Code Signature
    # LIEF handles this by invalidating it upon write or we can explicitly remove
    # Checking if we should remove explicitly or relies on LIEF
    # Ideally standard LIEF usage implies modification invalidates sig.
    
    print(f"[*] Saving to {output_path}...")
    try:
        binary.write(output_path)
    except Exception as e:
        print(f"[-] Failed to write binary: {e}")
        return False
        
    print("[+] Done.")
    return True

def main():
    parser = argparse.ArgumentParser(description="Strip specific symbols from Mach-O dylib (Symbol Table & Export Trie)")
    parser.add_argument("input_pos", nargs='?', help="Input dylib path (positional)")
    parser.add_argument("-i", "--input", help="Input dylib path (flag)")
    parser.add_argument("-o", "--output", help="Output dylib path (default: <input>_stripped.dylib)")
    parser.add_argument("-s", "--symbol", action='append', help="Symbol to strip (can be used multiple times)")
    parser.add_argument("-f", "--file", help="File containing list of symbols to strip (one per line)")
    
    args = parser.parse_args()
    
    input_file = args.input if args.input else args.input_pos
    if not input_file:
        parser.error("Input file is required. Use -i <input> or specify as positional argument.")

    if not args.symbol and not args.file:
        parser.error("No symbols specified. Use -s/--symbol or -f/--file.")
        
    symbols = set()
    if args.symbol:
        symbols.update(args.symbol)
        
    if args.file:
        if not os.path.exists(args.file):
            print(f"[-] Symbol file not found: {args.file}")
            sys.exit(1)
        with open(args.file, 'r') as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith('#'):
                    symbols.add(line)
                    
    output_path = args.output
    if not output_path:
        base, ext = os.path.splitext(input_file)
        output_path = f"{base}_stripped{ext}"
        
    # Ensure output directory exists
    out_dir = os.path.dirname(output_path)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    
    if not os.path.exists(input_file):
         print(f"[-] Input file not found: {input_file}")
         sys.exit(1)

    if not strip_symbols(input_file, output_path, symbols):
        sys.exit(1)

if __name__ == "__main__":
    main()
