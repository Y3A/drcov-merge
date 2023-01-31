#include <stdio.h>
#include <Windows.h>
#include <string.h>

#define MAX_ENTRIES 128000 * 1000

typedef struct _bb_entry_t {
    DWORD start; /* offset of bb start from the image base */
    WORD size;
    WORD mod_id;
} bb_entry_t;

int main(int argc, char *argv[]) {
  bb_entry_t bb;
  BYTE   *header = NULL, *data, *ptr, text[64], unique = 0;
  DWORD   header_len, idx = 2, off, s = sizeof(bb_entry_t), start, count, added, i;
  DWORD   entries = 0;
  HANDLE  cur, curmapping, out;
  LPVOID  mapbase;
  DWORD   cursz, written;
  
  if (argc > 1 && strcmp(argv[1], "-u") == 0) {
    unique = 1;
    argv++;
    argc--;
  }
  
  bb_entry_t *bbs = VirtualAlloc(NULL, MAX_ENTRIES * sizeof(bb_entry_t), MEM_COMMIT, PAGE_READWRITE);


  if (argc < 4 || strncmp(argv[1], "-h", 2) == 0) {
    printf("Syntax: %s [-u] drcov.log drcov.1,log drcov.2.log drcov.3.log ...\n", argv[0]);
    puts("Merges all drcov logs to the first specified filename");
    puts("Option -u uniques the basic block information");
    VirtualFree(bbs, MAX_ENTRIES * sizeof(bb_entry_t), MEM_RELEASE);
    return 0;
  }

  if ((out = CreateFileA(argv[1], GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
    printf("Error creating file : 0x%08X\n", GetLastError());
    VirtualFree(bbs, MAX_ENTRIES * sizeof(bb_entry_t), MEM_RELEASE);
    return 0;
  }

  while (idx < argc) {
    if ((cur = CreateFileA(argv[idx], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL)) == INVALID_HANDLE_VALUE) {
      printf("Error opening file %s : 0x%08X\n", argv[idx], GetLastError());
      goto next;
    }

    cursz = GetFileSize(cur, NULL);
    if (!cursz) {
        printf("File %s is empty?", argv[idx]);
        CloseHandle(cur);
        goto next;
    }

    if ((curmapping = CreateFileMappingA(cur, NULL, PAGE_READONLY | SEC_COMMIT, 0, 0, "CurMap")) == NULL) {
        printf("Error creating map of file %s : 0x%08X\n", argv[idx], GetLastError());
        CloseHandle(cur);
        goto next;
    }

    if ((data= MapViewOfFile(curmapping, FILE_MAP_READ, 0, 0, 0)) == NULL) {
        printf("Error mapping file %s : 0x%08X\n", argv[idx], GetLastError());
        CloseHandle(curmapping);
        CloseHandle(cur);
        goto next;
    }

    if ((ptr = strstr(data, "BB Table: ")) == NULL) {
      printf("%s: no drcov header\n", argv[idx]);
      goto unmap; 
    }

    off = ptr - data;
    if (!header) {
      if ((header = malloc(off)) == NULL) {
        puts("Malloc fail... exiting");
        UnmapViewOfFile(data);
        CloseHandle(curmapping);
        CloseHandle(cur);
        CloseHandle(out);
        VirtualFree(bbs, MAX_ENTRIES * sizeof(bb_entry_t), MEM_RELEASE);
        return 0;
      }
      memcpy(header, data, off);
      WriteFile(out, header, off, &written, NULL);
      header_len = off;
    } else {
      if (memcmp(header, data, header_len > off ? header_len : off) != 0) {
        printf("%s: different drcov header, maybe issue?\n", argv[idx]);
        //goto unmap;
      }
    }

    while (*ptr != '\n' && ptr - data < cursz) ++ptr;
    if (*ptr != '\n')  {
      printf("%s: no drcov header\n", argv[idx]);
      goto unmap; 
    } else {
      ++ptr;
    }

    printf("Processing %s (%lu bytes) ... ", argv[idx], cursz);
    
    start = ptr - data;    
    count = 0;
    added = 0;

    while (cursz - start >= s) {
      int found = 0;
      if (unique)
        for (i = 0; i < entries && !found; ++i)
          if (memcmp(&bbs[i], data + start, s) == 0) found = 1;
      if (!found) {
        if (entries >= MAX_ENTRIES) {
          fprintf(stderr, "MapFull!\n");
        } else {
          memcpy(&bbs[entries++], data + start, s);
          ++added;
        }
      }
      start += s;
      ++count;
    }

    printf("%u entries, %u new\n", count, added);

unmap:  
    UnmapViewOfFile(data);
    CloseHandle(curmapping);
    CloseHandle(cur);

next:
    ++idx;
 }

  sprintf(text, "BB Table: %u bbs\n", entries);
  WriteFile(out, text, strlen(text), &written, NULL);
  
  printf("Writing %u entries into %s\n", entries, argv[1]);

  for (i = 0; i < entries; ++i)
    WriteFile(out, &bbs[i], s, &written, NULL);

  printf("Done.\n");

  CloseHandle(out);
  free(header);
  VirtualFree(bbs, MAX_ENTRIES * sizeof(bb_entry_t), MEM_RELEASE);
  return 0; 
}
