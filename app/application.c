#include <application.h>

#include <bc_gfx.h>

#define VOC_TAG_UPDATE_INTERVAL (5 * 1000)
#define TEMPERATURE_TAG_UPDATE_INTERVAL (5 * 1000)
#define HUMIDITY_TAG_UPDATE_INTERVAL (5 * 1000)
#define BATTERY_UPDATE_INTERVAL (1 * 60 * 1000)
#define APPLICATION_TASK_ID 0
#define VOC_TAG_GRAPH (60 * 1000)
#define TEMPERATURE_TAG_GRAPH (5 * 60 * 1000)
#define HUMIDITY_TAG_GRAPH (5 * 60 * 1000)

#define HUMIDITY_TAG_REVISION BC_TAG_HUMIDITY_REVISION_R3

// LED instance
bc_led_t led;

// Button instance
bc_button_t button;

bc_tmp112_t tmp112;
bc_sgp30_t sgp30;
bc_tag_temperature_t tag_temperature;
bc_tag_humidity_t tag_humidity;

bc_gfx_t *gfx;

float temperature = NAN;
float humidity = NAN;
float tvoc = NAN;

BC_DATA_STREAM_FLOAT_BUFFER(tvoc_stream_buffer, (VOC_TAG_GRAPH / VOC_TAG_UPDATE_INTERVAL))
BC_DATA_STREAM_FLOAT_BUFFER(temperature_stream_buffer, (TEMPERATURE_TAG_GRAPH / TEMPERATURE_TAG_UPDATE_INTERVAL))
BC_DATA_STREAM_FLOAT_BUFFER(humidity_stream_buffer, (HUMIDITY_TAG_GRAPH / HUMIDITY_TAG_UPDATE_INTERVAL))
bc_data_stream_t tvoc_stream;
bc_data_stream_t temperature_stream;
bc_data_stream_t humidity_stream;

bool page = true;

void button_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    if (event == BC_BUTTON_EVENT_PRESS)
    {
        page = !page;

        bc_scheduler_plan_now(APPLICATION_TASK_ID);
    }
}

void battery_event_handler(bc_module_battery_event_t event, void *event_param)
{
    (void) event;
    (void) event_param;

    float voltage;

    if (bc_module_battery_get_voltage(&voltage))
    {
        bc_radio_pub_battery(&voltage);
    }
}

void compensation(void)
{
    static float c_temperature = -100;
    static float c_humidity = -1;

    if (isnan(temperature) || isnan(humidity))
    {
        return;
    }

    if ((fabsf(temperature - c_temperature) < 1.f) && fabsf(humidity - c_humidity) < 5.f )
    {
        return;
    }

    c_temperature = temperature;
    c_humidity = humidity;

    bc_sgp30_set_compensation(&sgp30, &temperature, &humidity);
}

void temperature_tag_event_handler(bc_tag_temperature_t *self, bc_tag_temperature_event_t event, void *event_param)
{
    temperature = NAN;

    if (event == BC_TAG_TEMPERATURE_EVENT_UPDATE)
    {
        if (bc_tag_temperature_get_temperature_celsius(self, &temperature))
        {
            compensation();

            bc_data_stream_feed(&temperature_stream, &temperature);

            bc_radio_pub_temperature(BC_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_DEFAULT, &temperature);
        }
    }

    bc_scheduler_plan_now(APPLICATION_TASK_ID);
}

void humidity_tag_event_handler(bc_tag_humidity_t *self, bc_tag_humidity_event_t event, void *event_param)
{
    humidity = NAN;

    if (event == BC_TAG_HUMIDITY_EVENT_UPDATE)
    {
        if (bc_tag_humidity_get_humidity_percentage(self, &humidity))
        {
            compensation();

            bc_data_stream_feed(&humidity_stream, &humidity);

            bc_radio_pub_humidity(BC_RADIO_PUB_CHANNEL_R3_I2C0_ADDRESS_DEFAULT, &humidity);
        }
    }

    bc_scheduler_plan_now(APPLICATION_TASK_ID);
}

void sgp30_event_handler(bc_sgp30_t *self, bc_sgp30_event_t event, void *event_param)
{
    tvoc = NAN;

    if (event == BC_SGP30_EVENT_UPDATE)
    {
        uint16_t value;

        if (bc_sgp30_get_tvoc_ppb(&sgp30, &value))
        {
            tvoc = value;
            int radio_tvoc = value;
            bc_radio_pub_int("voc-sensor/0:0/tvoc", &radio_tvoc);
        }

        bc_data_stream_feed(&tvoc_stream, &tvoc);
    }

    bc_scheduler_plan_now(APPLICATION_TASK_ID);
}

void application_init(void)
{
    // Initialize LED
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_set_mode(&led, BC_LED_MODE_OFF);

    // Initialize button
    bc_button_init(&button, BC_GPIO_BUTTON, BC_GPIO_PULL_DOWN, false);
    bc_button_set_event_handler(&button, button_event_handler, NULL);
    
    // Initialize battery
    bc_module_battery_init(BC_MODULE_BATTERY_FORMAT_STANDARD);
    bc_module_battery_set_event_handler(battery_event_handler, NULL);
    bc_module_battery_set_update_interval(BATTERY_UPDATE_INTERVAL);

    bc_sgp30_init(&sgp30, BC_I2C_I2C0, 0x58);
    bc_sgp30_set_event_handler(&sgp30, sgp30_event_handler, NULL);
    bc_sgp30_set_update_interval(&sgp30, VOC_TAG_UPDATE_INTERVAL);

    bc_tmp112_init(&tmp112, BC_I2C_I2C0, 0x49);

    bc_tag_temperature_init(&tag_temperature, BC_I2C_I2C0, BC_TAG_TEMPERATURE_I2C_ADDRESS_DEFAULT);
    bc_tag_temperature_set_event_handler(&tag_temperature, temperature_tag_event_handler, NULL);
    bc_tag_temperature_set_update_interval(&tag_temperature, TEMPERATURE_TAG_UPDATE_INTERVAL);

    bc_tag_humidity_init(&tag_humidity, HUMIDITY_TAG_REVISION, BC_I2C_I2C0, BC_TAG_HUMIDITY_I2C_ADDRESS_DEFAULT);
    bc_tag_humidity_set_update_interval(&tag_humidity, HUMIDITY_TAG_UPDATE_INTERVAL);
    bc_tag_humidity_set_event_handler(&tag_humidity, humidity_tag_event_handler, NULL);

    bc_data_stream_init(&tvoc_stream, 1, &tvoc_stream_buffer);
    bc_data_stream_init(&temperature_stream, 1, &temperature_stream_buffer);
    bc_data_stream_init(&humidity_stream, 1, &humidity_stream_buffer);

    bc_radio_init(BC_RADIO_MODE_NODE_SLEEPING);
    bc_radio_pairing_request("wireless-voc-sensor", VERSION);

    bc_module_lcd_init();
    gfx = bc_module_lcd_get_gfx();

    bc_led_pulse(&led, 2000);
}

void graph(bc_gfx_t *gfx, int x0, int y0, int x1, int y1, bc_data_stream_t *data_stream, int time_step, const char *format)
{
    int w, h;
    char str[32];
    int width = x1 - x0;
    int height = y1 - y0;
    float max_value = 0;
    float min_value = 0;

    bc_data_stream_get_max(data_stream, &max_value);

    bc_data_stream_get_min(data_stream, &min_value);

    if (min_value > 0)
    {
        min_value = 0;
    }

    max_value = ceilf(max_value / 5) * 5;

    bc_module_lcd_set_font(&bc_font_ubuntu_11);

    int number_of_samples = bc_data_stream_get_number_of_samples(data_stream);

    int end_time = - number_of_samples * time_step / 1000;

    h = 10;

    float range = fabsf(max_value) + fabsf(min_value);
    float fh = height - h - 2;

    snprintf(str, sizeof(str), "%ds", end_time);
    w = bc_gfx_calc_string_width(gfx, str) + 8;

    int lines = width / w;
    int y_time = y1 - h - 2;
    int y_zero = range > 0 ? y_time - ((fabsf(min_value) / range) * fh) : y_time;
    int tmp;

    for (int i = 0, time_step = end_time / lines, w_step = width / lines; i < lines; i++)
    {
        snprintf(str, sizeof(str), "%ds", time_step * i);

        w = bc_gfx_calc_string_width(gfx, str);

        tmp = width - w_step * i;

        bc_gfx_draw_string(gfx, tmp - w, y1 - h, str, 1);

        bc_gfx_draw_line(gfx, tmp - 2, y_zero - 2, tmp - 2, y_zero + 2, 1);

        bc_gfx_draw_line(gfx, tmp - 2, y0, tmp - 2, y0 + 2, 1);

        if (y_time != y_zero)
        {
            bc_gfx_draw_line(gfx, tmp - 2, y_time - 2, tmp - 2, y_time, 1);
        }
    }

    bc_gfx_draw_line(gfx, x0, y_zero, x1, y_zero, 1);

    if (y_time != y_zero)
    {
        bc_gfx_draw_line(gfx, x0, y_time, y1, y_time, 1);

        snprintf(str, sizeof(str), format, min_value);

        bc_gfx_draw_string(gfx, x0, y_time - 10, str, 1);
    }

    bc_gfx_draw_line(gfx, x0, y0, x1, y0, 1);

    snprintf(str, sizeof(str), format, max_value);

    bc_gfx_draw_string(gfx, x0, y0, str, 1);

    bc_gfx_draw_string(gfx, x0, y_zero - 10, "0", 1);

    if (range == 0)
    {
        return;
    }

    int length = bc_data_stream_get_length(data_stream);
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
        if (bc_data_stream_get_nth(data_stream, -i, &value))
        {
            fy = (value + min_value) / range;

            point_y = y_time - (fy * fh);
            point_x -= dx;

            if (i == 1)
            {
                last_y = point_y;
                last_x = point_x;
            }

            bc_gfx_draw_line(gfx, point_x, point_y, last_x, last_y, 1);

            last_y = point_y;
            last_x = point_x;

        }
    }
}

void application_task(void)
{
    if (!bc_gfx_display_is_ready(gfx))
    {
        return;
    }

    bc_system_pll_enable();

    bc_gfx_clear(gfx);

    if (page)
    {

        int w;

        bc_module_lcd_set_font(&bc_font_ubuntu_15);
        bc_gfx_printf(gfx, 5, 1, 1, "%.1f" "\xb0" "C", temperature);
        bc_gfx_printf(gfx, 80, 1, 1, "%.1f %%", humidity);

        bc_gfx_set_font(gfx, &bc_font_ubuntu_15);
        w = bc_gfx_draw_string(gfx, 5, 23, "TVOC", 1);

        bc_gfx_set_font(gfx, &bc_font_ubuntu_28);
        w = bc_gfx_printf(gfx, w + 5, 15, 1, "%.0f", tvoc);

        bc_gfx_set_font(gfx, &bc_font_ubuntu_15);
        bc_gfx_draw_string(gfx, w + 5, 23, "ppb", 1);

        graph(gfx, 0, 50, 127, 127, &tvoc_stream, VOC_TAG_UPDATE_INTERVAL, "%.0f");

    }
    else
    {
        graph(gfx, 0, 0, 127, 63, &temperature_stream, TEMPERATURE_TAG_UPDATE_INTERVAL, "%.0f" "\xb0" "C");

        graph(gfx, 0, 64, 127, 127, &humidity_stream, HUMIDITY_TAG_UPDATE_INTERVAL, "%.0f%%");
    }

    bc_gfx_update(gfx);

    bc_system_pll_disable();
}
