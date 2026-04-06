

#ifndef NESS_BUFFERED_IO_H
#define NESS_BUFFERED_IO_H

#include <stdint.h>
#include <stddef.h>

typedef struct NessIO NessIO;


NessIO* nio_open(const char* path);


NessIO* nio_open_read(const char* path);


int nio_write(NessIO* io, const void* buf, size_t size);
int nio_w8(NessIO* io, uint8_t val);
int nio_wb16(NessIO* io, uint16_t val);
int nio_wb24(NessIO* io, uint32_t val);
int nio_wb32(NessIO* io, uint32_t val);
int nio_wb64(NessIO* io, uint64_t val);
int nio_fill(NessIO* io, uint8_t byte, int count);


int     nio_read(NessIO* io, void* buf, size_t size);
int     nio_r8(NessIO* io);
int     nio_rb16(NessIO* io);
int     nio_rb32(NessIO* io);
int64_t nio_rb64(NessIO* io);
int     nio_eof(NessIO* io);


int64_t nio_tell(NessIO* io);
int     nio_seek(NessIO* io, int64_t pos, int whence);


int  nio_flush(NessIO* io);
void nio_close(NessIO* io);

#endif
