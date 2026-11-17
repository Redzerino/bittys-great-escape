#include "save.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
int main(void)
{
    const char *file="bitty-save-regression.tmp";
    remove(file);
    Save_Load(file);
    Save_SetCode("lesson.txt","int answer = 7;");
    Save_SetCurrent("lesson.txt");Save_SetDone("lesson.txt");
    assert(Save_Write());
    FILE *f=fopen(file,"rb");assert(f);
    char before[1024];size_t count=fread(before,1,sizeof before,f);fclose(f);
    assert(count>0);
#ifdef _WIN32
    assert(SetFileAttributesA(file,FILE_ATTRIBUTE_READONLY));
    Save_SetCode("lesson.txt","int answer = 99;");
    assert(!Save_Write());
    f=fopen(file,"rb");assert(f);char after[1024];size_t afterCount=fread(after,1,sizeof after,f);fclose(f);
    assert(count==afterCount&&!memcmp(before,after,count));
    assert(SetFileAttributesA(file,FILE_ATTRIBUTE_NORMAL));
    assert(Save_Write());
#endif
    Save_Load(file);
    assert(Save_IsDone("lesson.txt"));assert(!strcmp(Save_GetCurrent(),"lesson.txt"));
    remove(file);
    puts("PASS: saved code, current level, completion and failed replacement preserves previous save");
    return 0;
}
