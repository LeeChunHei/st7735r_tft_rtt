/*
 * Copyright (c) 2021 Lee Chun Hei, Leslie
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
 * OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __DRV_ST7735R_H__
#define __DRV_ST7735R_H__

#include "rtthread.h"

#ifdef PKG_USING_ST7735R_TFT
#include "drv_spi.h"
#include "drv_gpio.h"

#define RT_ST7735R_SET_RECT     0x30
#define RT_ST7735R_SET_BL       0x31

struct rt_st7735r
{
    struct rt_device parent;
    struct rt_spi_device *spi;
    rt_base_t res_pin;
    rt_base_t dc_pin;
#ifdef PKG_ST7735R_ADJ_BL
    struct rt_device_pwm *bl_pwm;
    rt_uint8_t bl_channel;
#else
    rt_base_t bl_pin;
#endif
    rt_uint8_t width;
    rt_uint8_t height;
};
typedef struct rt_st7735r *rt_st7735r_t;

struct rt_st7735r_rect
{
    rt_uint8_t x;
    rt_uint8_t y;
    rt_uint8_t width;
    rt_uint8_t height;
};
typedef struct rt_st7735r_rect *rt_st7735r_rect_t;

#define RT_ST7735R_WRITE_COLOR_PIXEL        0x01
#define RT_ST7735R_WRITE_GRAYSCALE_PIXEL    0x02

void st7735r_set_active_rect(rt_st7735r_t dev, rt_uint8_t x, rt_uint8_t y, rt_uint8_t width, rt_uint8_t height);
void st7735r_clear(rt_st7735r_t dev, rt_uint16_t color);
void st7735r_fill_color(rt_st7735r_t dev, rt_uint16_t color);
void st7735r_show_grayscale_pixel(rt_st7735r_t dev, rt_uint8_t* pixel, rt_size_t length);
void st7735r_show_color_pixel(rt_st7735r_t dev, rt_uint16_t* pixel, rt_size_t length);
#ifdef PKG_ST7735R_ADJ_BL
    rt_st7735r_t st7735r_user_init(char *spi_bus_name, rt_base_t cs_pin, rt_base_t res_pin, rt_base_t dc_pin, const char *bl_pwm_name, rt_uint8_t bl_pwm_channel, uint8_t width, uint8_t height);
#else
    rt_st7735r_t st7735r_user_init(char *spi_bus_name, rt_base_t cs_pin, rt_base_t res_pin, rt_base_t dc_pin, rt_base_t bl_pin, uint8_t width, uint8_t height);
#endif

#endif
#endif