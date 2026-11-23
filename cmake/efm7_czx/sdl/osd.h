#ifndef CZX_EFM7_SDL_OSD_H
#define CZX_EFM7_SDL_OSD_H

#include <cstring>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

typedef struct bitmap_s {
    int width;
    int height;
} bitmap_t;

typedef struct font_s {
    int width;
    int height;
} font_t;

typedef struct pen_s {
    int width;
} pen_t;

class OSD {
public:
    void set_vm_node(int, const _TCHAR*) {}
    bool check_feature(const _TCHAR* key) {
#if defined(_FM77AV40)
        if (key && std::strcmp(key, "_FM77AV40") == 0) return true;
#endif
        return false;
    }
    void start_waiting_in_debugger() {}
    void process_waiting_in_debugger() {}
    void finish_waiting_in_debugger() {}
};

#endif
