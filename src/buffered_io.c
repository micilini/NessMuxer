

#include "buffered_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NIO_BUFFER_SIZE (256 * 1024)

struct NessIO {
    FILE* fp;
    int   error;
};

NessIO* nio_open(const char* path)
{
    NessIO* io;
    FILE* fp;

    if (!path)
        return NULL;

    fp = fopen(path, "wb");
    if (!fp)
        return NULL;

    io = (NessIO*)calloc(1, sizeof(NessIO));
    if (!io) {
        fclose(fp);
        return NULL;
    }

   
    setvbuf(fp, NULL, _IOFBF, NIO_BUFFER_SIZE);
    io->fp = fp;
    io->error = 0;
    return io;
}

NessIO* nio_open_read(const char* path)
{
    NessIO* io;
    FILE* fp;

    if (!path)
        return NULL;

    fp = fopen(path, "rb");
    if (!fp)
        return NULL;

    io = (NessIO*)calloc(1, sizeof(NessIO));
    if (!io) {
        fclose(fp);
        return NULL;
    }

    setvbuf(fp, NULL, _IOFBF, NIO_BUFFER_SIZE);
    io->fp = fp;
    io->error = 0;
    return io;
}

int nio_write(NessIO* io, const void* buf, size_t size)
{
    if (!io || !io->fp || io->error)
        return -1;
    if (size == 0)
        return 0;
    if (fwrite(buf, 1, size, io->fp) != size) {
        io->error = 1;
        return -1;
    }
    return 0;
}

int nio_read(NessIO* io, void* buf, size_t size)
{
    if (!io || !io->fp || io->error)
        return -1;
    if (size == 0)
        return 0;
    if (fread(buf, 1, size, io->fp) != size) {
        io->error = 1;
        return -1;
    }
    return 0;
}

int nio_r8(NessIO* io)
{
    int c;

    if (!io || !io->fp || io->error)
        return -1;

    c = fgetc(io->fp);
    if (c == EOF) {
        io->error = 1;
        return -1;
    }

    return c & 0xFF;
}

int nio_rb16(NessIO* io)
{
    int b0 = nio_r8(io);
    int b1 = nio_r8(io);

    if (b0 < 0 || b1 < 0)
        return -1;

    return (b0 << 8) | b1;
}

int nio_rb32(NessIO* io)
{
    int b0 = nio_r8(io);
    int b1 = nio_r8(io);
    int b2 = nio_r8(io);
    int b3 = nio_r8(io);

    if (b0 < 0 || b1 < 0 || b2 < 0 || b3 < 0)
        return -1;

    return ((b0 & 0xFF) << 24) |
           ((b1 & 0xFF) << 16) |
           ((b2 & 0xFF) << 8)  |
           (b3 & 0xFF);
}

int64_t nio_rb64(NessIO* io)
{
    int b0 = nio_r8(io);
    int b1 = nio_r8(io);
    int b2 = nio_r8(io);
    int b3 = nio_r8(io);
    int b4 = nio_r8(io);
    int b5 = nio_r8(io);
    int b6 = nio_r8(io);
    int b7 = nio_r8(io);

    if (b0 < 0 || b1 < 0 || b2 < 0 || b3 < 0 ||
        b4 < 0 || b5 < 0 || b6 < 0 || b7 < 0)
        return -1;

    return ((int64_t)(b0 & 0xFF) << 56) |
           ((int64_t)(b1 & 0xFF) << 48) |
           ((int64_t)(b2 & 0xFF) << 40) |
           ((int64_t)(b3 & 0xFF) << 32) |
           ((int64_t)(b4 & 0xFF) << 24) |
           ((int64_t)(b5 & 0xFF) << 16) |
           ((int64_t)(b6 & 0xFF) << 8)  |
           ((int64_t)(b7 & 0xFF));
}

int nio_eof(NessIO* io)
{
    if (!io || !io->fp)
        return 1;
    return feof(io->fp) ? 1 : 0;
}

int nio_w8(NessIO* io, uint8_t val)
{
    if (!io || !io->fp || io->error)
        return -1;
    if (fputc(val, io->fp) == EOF) {
        io->error = 1;
        return -1;
    }
    return 0;
}

int nio_wb16(NessIO* io, uint16_t val)
{
    nio_w8(io, (uint8_t)(val >> 8));
    nio_w8(io, (uint8_t)(val));
    return io->error ? -1 : 0;
}

int nio_wb24(NessIO* io, uint32_t val)
{
    nio_w8(io, (uint8_t)(val >> 16));
    nio_w8(io, (uint8_t)(val >> 8));
    nio_w8(io, (uint8_t)(val));
    return io->error ? -1 : 0;
}

int nio_wb32(NessIO* io, uint32_t val)
{
    nio_w8(io, (uint8_t)(val >> 24));
    nio_w8(io, (uint8_t)(val >> 16));
    nio_w8(io, (uint8_t)(val >> 8));
    nio_w8(io, (uint8_t)(val));
    return io->error ? -1 : 0;
}

int nio_wb64(NessIO* io, uint64_t val)
{
    nio_w8(io, (uint8_t)(val >> 56));
    nio_w8(io, (uint8_t)(val >> 48));
    nio_w8(io, (uint8_t)(val >> 40));
    nio_w8(io, (uint8_t)(val >> 32));
    nio_w8(io, (uint8_t)(val >> 24));
    nio_w8(io, (uint8_t)(val >> 16));
    nio_w8(io, (uint8_t)(val >> 8));
    nio_w8(io, (uint8_t)(val));
    return io->error ? -1 : 0;
}

int nio_fill(NessIO* io, uint8_t byte, int count)
{
    int i;
    if (!io || !io->fp || io->error)
        return -1;
    for (i = 0; i < count; i++) {
        if (fputc(byte, io->fp) == EOF) {
            io->error = 1;
            return -1;
        }
    }
    return 0;
}

int64_t nio_tell(NessIO* io)
{
    if (!io || !io->fp)
        return -1;
#ifdef _WIN32
    return (int64_t)_ftelli64(io->fp);
#else
    return (int64_t)ftello(io->fp);
#endif
}

int nio_seek(NessIO* io, int64_t pos, int whence)
{
    if (!io || !io->fp)
        return -1;
#ifdef _WIN32
    return _fseeki64(io->fp, pos, whence) == 0 ? 0 : -1;
#else
    return fseeko(io->fp, (off_t)pos, whence) == 0 ? 0 : -1;
#endif
}

int nio_flush(NessIO* io)
{
    if (!io || !io->fp)
        return -1;
    return fflush(io->fp) == 0 ? 0 : -1;
}

void nio_close(NessIO* io)
{
    if (!io)
        return;
    if (io->fp) {
        fflush(io->fp);
        fclose(io->fp);
        io->fp = NULL;
    }
    free(io);
}
