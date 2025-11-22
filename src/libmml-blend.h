/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#ifndef __LIBMML_BLEND_H__
#define __LIBMML_BLEND_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdlib.h>

/*！
** 核心算法：生成旋转并混合过渡的一帧画面
**
** 这是一个 "逆向映射" (Inverse Mapping) 的实现。
** 它同时执行两个操作：
** 1. 几何变换：围绕中心旋转。
** 2. 像素混合：在图像 A 和 图像 B 之间进行线性插值。
**
** @param out   [输出] 目标图像数据的缓冲区 (RGB24格式)，结果写入这里
** @param imgA  [输入] 起始图片 (动画开始时的画面)
** @param imgB  [输入] 结束图片 (动画结束时的画面)
** @param w     图像宽度
** @param h     图像高度
** @param t     [关键参数] 动画进度 / 归一化时间 (Range: 0.0 到 1.0)
**              - t = 0.0: 0度，完全显示 imgA
**              - t = 0.5: 180度，imgA 和 imgB 各占 50% 透明度
**              - t = 1.0: 360度，完全显示 imgB
*/
void 
mml_blend_rotate(uint8_t* out, 
                 uint8_t* img_start, 
                 uint8_t* img_end, 
                 int w, 
                 int h, 
                 float t);

void 
mml_blend_fade(uint8_t* out, 
               uint8_t* img_start, 
               uint8_t* img_end, 
               int w, 
               int h, 
               float t);

#ifdef __cplusplus
}
#endif

#endif // __LIBMML_BLEND_H__  