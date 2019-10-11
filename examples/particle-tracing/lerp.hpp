//---------------------------------------------------------------------------
//
// linear interpolation
//
// courtesy Hanqi Guo
// Argonne National Laboratory
// 9700 S. Cass Ave.
// Argonne, IL 60439
//
// copied with permission by Tom Peterka
// tpeterka@mcs.anl.gov
//
//--------------------------------------------------------------------------

#ifndef _LERP_HPP
#define _LERP_HPP

#include <stdbool.h>
#include <cmath>
#include <stdio.h>

// inside excludes the boundary (changed by TP 10/11/19)
inline bool inside(int              num_dims,
                   const int*       st,
                   const int*       sz,
                   const float*     p)
{
    for (int i = 0; i < num_dims; i++)
        // changed by TP, 10/11/19
//         if (p[i] < (float)(st[i]) || p[i] >= (float)(st[i] + sz[i] - 1))
        if (p[i] <= (float)(st[i]) || p[i] >= (float)(st[i] + sz[i] - 1))
            return false;
    return true;
}

inline float texel2D(const float*   p,
                     const int*     sz,
                     int            x,
                     int            y)
{
    return p[x + sz[0] * y];
}

inline float texel3D(const float*   p,
                     const int*     sz,
                     int            x,
                     int            y,
                     int            z)
{
    return p[x + sz[0] * (y + sz[1] * z)];
}

inline float texel4D(const float*   p,
                     const int*     sz,
                     int            x,
                     int            y,
                     int            z,
                     int            t)
{
    return p[x + sz[0] * (y + sz[1] * (z + sz[2] * t))];
}

inline bool lerp2D(const float*     pt,
                   const int*       st,
                   const int*       sz,
                   int              num_vars,
                   const float**    ptrs,
                   float*           vars)
{
    if (!inside(2, st, sz, pt)) return false;

    float p[4];
    float x = pt[0] - (float)(st[0]), y = pt[1] - (float)(st[1]);
    int i = floor(x), j = floor(y);
    int i1 = i + 1, j1 = j + 1;
    float x0 = i, x1 = i1, y0 = j, y1 = j1;
    int v;

    for (v = 0; v < num_vars; v++)
    {
        p[0] = texel2D(ptrs[v], sz, i  , j  );
        p[1] = texel2D(ptrs[v], sz, i1 , j  );
        p[2] = texel2D(ptrs[v], sz, i  , j1 );
        p[3] = texel2D(ptrs[v], sz, i1 , j1 );

        vars[v] =
              p[0] * (x1 - x) * (y1 - y)
            + p[1] * (x - x0) * (y1 - y)
            + p[2] * (x1 - x) * (y - y0)
            + p[3] * (x - x0) * (y - y0);
    }

    return true;
}

inline bool lerp3D(const float*     pt,         // target point
                   const int*       st,         // min corner of block
                   const int*       sz,         // number of grid spaces in block
                   int              num_vars,   // dimensionality
                   const float**    ptrs,       // input vector field
                   float*           vars)       // output interpolated vector at target point
{
    if (!inside(3, st, sz, pt)) return false;

    float p[8];                              // one component of velocity at each corner of texel
    float x  = pt[0] - (float)(st[0]),       // physical coords of point relative to min. of block
          y  = pt[1] - (float)(st[1]),
          z  = pt[2] - (float)(st[2]);
    int   i  = floor(x),                     // indices of min corner of texel containing point
          j  = floor(y),
          k  = floor(z);
    int   i1 =i + 1,                         // indices of max corner of texel containing point
          j1 =j + 1,
          k1 =k + 1;
    float x0 = i, x1 = i1,                   // float version of min, max corner of texel
          y0 = j, y1 = j1,
          z0 = k, z1 = k1;
    int v;                                   // dimension

    for (v = 0; v < num_vars; v++)
    {
        p[0] = texel3D(ptrs[v], sz, i  , j  , k  );
        p[1] = texel3D(ptrs[v], sz, i1 , j  , k  );
        p[2] = texel3D(ptrs[v], sz, i  , j1 , k  );
        p[3] = texel3D(ptrs[v], sz, i1 , j1 , k  );
        p[4] = texel3D(ptrs[v], sz, i  , j  , k1 );
        p[5] = texel3D(ptrs[v], sz, i1 , j  , k1 );
        p[6] = texel3D(ptrs[v], sz, i  , j1 , k1 );
        p[7] = texel3D(ptrs[v], sz, i1 , j1 , k1 );

        vars[v] =
            p[0] * (x1 - x) * (y1 - y) * (z1 - z) +
            p[1] * (x - x0) * (y1 - y) * (z1 - z) +
            p[2] * (x1 - x) * (y - y0) * (z1 - z) +
            p[3] * (x - x0) * (y - y0) * (z1 - z) +
            p[4] * (x1 - x) * (y1 - y) * (z - z0) +
            p[5] * (x - x0) * (y1 - y) * (z - z0) +
            p[6] * (x1 - x) * (y - y0) * (z - z0) +
            p[7] * (x - x0) * (y - y0) * (z - z0);
    }

    return true;
}


inline bool lerp4D(const float*     pt,
                   const int*       st,
                   const int*       sz,
                   int              num_vars,
                   const float**    ptrs,
                   float*           vars)
{
    if (!inside(4, st, sz, pt)) return false;

    float   p[16];
    float   x = pt[0] - (float)(st[0]),
            y = pt[1] - (float)(st[1]),
            z = pt[2] - (float)(st[2]),
            t = pt[3] - (float)(st[3]);
    int     i = floor(x),
            j = floor(y),
            k = floor(z),
            l = floor(t);
    int     i1 = i + 1, j1 = j + 1, k1 = k + 1, l1 = l + 1;
    float   x0 = i, x1 = i1,
            y0 = j, y1 = j1,
            z0 = k, z1 = k1,
            t0 = l, t1 = l1;
    int     v; // for variables
    int     s; // for vertices

    for (v = 0; v < num_vars; v++)
    {
        p[0] = texel4D(ptrs[v], sz, i  , j  , k  , l  );
        p[1] = texel4D(ptrs[v], sz, i1 , j  , k  , l  );
        p[2] = texel4D(ptrs[v], sz, i  , j1 , k  , l  );
        p[3] = texel4D(ptrs[v], sz, i1 , j1 , k  , l  );
        p[4] = texel4D(ptrs[v], sz, i  , j  , k1 , l  );
        p[5] = texel4D(ptrs[v], sz, i1 , j  , k1 , l  );
        p[6] = texel4D(ptrs[v], sz, i  , j1 , k1 , l  );
        p[7] = texel4D(ptrs[v], sz, i1 , j1 , k1 , l  );
        p[8] = texel4D(ptrs[v], sz, i  , j  , k  , l1 );
        p[9] = texel4D(ptrs[v], sz, i1 , j  , k  , l1 );
        p[10]= texel4D(ptrs[v], sz, i  , j1 , k  , l1 );
        p[11]= texel4D(ptrs[v], sz, i1 , j1 , k  , l1 );
        p[12]= texel4D(ptrs[v], sz, i  , j  , k1 , l1 );
        p[13]= texel4D(ptrs[v], sz, i1 , j  , k1 , l1 );
        p[14]= texel4D(ptrs[v], sz, i  , j1 , k1 , l1 );
        p[15]= texel4D(ptrs[v], sz, i1 , j1 , k1 , l1 );
#if 0
        for (s=0; s<16; s++)
            if (isnan(p[i]) || isinf(p[i]))
                return false;
#endif

        vars[v] =
              p[0]*(x1-x)*(y1-y)*(z1-z)*(t1-t)
            + p[1]*(x-x0)*(y1-y)*(z1-z)*(t1-t)
            + p[2]*(x1-x)*(y-y0)*(z1-z)*(t1-t)
            + p[3]*(x-x0)*(y-y0)*(z1-z)*(t1-t)
            + p[4]*(x1-x)*(y1-y)*(z-z0)*(t1-t)
            + p[5]*(x-x0)*(y1-y)*(z-z0)*(t1-t)
            + p[6]*(x1-x)*(y-y0)*(z-z0)*(t1-t)
            + p[7]*(x-x0)*(y-y0)*(z-z0)*(t1-t)
            + p[8]*(x1-x)*(y1-y)*(z1-z)*(t-t0)
            + p[9]*(x-x0)*(y1-y)*(z1-z)*(t-t0)
            + p[10]*(x1-x)*(y-y0)*(z1-z)*(t-t0)
            + p[11]*(x-x0)*(y-y0)*(z1-z)*(t-t0)
            + p[12]*(x1-x)*(y1-y)*(z-z0)*(t-t0)
            + p[13]*(x-x0)*(y1-y)*(z-z0)*(t-t0)
            + p[14]*(x1-x)*(y-y0)*(z-z0)*(t-t0)
            + p[15]*(x-x0)*(y-y0)*(z-z0)*(t-t0);
    }

    return true;
}

#endif
