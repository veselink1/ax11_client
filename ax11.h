#ifndef AX11_LIB
#define AX11_LIB
#include <stdint.h>

typedef struct ax11_image
{
    int16_t format;
    int16_t depth;
    int16_t byte_order;
    int16_t bits_per_pixel;
    int16_t bytes_per_line;
    int16_t width;
    int16_t height;
    void* data;
    void* handle;
} ax11_image;

int32_t ax11_connect(const char* display, void** out_connection);

int32_t ax11_get_image(void* con, ax11_image* out_image);

int32_t ax11_free_image(ax11_image* image);

int32_t ax11_fake_key_event(void* con, int32_t key_sym, int32_t is_press, int32_t delay);

int32_t ax11_fake_button_event(void* con, int32_t button, int32_t is_press, int32_t delay);

int32_t ax11_fake_motion_event(void* con, int32_t x, int32_t y, int32_t delay);

#endif // AX11_LIB