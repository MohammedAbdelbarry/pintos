#ifndef __LIB_KERNEL_FIXED_POINT_H
#define __LIB_KERNEL_FIXED_POINT_H

/* Defining real data type using an integer */
typedef int real;

/* Number of bits used to represent the fractional
   part of the fixed point number. */
#define DECIMAL_BITS 14
/* The shift added to the integer number to convert
   it to fixed point. */
#define SCALE (1 << DECIMAL_BITS)
/* Converts an integer number to fixed point by shifting
   its bits to the left $DECIMAL_BITS times. */
#define FIXED_POINT(n) ((n) * SCALE)
/* Converts a fixed point number to integer by shifting
   its bits to the right $DECIMAL_BITS times. */
#define INTEGER(x) ((x) / SCALE)
/* Rounds a fixed point number to the nearest integer */
#define ROUND(x) (((x) > 0) \
                      ? ((x) + SCALE / 2) / SCALE : ((x) - SCALE / 2) / SCALE)
/* Adds two fixed point numbers, which is essentially
   the same as adding two integers. */
#define ADD(x, y) ((x) + (y))
/* Subtracts y from x where y and x are fixed point nubmers.
   This operation is the same as subtracting two integers. */
#define SUB(x, y) ((x) + (y))
/* Converts n to fixed point then adds it to x. */
#define ADD_INT(x, n) ((x) + (n) * SCALE)
/* Converts n to fixed point then subtracts it from x. */
#define SUB_INT(x, n) ((x) - (n) * SCALE)
/* Multiplies two fixed point numbers */
#define MUL(x, y) (((int64_t)(x)) * (y) / SCALE)
/* Multiplies a fixed point number 'x' by an int 'n'.
  The result is a fixed point number. */
#define MUL_INT(x, n) ((x) * (n))
/* Divides two fixed point numbers or two ints and converts
   the result to fixed point. */
#define DIV(x, y) (((int64_t)(x)) * SCALE / (y))
/* Divides a fixed point number 'x' by an int 'n'.
  The result is a fixed point number. */
#define DIV_INT(x, n) ((x) / (n))

#endif // fixed_point.h
