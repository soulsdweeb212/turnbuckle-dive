/* The byte-order check (src/dev/order_check.c), for the port's own code. C only. */
#ifndef TD_ORDER_CHECK_H
#define TD_ORDER_CHECK_H

#include <stddef.h>

#ifdef TD_ORDER_CHECK
/* The port has just turned the width-byte value at p into the PC's byte order (a resource, a game variable from the
 * ROM): the check then knows the value's width, as if game code had written it. */
void order_check_value(const void *p, unsigned width);
#define ORDER_CHECK_VALUE(p, width) order_check_value((p), (width))
/* The n bytes at p are new memory (a heap block): the check forgets what it knew of the bytes there before. */
void order_check_forget(const void *p, size_t n);
#define ORDER_CHECK_FORGET(p, n) order_check_forget((p), (n))
#else
#define ORDER_CHECK_VALUE(p, width) ((void)0)
#define ORDER_CHECK_FORGET(p, n) ((void)0)
#endif

#endif
