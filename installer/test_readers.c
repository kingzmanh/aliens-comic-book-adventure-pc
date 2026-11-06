#include "setup.h"
#include <stdarg.h>
void setup_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); printf("\n"); }
int main(int argc, char **argv)
{
    if (argc > 3 && !strcmp(argv[1], "lha")) {
        size_t n; u8 *d = read_file(argv[2], &n);
        int k = lha_extract(d, n, argv[3], NULL);
        printf("lha: %d files\n", k);
        return k < 0;
    }
    if (argc > 3 && !strcmp(argv[1], "iso")) {
        CdSrc *c = cd_open(argv[2]);
        if (!c) { printf("open failed\n"); return 1; }
        printf("%s: total %llu bytes, DISK1 marker %d, DISK2 marker %d\n", cd_describe(c), (unsigned long long)cd_total_bytes(c),
               cd_exists(c, "ALIENS/DISK1/DISK1"), cd_exists(c, "ALIENS/DISK2/DISK2"));
        if (argc > 4) { size_t n; u8 *d = cd_read(c, argv[3], &n); printf("%s: %zu bytes crc %08x\n", argv[3], d ? n : 0, d ? crc32_buf(d, n) : 0); }
        return 0;
    }
    return 2;
}
