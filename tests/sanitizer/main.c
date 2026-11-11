/* sanitizer_catches_overflow: proves that a sanitizer build stops a read past the end of a game variable and names the
 * line. It reads one byte just past the four wrestler records (D_800F9D10, 4 records of sizeof(Wrestler) = 0x3B8
 * bytes, stored on their own; src/game_vars.h), as a game file indexing D_800F9D10[4] would. In the asan preset
 * AddressSanitizer must report "global-buffer-overflow" with this file and line, and the test passes on that report
 * (CMakeLists.txt); a build without AddressSanitizer does not build this test. */
#include <stdint.h>
#include <stdio.h>

extern uint8_t D_800F9D10[];

int main(void) {
    volatile uint8_t *records = D_800F9D10;

    printf("read past the wrestlers: %d\n", records[4 * 0x3B8 + 8]); /* the planted read */
    return 0;
}
