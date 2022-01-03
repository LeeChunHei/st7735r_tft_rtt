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

#include <rtthread.h>
#include <rtdevice.h>

#ifdef PKG_USING_ST7735R_TFT
#include "drv_spi.h"
#include "drv_gpio.h"
#include "drv_st7735r.h"

#define LOG_TAG             "drv.st7735r"
#include <drv_log.h>

#ifdef PKG_ST7735R_USING_KCONFIG
	#define PKG_ST7735R_CS      GET_PIN(PKG_ST7735R_CS_GPIO, PKG_ST7735R_CS_PIN)
	#if !defined(PKG_ST7735R_ADJ_BL)
		#define PKG_ST7735R_BL      GET_PIN(PKG_ST7735R_BL_GPIO, PKG_ST7735R_BL_PIN)
	#endif
	#define PKG_ST7735R_DC      GET_PIN(PKG_ST7735R_DC_GPIO, PKG_ST7735R_DC_PIN)
	#define PKG_ST7735R_RES     GET_PIN(PKG_ST7735R_RES_GPIO, PKG_ST7735R_RES_PIN)
	#define PKG_ST7735R_CLEAR_SEND_NUMBER 5760
#endif

#define ST7735R_NOP 0x00	 // NOP
#define ST7735R_SWRESET 0x01 // Software Reset
#define ST7735R_RDDID 0x04   // Read Display ID
#define ST7735R_SLPIN 0x10   // Sleep In
#define ST7735R_SLPOUT 0x11  // Sleep Out
#define ST7735R_INVOFF 0x20  // Display Inversion Off
#define ST7735R_INVON 0x21   // Display Inversion On
#define ST7735R_DISPOFF 0x28 // Display Off
#define ST7735R_DISPON 0x29  // Display On
#define ST7735R_CASET 0x2A   // Column Address Set
#define ST7735R_RASET 0x2B   // Row Address Set
#define ST7735R_RAMWR 0x2C   // Memory Write
#define ST7735R_MADCTL 0x36  // Memory Data Access Control
#define ST7735R_COLMOD 0x3A  // Interface Pixel Format
#define ST7735R_FRMCTR1 0xB1 // Frame Rate Control (in normal mode)
#define ST7735R_INVCTR 0xB4  // Display Inversion Control
#define ST7735R_PWCTR1 0xC0  // Power Control 1
#define ST7735R_PWCTR2 0xC1  // Power Control 2
#define ST7735R_PWCTR3 0xC2  // Power Control 3 (in normal mode)
#define ST7735R_PWCTR4 0xC3  // Power Control 4 (in idle/8-bit mode)
#define ST7735R_PWCTR5 0xC4  // Power Control 5 (in partial mode)
#define ST7735R_VMCTR1 0xC5  // VCOM Control 1
#define ST7735R_GMCTRP1 0xE0 // Gamma (+ polarity) Correction Characteristics Setting
#define ST7735R_GMCTRN1 0xE1 // Gamma (- polarity) Correction Characteristics Setting

static rt_st7735r_t graphics_lcd;

rt_inline rt_err_t st7735r_send(rt_st7735r_t dev, rt_bool_t is_data, rt_uint8_t data)
{
    rt_pin_write(dev->dc_pin, is_data);
    if (rt_spi_send(dev->spi, (const void *)(&data), 1) != 1)
    {
        LOG_E(LOG_TAG" send %s %d failed", is_data ? "data" : "command", data);
        return -RT_ERROR;
    }
    else
    {
        return RT_EOK;
    }
}

static void st7735r_init_ori(rt_st7735r_t dev, rt_uint8_t orientation)
{
	uint8_t param = 0;
	switch (orientation)
	{
	case 1:
		param = 1 << 5 | 1 << 7;
		break;
	case 2:
		param = 1 << 7 | 1 << 6;
		break;
	case 3:
		param = 1 << 5 | 1 << 6;
		break;
	default:
		break;
	}
	// param |= config.is_bgr << 4;

	st7735r_send(dev, RT_FALSE, ST7735R_MADCTL);
	st7735r_send(dev, RT_TRUE, param);
}

static void st7735r_init_frmctr(rt_st7735r_t dev, rt_uint8_t fps)
{
	const rt_uint8_t line = 160;
	const rt_uint32_t fosc = 850000;
	rt_uint32_t best_rtna = 0;
	rt_uint32_t best_fpa = 0;
	rt_uint32_t best_bpa = 0;
	rt_uint32_t min_diff = (rt_uint32_t)(-1);
	for (rt_uint32_t rtna = 0; rtna <= 0x0F; ++rtna)
	{
		const rt_uint32_t this_rtna = rtna * 2 + 40;
		for (rt_uint32_t fpa = 1; fpa <= 0x3F; ++fpa)
		{
			for (rt_uint32_t bpa = 1; bpa <= 0x3F; ++bpa)
			{
				const rt_uint32_t this_rate = fosc / (this_rtna * (line + fpa + bpa + 2));
				const rt_uint32_t this_diff = (int32_t)(this_rate - fps) < 0 ? fps - this_rate : this_rate - fps;
				if (this_diff < min_diff)
				{
					min_diff = this_diff;
					best_rtna = rtna;
					best_fpa = fpa;
					best_bpa = bpa;
				}
				if (min_diff == 0)
				{
					break;
				}
			}
		}
	}

	st7735r_send(dev, RT_FALSE, ST7735R_FRMCTR1);
	st7735r_send(dev, RT_TRUE, best_rtna);
	st7735r_send(dev, RT_TRUE, best_fpa);
	st7735r_send(dev, RT_TRUE, best_bpa);
}

static void st7735r_init_pwctr(rt_st7735r_t dev)
{
	st7735r_send(dev, RT_FALSE, ST7735R_PWCTR1);
	st7735r_send(dev, RT_TRUE, 0xA2);
	st7735r_send(dev, RT_TRUE, 0x02);
	st7735r_send(dev, RT_TRUE, 0x84);

	st7735r_send(dev, RT_FALSE, ST7735R_PWCTR2);
	st7735r_send(dev, RT_TRUE, 0xC5);

	st7735r_send(dev, RT_FALSE, ST7735R_PWCTR3);
	st7735r_send(dev, RT_TRUE, 0x0A);
	st7735r_send(dev, RT_TRUE, 0x00);

	st7735r_send(dev, RT_FALSE, ST7735R_PWCTR4);
	st7735r_send(dev, RT_TRUE, 0x8A);
	st7735r_send(dev, RT_TRUE, 0x2A);

	st7735r_send(dev, RT_FALSE, ST7735R_PWCTR5);
	st7735r_send(dev, RT_TRUE, 0x8A);
	st7735r_send(dev, RT_TRUE, 0xEE);
}

static void st7735r_init_gamma(rt_st7735r_t dev)
{
	st7735r_send(dev, RT_FALSE, ST7735R_GMCTRP1);
	st7735r_send(dev, RT_TRUE, 0x02);
	st7735r_send(dev, RT_TRUE, 0x1C);
	st7735r_send(dev, RT_TRUE, 0x07);
	st7735r_send(dev, RT_TRUE, 0x12);
	st7735r_send(dev, RT_TRUE, 0x37);
	st7735r_send(dev, RT_TRUE, 0x32);
	st7735r_send(dev, RT_TRUE, 0x29);
	st7735r_send(dev, RT_TRUE, 0x2D);
	st7735r_send(dev, RT_TRUE, 0x29);
	st7735r_send(dev, RT_TRUE, 0x25);
	st7735r_send(dev, RT_TRUE, 0x2B);
	st7735r_send(dev, RT_TRUE, 0x39);
	st7735r_send(dev, RT_TRUE, 0x00);
	st7735r_send(dev, RT_TRUE, 0x01);
	st7735r_send(dev, RT_TRUE, 0x03);
	st7735r_send(dev, RT_TRUE, 0x10);

	st7735r_send(dev, RT_FALSE, ST7735R_GMCTRN1);
	st7735r_send(dev, RT_TRUE, 0x03);
	st7735r_send(dev, RT_TRUE, 0x1D);
	st7735r_send(dev, RT_TRUE, 0x07);
	st7735r_send(dev, RT_TRUE, 0x06);
	st7735r_send(dev, RT_TRUE, 0x2E);
	st7735r_send(dev, RT_TRUE, 0x2C);
	st7735r_send(dev, RT_TRUE, 0x29);
	st7735r_send(dev, RT_TRUE, 0x2D);
	st7735r_send(dev, RT_TRUE, 0x2E);
	st7735r_send(dev, RT_TRUE, 0x2E);
	st7735r_send(dev, RT_TRUE, 0x37);
	st7735r_send(dev, RT_TRUE, 0x3F);
	st7735r_send(dev, RT_TRUE, 0x00);
	st7735r_send(dev, RT_TRUE, 0x00);
	st7735r_send(dev, RT_TRUE, 0x02);
	st7735r_send(dev, RT_TRUE, 0x10);
}

void st7735r_set_active_rect(rt_st7735r_t dev, rt_uint8_t x, rt_uint8_t y, rt_uint8_t width, rt_uint8_t height)
{
	st7735r_send(dev, RT_FALSE, ST7735R_CASET);
	// start
	st7735r_send(dev, RT_TRUE, 0x00);
	st7735r_send(dev, RT_TRUE, x);
	// end
	st7735r_send(dev, RT_TRUE, 0x00);
	st7735r_send(dev, RT_TRUE, x + width - 1);

	st7735r_send(dev, RT_FALSE, ST7735R_RASET);
	st7735r_send(dev, RT_TRUE, 0x00);
	st7735r_send(dev, RT_TRUE, y);
	st7735r_send(dev, RT_TRUE, 0x00);
	st7735r_send(dev, RT_TRUE, y + height - 1);
}

void st7735r_clear(rt_st7735r_t dev, rt_uint16_t color)
{
    st7735r_set_active_rect(dev, 0, 0, dev->width, dev->height);
	st7735r_fill_color(dev, color);
}

void st7735r_fill_color(rt_st7735r_t dev, rt_uint16_t color)
{
	st7735r_send(dev, RT_FALSE, ST7735R_RAMWR);
	const rt_uint32_t max = 128 * 160;
	for (rt_uint32_t i = 0; i < max; ++i)
	{
		st7735r_send(dev, RT_TRUE, color >> 8);
		st7735r_send(dev, RT_TRUE, color);
	}
}

void st7735r_show_grayscale_pixel(rt_st7735r_t dev, const rt_uint8_t *pixel, rt_size_t length)
{
	st7735r_send(dev, RT_FALSE, ST7735R_RAMWR);
	for (rt_uint32_t i = 0; i < length; ++i)
	{
		rt_uint8_t gs_color = pixel[i];
		const uint16_t color = ((gs_color >> 3) << 11) | ((gs_color >> 2) << 5) | (gs_color >> 3);
		st7735r_send(dev, RT_TRUE, color >> 8);
		st7735r_send(dev, RT_TRUE, color);
	}
}

void st7735r_show_color_pixel(rt_st7735r_t dev, const rt_uint16_t *pixel, rt_size_t length)
{
	st7735r_send(dev, RT_FALSE, ST7735R_RAMWR);
	for (rt_uint32_t i = 0; i < length; ++i)
	{
		st7735r_send(dev, RT_TRUE, pixel[i] >> 8);
		st7735r_send(dev, RT_TRUE, pixel[i]);
	}
}

void st7735r_set_bl(rt_st7735r_t dev, rt_bool_t on)
{
#ifdef PKG_ST7735R_ADJ_BL
	if (on)
	{
		rt_size_t period = 1000000000 / PKG_ST7735R_BL_PWM_FREQ;
		rt_pwm_set(dev->bl_pwm, dev->bl_channel, period, period * dev->bl_value / 100);
		rt_pwm_enable(dev->bl_pwm, dev->bl_channel);
	}
	else
	{
		rt_pwm_disable(dev->bl_pwm, dev->bl_channel);
	}
#else
	rt_pin_write(dev->bl_pin, on);
#endif
}

static rt_err_t st7735r_init(rt_device_t dev)
{
	rt_st7735r_t st7735r_dev = (rt_st7735r_t)dev;
    struct rt_spi_configuration spi_config;
    spi_config.data_width = 8;
    spi_config.mode = RT_SPI_MASTER | RT_SPI_MODE_0 | RT_SPI_MSB;
    /* Max freq of ST7735R is 15MHz */
	spi_config.max_hz = 15 * 1000 * 1000;
    rt_spi_configure(st7735r_dev->spi, &spi_config);
	
	rt_pin_write(st7735r_dev->res_pin, PIN_LOW);
	rt_thread_mdelay(100);
	rt_pin_write(st7735r_dev->res_pin, PIN_HIGH);
#if !defined(PKG_ST7735R_ADJ_BL)
		rt_pin_write(st7735r_dev->bl_pin, PIN_LOW);
#endif
	rt_pin_write(st7735r_dev->dc_pin, PIN_HIGH);

    st7735r_send(st7735r_dev, RT_FALSE, ST7735R_SWRESET);
    rt_thread_mdelay(10);

    st7735r_send(st7735r_dev, RT_FALSE, ST7735R_SLPOUT);
	rt_thread_mdelay(120);

    st7735r_init_ori(st7735r_dev, 2);

	// 16-bit
	st7735r_send(st7735r_dev, RT_FALSE, ST7735R_COLMOD);
	st7735r_send(st7735r_dev, RT_TRUE, 0x05);

    /* Set to 60FPS */
	st7735r_init_frmctr(st7735r_dev, 60);
	st7735r_init_pwctr(st7735r_dev);
	st7735r_init_gamma(st7735r_dev);

	st7735r_send(st7735r_dev, RT_FALSE, ST7735R_VMCTR1);
	st7735r_send(st7735r_dev, RT_TRUE, 0x0E);

	st7735r_set_active_rect(st7735r_dev, 0, 0, st7735r_dev->width, st7735r_dev->height);

	st7735r_send(st7735r_dev, RT_FALSE, ST7735R_DISPON);
	rt_thread_mdelay(10);
	return RT_EOK;
}

static rt_err_t st7735r_open(rt_device_t dev, rt_uint16_t oflag)
{
	rt_st7735r_t lcd = (rt_st7735r_t)dev;
    st7735r_clear(lcd, 0x0);
	st7735r_set_bl(lcd, RT_TRUE);
	return RT_EOK;
}

static rt_err_t st7735r_close(rt_device_t dev)
{
	if (dev->ref_count == 0)
	{
		rt_st7735r_t lcd = (rt_st7735r_t)dev;
    	st7735r_clear(lcd, 0x0);
#ifdef PKG_ST7735R_ADJ_BL
		rt_pwm_disable(lcd->bl_pwm, lcd->bl_channel);
#else
		rt_pin_write(lcd->bl_pin, PIN_LOW);
#endif
	}
	return RT_EOK;
}

static rt_size_t st7735r_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
	//TODO: finding a way to complete this function without adding a frame buffer
	return 0;
}

static rt_size_t st7735r_write(rt_device_t _dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
	rt_st7735r_t dev = (rt_st7735r_t)_dev;
	switch (pos)
	{
	case RT_ST7735R_WRITE_COLOR_PIXEL:
	{
		st7735r_show_color_pixel(dev, (const rt_uint16_t *)buffer, size);
		return size;
	}
	case RT_ST7735R_WRITE_GRAYSCALE_PIXEL:
	{
		st7735r_show_grayscale_pixel(dev, (const rt_uint8_t *)buffer, size);
		return size;
	}
	default:
		return 0;
	}
}

static rt_err_t st7735r_control(rt_device_t dev, int cmd, void *args)
{
	rt_st7735r_t lcd = (rt_st7735r_t)dev;
	switch (cmd)
	{
	case RT_ST7735R_SET_RECT:
	{
		rt_st7735r_rect_t rect = (rt_st7735r_rect_t)args;
		st7735r_set_active_rect(lcd, rect->x, rect->y, rect->width, rect->height);
		return RT_EOK;
	}
	case RT_ST7735R_SET_BL:
	{
		rt_uint8_t bl = *((rt_uint8_t *)args);
		if (bl > 100)
		{
			LOG_E(LOG_TAG" %d is wrong backlight value", bl);
			return -RT_ERROR;
		}
#ifdef PKG_ST7735R_ADJ_BL
		dev->bl_value = bl;
#endif
		st7735r_set_bl(lcd, bl != 0);
		return RT_EOK;
	}
	case RTGRAPHIC_CTRL_POWERON:
	{
		st7735r_clear(lcd, 0x0);
		st7735r_set_bl(lcd, RT_TRUE);
		return RT_EOK;
	}
    case RTGRAPHIC_CTRL_POWEROFF:
	{
		st7735r_set_bl(lcd, RT_FALSE);
		return RT_EOK;
	}
	case RTGRAPHIC_CTRL_SET_MODE:
    case RTGRAPHIC_CTRL_GET_EXT:
	case RTGRAPHIC_CTRL_RECT_UPDATE:
		return RT_EOK;
    case RTGRAPHIC_CTRL_GET_INFO:
    {
        struct rt_device_graphic_info *info = (struct rt_device_graphic_info *)args;
        RT_ASSERT(info != RT_NULL);
		rt_memcpy(info, &(lcd->lcd_info), sizeof(struct rt_device_graphic_info));
		return RT_EOK;
    }
	default:
		return -RT_ERROR;
	}
}

static void st7735r_set_pixel(const char *pixel, int x, int y)
{
    st7735r_set_active_rect(graphics_lcd, x, y, 1, 1);
	st7735r_send(graphics_lcd, RT_FALSE, ST7735R_RAMWR);
	const rt_uint16_t *color = (const rt_uint16_t *)pixel;
	st7735r_send(graphics_lcd, RT_TRUE, *(color) >> 8);
	st7735r_send(graphics_lcd, RT_TRUE, *(color));
}

static void st7735r_get_pixel(char *pixel, int x, int y)
{
//TODO: finding a way to complete this function without adding a frame buffer
}

static void st7735r_draw_hline(const char *pixel, int x1, int x2, int y)
{
	int width;
	if (x2 > x1)
	{
		width = x2 - x1;
		st7735r_set_active_rect(graphics_lcd, x1, y, width, 1);
	}
	else
	{
		width = x1 - x2;
		st7735r_set_active_rect(graphics_lcd, x2, y, width, 1);
	}
	st7735r_send(graphics_lcd, RT_FALSE, ST7735R_RAMWR);
	const rt_uint16_t *color = (const rt_uint16_t *)pixel;
	for (rt_uint32_t i = 0; i < width; ++i)
	{
		st7735r_send(graphics_lcd, RT_TRUE, *(color) >> 8);
		st7735r_send(graphics_lcd, RT_TRUE, *(color));
	}
}

static void st7735r_draw_vline(const char *pixel, int x, int y1, int y2)
{
	int height;
	if (y2 > y1)
	{
		height = y2 - y1;
		st7735r_set_active_rect(graphics_lcd, x, y1, 1, height);
	}
	else
	{
		height = y1 - y2;
		st7735r_set_active_rect(graphics_lcd, x, y2, 1, height);
	}
	st7735r_send(graphics_lcd, RT_FALSE, ST7735R_RAMWR);
	const rt_uint16_t *color = (const rt_uint16_t *)pixel;
	for (rt_uint32_t i = 0; i < height; ++i)
	{
		st7735r_send(graphics_lcd, RT_TRUE, *(color) >> 8);
		st7735r_send(graphics_lcd, RT_TRUE, *(color));
	}
}

static void st7735r_blit_line(const char *pixel, int x, int y, rt_size_t size)
{
    const rt_uint16_t *ptr = (const rt_uint16_t *)pixel;
	st7735r_set_active_rect(graphics_lcd, x, y, size, 1);
	st7735r_send(graphics_lcd, RT_FALSE, ST7735R_RAMWR);
	for (rt_uint32_t i = 0; i < size; ++i)
	{
		st7735r_send(graphics_lcd, RT_TRUE, *(ptr) >> 8);
		st7735r_send(graphics_lcd, RT_TRUE, *(ptr));
		++ptr;
	}
    return;
}

static struct rt_device_graphic_ops st7735r_graphic_ops =
{
    st7735r_set_pixel,
    st7735r_get_pixel,
    st7735r_draw_hline,
    st7735r_draw_vline,
    st7735r_blit_line
};

#ifdef RT_USING_DEVICE_OPS
static struct rt_device_ops st7735r_dev_ops =
{
    .init = st7735r_init,
    .open = st7735r_open,
    .close = st7735r_close,
    .read = st7735r_read,
    .write = st7735r_write,
    .control = st7735r_control,
#endif

#ifdef PKG_ST7735R_ADJ_BL
    rt_st7735r_t st7735r_user_init(char *spi_bus_name, rt_base_t cs_pin, rt_base_t res_pin, rt_base_t dc_pin, const char *bl_pwm_name, rt_uint8_t bl_pwm_channel, uint8_t width, uint8_t height)
#else
    rt_st7735r_t st7735r_user_init(char *spi_bus_name, rt_base_t cs_pin, rt_base_t res_pin, rt_base_t dc_pin, rt_base_t bl_pin, uint8_t width, uint8_t height)
#endif
{
	rt_uint8_t dev_num = 0;
	char dev_name[RT_NAME_MAX];
	do
	{
		rt_sprintf(dev_name, "%s%d", spi_bus_name, dev_num++);
		if (dev_num == 255)
		{
			return RT_NULL;
		}
	} while (rt_device_find(dev_name));
	
	rt_hw_spi_device_attach(spi_bus_name, dev_name, cs_pin);
#if !defined(PKG_ST7735R_ADJ_BL)
    rt_pin_mode(bl_pin, PIN_MODE_OUTPUT);
#endif
    rt_pin_mode(dc_pin, PIN_MODE_OUTPUT);
    rt_pin_mode(res_pin, PIN_MODE_OUTPUT);

    rt_st7735r_t dev_obj = rt_malloc(sizeof(struct rt_st7735r));
	if (dev_obj)
	{
		rt_memset(dev_obj, 0x0, sizeof(struct rt_st7735r));
		dev_obj->lcd_info.height = height;
		dev_obj->lcd_info.width = width;
		dev_obj->lcd_info.bits_per_pixel = 16;
		dev_obj->lcd_info.pixel_format = RTGRAPHIC_PIXEL_FORMAT_RGB565;
		dev_obj->lcd_info.framebuffer = RT_NULL;
		dev_obj->spi = (struct rt_spi_device *)rt_device_find(dev_name);
#ifdef PKG_ST7735R_ADJ_BL
		dev_obj->bl_pwm = (struct rt_device_pwm *)rt_device_find(bl_pwm_name);
		dev_obj->bl_channel = bl_pwm_channel;
		dev_obj->bl_value = PKG_ST7735R_BL_DEFAULT_INTENSITY
#else
		dev_obj->bl_pin = bl_pin;
#endif
		dev_obj->res_pin = res_pin;
		dev_obj->dc_pin = dc_pin;
		dev_obj->width = width;
		dev_obj->height = height;
#ifdef RT_USING_DEVICE_OPS
		dev_obj->parent.ops = &st7735r_dev_ops;
#else
		dev_obj->parent.type = RT_Device_Class_Graphic;
		dev_obj->parent.init = st7735r_init;
		dev_obj->parent.open = st7735r_open;
		dev_obj->parent.close = st7735r_close;
		dev_obj->parent.read = st7735r_read;
		dev_obj->parent.write = st7735r_write;
		dev_obj->parent.control = st7735r_control;
#endif
		dev_num = 0;
		do
		{
			rt_sprintf(dev_name, "lcd%d", dev_num++);
			if (dev_num == 255)
			{
				rt_device_destroy(&(dev_obj->parent));
				return RT_NULL;
			}
		} while (rt_device_find(dev_name));
		rt_device_register(&(dev_obj->parent), dev_name, RT_DEVICE_FLAG_DEACTIVATE);
		return (rt_st7735r_t)rt_device_find(dev_name);
	}
	else
	{
		return RT_NULL;
	}
}

#ifdef PKG_ST7735R_USING_KCONFIG
static int st7735r_hw_init(void)
{
#ifdef PKG_ST7735R_ADJ_BL
	graphics_lcd = st7735r_user_init(PKG_ST7735R_SPI_BUS, PKG_ST7735R_CS, PKG_ST7735R_RES, PKG_ST7735R_DC, PKG_ST7735R_BL_PWM, PKG_ST7735R_BL_PWM_CHANNEL, PKG_ST7735R_WIDTH, PKG_ST7735R_HEIGHT);
#else
	graphics_lcd = st7735r_user_init(PKG_ST7735R_SPI_BUS, PKG_ST7735R_CS, PKG_ST7735R_RES, PKG_ST7735R_DC, PKG_ST7735R_BL, PKG_ST7735R_WIDTH, PKG_ST7735R_HEIGHT);
#endif
	if (graphics_lcd == RT_NULL)
	{
		return -RT_ERROR;
	}
	graphics_lcd->parent.user_data = &st7735r_graphic_ops;
    return RT_EOK;
}
INIT_DEVICE_EXPORT(st7735r_hw_init);
#endif

#endif