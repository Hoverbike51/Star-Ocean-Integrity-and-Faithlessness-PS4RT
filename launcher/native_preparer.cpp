// SPDX-License-Identifier: GPL-2.0-or-later
// Native Windows port of scripts/prepare.py + scripts/link_modules.py.
#include "native_preparer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace ps4rt {
namespace {

using Bytes = std::vector<std::uint8_t>;

template <class T> T get(const Bytes& bytes, std::uint64_t offset) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - static_cast<size_t>(offset))
        throw std::runtime_error("unexpected end of file");
    T result{};
    std::memcpy(&result, bytes.data() + offset, sizeof(result));
    return result;
}

template <class T> void put(Bytes& bytes, std::uint64_t offset, const T& value) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - static_cast<size_t>(offset))
        throw std::runtime_error("write outside image");
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

template <class T> void emit(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    if (!out) throw std::runtime_error("cannot write prepared image");
}

Bytes read_all(const fs::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("cannot open " + path.filename().string());
    const auto end = input.tellg();
    if (end <= 0) throw std::runtime_error(path.filename().string() + " is empty");
    if (end > static_cast<std::streamoff>(1024ull * 1024 * 1024))
        throw std::runtime_error("input file is too large");
    Bytes result(static_cast<size_t>(end));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
    if (!input) throw std::runtime_error("cannot read " + path.filename().string());
    return result;
}

void check_span(const Bytes& bytes, std::uint64_t offset, std::uint64_t size) {
    if (offset > bytes.size() || size > bytes.size() - static_cast<size_t>(offset))
        throw std::runtime_error("invalid offset or size");
}

struct ProgramHeader {
    std::uint32_t type{}, flags{};
    std::uint64_t offset{}, vaddr{}, paddr{}, filesz{}, memsz{}, align{};
};

struct Self {
    Bytes elf;
    std::uint64_t entry{};
    std::vector<ProgramHeader> ph;
};

Self parse_self(const fs::path& path) {
    const Bytes source = read_all(path);
    if (source.size() < 32 || get<std::uint32_t>(source, 0) != 0x1d3d154f)
        throw std::runtime_error(path.filename().string() + ": expected PS4 SELF");
    const std::uint16_t count = get<std::uint16_t>(source, 24);
    const std::uint64_t base = 32ull + static_cast<std::uint64_t>(count) * 32;
    check_span(source, base, 64);
    if (std::memcmp(source.data() + base, "\x7f" "ELF\x02\x01\x01", 7) ||
        get<std::uint16_t>(source, base + 18) != 62)
        throw std::runtime_error(path.filename().string() + ": expected x86-64 ELF");
    const std::uint64_t entry = get<std::uint64_t>(source, base + 24);
    const std::uint64_t phoff = get<std::uint64_t>(source, base + 32);
    const std::uint16_t phentsize = get<std::uint16_t>(source, base + 54);
    const std::uint16_t phnum = get<std::uint16_t>(source, base + 56);
    if (phentsize != 56 || !phnum || phnum >= 256)
        throw std::runtime_error(path.filename().string() + ": unsupported program headers");
    std::vector<ProgramHeader> ph;
    std::uint64_t elf_size = 0;
    for (std::uint16_t i = 0; i < phnum; ++i) {
        const std::uint64_t at = base + phoff + static_cast<std::uint64_t>(i) * 56;
        check_span(source, at, 56);
        ProgramHeader p;
        p.type = get<std::uint32_t>(source, at); p.flags = get<std::uint32_t>(source, at + 4);
        p.offset = get<std::uint64_t>(source, at + 8); p.vaddr = get<std::uint64_t>(source, at + 16);
        p.paddr = get<std::uint64_t>(source, at + 24); p.filesz = get<std::uint64_t>(source, at + 32);
        p.memsz = get<std::uint64_t>(source, at + 40); p.align = get<std::uint64_t>(source, at + 48);
        if (p.offset + p.filesz < p.offset) throw std::runtime_error("SELF segment overflow");
        elf_size = std::max(elf_size, p.offset + p.filesz);
        ph.push_back(p);
    }
    if (elf_size > 512ull * 1024 * 1024) throw std::runtime_error("SELF exceeds 512 MiB");
    Bytes elf(static_cast<size_t>(elf_size));
    const std::uint64_t header_end = phoff + static_cast<std::uint64_t>(phnum) * 56;
    check_span(source, base, header_end);
    std::memcpy(elf.data(), source.data() + base, static_cast<size_t>(header_end));
    std::vector<std::pair<std::uint64_t, std::uint64_t>> covered{{0, header_end}};
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::uint64_t at = 32ull + static_cast<std::uint64_t>(i) * 32;
        const std::uint64_t flags = get<std::uint64_t>(source, at);
        const std::uint64_t offset = get<std::uint64_t>(source, at + 8);
        const std::uint64_t size = get<std::uint64_t>(source, at + 16);
        const std::uint64_t memory = get<std::uint64_t>(source, at + 24);
        if (!(flags & 0x800)) continue;
        if (flags & 10) throw std::runtime_error("encrypted/compressed SELF segment unsupported");
        const size_t index = static_cast<size_t>((flags >> 20) & 4095);
        if (index >= ph.size()) throw std::runtime_error("invalid SELF segment index");
        const auto& p = ph[index];
        if (size != p.filesz || size != memory) throw std::runtime_error("blocked SELF layout unsupported");
        check_span(source, offset, size); check_span(elf, p.offset, size);
        std::memcpy(elf.data() + p.offset, source.data() + offset, static_cast<size_t>(size));
        covered.emplace_back(p.offset, p.offset + size);
    }
    for (size_t i = 0; i < ph.size(); ++i) {
        const auto& p = ph[i];
        bool present = !p.filesz;
        for (const auto& range : covered)
            present |= range.first <= p.offset && p.offset + p.filesz <= range.second;
        if (!present && (p.type == 1 || p.type == 2 || p.type == 0x61000000 || p.type == 0x61000010))
            throw std::runtime_error("required SELF segment unavailable");
    }
    return {std::move(elf), entry, std::move(ph)};
}

const ProgramHeader& header(const Self& self, std::uint32_t type) {
    const auto it = std::find_if(self.ph.begin(), self.ph.end(), [=](const auto& p) { return p.type == type; });
    if (it == self.ph.end()) throw std::runtime_error("required ELF header missing");
    return *it;
}

std::map<std::uint64_t, std::uint64_t> dynamic_tags(const Self& self,
                                                     std::vector<std::pair<std::uint64_t, std::uint64_t>>* ordered = nullptr) {
    const auto& dyn = header(self, 2);
    check_span(self.elf, dyn.offset, dyn.filesz);
    std::map<std::uint64_t, std::uint64_t> tags;
    for (std::uint64_t at = dyn.offset; at + 16 <= dyn.offset + dyn.filesz; at += 16) {
        const auto tag = get<std::uint64_t>(self.elf, at);
        const auto value = get<std::uint64_t>(self.elf, at + 8);
        if (!tag) break;
        tags[tag] = value;
        if (ordered) ordered->emplace_back(tag, value);
    }
    return tags;
}

std::string read_cstr(const Bytes& bytes, std::uint64_t offset, std::uint64_t end) {
    if (offset >= end || end > bytes.size()) throw std::runtime_error("bad ELF string offset");
    size_t at = static_cast<size_t>(offset);
    while (at < end && bytes[at]) ++at;
    if (at == end) throw std::runtime_error("unterminated ELF string");
    return std::string(reinterpret_cast<const char*>(bytes.data() + offset), at - static_cast<size_t>(offset));
}

std::string encode_id(std::uint64_t value) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";
    std::string out(1, alphabet[value & 63]);
    while (value >>= 6) out.insert(out.begin(), alphabet[value & 63]);
    return out;
}

using VersionedName = std::pair<std::string, std::uint16_t>;
struct Identity {
    std::string nid;
    VersionedName library, module;
    bool operator<(const Identity& other) const { return std::tie(nid, library, module) < std::tie(other.nid, other.library, other.module); }
    bool operator==(const Identity& other) const { return nid == other.nid && library == other.library && module == other.module; }
};

struct Symbol {
    std::string name;
    std::uint8_t type{}, binding{};
    std::uint16_t section{};
    std::uint64_t value{}, size{};
    std::optional<Identity> identity;
};
struct Relocation { std::uint64_t target{}; std::uint32_t kind{}, symbol{}; std::int64_t addend{}; };
struct Module {
    Self self;
    std::map<std::uint64_t, std::uint64_t> tags;
    std::vector<Symbol> symbols;
    std::vector<Relocation> relocations;
    std::map<std::string, VersionedName> libraries, modules;
};

Module parse_module(const fs::path& path) {
    Module result;
    result.self = parse_self(path);
    std::vector<std::pair<std::uint64_t, std::uint64_t>> ordered;
    result.tags = dynamic_tags(result.self, &ordered);
    const auto& lp = header(result.self, 0x61000000);
    const std::uint64_t blob = lp.offset;
    const auto tag = [&](std::uint64_t key) -> std::uint64_t {
        const auto it = result.tags.find(key); if (it == result.tags.end()) throw std::runtime_error("dynamic tag missing"); return it->second;
    };
    const std::uint64_t strings = blob + tag(0x61000035), strings_end = strings + tag(0x61000037);
    check_span(result.self.elf, strings, strings_end - strings);
    const auto string = [&](std::uint64_t offset) { return read_cstr(result.self.elf, strings + offset, strings_end); };
    for (const auto& [kind, value] : ordered) {
        auto* table = (kind == 0x61000013 || kind == 0x61000015) ? &result.libraries :
                      (kind == 0x6100000d || kind == 0x6100000f) ? &result.modules : nullptr;
        if (!table) continue;
        const std::string key = encode_id(value >> 48);
        const VersionedName identity{string(value & 0xffffffff), static_cast<std::uint16_t>((value >> 32) & 0xffff)};
        const auto existing = table->find(key);
        if (existing != table->end() && existing->second != identity) throw std::runtime_error("conflicting module/library IDs");
        (*table)[key] = identity;
    }
    const auto identity = [&](const std::string& name) -> std::optional<Identity> {
        const size_t a = name.find('#'), b = a == std::string::npos ? a : name.find('#', a + 1);
        if (a == std::string::npos || b == std::string::npos || name.find('#', b + 1) != std::string::npos) return {};
        const auto li = result.libraries.find(name.substr(a + 1, b - a - 1));
        const auto mi = result.modules.find(name.substr(b + 1));
        if (li == result.libraries.end() || mi == result.modules.end()) throw std::runtime_error("unknown symbol identity");
        return Identity{name.substr(0, a), li->second, mi->second};
    };
    const std::uint64_t symtab = blob + tag(0x61000039), symsize = tag(0x6100003f);
    if (symsize % 24) throw std::runtime_error("bad symbol table size");
    check_span(result.self.elf, symtab, symsize);
    for (std::uint64_t at = symtab; at < symtab + symsize; at += 24) {
        Symbol s;
        s.name = string(get<std::uint32_t>(result.self.elf, at));
        const auto info = get<std::uint8_t>(result.self.elf, at + 4);
        s.type = info & 15; s.binding = info >> 4; s.section = get<std::uint16_t>(result.self.elf, at + 6);
        s.value = get<std::uint64_t>(result.self.elf, at + 8); s.size = get<std::uint64_t>(result.self.elf, at + 16);
        if (!s.name.empty()) s.identity = identity(s.name);
        result.symbols.push_back(std::move(s));
    }
    for (const auto [offset_tag, size_tag] : {std::pair{0x61000029ull, 0x6100002dull}, std::pair{0x6100002full, 0x61000031ull}}) {
        const std::uint64_t table = blob + tag(offset_tag), size = tag(size_tag);
        if (size % 24) throw std::runtime_error("bad relocation table size");
        check_span(result.self.elf, table, size);
        for (std::uint64_t at = table; at < table + size; at += 24) {
            const auto info = get<std::uint64_t>(result.self.elf, at + 8);
            result.relocations.push_back({get<std::uint64_t>(result.self.elf, at), static_cast<std::uint32_t>(info),
                                          static_cast<std::uint32_t>(info >> 32), get<std::int64_t>(result.self.elf, at + 16)});
        }
    }
    return result;
}

bool mapped(const std::vector<ProgramHeader>& loads, std::uint64_t address, std::uint64_t length = 8) {
    if (address + length < address) return false;
    return std::any_of(loads.begin(), loads.end(), [&](const auto& p) {
        return p.vaddr <= address && address + length <= p.vaddr + p.memsz;
    });
}

std::vector<ProgramHeader> loads(const Self& self) {
    std::vector<ProgramHeader> result;
    std::copy_if(self.ph.begin(), self.ph.end(), std::back_inserter(result), [](const auto& p) {
        return p.type == 1 || p.type == 0x61000010;
    });
    if (result.empty()) throw std::runtime_error("no loadable ELF segments");
    return result;
}

struct OutReloc { std::uint64_t target{}, kind{}; std::int64_t value{}, addend{}; };
struct Segment { std::uint64_t address{}, size{}, flags{}; };
struct ModuleInfo { std::uint64_t base{}, size{}, init{}, tls_address{}, tls_memsz{}, tls_filesz{}, tls_module{}; };
struct Binding { std::uint64_t index{}, address{}, kind{}; };

unsigned patch_fs(Bytes& image, const std::vector<ProgramHeader>& ph, std::uint64_t base) {
    static constexpr std::array<std::uint8_t, 9> code{0x64,0x48,0x8b,0x04,0x25,0,0,0,0};
    unsigned count = 0;
    for (const auto& p : ph) {
        if (p.type != 1 || !(p.flags & 1)) continue;
        const size_t start = static_cast<size_t>(base + p.vaddr), end = static_cast<size_t>(base + p.vaddr + p.filesz);
        if (end > image.size()) throw std::runtime_error("executable segment outside image");
        for (size_t at = start; at + code.size() <= end; ++at)
            if (!std::memcmp(image.data() + at, code.data(), code.size())) { image[at] = 0x65; ++count; at += code.size() - 1; }
    }
    return count;
}

std::string canonical(const std::optional<Identity>& identity, const std::string& fallback = {}) {
    return identity ? identity->nid + "#" + identity->library.first : fallback;
}

std::uint64_t align64k(std::uint64_t value) { return (value + 65535) & ~65535ull; }

std::uint64_t image_size(const std::vector<ProgramHeader>& p) {
    std::uint64_t size = 0;
    for (const auto& item : p) size = std::max(size, item.vaddr + item.memsz);
    if (size > 512ull * 1024 * 1024) throw std::runtime_error("prepared image exceeds 512 MiB");
    return size;
}

void copy_loads(const Self& self, const std::vector<ProgramHeader>& source, Bytes& image, std::uint64_t base,
                std::vector<Segment>& segments) {
    for (const auto& p : source) {
        if (p.filesz > p.memsz) throw std::runtime_error("ELF segment file size exceeds memory size");
        check_span(self.elf, p.offset, p.filesz); check_span(image, base + p.vaddr, p.filesz);
        std::memcpy(image.data() + base + p.vaddr, self.elf.data() + p.offset, static_cast<size_t>(p.filesz));
        segments.push_back({base + p.vaddr, p.memsz, p.flags});
    }
}

bool libc_init_is_ret(const Module& libc) {
    for (const auto& s : libc.symbols) {
        if (!s.section || s.type != 2 || !s.identity || s.identity->nid != "bzQExy189ZI") continue;
        for (const auto& p : libc.self.ph)
            if (p.type == 1 && (p.flags & 1) && p.vaddr <= s.value && s.value + s.size <= p.vaddr + p.filesz) {
                check_span(libc.self.elf, p.offset + s.value - p.vaddr, s.size);
                return s.size == 1 && libc.self.elf[p.offset + s.value - p.vaddr] == 0xc3;
            }
    }
    return false;
}

void write_content(const fs::path& sfo_path, const fs::path& output) {
    const Bytes bytes = read_all(sfo_path);
    if (bytes.size() < 20 || std::memcmp(bytes.data(), "\0PSF", 4)) throw std::runtime_error("invalid param.sfo");
    const auto keys = get<std::uint32_t>(bytes, 8), values = get<std::uint32_t>(bytes, 12), count = get<std::uint32_t>(bytes, 16);
    std::array<std::uint32_t, 4> params{};
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint64_t at = 20ull + i * 16;
        check_span(bytes, at, 16);
        const auto key_offset = get<std::uint16_t>(bytes, at), format = get<std::uint16_t>(bytes, at + 2);
        const auto size = get<std::uint32_t>(bytes, at + 4), value_offset = get<std::uint32_t>(bytes, at + 12);
        const std::string key = read_cstr(bytes, keys + key_offset, bytes.size());
        if (key.rfind("USER_DEFINED_PARAM_", 0) == 0 && key.size() == 20 && key.back() >= '1' && key.back() <= '4' && format == 0x404 && size >= 4) {
            params[static_cast<size_t>(key.back() - '1')] = get<std::uint32_t>(bytes, values + value_offset);
        }
    }
    std::ofstream out(output / "content.bin", std::ios::binary | std::ios::trunc);
    const std::array<char, 8> magic{'P','S','4','R','T','C','0','1'}; emit(out, magic);
    const std::uint32_t full = 3; emit(out, full); for (auto value : params) emit(out, value);
}

} // namespace

std::string title_id_from_sfo(const fs::path& path) {
    const Bytes bytes = read_all(path);
    if (bytes.size() < 20 || std::memcmp(bytes.data(), "\0PSF", 4)) throw std::runtime_error("invalid param.sfo");
    const auto keys = get<std::uint32_t>(bytes, 8), values = get<std::uint32_t>(bytes, 12), count = get<std::uint32_t>(bytes, 16);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint64_t at = 20ull + i * 16; check_span(bytes, at, 16);
        const auto key_offset = get<std::uint16_t>(bytes, at);
        const auto size = get<std::uint32_t>(bytes, at + 4);
        const auto value_offset = get<std::uint32_t>(bytes, at + 12);
        if (read_cstr(bytes, keys + key_offset, bytes.size()) != "TITLE_ID") continue;
        check_span(bytes, values + value_offset, size);
        std::string id(reinterpret_cast<const char*>(bytes.data() + values + value_offset), size);
        if (const auto zero = id.find('\0'); zero != std::string::npos) id.resize(zero);
        return id;
    }
    throw std::runtime_error("TITLE_ID missing from param.sfo");
}

void prepare_title(const fs::path& game, const fs::path& output, const PrepareLog& log) {
    if (log) log(L"Native C++ preparer · analyzing eboot.bin");
    Module main = parse_module(game / "eboot.bin");
    Module libc = parse_module(game / "sce_module" / "libc.prx");
    const auto main_loads = loads(main.self);
    const std::uint64_t main_size = image_size(main_loads);
    if (!mapped(main_loads, main.self.entry, 1)) throw std::runtime_error("entry point outside image");
    Bytes image(static_cast<size_t>(main_size));
    std::vector<Segment> segments;
    copy_loads(main.self, main_loads, image, 0, segments);
    std::vector<OutReloc> relocs;
    std::vector<std::string> names;
    std::map<std::uint32_t, size_t> main_imports;
    for (const auto& r : main.relocations) {
        if (!mapped(main_loads, r.target)) throw std::runtime_error("main relocation outside image");
        if (r.kind == 8) relocs.push_back({r.target, 0, r.addend, 0});
        else if (r.kind == 1 || r.kind == 6 || r.kind == 7) {
            if (r.symbol >= main.symbols.size()) throw std::runtime_error("bad relocation symbol");
            const auto& s = main.symbols[r.symbol];
            if (s.section) {
                if (s.section == 0xfff1) put(image, r.target, static_cast<std::uint64_t>(s.value + r.addend));
                else relocs.push_back({r.target, 0, static_cast<std::int64_t>(s.value + r.addend), 0});
            } else {
                auto [it, inserted] = main_imports.emplace(r.symbol, names.size());
                if (inserted) names.push_back(s.name);
                const auto kind = s.type == 1 ? 2ull : 1ull;
                if ((kind == 1 && r.addend) || r.addend < 0 || r.addend >= 4096) throw std::runtime_error("unsupported import addend");
                relocs.push_back({r.target, kind, static_cast<std::int64_t>(it->second), r.addend});
            }
        } else throw std::runtime_error("unsupported main relocation");
    }
    const std::uint64_t flags = libc_init_is_ret(libc) ? 1 : 0;
    std::map<Identity, size_t> identities;
    // Preserve the Python linker's relocation order while keying the imported symbols by identity.
    for (const auto& [symbol_index, name_index] : main_imports) {
        const auto& identity = main.symbols[symbol_index].identity;
        if (!identity) throw std::runtime_error("main import has no PS4 identity");
        identities[*identity] = name_index;
        names[name_index] = canonical(identity, main.symbols[symbol_index].name);
    }
    auto imported = [&](const Symbol& s) -> size_t {
        if (!s.identity) throw std::runtime_error("module import has no identity");
        const auto it = identities.find(*s.identity);
        if (it != identities.end()) return it->second;
        const size_t index = names.size(); identities[*s.identity] = index; names.push_back(canonical(s.identity)); return index;
    };
    std::map<Identity, std::tuple<std::uint64_t, std::uint64_t, std::string>> exports;
    std::multimap<std::pair<std::string, std::string>, std::tuple<std::uint64_t, std::uint64_t, std::string>> by_nid;
    std::vector<ModuleInfo> module_table;
    std::uint64_t base = align64k(main_size), tls_module = 2;
    unsigned fs_patched = patch_fs(image, main.self.ph, 0);
    for (const auto& filename : {std::string("libc.prx"), std::string("libSceFios2.prx")}) {
        Module module = filename == "libc.prx" ? std::move(libc) : parse_module(game / "sce_module" / filename);
        const auto module_loads = loads(module.self); const auto size = image_size(module_loads);
        if (base + size > 512ull * 1024 * 1024) throw std::runtime_error("linked image exceeds 512 MiB");
        image.resize(static_cast<size_t>(base + size)); copy_loads(module.self, module_loads, image, base, segments);
        const auto tls_it = std::find_if(module.self.ph.begin(), module.self.ph.end(), [](const auto& p) { return p.type == 7; });
        const bool has_tls = tls_it != module.self.ph.end();
        std::uint64_t module_id = 0;
        if (has_tls) {
            if (tls_it->filesz > tls_it->memsz || !mapped(module_loads, tls_it->vaddr, tls_it->memsz)) throw std::runtime_error(filename + ": unsupported TLS");
            module_id = tls_module++;
        }
        for (const auto& r : module.relocations) {
            if (!mapped(module_loads, r.target) || r.symbol >= module.symbols.size()) throw std::runtime_error(filename + ": bad relocation");
            const auto target = base + r.target; const auto& s = module.symbols[r.symbol];
            if (r.kind == 8) relocs.push_back({target, 0, static_cast<std::int64_t>(base + r.addend), 0});
            else if (r.kind == 16) {
                if (!s.name.empty() || s.type != 3 || r.addend || !module_id) throw std::runtime_error(filename + ": unsupported TLS relocation");
                put(image, target, module_id);
            } else if (r.kind == 1 || r.kind == 6 || r.kind == 7) {
                if (s.section) {
                    if (s.section == 0xfff1) put(image, target, static_cast<std::uint64_t>(s.value + r.addend));
                    else relocs.push_back({target, 0, static_cast<std::int64_t>(base + s.value + r.addend), 0});
                } else if (!s.identity && s.binding == 2) put(image, target, std::uint64_t{});
                else {
                    const auto kind = s.type == 1 ? 2ull : 1ull;
                    if ((kind == 1 && r.addend) || r.addend < 0 || r.addend >= 4096) throw std::runtime_error(filename + ": unsupported import addend");
                    relocs.push_back({target, kind, static_cast<std::int64_t>(imported(s)), r.addend});
                }
            } else throw std::runtime_error(filename + ": unsupported relocation type");
        }
        for (const auto& s : module.symbols) if (s.section && (s.binding == 1 || s.binding == 2) && s.identity && (s.type == 1 || s.type == 2)) {
            if (!mapped(module_loads, s.value, std::max<std::uint64_t>(1, s.size))) throw std::runtime_error(filename + ": export outside image");
            const auto value = std::make_tuple(base + s.value, s.type == 1 ? 2ull : 1ull, filename);
            if (!exports.emplace(*s.identity, value).second) throw std::runtime_error(filename + ": ambiguous export");
            by_nid.emplace(std::make_pair(s.identity->nid, s.identity->library.first), value);
        }
        fs_patched += patch_fs(image, module.self.ph, base);
        module_table.push_back({base, size, base + (module.tags.count(12) ? module.tags.at(12) : 0),
            has_tls ? base + tls_it->vaddr : 0, has_tls ? tls_it->memsz : 0, has_tls ? tls_it->filesz : 0, module_id});
        base = align64k(base + size);
    }
    std::vector<Binding> bindings;
    for (const auto& [identity, index] : identities) {
        auto found = exports.find(identity);
        std::optional<std::tuple<std::uint64_t, std::uint64_t, std::string>> value;
        if (found != exports.end()) value = found->second;
        else if (identity.library.first == "libSceLibcInternal") {
            auto range = by_nid.equal_range({identity.nid, "libc"});
            if (std::distance(range.first, range.second) == 1) value = range.first->second;
        }
        if (value) bindings.push_back({index, std::get<0>(*value), std::get<1>(*value)});
    }
    std::sort(bindings.begin(), bindings.end(), [](const Binding& left, const Binding& right) {
        return left.index < right.index;
    });
    const auto main_tls = std::find_if(main.self.ph.begin(), main.self.ph.end(), [](const auto& p) { return p.type == 7; });
    std::array<std::uint64_t, 4> tls{};
    if (main_tls != main.self.ph.end()) {
        if (main_tls->filesz > main_tls->memsz || main_tls->memsz > 1024 * 1024 || main_tls->vaddr + main_tls->filesz > main_size)
            throw std::runtime_error("unsupported eboot TLS");
        tls = {main_tls->vaddr, main_tls->filesz, main_tls->memsz, main_tls->align};
    }
    const auto proc = std::find_if(main.self.ph.begin(), main.self.ph.end(), [](const auto& p) { return p.type == 0x61000001; });
    if (proc == main.self.ph.end()) throw std::runtime_error("process parameter header missing");
    fs::create_directories(output);
    std::ofstream out(output / "boot-linked.bin", std::ios::binary | std::ios::trunc);
    const std::array<char, 8> magic{'P','S','4','R','T','B','0','5'}; emit(out, magic);
    for (auto value : {static_cast<std::uint64_t>(image.size()), main.self.entry, static_cast<std::uint64_t>(segments.size()),
                       static_cast<std::uint64_t>(relocs.size()), static_cast<std::uint64_t>(names.size()), flags}) emit(out, value);
    emit(out, proc->vaddr); for (auto value : tls) emit(out, value);
    emit(out, static_cast<std::uint64_t>(module_table.size()));
    for (const auto& item : module_table) for (auto value : {item.base,item.size,item.init,item.tls_address,item.tls_memsz,item.tls_filesz,item.tls_module}) emit(out, value);
    emit(out, static_cast<std::uint64_t>(bindings.size()));
    for (const auto& item : bindings) for (auto value : {item.index,item.address,item.kind}) emit(out, value);
    for (const auto& item : segments) for (auto value : {item.address,item.size,item.flags}) emit(out, value);
    for (const auto& name : names) {
        if (name.size() >= 128) throw std::runtime_error("import name too long");
        std::array<char, 128> field{}; std::copy(name.begin(), name.end(), field.begin()); emit(out, field);
    }
    for (const auto& item : relocs) { emit(out, item.target); emit(out, item.kind); emit(out, item.value); emit(out, item.addend); }
    out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    if (!out) throw std::runtime_error("cannot finish boot-linked.bin");
    write_content(game / "sce_sys" / "param.sfo", output);
    if (log) log(L"Native preparation complete · linked modules: libc.prx, libSceFios2.prx");
    (void)fs_patched;
}

} // namespace ps4rt
