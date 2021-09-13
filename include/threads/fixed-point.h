#ifndef FIXED_POINT_H
#define FIXED_POINT_H

typedef int fixed_point;

#define F   (1<<14)

#define INT_TO_FP(n)        (fixed_point)((n)*F)
#define FP_TO_INT(x)        (int)(((x) >= 0) ? ((x) + F/2)/F : ((x) - F/2)/F)
#define ADD_FP(x, y)        (fixed_point)((x) + (y))
#define SUB_FP(x, y)        (fixed_point)((x) - (y))
#define ADD_FP_INT(x, n)    (fixed_point)((x) + (n)*F)
#define SUB_FP_INT(x, n)    (fixed_point)((x) - (n)*F)
#define SUB_INT_FP(n, x)    (fixed_point)((n)*F - (x))
#define MUL_FP(x, y)        (fixed_point)(((int64_t)(x))*(y)/F)
#define MUL_FP_INT(x, n)    (fixed_point)((x)*(n))
#define DIV_FP(x, y)        (fixed_point)(((int64_t)(x))*F/(y))
#define DIV_FP_INT(x, n)    (fixed_point)((x)/(n))

#endif