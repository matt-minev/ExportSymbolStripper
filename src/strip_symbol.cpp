#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/stab.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <map>



class MachORebuilder {
public:
    MachORebuilder(const std::string& path) : filepath(path) {}

    bool load() {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "[-] Failed to open file: " << filepath << std::endl;
            return false;
        }
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        buffer.resize(size);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            std::cerr << "[-] Failed to read file" << std::endl;
            return false;
        }
        return true;
    }

    struct CommandInfo {
        uint32_t offset;
        uint32_t cmd;
        uint32_t size;
    };

    bool parse() {
        if (buffer.size() < sizeof(mach_header_64)) {
            std::cerr << "[-] File too small" << std::endl;
            return false;
        }

        header = reinterpret_cast<mach_header_64*>(buffer.data());
        if (header->magic != MH_MAGIC_64) {
            std::cerr << "[-] Not a 64-bit Mach-O file" << std::endl;
            return false;
        }
        if (header->cputype != CPU_TYPE_ARM64) {
            std::cerr << "[-] Not an ARM64 binary" << std::endl;
            // return false; 
        }

        uint32_t offset = sizeof(mach_header_64);
        for (uint32_t i = 0; i < header->ncmds; i++) {
            if (offset + sizeof(load_command) > buffer.size()) {
                std::cerr << "[-] Malformed load commands" << std::endl;
                return false;
            }

            load_command* lc = reinterpret_cast<load_command*>(buffer.data() + offset);
            
            if (lc->cmd == LC_SYMTAB) {
                symtab_cmd_offset = offset;
            } else if (lc->cmd == LC_DYSYMTAB) {
                dysymtab_cmd_offset = offset;
            } else if (lc->cmd == LC_DYLD_INFO_ONLY || lc->cmd == LC_DYLD_INFO) {
                dyld_info_cmd_offset = offset;
            } else if (lc->cmd == LC_DYLD_EXPORTS_TRIE) {
                exports_trie_cmd_offset = offset;
            } else if (lc->cmd == LC_CODE_SIGNATURE) {
                code_sig_cmd_offset = offset;
            }

            offset += lc->cmdsize;
        }

        return true;
    }

    bool strip_symbols(const std::vector<std::string>& symbols) {
        for (const auto& sym : symbols) {
             remove_from_symbol_table(sym);
        }
        if (!remove_from_export_trie(symbols)) return false;
        remove_code_signature(); // Always remove signature if we modified file
        return true; 
    }

    bool save(const std::string& out_path) {
        std::ofstream file(out_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[-] Failed to open output file: " << out_path << std::endl;
            return false;
        }
        file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        return true;
    }

private:
    struct TrieNode {
        uint64_t terminal_size = 0;
        std::vector<uint8_t> terminal_data;
        std::vector<std::pair<std::string, TrieNode*>> children; // Keep order potentially? Or Vector of pairs.
        
        ~TrieNode() {
            for (auto& child : children) {
                delete child.second;
            }
        }
    };

    void remove_code_signature() {
        if (code_sig_cmd_offset == 0) return;
        
        std::cout << "[*] Removing code signature command..." << std::endl;
        
        uint32_t next_cmd_offset = code_sig_cmd_offset + sizeof(linkedit_data_command); 
        uint32_t commands_end = sizeof(mach_header_64) + header->sizeofcmds;
        
        size_t bytes_to_move = commands_end - next_cmd_offset;
        
        memmove(buffer.data() + code_sig_cmd_offset, buffer.data() + next_cmd_offset, bytes_to_move);
        
        header->ncmds--;
        header->sizeofcmds -= sizeof(linkedit_data_command);
        
        memset(buffer.data() + sizeof(mach_header_64) + header->sizeofcmds, 0, sizeof(linkedit_data_command));
        
        code_sig_cmd_offset = 0;
    }

    // LC_DYLD_EXPORTS_TRIE might not be defined in oldSDKs
    #ifndef LC_DYLD_EXPORTS_TRIE
    #define LC_DYLD_EXPORTS_TRIE (0x33 | LC_REQ_DYLD)
    #endif

    bool remove_from_export_trie(const std::vector<std::string>& symbols) {
        uint32_t export_off = 0;
        uint32_t export_size = 0;


        if (exports_trie_cmd_offset != 0) {
            auto* cmd = get_ptr<linkedit_data_command>(exports_trie_cmd_offset);
            export_off = cmd->dataoff;
            export_size = cmd->datasize;
        } else if (dyld_info_cmd_offset != 0) {
            auto* cmd = get_ptr<dyld_info_command>(dyld_info_cmd_offset);
            export_off = cmd->export_off;
            export_size = cmd->export_size;

        } else {
            return true; // No export info
        }

        if (export_off == 0 || export_size == 0) return true;
        
        std::cout << "[*] Parsing Export Trie (Offset: " << export_off << ", Size: " << export_size << ")..." << std::endl;
        
        const uint8_t* start = buffer.data() + export_off;
        const uint8_t* end = start + export_size;
        
        TrieNode* root = parse_trie(start, end, start);
        if (!root) {
             std::cerr << "[-] Failed to parse Export Trie" << std::endl;
             return false;
        }
        
        std::cout << "[*] Pruning symbols from Trie..." << std::endl;
        for (const auto& sym : symbols) {
            prune_trie(root, sym);
        }
        
        std::cout << "[*] Rebuilding Export Trie..." << std::endl;
        std::vector<uint8_t> new_trie;
        
        serialize_trie(root, new_trie);
        
        std::cout << "[*] New Export Trie size: " << new_trie.size() << " (Old: " << export_size << ")" << std::endl;
        
        if (new_trie.size() <= export_size) {
            memcpy(buffer.data() + export_off, new_trie.data(), new_trie.size());
            memset(buffer.data() + export_off + new_trie.size(), 0, export_size - new_trie.size());
            
            // Should we update the size? 
            // If we update size, we must consider if it affects anything?
            // Usually ok to claim it's smaller. But padding is safer to avoid gaps logic unless we move everything.
            // For now, keep original size claim, but padded with 0.
        } else {
             std::cerr << "[-] New Export Trie is larger than original!" << std::endl;
             // return false; 
        }
        
        delete root;
        return true;
    }


    void print_trie(TrieNode* node, std::string prefix) {
        if (node->terminal_size > 0) {
            std::cout << "    Export: " << prefix << std::endl;
        }
        for (auto& child : node->children) {
            print_trie(child.second, prefix + child.first);
        }
    }

    // ULEB128 Helpers
    uint64_t read_uleb128(const uint8_t*& p, const uint8_t* end) {
        uint64_t result = 0;
        int shift = 0;
        while (p < end) {
            uint8_t byte = *p++;
            result |= (static_cast<uint64_t>(byte & 0x7f) << shift);
            if ((byte & 0x80) == 0) break;
            shift += 7;
        }
        return result;
    }

    void append_uleb128(std::vector<uint8_t>& buf, uint64_t val) {
        do {
            uint8_t byte = val & 0x7f;
            val >>= 7;
            if (val != 0) byte |= 0x80;
            buf.push_back(byte);
        } while (val != 0);
    }

    TrieNode* parse_trie(const uint8_t* start, const uint8_t* end, const uint8_t* current) {
        if (current >= end) return nullptr;
        
        TrieNode* node = new TrieNode();
        const uint8_t* p = current;
        
        node->terminal_size = read_uleb128(p, end);
        if (node->terminal_size > 0) {
            if (p + node->terminal_size > end) {
                delete node;
                return nullptr;
            }
            node->terminal_data.assign(p, p + node->terminal_size);
            p += node->terminal_size;
        }
        
        uint8_t child_count = *p++;
        for (int i = 0; i < child_count; i++) {
            std::string edge_str;
            while (p < end && *p != 0) {
                edge_str.push_back(static_cast<char>(*p++));
            }
            p++; // skip null
            
            uint64_t child_offset = read_uleb128(p, end);
            TrieNode* child = parse_trie(start, end, start + child_offset);
            if (child) {
                node->children.push_back({edge_str, child});
            }
        }
        return node;
    }

    bool prune_trie(TrieNode* node, const std::string& symbol) {
        // Returns true if child should be removed (pruned)
        
        // This is a bit tricky since the trie edges are segments of the string.
        // We need to match the symbol against the edges.
        
        for (auto it = node->children.begin(); it != node->children.end(); ) {
            const std::string& edge = it->first;
            
            // Check if symbol starts with this edge
            if (symbol.rfind(edge, 0) == 0) {
                std::string remaining = symbol.substr(edge.length());
                
                if (remaining.empty()) {
                    // Match found! 'it->second' is the node for this symbol.
                    // If it has no children, we remove it. 
                    // If it has children, we just remove the terminal info.
                    
                    TrieNode* target = it->second;
                    target->terminal_size = 0;
                    target->terminal_data.clear();
                    
                    std::cout << "[+] Found export trie node for " << symbol << std::endl;
                    
                    if (target->children.empty()) {
                        // Leaf node, remove entirely
                        delete target;
                        it = node->children.erase(it);
                        return true; // Let parent know we might be empty now
                    } else {
                        // Has children, just removed export info
                        it++;
                        return false; 
                    }
                } else {
                    // Traverse deeper
                    bool child_empty = prune_trie(it->second, remaining);
                    if (child_empty && it->second->children.empty() && it->second->terminal_size == 0) {
                         // Child became empty and useless, prune it
                         delete it->second;
                         it = node->children.erase(it);
                    } else {
                        it++;
                    }
                }
                return false; // Assuming only one match
            } else {
                it++;
            }
        }
        return false;
    }

    void serialize_trie(TrieNode* root, std::vector<uint8_t>& buf) {
        // BFS to determine order: Root first
        std::vector<TrieNode*> linear_nodes;
        std::vector<TrieNode*> q;
        q.push_back(root);
        linear_nodes.push_back(root);
        
        size_t head = 0;
        while(head < linear_nodes.size()){
            TrieNode* curr = linear_nodes[head++];
            // We need to preserve edge order if we want stability, 
            // but for BFS linear list it doesn't strictly matter as long as children are accessible.
            for(auto& pair : curr->children){
                linear_nodes.push_back(pair.second);
            }
        }
        
        // Initialize offsets to 0
        std::map<TrieNode*, uint64_t> node_offsets;
        for(auto* n : linear_nodes) node_offsets[n] = 0;
        
        // Loop to calculate offsets until stable
        bool changed = true;
        while(changed) {
            changed = false;
            uint64_t current_off = 0;
            for(auto* n : linear_nodes) {
                if(node_offsets[n] != current_off) {
                    node_offsets[n] = current_off;
                    changed = true;
                }
                
                // Calculate size of this node
                uint64_t size = 0;
                
                // Terminal info
                std::vector<uint8_t> tmp;
                append_uleb128(tmp, n->terminal_size);
                size += tmp.size();
                size += n->terminal_data.size();
                
                // Child Count
                size += 1; 
                
                // Children edges
                for(auto& c : n->children) {
                    size += c.first.length() + 1; // edge string + null
                    
                    tmp.clear();
                    append_uleb128(tmp, node_offsets[c.second]); // Use current known offset
                    size += tmp.size();
                }
                
                current_off += size;
            }
        }
        
        // Final write
        buf.clear();
        for(auto* n : linear_nodes) {
            // Write Terminal Size
            append_uleb128(buf, n->terminal_size);
            
            // Write Terminal Data
            buf.insert(buf.end(), n->terminal_data.begin(), n->terminal_data.end());
            
            // Write Child Count
            buf.push_back(static_cast<uint8_t>(n->children.size()));
            
            // Write Children
            for(auto& child : n->children) {
                const std::string& edge = child.first;
                buf.insert(buf.end(), edge.begin(), edge.end());
                buf.push_back(0); // null terminator
                
                append_uleb128(buf, node_offsets[child.second]);
            }
        }
    }

private:
    bool remove_from_symbol_table(const std::string& symbol) {
        bool any_removed = false;
        
        while (true) {
            auto* symtab = get_ptr<symtab_command>(symtab_cmd_offset);
            if (!symtab) return true; 
            
            auto* dysymtab = get_ptr<dysymtab_command>(dysymtab_cmd_offset);
            
            char* strtab = reinterpret_cast<char*>(buffer.data() + symtab->stroff);
            nlist_64* syms = reinterpret_cast<nlist_64*>(buffer.data() + symtab->symoff);
            
            int found_idx = -1;
            for (uint32_t i = 0; i < symtab->nsyms; i++) {
                uint32_t str_idx = syms[i].n_un.n_strx;
                if (str_idx == 0 || str_idx >= symtab->strsize) continue;
                
                const char* name = strtab + str_idx;
                if (symbol == name) {
                    found_idx = i;
                    break;
                }
            }
            
            if (found_idx == -1) {
                // Done removing this symbol
                break; 
            }
            
            std::cout << "[+] Found symbol at index " << found_idx << ", removing..." << std::endl;
            any_removed = true;
            
            // Shift symbols
            size_t bytes_to_move = (symtab->nsyms - 1 - found_idx) * sizeof(nlist_64);
            memmove(&syms[found_idx], &syms[found_idx + 1], bytes_to_move);
            
            // Zero out the last entry
            memset(&syms[symtab->nsyms - 1], 0, sizeof(nlist_64));
            
            // Update counts
            symtab->nsyms--;
            
            // Update Dynamic Symbol Table counts if available
            if (dysymtab) {
                if (found_idx >= (int)dysymtab->ilocalsym && found_idx < (int)(dysymtab->ilocalsym + dysymtab->nlocalsym)) {
                    dysymtab->nlocalsym--;
                    dysymtab->iextdefsym--;  
                    dysymtab->iundefsym--;   
                } else if (found_idx >= (int)dysymtab->iextdefsym && found_idx < (int)(dysymtab->iextdefsym + dysymtab->nextdefsym)) {
                    dysymtab->nextdefsym--;
                    dysymtab->iundefsym--;
                } else if (found_idx >= (int)dysymtab->iundefsym && found_idx < (int)(dysymtab->iundefsym + dysymtab->nundefsym)) {
                    dysymtab->nundefsym--;
                }
            }
        }
        
        if (!any_removed) {
            // std::cout << "[-] Symbol not found in symbol table: " << symbol << std::endl;
        }
        return true;
    }


private:
    std::string filepath;
    std::vector<uint8_t> buffer;
    mach_header_64* header = nullptr;
    
    // Offsets to important commands (0 if not found)
    uint32_t symtab_cmd_offset = 0;
    uint32_t dysymtab_cmd_offset = 0;
    uint32_t dyld_info_cmd_offset = 0;
    uint32_t exports_trie_cmd_offset = 0;
    uint32_t code_sig_cmd_offset = 0;
    
    // Helper to get ptr from offset
    template <typename T>
    T* get_ptr(uint32_t offset) {
        if (offset == 0) return nullptr;
        if (offset + sizeof(T) > buffer.size()) return nullptr;
        return reinterpret_cast<T*>(buffer.data() + offset);
    }
};

// Helper to read lines from file
std::vector<std::string> read_lines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[-] Failed to open file: " << path << std::endl;
        return lines;
    }
    std::string line;
    while (std::getline(file, line)) {
        // Trim whitespace
        size_t first = line.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) continue;
        size_t last = line.find_last_not_of(" \t\n\r");
        std::string trimmed = line.substr(first, (last - first + 1));
        
        if (!trimmed.empty() && trimmed[0] != '#') {
            lines.push_back(trimmed);
        }
    }
    return lines;
}

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " -i <input_dylib> [-o <output_dylib>] [-s <symbol>] [-f <symbol_file>]" << std::endl;
    std::cerr << "  -i <input>   : Path to input dylib" << std::endl;
    std::cerr << "  -o <output>  : Path to output dylib (default: <input>_stripped.dylib)" << std::endl;
    std::cerr << "  -s <symbol>  : Symbol to strip (can be used multiple times)" << std::endl;
    std::cerr << "  -f <file>    : File containing list of symbols to strip (one per line)" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string input_path;
    std::string output_path;
    std::vector<std::string> symbols;
    
    // Manual arg parsing to avoid dependencies
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-i" && i + 1 < argc) {
            input_path = argv[++i];
        } else if (arg == "-o" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "-s" && i + 1 < argc) {
            symbols.push_back(argv[++i]);
        } else if (arg == "-f" && i + 1 < argc) {
            std::vector<std::string> file_syms = read_lines(argv[++i]);
            symbols.insert(symbols.end(), file_syms.begin(), file_syms.end());
        } else {
             // Maybe it's a positional arg if we supported that, but let's be strict or allow legacy
             if (input_path.empty() && arg[0] != '-') {
                 input_path = arg; // Legacy support: first arg is input
             } else {
                 // std::cerr << "Unknown argument: " << arg << std::endl;
             }
        }
    }
    
    // Legacy support for positional args: input output sym1 sym2 ... happens if flags weren't used primarily
    // But let's enforce flags for "Professional CLI" or at least robust heuristics.
    // Actually, let's keep it simple: if -i is missing, try argv[1].
    
    if (input_path.empty()) {
        if (argc > 1 && argv[1][0] != '-') {
            input_path = argv[1];
            // If argc > 2 and argv[2] is not a flag...? 
            // Let's stick to the requested flags for strict "Professional" verify.
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }
    
    if (output_path.empty()) {
        // Default output
        size_t dot = input_path.find_last_of(".");
        if (dot != std::string::npos) {
            output_path = input_path.substr(0, dot) + "_stripped" + input_path.substr(dot);
        } else {
            output_path = input_path + "_stripped";
        }
    }
    
    if (symbols.empty()) {
         std::cerr << "[-] No symbols specified to strip." << std::endl;
         print_usage(argv[0]);
         return 1;
    }
    
    std::cout << "[*] Input: " << input_path << std::endl;
    std::cout << "[*] Output: " << output_path << std::endl;
    std::cout << "[*] Symbols to strip: " << symbols.size() << std::endl;

    MachORebuilder rebuilder(input_path);
    if (!rebuilder.load()) return 1;
    if (!rebuilder.parse()) return 1;

    if (!rebuilder.strip_symbols(symbols)) {
        std::cerr << "[-] Failed to strip symbols" << std::endl;
        return 1;
    }

    if (!rebuilder.save(output_path)) return 1;

    std::cout << "[+] Symbols stripped successfully" << std::endl;
    return 0;
}
