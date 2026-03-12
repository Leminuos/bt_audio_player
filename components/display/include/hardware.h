#pragma once

#define LCD_H_RES               320
#define LCD_V_RES               240
#define LCD_BITS_PIXEL          16
#define LCD_BUF_LINES           20
#define LCD_DOUBLE_BUFFER       1
#define LCD_DRAWBUF_SIZE        (LCD_H_RES * LCD_BUF_LINES)

#define LCD_PIXEL_CLOCK_HZ      (20 * 1000 * 1000)
#define LCD_CMD_BITS            (8)
#define LCD_PARAM_BITS          (8)
#define LCD_HOST                SPI2_HOST
#define LCD_CLK_GPIO            (gpio_num_t) GPIO_NUM_14
#define LCD_MOSI_GPIO           (gpio_num_t) GPIO_NUM_13
#define LCD_MISO_GPIO           (gpio_num_t) GPIO_NUM_12
#define LCD_CS_GPIO             (gpio_num_t) GPIO_NUM_15
#define LCD_DC_GPIO             (gpio_num_t) GPIO_NUM_2
#define LCD_RST_GPIO            (gpio_num_t) GPIO_NUM_4
#define LCD_BUSY_GPIO           (gpio_num_t) GPIO_NUM_NC
#define LCD_MIRROR_X            (false)
#define LCD_MIRROR_Y            (true)

#define LCD_BL_GPIO             (gpio_num_t) GPIO_NUM_21
#define LCD_BACKLIGHT_LEDC_CH   (1)

#define TOUCH_SPI_HOST          SPI3_HOST
#define TOUCH_CLOCK_HZ          (2 * 1000 * 1000)
#define TOUCH_CLK_GPIO          (gpio_num_t) GPIO_NUM_25
#define TOUCH_MOSI_GPIO         (gpio_num_t) GPIO_NUM_32
#define TOUCH_MISO_GPIO         (gpio_num_t) GPIO_NUM_39
#define TOUCH_CS_GPIO           (gpio_num_t) GPIO_NUM_33
#define TOUCH_DC_GPIO           (gpio_num_t) GPIO_NUM_NC
#define TOUCH_IRQ_GPIO          (gpio_num_t) GPIO_NUM_NC
#define TOUCH_RST_GPIO          (gpio_num_t) GPIO_NUM_NC

#define TOUCH_MIRROR_X          (true)
#define TOUCH_MIRROR_Y          (false)

#define LVGL_TASK_PRIORITY      5
#define LVGL_TASK_STACK_SIZE    8192
#define LVGL_TASK_MAX_DELAY_MS  50
#define LVGL_TASK_MIN_DELAY_MS  5
#define LVGL_TICK_PERIOD_MS     1