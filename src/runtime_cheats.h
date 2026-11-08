/* Built-in code cheats (PS4RTCH1 table from the native preparer), switched live from the
 * settings menu. External, retired and modified definitions are rejected by compiled
 * fingerprints. Each cheat is jmp hooks (E9 rel32 + NOPs, one instruction each) into code caves in the
 * eboot's padding. Enabling writes the caves, then the hooks; disabling restores the hooks and
 * keeps the caves (a guest thread may still run in one; only the hooks reach them). Every write
 * first checks that the bytes are still ValueOff or ValueOn. A hook is written int3-first; a
 * thread that meets the int3 waits in runtime_cheat_trap and runs the finished instruction. */
#ifndef PS4RT_RUNTIME_CHEATS_H
#define PS4RT_RUNTIME_CHEATS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint64_t start, end; } CheatRange;  /* image offsets, end exclusive */
/* 1 when [offset, offset+size) holds bytes another rewrite changed (ISA sites, --patches,
 * relocations); `why` then names it. */
typedef int (*CheatReserved)(uint64_t offset, uint64_t size, char *why, size_t why_size);
enum { RUNTIME_CHEAT_UNAVAILABLE = -1, RUNTIME_CHEAT_OFF = 0, RUNTIME_CHEAT_ON = 1 };

/* Reads the table for code at `image`; lines must lie in `exec` (executable segments, rounded to
 * pages). Cheats whose bytes are not ValueOff are loaded as unavailable. -1 and one stderr line
 * when the file is invalid (no cheats then). Call once, after the image is protected. */
void runtime_cheats_bind_image(unsigned char *image,size_t size,const CheatRange *writable,size_t count);
int runtime_cheat_is_action(int index);
int runtime_cheat_add(int index,char *message,size_t size);
int runtime_cheats_load(const char *path, unsigned char *image, const CheatRange *exec, size_t exec_count,
                        CheatReserved reserved);
/* The overlay (settings menu thread) reads and switches the cheats. */
int runtime_cheat_count(void);
const char *runtime_cheat_name(int index);
int runtime_cheat_catalog_id(int index);
int runtime_cheat_builtin(int index);
const char *runtime_cheat_description(int index);
int runtime_cheat_state(int index);
const char *runtime_cheat_problem(int index);           /* why it is unavailable, else NULL */
int runtime_cheat_file_problem_count(void);             /* refused files and cheats (launcher) */
const char *runtime_cheat_file_problem(int index);
/* 0 when the cheat is now `on`; -1 when refused (it becomes unavailable). Either way `message`
 * holds one line for the player ("Max Money: on" or the reason). */
int runtime_cheat_set(int index, int on, char *message, size_t size);
/* Valued cheats (a PS4RTCH1 `value` record, ours, not GoldHEN): one f32 slot inside the cheat's
 * own cave that its code reads, and the choices the menu offers for it ("Off / 2x / 3x / 4x").
 * The slot starts at the first choice. */
int runtime_cheat_choice_count(int index);              /* 0: an on/off cheat */
float runtime_cheat_choice_value(int index, int choice);
int runtime_cheat_choice(int index);                    /* the choice while on, else -1 */
/* Stores the choice in the slot (live, an aligned 4-byte store) and turns the cheat on; a
 * choice < 0 turns it off. Results and `message` as runtime_cheat_set ("Movement Speed: 3x"). */
int runtime_cheat_select(int index, int choice, char *message, size_t size);
/* A breakpoint at `address` (the CC byte): 1 when it is a hook being written; the thread has
 * waited for the write and resumes at `address`. 0 otherwise. Fault-handler safe. */
int runtime_cheat_trap(uintptr_t address);

#ifdef __cplusplus
}
#endif
#endif
