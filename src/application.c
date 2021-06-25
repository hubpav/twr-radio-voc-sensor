#include <application.h>

#define VOC_TAG_UPDATE_INTERVAL (5 * 1000)
#define TEMPERATURE_TAG_UPDATE_INTERVAL (5 * 1000)
#define HUMIDITY_TAG_UPDATE_INTERVAL (5 * 1000)
#define BATTERY_UPDATE_INTERVAL (1 * 60 * 1000)
#define APPLICATION_TASK_ID 0
#define VOC_TAG_GRAPH (60 * 1000)
#define TEMPERATURE_TAG_GRAPH (5 * 60 * 1000)
#define HUMIDITY_TAG_GRAPH (5 * 60 * 1000)

#define HUMIDITY_TAG_REVISION TWR_TAG_HUMIDITY_REVISION_R3

// LED instance
twr_led_t led;

// Button instance
twr_button_t button;

twr_tmp112_t tmp112;
twr_tag_voc_t tag_voc;
twr_tag_temperature_t tag_temperature;
twr_tag_humidity_t tag_humidity;

twr_gfx_t *gfx;

float temperature = NAN;
float humidity = NAN;
float tvoc = NAN;

TWR_DATA_STREAM_FLOAT_BUFFER(tvoc_stream_buffer, (VOC_TAG_GRAPH / VOC_TAG_UPDATE_INTERVAL))
TWR_DATA_STREAM_FLOAT_BUFFER(temperature_stream_buffer, (TEMPERATURE_TAG_GRAPH / TEMPERATURE_TAG_UPDATE_INTERVAL))
TWR_DATA_STREAM_FLOAT_BUFFER(humidity_stream_buffer, (HUMIDITY_TAG_GRAPH / HUMIDITY_TAG_UPDATE_INTERVAL))
twr_data_stream_t tvoc_stream;
twr_data_stream_t temperature_stream;
twr_data_stream_t humidity_stream;

bool page = true;

void button_event_handler(twr_button_t *self, twr_button_event_t event, void *event_param)
{
    if (event == TWR_BUTTON_EVENT_PRESS)
    {
        page = !page;

        twr_scheduler_plan_now(APPLICATION_TASK_ID);
    }
}

void battery_event_handler(twr_module_battery_event_t event, void *event_param)
{
    (void) event;
    (void) event_param;

    float voltage;

    if (twr_module_battery_get_voltage(&voltage))
    {
        twr_radio_pub_battery(&voltage);
    }
}

void temperature_tag_event_handler(twr_tag_temperature_t *self, twr_tag_temperature_event_t event, void *event_param)
{
    temperature = NAN;

    if (event == TWR_TAG_TEMPERATURE_EVENT_UPDATE)
    {
        if (twr_tag_temperature_get_temperature_celsius(self, &temperature))
        {
            twr_data_stream_feed(&temperature_stream, &temperature);

            twr_radio_pub_temperature(TWR_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_DEFAULT, &temperature);

            float avg_temperature = NAN;

            float avg_humidity = NAN;

            twr_data_stream_get_average(&temperature_stream, &avg_temperature);

            twr_data_stream_get_average(&humidity_stream, &avg_humidity);

            twr_tag_voc_set_compensation(&tag_voc, isnan(avg_temperature) ? NULL : &avg_temperature, isnan(avg_humidity) ? NULL: &avg_humidity);
        }
    }

    twr_scheduler_plan_now(APPLICATION_TASK_ID);
}

void humidity_tag_event_handler(twr_tag_humidity_t *self, twr_tag_humidity_event_t event, void *event_param)
{
    humidity = NAN;

    if (event == TWR_TAG_HUMIDITY_EVENT_UPDATE)
    {
        if (twr_tag_humidity_get_humidity_percentage(self, &humidity))
        {
            twr_data_stream_feed(&humidity_stream, &humidity);

            twr_radio_pub_humidity(TWR_RADIO_PUB_CHANNEL_R3_I2C0_ADDRESS_DEFAULT, &humidity);
        }
    }

    twr_scheduler_plan_now(APPLICATION_TASK_ID);
}

void voc_tag_event_handler(twr_tag_voc_t *self, twr_tag_voc_event_t event, void *event_param)
{
    tvoc = NAN;

    if (event == TWR_TAG_VOC_EVENT_UPDATE)
    {
        uint16_t value;

        if (twr_tag_voc_get_tvoc_ppb(&tag_voc, &value))
        {
            tvoc = value;
            int radio_tvoc = value;
            twr_radio_pub_int("voc-sensor/0:0/tvoc", &radio_tvoc);
        }

        twr_data_stream_feed(&tvoc_stream, &tvoc);
    }

    twr_scheduler_plan_now(APPLICATION_TASK_ID);
}

void application_init(void)
{
    // Initialize LED
    twr_led_init(&led, TWR_GPIO_LED, false, false);
    twr_led_set_mode(&led, TWR_LED_MODE_OFF);

    // Initialize button
    twr_button_init(&button, TWR_GPIO_BUTTON, TWR_GPIO_PULL_DOWN, false);
    twr_button_set_event_handler(&button, button_event_handler, NULL);

    // Initialize battery
    twr_module_battery_init();
    twr_module_battery_set_event_handler(battery_event_handler, NULL);
    twr_module_battery_set_update_interval(BATTERY_UPDATE_INTERVAL);

    twr_tag_voc_init(&tag_voc, TWR_I2C_I2C0);
    twr_tag_voc_set_event_handler(&tag_voc, voc_tag_event_handler, NULL);
    twr_tag_voc_set_update_interval(&tag_voc, VOC_TAG_UPDATE_INTERVAL);

    twr_tmp112_init(&tmp112, TWR_I2C_I2C0, 0x49);

    twr_tag_temperature_init(&tag_temperature, TWR_I2C_I2C0, TWR_TAG_TEMPERATURE_I2C_ADDRESS_DEFAULT);
    twr_tag_temperature_set_event_handler(&tag_temperature, temperature_tag_event_handler, NULL);
    twr_tag_temperature_set_update_interval(&tag_temperature, TEMPERATURE_TAG_UPDATE_INTERVAL);

    twr_tag_humidity_init(&tag_humidity, HUMIDITY_TAG_REVISION, TWR_I2C_I2C0, TWR_TAG_HUMIDITY_I2C_ADDRESS_DEFAULT);
    twr_tag_humidity_set_update_interval(&tag_humidity, HUMIDITY_TAG_UPDATE_INTERVAL);
    twr_tag_humidity_set_event_handler(&tag_humidity, humidity_tag_event_handler, NULL);

    twr_data_stream_init(&tvoc_stream, 1, &tvoc_stream_buffer);
    twr_data_stream_init(&temperature_stream, 1, &temperature_stream_buffer);
    twr_data_stream_init(&humidity_stream, 1, &humidity_stream_buffer);

    twr_radio_init(TWR_RADIO_MODE_NODE_SLEEPING);
    twr_radio_pairing_request("voc-sensor", VERSION);

    twr_module_lcd_init();
    gfx = twr_module_lcd_get_gfx();

    twr_led_pulse(&led, 2000);
}

void graph(twr_gfx_t *gfx, int x0, int y0, int x1, int y1, twr_data_stream_t *data_stream, int time_step, const char *format)
{
    int w, h;
    char str[32];
    int width = x1 - x0;
    int height = y1 - y0;
    float max_value = 0;
    float min_value = 0;

    twr_data_stream_get_max(data_stream, &max_value);

    twr_data_stream_get_min(data_stream, &min_value);

    if (min_value > 0)
    {
        min_value = 0;
    }

    max_value = ceilf(max_value / 5) * 5;

    twr_module_lcd_set_font(&twr_font_ubuntu_11);

    int number_of_samples = twr_data_stream_get_number_of_samples(data_stream);

    int end_time = - number_of_samples * time_step / 1000;

    h = 10;

    float range = fabsf(max_value) + fabsf(min_value);
    float fh = height - h - 2;

    snprintf(str, sizeof(str), "%ds", end_time);
    w = twr_gfx_calc_string_width(gfx, str) + 8;

    int lines = width / w;
    int y_time = y1 - h - 2;
    int y_zero = range > 0 ? y_time - ((fabsf(min_value) / range) * fh) : y_time;
    int tmp;

    for (int i = 0, time_step = end_time / lines, w_step = width / lines; i < lines; i++)
    {
        snprintf(str, sizeof(str), "%ds", time_step * i);

        w = twr_gfx_calc_string_width(gfx, str);

        tmp = width - w_step * i;

        twr_gfx_draw_string(gfx, tmp - w, y1 - h, str, 1);

        twr_gfx_draw_line(gfx, tmp - 2, y_zero - 2, tmp - 2, y_zero + 2, 1);

        twr_gfx_draw_line(gfx, tmp - 2, y0, tmp - 2, y0 + 2, 1);

        if (y_time != y_zero)
        {
            twr_gfx_draw_line(gfx, tmp - 2, y_time - 2, tmp - 2, y_time, 1);
        }
    }

    twr_gfx_draw_line(gfx, x0, y_zero, x1, y_zero, 1);

    if (y_time != y_zero)
    {
        twr_gfx_draw_line(gfx, x0, y_time, y1, y_time, 1);

        snprintf(str, sizeof(str), format, min_value);

        twr_gfx_draw_string(gfx, x0, y_time - 10, str, 1);
    }

    twr_gfx_draw_line(gfx, x0, y0, x1, y0, 1);

    snprintf(str, sizeof(str), format, max_value);

    twr_gfx_draw_string(gfx, x0, y0, str, 1);

    twr_gfx_draw_string(gfx, x0, y_zero - 10, "0", 1);

    if (range == 0)
    {
        return;
    }

    int length = twr_data_stream_get_length(data_stream);
    float value;

    int x_zero = x1 - 2;
    float fy;

    int dx = width / (number_of_samples - 1);
    int point_x = x_zero + dx;
    int point_y;
    int last_x;
    int last_y;

    min_value = fabsf(min_value);

    for (int i = 1; i <= length; i++)
    {
        if (twr_data_stream_get_nth(data_stream, -i, &value))
        {
            fy = (value + min_value) / range;

            point_y = y_time - (fy * fh);
            point_x -= dx;

            if (i == 1)
            {
                last_y = point_y;
                last_x = point_x;
            }

            twr_gfx_draw_line(gfx, point_x, point_y, last_x, last_y, 1);

            last_y = point_y;
            last_x = point_x;

        }
    }
}

void application_task(void)
{
    if (!twr_gfx_display_is_ready(gfx))
    {
        return;
    }

    twr_system_pll_enable();

    twr_gfx_clear(gfx);

    if (page)
    {

        int w;

        twr_module_lcd_set_font(&twr_font_ubuntu_15);
        twr_gfx_printf(gfx, 5, 1, 1, "%.1f" "\xb0" "C", temperature);
        twr_gfx_printf(gfx, 80, 1, 1, "%.1f %%", humidity);

        twr_gfx_set_font(gfx, &twr_font_ubuntu_15);
        w = twr_gfx_draw_string(gfx, 5, 23, "TVOC", 1);

        twr_gfx_set_font(gfx, &twr_font_ubuntu_28);
        w = twr_gfx_printf(gfx, w + 5, 15, 1, "%.0f", tvoc);

        twr_gfx_set_font(gfx, &twr_font_ubuntu_15);
        twr_gfx_draw_string(gfx, w + 5, 23, "ppb", 1);

        graph(gfx, 0, 50, 127, 127, &tvoc_stream, VOC_TAG_UPDATE_INTERVAL, "%.0f");

    }
    else
    {
        graph(gfx, 0, 0, 127, 63, &temperature_stream, TEMPERATURE_TAG_UPDATE_INTERVAL, "%.0f" "\xb0" "C");

        graph(gfx, 0, 64, 127, 127, &humidity_stream, HUMIDITY_TAG_UPDATE_INTERVAL, "%.0f%%");
    }

    twr_gfx_update(gfx);

    twr_system_pll_disable();
}
