/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#ifndef __LIBMML_SHAPE_H__
#define __LIBMML_SHAPE_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdlib.h>
#include <libavutil/frame.h>

/*!
** 在 YUV420P 帧上画一个带边框的椭圆
**
** @param frame       目标 AVFrame
** @param cx          中心点 X
** @param cy          中心点 Y
** @param rx          X轴半径 (半长轴)
** @param ry          Y轴半径 (半短轴)
** @param thickness   边框厚度 (像素)
** @param y_val       颜色的 Y 分量
** @param u_val       颜色的 U 分量
** @param v_val       颜色的 V 分量
** @param alpha       透明度
*/
void 
mml_shape_ellipse(AVFrame* frame, 
                  int cx, 
                  int cy, 
                  int rx, 
                  int ry, 
                  int thickness, 
                  uint8_t y_val, 
                  uint8_t u_val, 
                  uint8_t v_val,
                  float alpha);

/*！
** @brief 在 YUV 帧上绘制任意四边形 (P1-P2-P3-P4-P1)。
** 
** @param frame      目标帧 (需为 YUV420P)。
** @param x1,y1      点 1 坐标。
** @param x2,y2      点 2 坐标。
** @param x3,y3      点 3 坐标。
** @param x4,y4      点 4 坐标。
** @param thickness  线条粗细 (像素)。
** @param y_val, u_val, v_val  YUV 颜色分量。
** @param alpha      透明度 (0.0 - 1.0)。
*/
void 
mml_shape_quad(AVFrame* frame, 
               int x1, int y1, 
               int x2, int y2, 
               int x3, int y3, 
               int x4, int y4,
               int thickness, 
               uint8_t y_val, 
               uint8_t u_val, 
               uint8_t v_val,
               float alpha);

#ifdef __cplusplus
}
#endif  

#endif // __LIBMML_SHAPE_H__