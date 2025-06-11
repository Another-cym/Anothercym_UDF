#include "udf.h"

#define CELL_ID 2
#define FACE_ID 28
DEFINE_ADJUST(FindThePlace, d) // 其中d是一个整数类型的参数
{
    Domain *d0 = Get_Domain(1);
    Thread *cellthread = Lookup_Thread(d0, CELL_ID); // 获取网格的指针
    Thread *facethread = Lookup_Thread(d0, FACE_ID); // 获取面的指针
    cell_t c;
    face_t f;
    real x[ND_ND];
    real y[ND_ND];

    // 下面的代码获取面的中心坐标
    // 注意：在 Fluent 中，面的中心坐标是通过 F_CENTROID 函数获取的
    begin_f_loop(f, facethread)
    {
        F_CENTROID(x, f, facethread); // 获取面的中心坐标
        Message("Face %d centroid: (%f, %f)\n", f, x[0], x[1]);
    }
    end_f_loop(f, facethread)

        // 下面的代码获取单元的中心坐标
        // 注意：在 Fluent 中，单元的中心坐标是通过 C_CENTROID 函数获取的
        begin_c_loop(c, cellthread)
    {
        C_CENTROID(y, c, cellthread); // 获取单元的中心坐标
        Message("Cell %d centroid: (%f, %f)\n", c, y[0], y[1]);
    }
    end_c_loop(c, cellthread)
}