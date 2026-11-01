/* Exercise the real Windows save-data API on disposable files, never player saves. */
#include "../src/runtime_savedata.c"
#include <windows.h>
#include <assert.h>

static char fixture[700];
static char user_root[700];
static unsigned unmounts;
const char *runtime_file_user_dir(void) { return user_root; }
int runtime_file_mount(const char *guest,const char *host) { (void)guest; (void)host; return 0; }
void runtime_file_unmount(const char *guest) { (void)guest; ++unmounts; }
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name) {
    for (size_t i=0;i<count;++i) if (!strcmp(table[i].name,name)) return (uintptr_t)table[i].function;
    return 0;
}
static void environment(const char *name,const char *value) { assert(!_putenv_s(name,value)); }
static void put_file(const char *path,const char *value) {
    FILE *file=fopen(path,"wb"); assert(file);
    assert(fwrite(value,1,strlen(value),file)==strlen(value)); assert(!fclose(file));
}
static void expect_file(const char *path,const char *value) {
    char buffer[128]={0}; FILE *file=fopen(path,"rb"); assert(file);
    size_t n=fread(buffer,1,sizeof(buffer)-1,file); assert(!ferror(file)); fclose(file);
    assert(n==strlen(value) && !strcmp(buffer,value));
}
static unsigned snapshot_count(const char *base,const char *prefix,char *latest,size_t size) {
    DIR *dir=opendir(base); if (!dir) { assert(errno==ENOENT); return 0; }
    struct dirent *entry; unsigned count=0;
    while ((entry=readdir(dir))) if (!strncmp(entry->d_name,prefix,strlen(prefix))) {
        ++count; if (latest) snprintf(latest,size,"%s/%s",base,entry->d_name);
    }
    closedir(dir); return count;
}
static MountResult create_slot(int user,const char *name) {
    DirName dir={0}; snprintf(dir.data,sizeof(dir.data),"%s",name);
    Mount2 request={.user=user,.dir=&dir,.mode=MODE_CREATE2}; MountResult result;
    assert(!save_mount2(&request,&result)); return result;
}
int main(void) {
    char temporary[MAX_PATH]; assert(GetTempPathA(sizeof(temporary),temporary));
    assert(GetTempFileNameA(temporary,"s5b",0,fixture));
    assert(DeleteFileA(fixture) && CreateDirectoryA(fixture,NULL));
    snprintf(user_root,sizeof(user_root),"%s/user",fixture); assert(!make_dirs(user_root));
    environment("PS4RT_SAVEDATA_TITLE_DIR",""); environment("PS4RT_SAVEDATA_ROOT","");
    environment("PS4RT_SAVEDATA_BACKUP",""); runtime_savedata_configure("CUSA03219");
    assert(!save_initialize(NULL));
    MountResult save=create_slot(1,"SAVE01");
    char source[850],path[950],base[850],snapshot[950];
    snprintf(source,sizeof(source),"%s/savedata/1/CUSA03219/SAVE01",user_root);
    snprintf(path,sizeof(path),"%s/save.bin",source); put_file(path,"first save");
    snprintf(path,sizeof(path),"%s/subdirectory",source); assert(!make_dirs(path));
    snprintf(path,sizeof(path),"%s/subdirectory/state.bin",source); put_file(path,"nested file");
    snprintf(base,sizeof(base),"%s/savedata-backups/1/CUSA03219/SAVE01",user_root);
    assert(!save_umount_backup(&save.point));
    assert(snapshot_count(base,"backup-",snapshot,sizeof(snapshot))==1);
    snprintf(path,sizeof(path),"%s/data/save.bin",snapshot); expect_file(path,"first save");
    snprintf(path,sizeof(path),"%s/data/subdirectory/state.bin",snapshot); expect_file(path,"nested file");
    snprintf(path,sizeof(path),"%s/sce_sys/param.bin",snapshot); assert(!_access(path,0));
    assert(backup_progress==1 && !slots[0].used);

    /* Rapid saves keep distinct complete snapshots without overwriting old ones. */
    save=create_slot(1,"SAVE01"); assert(!save_umount_backup(&save.point));
    assert(snapshot_count(base,"backup-",NULL,0)==2);

    /* The pre-launch path uses the same transaction/layout without mounting,
     * unmounting or changing the current live save. */
    unsigned launch_unmounts=unmounts;
    assert(!runtime_savedata_backup_at_launch());
    assert(unmounts==launch_unmounts && snapshot_count(base,"backup-",NULL,0)==3);
    assert(!runtime_savedata_backup_at_launch());
    assert(snapshot_count(base,"backup-",NULL,0)==4);
    environment("PS4RT_SAVEDATA_BACKUP","0");
    assert(!runtime_savedata_backup_at_launch() && snapshot_count(base,"backup-",NULL,0)==4);
    environment("PS4RT_SAVEDATA_BACKUP","1");

    /* Disabled backups still unmount successfully; the live save is preserved. */
    environment("PS4RT_SAVEDATA_BACKUP","0"); save=create_slot(1,"SAVE01");
    unsigned previous=unmounts; assert(!save_umount_backup(&save.point));
    assert(unmounts==previous+1 && !slots[0].used && backup_progress==1);
    assert(snapshot_count(base,"backup-",NULL,0)==4);
    snprintf(path,sizeof(path),"%s/save.bin",source); expect_file(path,"first save");
    assert(save_umount_backup(&save.point)==ERR_NOT_FOUND);
    assert(save_umount_backup(NULL)==ERR_PARAMETER);

    /* Copy failure cannot commit a partial backup, delete old backups or unmount the source. */
    environment("PS4RT_SAVEDATA_BACKUP","1"); save=create_slot(1,"SAVE01");
    HANDLE locked=CreateFileA(path,GENERIC_READ,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    assert(locked!=INVALID_HANDLE_VALUE);
    assert(save_umount_backup(&save.point)==ERR_INTERNAL);
    assert(slots[0].used && backup_progress==0 && snapshot_count(base,"backup-",NULL,0)==4);
    assert(snapshot_count(base,"incomplete-",NULL,0)==1); CloseHandle(locked);
    assert(!save_umount_backup(&save.point)); assert(snapshot_count(base,"backup-",NULL,0)==5);
    expect_file(path,"first save");

    /* A custom title directory uses its actual files and retains the title/user namespace. */
    char custom[800]; snprintf(custom,sizeof(custom),"%s/custom saves",fixture);
    environment("PS4RT_SAVEDATA_TITLE_DIR",custom); runtime_savedata_configure("CUSA99999");
    save=create_slot(2,"SAVE02");
    snprintf(path,sizeof(path),"%s/SAVE02/save.bin",custom); put_file(path,"custom edition");
    snprintf(base,sizeof(base),"%s/savedata-backups/2/CUSA99999/SAVE02",user_root);
    assert(!save_umount_backup(&save.point));
    assert(snapshot_count(base,"backup-",snapshot,sizeof(snapshot))==1);
    snprintf(path,sizeof(path),"%s/data/save.bin",snapshot); expect_file(path,"custom edition");
    snprintf(path,sizeof(path),"%s/SAVE02/save.bin",custom); expect_file(path,"custom edition");

    /* Save-root overrides and saves lacking optional metadata also remain usable. */
    environment("PS4RT_SAVEDATA_TITLE_DIR",""); environment("PS4RT_SAVEDATA_ROOT",custom);
    save=create_slot(3,"SAVE03");
    assert(!compat_remove_tree(slots[0].meta));
    assert(!save_umount_backup(&save.point));
    snprintf(base,sizeof(base),"%s/savedata-backups/3/CUSA99999/SAVE03",user_root);
    assert(snapshot_count(base,"backup-",NULL,0)==1);
    assert(!save_terminate());
    assert(save_umount_backup(&save.point)==ERR_NOT_INITIALIZED);
    /* fixture was exclusively created by this test; compat_remove_tree rejects links. */
    assert(!compat_remove_tree(fixture));
    puts("PASS: runtime save backups, switch, metadata, custom paths, title isolation and copy failure");
    return 0;
}
