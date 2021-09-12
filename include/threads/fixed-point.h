#ifndef FIXED_POINT_H
#define FIXED_POINT_H

typedef int fixed_point;

#define F   (1<<14)

#define INT_TO_FP(n)        (n*F)
#define FP_TO_INT(x)        ((x >= 0) ? (x + F/2)/F : (x - F/2)/F)
#define ADD_FP(x, y)        (x + y)
#define SUB_FP(x, y)        (x - y)
#define ADD_FP_INT(x, n)    (x + n*F)
#define SUB_FP_INT(x, n)    (x - n*F)
#define SUB_INT_FP(n, x)    (n*F - x)
#define MUL_FP(x, y)        (((int64_t)x)*y/F)
#define MUL_FP_INT(x, n)    (x*n)
#define DIV_FP(x, y)        (((int64_t)x)*F/y)
#define DIV_FP_INT(x, n)    (x/n)

#endif