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
*/
void 
mml_shape_ellipse_draw(AVFrame* frame, 
                       int cx, 
                       int cy, 
                       int rx, 
                       int ry, 
                       int thickness, 
                       uint8_t y_val, 
                       uint8_t u_val, 
                       uint8_t v_val);

void 
mml_shape_ellipse_move(AVFrame* frame, 
                       int start_cx, 
                       int start_cy,
                       int end_cx, 
                       int end_cy,  
                       int rx, 
                       int ry, 
                       int thickness, 
                       uint8_t y_val, 
                       uint8_t u_val, 
                       uint8_t v_val,
                       double seconds);

#ifdef __cplusplus
}
#endif  

#endif // __LIBMML_SHAPE_H__