/* Chromium engines require floating-point printf conversions. The SDK's
 * size-oriented stdc.static formatter intentionally omits them. Compile the
 * matching AROS formatter with full conversions into each engine module;
 * do not change the SDK default or maintain a second formatting algorithm. */
#define FULL_SPECIFIERS
#include "__vcformat.c"
