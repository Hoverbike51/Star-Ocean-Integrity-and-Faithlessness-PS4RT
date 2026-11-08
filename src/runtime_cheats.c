/* GoldHEN code cheats switched live from the settings menu (see runtime_cheats.h). */
#define _GNU_SOURCE
#include "runtime_cheats.h"
#include "sha256_win.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

enum { MAX_CHEATS = 64, MAX_LINES = 1024, MAX_LINE_BYTES = 4096, MAX_FILE_PROBLEMS = 32,
       NAME_BYTES = 64, PROBLEM_BYTES = 384, HOOK_MIN = 5, HOOK_MAX = 16, MAX_CHOICES = 8 };
typedef struct {
    uint64_t offset;
    uint32_t size;
    int hook;                       /* E9 rel32 + NOPs into a cave of the same cheat */
    unsigned char *on, *off;
} CheatLine;
typedef struct {
    char name[NAME_BYTES];
    char description[384];
    int catalog_id,builtin,meta_seen;
    char problem[PROBLEM_BYTES];    /* non-empty: unavailable */
    int action; uint64_t action_offset; uint32_t add,limit;
    uint32_t first, count;          /* lines[first .. first+count) */
    /* A valued cheat: the f32 slot's offset in its cave line lines[value_line], and the choices. */
    uint64_t value_offset;
    uint32_t value_line;
    int choice_count;
    float choices[MAX_CHOICES];
} Cheat;

static unsigned char *base;
static char bound_hash[65];
static CheatRange data_ranges[64]; static size_t data_count;
void runtime_cheats_bind_image(unsigned char *image,size_t size,const CheatRange *ranges,size_t count) {
    base=image; data_count=count<64?count:64;
    if(data_count)memcpy(data_ranges,ranges,data_count*sizeof(*ranges));
    if(!so5_image_sha256(image,size,bound_hash))bound_hash[0]=0;
}

static Cheat cheats[MAX_CHEATS];
static int cheat_total;
static CheatLine lines[MAX_LINES];
static uint32_t line_total;
static char file_problems[MAX_FILE_PROBLEMS][PROBLEM_BYTES];
static int file_problem_total;
/* The hook whose CC byte is in place (0: none); runtime_cheat_trap waits while it is set. */
static _Atomic uintptr_t patching;
static SRWLOCK lock = SRWLOCK_INIT;
static void lock_cheats(void) { AcquireSRWLockExclusive(&lock); }
static void unlock_cheats(void) { ReleaseSRWLockExclusive(&lock); }

/* ---- table ---- */

static int hex_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
/* Parses `length` hex digits from text into out (NULL: size only): the byte count, -1 when invalid. */
static long parse_hex(const char *text, size_t length, unsigned char *out) {
    if (!length || length % 2 || length / 2 > MAX_LINE_BYTES) return -1;
    for (size_t i = 0; i < length; i += 2) {
        int hi = hex_value(text[i]), lo = hex_value(text[i + 1]);
        if (hi < 0 || lo < 0) return -1;
        if (out) out[i / 2] = (unsigned char)(hi << 4 | lo);
    }
    return (long)(length / 2);
}
static void spaced(char *out, size_t size, const unsigned char *data, size_t count) {
    size_t at = 0;
    out[0] = 0;
    for (size_t i = 0; i < count && at + 4 <= size; ++i)
        at += (size_t)snprintf(out + at, size - at, i ? " %02X" : "%02X", data[i]);
}
/* "<name>: 0x<offset>: expected <bytes> (<label>), found <bytes>; ...", the first 16 bytes from
 * the first difference (scripts/cheats.py mismatch()). */
static void mismatch(char *out, size_t size, const Cheat *c, const CheatLine *l, const char *label) {
    const unsigned char *found = base + l->offset;
    uint32_t first = 0;
    while (first < l->size && found[first] == l->off[first]) ++first;
    if (first == l->size) first = 0;
    uint32_t start = l->size > 16 ? first : 0, shown = l->size - start < 16 ? l->size - start : 16;
    char expected[64], actual[64], from[32] = "";
    spaced(expected, sizeof(expected), l->off + start, shown);
    spaced(actual, sizeof(actual), found + start, shown);
    if (start) snprintf(from, sizeof(from), " (from byte %u)", start);
    snprintf(out, size, "%s: 0x%llX%s: expected %s (%s), found %s; different game version, or patched by the runtime",
             c->name, (unsigned long long)l->offset, from, expected, label, actual);
}
static int64_t hook_target(const CheatLine *l) {
    if (l->size < HOOK_MIN || l->size > HOOK_MAX || l->on[0] != 0xe9) return -1;
    for (uint32_t i = 5; i < l->size; ++i) if (l->on[i] != 0x90) return -1;
    int32_t rel;
    memcpy(&rel, l->on + 1, 4);
    return (int64_t)l->offset + 5 + rel;
}
static int in_cave(const Cheat *c, int64_t target) {
    for (uint32_t i = c->first; i < c->first + c->count; ++i)
        if (!lines[i].hook && target >= (int64_t)lines[i].offset && target < (int64_t)(lines[i].offset + lines[i].size)) return 1;
    return 0;
}
/* The structure scripts/cheats.py checks, again here: hooks into own caves, every cave reached. */
static void classify(Cheat *c, const CheatRange *exec, size_t exec_count, CheatReserved reserved) {
    if(c->action)return;
    for(uint32_t i=c->first;i<c->first+c->count;++i)
        for(uint32_t j=i+1;j<c->first+c->count;++j)
            if(lines[i].offset < lines[j].offset+lines[j].size && lines[j].offset < lines[i].offset+lines[i].size)
                snprintf(c->problem,PROBLEM_BYTES,"%s: overlapping lines",c->name);
    for (uint32_t i = c->first; i < c->first + c->count; ++i) lines[i].hook = hook_target(&lines[i]) >= 0;
    int hooks = 0;
    for (uint32_t i = c->first; i < c->first + c->count && !c->problem[0]; ++i) {
        CheatLine *l = &lines[i];
        int inside = 0;
        for (size_t r = 0; r < exec_count; ++r)
            if (l->offset >= exec[r].start && l->offset + l->size <= exec[r].end) inside = 1;
        char why[128] = "";
        if (!inside)
            snprintf(c->problem, PROBLEM_BYTES, "%s: 0x%llX: outside the eboot's executable segment", c->name, (unsigned long long)l->offset);
        else if ((l->offset & 4095u)+l->size > 4096u)
            snprintf(c->problem,PROBLEM_BYTES,"%s: cross-page patch refused",c->name);
        else if (reserved && reserved(l->offset, l->size, why, sizeof(why)))
            snprintf(c->problem, PROBLEM_BYTES, "%s: 0x%llX: overlaps %s", c->name, (unsigned long long)l->offset, why);
        else if (l->hook) {
            ++hooks;
            if (!in_cave(c, hook_target(l)))
                snprintf(c->problem, PROBLEM_BYTES, "%s: hook 0x%llX jumps outside its own cave lines", c->name, (unsigned long long)l->offset);
            else if (l->off[0] == 0xcc)
                snprintf(c->problem, PROBLEM_BYTES, "%s: hook 0x%llX replaces an int3", c->name, (unsigned long long)l->offset);
        } else {
            int reached = 0;
            for (uint32_t h = c->first; h < c->first + c->count; ++h) {
                int64_t t = lines[h].hook ? hook_target(&lines[h]) : -1;
                if (t >= (int64_t)l->offset && t < (int64_t)(l->offset + l->size)) reached = 1;
            }
            if (!reached)
                snprintf(c->problem, PROBLEM_BYTES, "%s: line at 0x%llX is not a jmp hook or a hook target; unsupported in phase 1",
                         c->name, (unsigned long long)l->offset);
        }
    }
    if (!c->problem[0] && !hooks) snprintf(c->problem, PROBLEM_BYTES, "%s: no jmp hook line", c->name);
    if (!c->problem[0] && c->choice_count) {
        /* The slot lies in one of its own caves; ValueOn holds the first choice from now on. */
        uint32_t found = UINT32_MAX;
        for (uint32_t i = c->first; i < c->first + c->count; ++i)
            if (!lines[i].hook && c->value_offset >= lines[i].offset && c->value_offset + 4 <= lines[i].offset + lines[i].size)
                found = i;
        if (found == UINT32_MAX || c->value_offset % 4)
            snprintf(c->problem, PROBLEM_BYTES, "%s: value slot 0x%llX is not an aligned f32 inside its own cave lines",
                     c->name, (unsigned long long)c->value_offset);
        else {
            c->value_line = found;
            memcpy(lines[found].on + (c->value_offset - lines[found].offset), &c->choices[0], 4);
        }
    }
    for (uint32_t i = c->first; i < c->first + c->count && !c->problem[0]; ++i)
        if (memcmp(base + lines[i].offset, lines[i].off, lines[i].size))
            mismatch(c->problem, PROBLEM_BYTES, c, &lines[i], "ValueOff");
}
static int overlap(uint32_t a, uint32_t b) {
    return lines[a].offset < lines[b].offset + lines[b].size && lines[b].offset < lines[a].offset + lines[a].size;
}

#include "runtime_cheat_native.inc"
#ifdef SO5_CHEATS_TEST
// Test-only opt-in for the synthetic hook/rollback fixtures. Never built into player binaries.
static int allow_test_definitions;
void runtime_cheats_test_allow_synthetic(int enabled) { allow_test_definitions=enabled; }
#endif
static int verified_native_definition(const Cheat *c) {
#ifdef SO5_CHEATS_TEST
    if(allow_test_definitions)return 1;
#endif
    if(!c->builtin||!c->meta_seen||c->action||!c->count)return 0;
    const char *expected=NULL;
    for(size_t i=0;i<sizeof(native_fingerprints)/sizeof(native_fingerprints[0]);++i)
        if(native_fingerprints[i].id==c->catalog_id)expected=native_fingerprints[i].hash;
    if(!expected)return 0;
    size_t size=10+4+8+4+(size_t)c->choice_count*4+4;
    for(uint32_t i=c->first;i<c->first+c->count;++i)size+=12+(size_t)lines[i].size*2;
    unsigned char *buffer=malloc(size);
    if(!buffer)return 0;
    unsigned char *at=buffer;
#define NATIVE_FIELD(value) do { memcpy(at,&(value),sizeof(value));at+=sizeof(value); } while(0)
    memcpy(at,"SO5NATIVE1",10);at+=10;
    uint32_t id=(uint32_t)c->catalog_id,choices=(uint32_t)c->choice_count;
    NATIVE_FIELD(id);NATIVE_FIELD(c->value_offset);NATIVE_FIELD(choices);
    for(int i=0;i<c->choice_count;++i)NATIVE_FIELD(c->choices[i]);
    NATIVE_FIELD(c->count);
    for(uint32_t i=c->first;i<c->first+c->count;++i) {
        const CheatLine *line=&lines[i];
        NATIVE_FIELD(line->offset);NATIVE_FIELD(line->size);
        memcpy(at,line->on,line->size);at+=line->size;
        memcpy(at,line->off,line->size);at+=line->size;
    }
#undef NATIVE_FIELD
    char digest[65];
    const int valid=so5_image_sha256(buffer,size,digest)&&!strcmp(digest,expected);
    free(buffer);return valid;
}

int runtime_cheats_load(const char *path, unsigned char *image, const CheatRange *exec, size_t exec_count,
                        CheatReserved reserved) {
    FILE *f = fopen(path, "rb");
    size_t capacity = 4 * MAX_LINE_BYTES + 256;
    char *text = malloc(capacity);
    const char *error = NULL;
    base = image;
    if (!f || !text) error = "cannot read the file";
    else if (!fgets(text, (int)capacity, f) || strncmp(text, "PS4RTCH1", 8) || strspn(text + 8, "\r\n") != strlen(text + 8))
        error = "not a PS4RTCH1 table";
    Cheat *c = NULL;
    uint32_t expected = 0;
    while (!error && fgets(text, (int)capacity, f)) {
        size_t length = strlen(text);
        if (!length || text[length - 1] != '\n') { error = "line too long or unterminated"; break; }
        text[--length] = 0;
        if (length && text[length - 1] == '\r') text[--length] = 0;   /* written on Windows */
        if(!strncmp(text,"action ",7)) {
            unsigned long long at; unsigned amount,limit; char hash[65],name[64]; int consumed=0;
            if(expected||cheat_total==MAX_CHEATS||sscanf(text+7,"%llx %u %u %64s %63[^\n]%n",&at,&amount,&limit,hash,name,&consumed)!=5||!consumed) {error="invalid action";break;}
            int inside=0;
            for(size_t i=0;i<data_count;++i)if(at>=data_ranges[i].start&&at<=data_ranges[i].end&&data_ranges[i].end-at>=4)inside=1;
            if(!inside||at%4||!amount||!limit||amount>limit||strlen(hash)!=64||!bound_hash[0]||strcmp(hash,bound_hash)) {error="action image identity or writable range mismatch";break;}
            c=&cheats[cheat_total++];memset(c,0,sizeof(*c));c->action=1;c->action_offset=at;c->add=amount;c->limit=limit;
            snprintf(c->name,NAME_BYTES,"%s",name);
        } else if (!strncmp(text,"meta ",5)) {
            int id,origin,used=0;
            if(!c||expected||c->meta_seen||sscanf(text+5,"%d %d%n",&id,&origin,&used)!=2||text[5+used]||id<0||id>9||(origin!=0&&origin!=1)||(!origin&&id)) {error="invalid cheat metadata";break;}
            c->catalog_id=id;c->builtin=origin;c->meta_seen=1;
        } else if (!strncmp(text,"detail ",7)) {
            long n=parse_hex(text+7,strlen(text+7),NULL);
            if(!c||expected||c->description[0]||n<1||n>=384) {error="invalid cheat description";break;}
            parse_hex(text+7,strlen(text+7),(unsigned char *)c->description);
            for(long i=0;i<n;++i)if((unsigned char)c->description[i]<32){error="control character in cheat description";break;}
            c->description[n]=0;
        } else if (!strncmp(text, "problem ", 8)) {
            if (file_problem_total < MAX_FILE_PROBLEMS)
                snprintf(file_problems[file_problem_total++], PROBLEM_BYTES, "%s", text + 8);
        } else if (!strncmp(text, "cheat ", 6)) {
            char *end;
            unsigned long count = strtoul(text + 6, &end, 10);
            if (c && expected) { error = "a cheat has fewer lines than it declares"; break; }
            if (cheat_total == MAX_CHEATS || *end != ' ' || !count || count > MAX_LINES - line_total) { error = "too many cheats or lines"; break; }
            c = &cheats[cheat_total++];
            memset(c,0,sizeof(*c));
            snprintf(c->name, NAME_BYTES, "%s", end + 1);
            c->first = line_total;
            c->count = 0;
            c->choice_count = 0;
            expected = (uint32_t)count;
        } else if (!strncmp(text, "value ", 6)) {
            /* value <slot offset hex> f32 <choice>,<choice>,... after its cheat's lines */
            if (!c || expected || c->choice_count) { error = "a value record outside a cheat"; break; }
            char *end;
            unsigned long long offset = strtoull(text + 6, &end, 16);
            if (end == text + 6 || strncmp(end, " f32 ", 5)) { error = "bad value record"; break; }
            for (char *at = end + 5; !error && *at; at = end + (*end == ',')) {
                float choice = strtof(at, &end);
                /* Positive and finite (NaN fails both comparisons). */
                if (end == at || (*end && *end != ',') || !(choice > 0 && choice <= 1e6f) || c->choice_count == MAX_CHOICES)
                    error = "bad value choices";
                else c->choices[c->choice_count++] = choice;
            }
            if (!error && !c->choice_count) error = "bad value choices";
            c->value_offset = offset;
        } else if (!strncmp(text, "line ", 5)) {
            if (!c || !expected) { error = "a line outside a cheat"; break; }
            char *end, *on = NULL, *off = NULL;
            unsigned long long offset = strtoull(text + 5, &end, 16);
            if (*end == ' ') { on = end + 1; off = strchr(on, ' '); }
            if (!off || end == text + 5) { error = "bad line"; break; }
            *off++ = 0;
            long on_size = parse_hex(on, strlen(on), NULL), off_size = parse_hex(off, strlen(off), NULL);
            if (on_size < 0 || on_size != off_size || offset > UINT32_MAX * 16ull) { error = "bad line bytes"; break; }
            CheatLine *l = &lines[line_total++];
            l->offset = offset;
            l->size = (uint32_t)on_size;
            l->on = malloc((size_t)on_size * 2);
            if (!l->on) { error = "out of memory"; break; }
            l->off = l->on + on_size;
            parse_hex(on, strlen(on), l->on);
            parse_hex(off, strlen(off), l->off);
            ++c->count;
            --expected;
        } else { error = "unknown record"; break; }
    }
    if (!error && expected) error = "a cheat has fewer lines than it declares";
    if(!error)for(int i=0;i<cheat_total;++i)
        if(!verified_native_definition(&cheats[i])) {error="external, retired or modified cheat definition refused; only built-in definitions are supported";break;}
    if (f) fclose(f);
    free(text);
    if (error) {
        fprintf(stderr, "Cheats: %s: %s\n", path, error);
        cheat_total = 0;
        return -1;
    }
    for (int i = 0; i < cheat_total; ++i) classify(&cheats[i], exec, exec_count, reserved);
    /* scripts/cheats.py refuses overlapping cheats; a hand-made table gets the same answer. */
    for (int i = 0; i < cheat_total; ++i)
        for (int j = i + 1; j < cheat_total; ++j)
            for (uint32_t a = cheats[i].first; a < cheats[i].first + cheats[i].count; ++a)
                for (uint32_t b = cheats[j].first; b < cheats[j].first + cheats[j].count; ++b)
                    if (overlap(a, b)) {
                        snprintf(cheats[i].problem, PROBLEM_BYTES, "%s: overlaps %s at 0x%llX", cheats[i].name,
                                 cheats[j].name, (unsigned long long)lines[b].offset);
                        snprintf(cheats[j].problem, PROBLEM_BYTES, "%s: overlaps %s at 0x%llX", cheats[j].name,
                                 cheats[i].name, (unsigned long long)lines[a].offset);
                    }
    int available = 0;
    for (int i = 0; i < cheat_total; ++i) {
        if (cheats[i].problem[0]) printf("Cheats: unavailable: %s\n", cheats[i].problem);
        else ++available;
    }
    for (int i = 0; i < file_problem_total; ++i) printf("Cheats: refused by the launcher: %s\n", file_problems[i]);
    printf("Cheats: %d loaded, %d available\n", cheat_total, available);
    return 0;
}

/* ---- live writes ---- */

/* Every core sees the new bytes before the next step (cross-modifying code). */
static void serialize(unsigned char *at, size_t size) {
    FlushInstructionCache(GetCurrentProcess(), at, size);
    FlushProcessWriteBuffers();
}
/* Makes [at, at+size) writable (1) or executable read-only again (0). */
static DWORD saved_protection;
static int writable(unsigned char *at, size_t size, int write) {
    DWORD old;
    if (!VirtualProtect(at,size,write ? PAGE_EXECUTE_READWRITE : saved_protection,&old)) return 0;
    if(write) saved_protection=old;
    return 1;
}
/* A cave is unreachable until its hook is written: a plain copy. A hook replaces one live
 * instruction: int3 first, then the tail, then the first byte (threads meeting the int3 wait). */
#ifdef SO5_CHEATS_TEST
static int fail_after=-1;
void runtime_cheats_test_fail_after(int writes){fail_after=writes;}
#endif
static int write_line(const CheatLine *l, const unsigned char *bytes) {
#ifdef SO5_CHEATS_TEST
    if(fail_after==0){fail_after=-1;return 0;}if(fail_after>0)--fail_after;
#endif
    unsigned char *at = base + l->offset;
    if (!memcmp(at, bytes, l->size)) return 1;
    if (!writable(at, l->size, 1)) return 0;
    if (!l->hook) {
        memcpy(at, bytes, l->size);
        serialize(at, l->size);
    } else {
        atomic_store(&patching, (uintptr_t)at);
        atomic_store_explicit((_Atomic unsigned char *)at, 0xcc, memory_order_seq_cst);
        serialize(at, 1);
        memcpy(at + 1, bytes + 1, l->size - 1);
        serialize(at, l->size);
        atomic_store_explicit((_Atomic unsigned char *)at, bytes[0], memory_order_seq_cst);
        serialize(at, l->size);
        atomic_store(&patching, 0);
    }
    return writable(at, l->size, 0);
}

int runtime_cheat_trap(uintptr_t address) {
    uintptr_t site = atomic_load(&patching);
    int hook = site == address;
    /* The write may have finished between the int3 and this handler. */
    for (uint32_t i = 0; i < line_total && !hook; ++i)
        hook = lines[i].hook && (uintptr_t)(base + lines[i].offset) == address;
    if (!hook) return 0;
    while (atomic_load(&patching) == address)
        SwitchToThread();
    return *(volatile unsigned char *)address != 0xcc;
}

/* Still ValueOff or ValueOn everywhere: nothing else changed these bytes since load. */
static int verify(Cheat *c) {
    if (c->problem[0]) return -1;
    for (uint32_t i = c->first; i < c->first + c->count; ++i) {
        const unsigned char *at = base + lines[i].offset;
        if (memcmp(at, lines[i].off, lines[i].size) && memcmp(at, lines[i].on, lines[i].size)) {
            mismatch(c->problem, PROBLEM_BYTES, c, &lines[i], "ValueOff or ValueOn");
            return -1;
        }
    }
    return 0;
}
/* A valued cheat's slot becomes `value` in ValueOn and, when its cave is written, in the live
 * code too (one aligned 4-byte store: the cave's code reads either value whole). */
static int store_value(Cheat *c, float value) {
    CheatLine *l = &lines[c->value_line];
    unsigned char *live = base + c->value_offset;
    const int written = !memcmp(base + l->offset, l->on, l->size);
    if (!written) { /* still ValueOff: enabling writes the whole cave */
        memcpy(l->on + (c->value_offset - l->offset), &value, 4);
        return 0;
    }
    uint32_t bits;
    memcpy(&bits, &value, 4);
    if (!writable(live, 4, 1)) return -1;
    const uint32_t previous=atomic_load_explicit((_Atomic uint32_t *)live,memory_order_seq_cst);
    atomic_store_explicit((_Atomic uint32_t *)live, bits, memory_order_seq_cst);
    serialize(live, 4);
    if (!writable(live, 4, 0)) {
        atomic_store_explicit((_Atomic uint32_t *)live,previous,memory_order_seq_cst);
        serialize(live,4);
        if(!writable(live,4,0)) {
            fputs("FATAL: cheat value page protection could not be restored\n",stderr);
            fflush(NULL);ExitProcess(25);
        }
        return -1;
    }
    memcpy(l->on + (c->value_offset - l->offset), &value, 4);
    return 0;
}
static float slot_value(const Cheat *c) {
    float value;
    memcpy(&value, lines[c->value_line].on + (c->value_offset - lines[c->value_line].offset), 4);
    return value;
}
/* runtime_cheat_set under the lock: 0, or -1 with c->problem set. */
static int set_locked(Cheat *c, int on) {
    if(c->action){snprintf(c->problem,PROBLEM_BYTES,"%s: use the Add action",c->name);return -1;}
    int result=verify(c);
    if(result) return result;
    unsigned char previous[MAX_LINES]={0};
    for(uint32_t i=0;i<c->count;++i) {
        CheatLine *l=&lines[c->first+i];
        previous[i]=!memcmp(base+l->offset,l->on,l->size);
    }
    if(on) {
        for(uint32_t i=c->first;i<c->first+c->count && !result;++i)
            if(!lines[i].hook && !write_line(&lines[i],lines[i].on)) result=-1;
        for(uint32_t i=c->first;i<c->first+c->count && !result;++i)
            if(lines[i].hook && !write_line(&lines[i],lines[i].on)) result=-1;
    } else {
        for(uint32_t i=c->first+c->count;i-- > c->first && !result;)
            if(lines[i].hook && !write_line(&lines[i],lines[i].off)) result=-1;
    }
    if(result) {
        int restored=1;
        for(uint32_t i=c->count;i-- > 0;)
            if(lines[c->first+i].hook && !write_line(&lines[c->first+i],previous[i]?lines[c->first+i].on:lines[c->first+i].off)) restored=0;
        if(!restored) {
            fputs("FATAL: cheat hook rollback failed; refusing to continue with partially patched code\n",stderr);
            fflush(NULL); ExitProcess(25);
        }
        snprintf(c->problem,PROBLEM_BYTES,"%s: code update refused; hooks restored",c->name);
    }
    return result;
}
static int finish(const Cheat *c, int result, int on, char *message, size_t size) {
    if (result) snprintf(message, size, "%s", c->problem);
    else if (on && c->choice_count) snprintf(message, size, "%s: %gx", c->name, (double)slot_value(c));
    else snprintf(message, size, "%s: %s", c->name, on ? "on" : "off");
    unlock_cheats();
    printf("Cheats: %s\n", message);
    fflush(stdout);
    return result;
}

int runtime_cheat_set(int index, int on, char *message, size_t size) {
    if (index < 0 || index >= cheat_total) { snprintf(message, size, "No such cheat"); return -1; }
    lock_cheats();
    Cheat *c = &cheats[index];
    return finish(c, set_locked(c, on), on, message, size);
}

int runtime_cheat_select(int index, int choice, char *message, size_t size) {
    if (choice < 0) return runtime_cheat_set(index, 0, message, size);
    if (index < 0 || index >= cheat_total || choice >= cheats[index].choice_count) {
        snprintf(message, size, "No such cheat value");
        return -1;
    }
    lock_cheats();
    Cheat *c = &cheats[index];
    int result = verify(c);
    if (!result && store_value(c, c->choices[choice])) {
        snprintf(c->problem, PROBLEM_BYTES, "%s: cannot change the code's page protection", c->name);
        result = -1;
    }
    if (!result) result = set_locked(c, 1);
    return finish(c, result, 1, message, size);
}

int runtime_cheat_count(void) { return cheat_total; }
const char *runtime_cheat_name(int index) { return index >= 0 && index < cheat_total ? cheats[index].name : ""; }
int runtime_cheat_catalog_id(int index){return index>=0&&index<cheat_total?cheats[index].catalog_id:0;}
int runtime_cheat_builtin(int index){return index>=0&&index<cheat_total&&cheats[index].builtin;}
const char *runtime_cheat_description(int index){return index>=0&&index<cheat_total?cheats[index].description:"";}
const char *runtime_cheat_problem(int index) {
    return index >= 0 && index < cheat_total && cheats[index].problem[0] ? cheats[index].problem : NULL;
}
int runtime_cheat_state(int index) {
    if (index < 0 || index >= cheat_total || cheats[index].problem[0]) return RUNTIME_CHEAT_UNAVAILABLE;
    const Cheat *c = &cheats[index];
    for (uint32_t i = c->first; i < c->first + c->count; ++i)
        if (lines[i].hook && memcmp(base + lines[i].offset, lines[i].on, lines[i].size)) return RUNTIME_CHEAT_OFF;
    return RUNTIME_CHEAT_ON;
}
int runtime_cheat_choice_count(int index) {
    return index >= 0 && index < cheat_total && !cheats[index].problem[0] ? cheats[index].choice_count : 0;
}
float runtime_cheat_choice_value(int index, int choice) {
    return choice >= 0 && choice < runtime_cheat_choice_count(index) ? cheats[index].choices[choice] : 0;
}
int runtime_cheat_choice(int index) {
    if (!runtime_cheat_choice_count(index) || runtime_cheat_state(index) != RUNTIME_CHEAT_ON) return -1;
    lock_cheats();
    const float value = slot_value(&cheats[index]);
    unlock_cheats();
    for (int i = 0; i < cheats[index].choice_count; ++i)
        if (cheats[index].choices[i] == value) return i;
    return -1;
}
int runtime_cheat_file_problem_count(void) { return file_problem_total; }
const char *runtime_cheat_file_problem(int index) {
    return index >= 0 && index < file_problem_total ? file_problems[index] : "";
}

int runtime_cheat_is_action(int index) {return index>=0 && index<cheat_total && cheats[index].action;}
int runtime_cheat_add(int index,char *message,size_t size) {
    lock_cheats();
    if(!runtime_cheat_is_action(index)){snprintf(message,size,"Invalid one-shot action");unlock_cheats();return -1;}
    Cheat *c=&cheats[index];
    volatile LONG *address=(volatile LONG *)(base+c->action_offset);
    uint32_t current; SIZE_T got;
    if(!ReadProcessMemory(GetCurrentProcess(),(const void *)address,&current,4,&got)||got!=4||current>c->limit) {
        snprintf(message,size,"%s: current value is outside the verified profile limit",c->name);unlock_cheats();return -1;
    }
    for(int attempt=0;attempt<64;++attempt) {
        if(current>c->limit)break;
        uint32_t next=c->add>c->limit-current?c->limit:current+c->add;
        uint32_t previous=(uint32_t)InterlockedCompareExchange(address,(LONG)next,(LONG)current);
        if(previous==current) {
            snprintf(message,size,"%s: %u -> %u",c->name,current,next);unlock_cheats();printf("Cheats: %s\n",message);return 0;
        }
        current=previous;
    }
    snprintf(message,size,"%s: concurrent change; action not applied",c->name);unlock_cheats();return -1;
}
