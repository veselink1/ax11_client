#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h> // error codes
#include <sys/resource.h> // setpriority
#include <sys/shm.h> // shmget, etc.
#include <netinet/in.h> // IPPROTO_TCP
#include <netinet/tcp.h> // TCP_NODELAY
#include <sys/socket.h> // accept, etc.
#include <xcb/xcb.h>
#include <xcb/xcb_image.h> // xcb_image_t
#include <xcb/shm.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xtest.h>
#include "ax11.h"

#define AX11_OK 0

#define AX11_PORT 28384

#define AX11_MSG_FRAMEBUFFER 1
#define AX11_MSG_REQUEST_REFRESH 2
#define AX11_MSG_FAKE_KEY_EVENT 3
#define AX11_MSG_FAKE_BUTTON_EVENT 4
#define AX11_MSG_FAKE_MOTION_EVENT 5

#define AX11_PACK_STRUCT __attribute__((__packed__))

#define MIT_SHM_EXTENSION_NAME "MIT-SHM"
#define THREAD_PRIORITY_URGENT_DISPLAY (-8)

typedef struct AX11_PACK_STRUCT ax11_msg
{
    int32_t type;
    int32_t size;
} ax11_msg;

typedef struct AX11_PACK_STRUCT ax11_msg_framebuffer
{
    ax11_msg header;
    int16_t format;
    int16_t depth;
    int16_t byte_order;
    int16_t bits_per_pixel;
    int16_t bytes_per_line;
    int16_t width;
    int16_t height;
} ax11_msg_framebuffer;

typedef struct AX11_PACK_STRUCT ax11_msg_fake_key_event
{
    ax11_msg header;
    int32_t key_sym;
    int16_t is_press;
    int32_t delay;
} ax11_msg_fake_key_event;

typedef struct AX11_PACK_STRUCT ax11_msg_fake_button_event
{
    ax11_msg header;
    int16_t button;
    int16_t is_press;
    int32_t delay;
} ax11_msg_fake_button_event;

typedef struct AX11_PACK_STRUCT ax11_msg_fake_motion_event
{
    ax11_msg header;
    int16_t x;
    int16_t y;
    int32_t delay;
} ax11_msg_fake_motion_event;

static int g_supportsMitShm = 0;
static int g_supportsVShm = 0;
static xcb_key_symbols_t* g_syms = NULL;

int create_socket_server(struct sockaddr_in* address)
{
    int server_sock; 
    int opt = 1; 

    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) == 0) 
    { 
        perror("socket failed"); 
        exit(EXIT_FAILURE); 
    }

    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) 
    { 
        perror("setsockopt"); 
        exit(EXIT_FAILURE); 
    }
       
    if (bind(server_sock, (struct sockaddr*)address, sizeof(*address)) < 0) 
    { 
        perror("bind failed"); 
        exit(EXIT_FAILURE); 
    } 

    printf("Successfully bound to port %d.\n", ntohs(address->sin_port));
    if (listen(server_sock, 3) < 0) 
    { 
        perror("listen"); 
        exit(EXIT_FAILURE); 
    }

    printf("Listening for connections...\n");
    return server_sock;
}

struct sockaddr_in create_socket_address(int port)
{
    struct sockaddr_in address; 
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    return address;
}

int read_all(int sock, void* dest, int size) 
{
    int bytes = recv(sock, dest, size, MSG_WAITALL);
    if (bytes != size)
        return ECONNRESET;
    return 0;
}

void get_image_no_shmem(xcb_connection_t* con, xcb_screen_t* scr, xcb_image_t** pImage)
{
    xcb_image_format_t format = XCB_IMAGE_FORMAT_Z_PIXMAP;    
    xcb_window_t window = scr->root;

    xcb_get_geometry_cookie_t gcookie = xcb_get_geometry(con, window);
    xcb_get_geometry_reply_t* greply = xcb_get_geometry_reply(con, gcookie, NULL);

    assert(greply->x == 0);
    assert(greply->y == 0);

    xcb_get_image_cookie_t cookie = xcb_get_image(con, format, window, 0, 0, 
                                                greply->width, greply->height, ~0);

    xcb_get_image_reply_t* reply = xcb_get_image_reply(con, cookie, NULL);
    void* data = xcb_get_image_data(reply);

    if (*pImage) {
        printf("Freeing previous image %p\n", (*pImage)->base);
        xcb_image_destroy(*pImage);
    }

    *pImage = xcb_image_create_native(con, greply->width, greply->height, format, 
                                   reply->depth, NULL, ~0, data);

    (*pImage)->base = reply;
    assert((*pImage)->data == data);
}

void get_image(xcb_connection_t* con, xcb_screen_t* scr, xcb_image_t** pImage)
{
    static clock_t captureLast = 0;
    static clock_t captureStart = 0;

    captureStart = clock(); 
    if (g_supportsVShm && g_supportsMitShm) {
		// TODO	
        printf("MIT-SHM Not implemented\n");
        exit(1);
	}
	else {
        get_image_no_shmem(con, scr, pImage);
	}

    clock_t now = clock();
    double captureTime = (double)(now - captureStart) / (double)(CLOCKS_PER_SEC);
    double streamTime = (double)(now - captureLast) / (double)(CLOCKS_PER_SEC);
    printf("Capture FPS: %f | Stream FPS: %f\n", 1.0 / captureTime, 1.0 / streamTime);
    captureLast = now;
}

/** BEGIN LIBRARY API */

int32_t ax11_connect(const char* display, void** out_connection)
{
    xcb_connection_t* con = xcb_connect(display, NULL);
    if (xcb_connection_has_error(con))
	{
        *out_connection = NULL;
        return EAGAIN;
	}

    *out_connection = con;
    return 0;
}

int32_t ax11_get_image(void* con, ax11_image* out_image)
{
    const xcb_setup_t* setup = xcb_get_setup(con);
    xcb_screen_t* scr = xcb_setup_roots_iterator(setup).data;
    xcb_image_format_t format = XCB_IMAGE_FORMAT_Z_PIXMAP;
    xcb_window_t window = scr->root;

    xcb_get_geometry_cookie_t gcookie = xcb_get_geometry(con, window);
    xcb_get_geometry_reply_t* greply = xcb_get_geometry_reply(con, gcookie, NULL);

    assert(greply->x == 0);
    assert(greply->y == 0);

    xcb_get_image_cookie_t cookie = xcb_get_image(con, format, window, 0, 0, 
                                                greply->width, greply->height, ~0);

    xcb_get_image_reply_t* reply = xcb_get_image_reply(con, cookie, NULL);
    void* data = xcb_get_image_data(reply);

    *out_image = (ax11_image){
        format,
        greply->depth,
        setup->image_byte_order,
        32,
        4 * greply->width,
        greply->width,
        greply->height,
        data,
        reply,
    };
    return 0;
}

int32_t ax11_free_image(ax11_image* image)
{
    free(image->handle);
    return 0;
}

int32_t ax11_fake_key_event(void* con, int32_t key_sym, int32_t is_press, int32_t delay)
{
    xcb_keysym_t keysym = key_sym;
    xcb_keycode_t* keycodes = xcb_key_symbols_get_keycode(g_syms, keysym);
    xcb_keycode_t keycode = keycodes[0];
    free(keycodes);

    if (keycode == XCB_NO_SYMBOL) {
        return EBADMSG;
    }

    uint8_t type = is_press ? XCB_KEY_PRESS : XCB_KEY_RELEASE;
    xcb_test_fake_input(con, type, keycode, XCB_CURRENT_TIME + delay, XCB_NONE, 0, 0, 0);
    xcb_flush(con);
    return 0;
}

int32_t ax11_fake_button_event(void* con, int32_t button, int32_t is_press, int32_t delay)
{
    uint8_t type = is_press ? XCB_BUTTON_PRESS : XCB_BUTTON_RELEASE;
    xcb_test_fake_input(con, type, button, XCB_CURRENT_TIME + delay, XCB_NONE, 0, 0, 0);
    xcb_flush(con);
    return 0;
}

int32_t ax11_fake_motion_event(void* con, int32_t x, int32_t y, int32_t delay)
{
    xcb_test_fake_input(con, XCB_MOTION_NOTIFY, 0, XCB_CURRENT_TIME + delay, XCB_NONE, x, y, 0);
    xcb_flush(con);
    return 0;
}

/** END_LIBRARY_API */

int handle_refresh_request(int sock, xcb_connection_t* con, xcb_screen_t* scr, xcb_image_t** pImage)
{
    get_image(con, scr, pImage);
    xcb_image_t* image = *pImage;
    if (!image) {
        return EAGAIN;
    }

    int image_size = image->size;

    ax11_msg_framebuffer resp = {
        {
            htonl(AX11_MSG_FRAMEBUFFER),
            htonl(image_size + sizeof(resp) - sizeof(ax11_msg)),
        },
        htons(image->format),
        htons(image->depth),
        htons(image->byte_order),
        htons(image->bpp),
        htons(image->stride),
        htons(image->width),
        htons(image->height),
    };
    
    send(sock, &resp, sizeof(resp), 0);
    send(sock, image->data, image_size, 0);

    return 0;
}

int handle_fake_key_request(int sock, xcb_connection_t* con)
{
    ax11_msg_fake_key_event evt = {};
    if (read_all(sock, &evt.header + 1, sizeof(evt) - sizeof(evt.header))) {
        return ECONNRESET;
    }

    xcb_keysym_t keysym = ntohl(evt.key_sym);
    xcb_keycode_t* keycodes = xcb_key_symbols_get_keycode(g_syms, keysym);
    xcb_keycode_t keycode = keycodes[0];
    free(keycodes);

    if (keycode == XCB_NO_SYMBOL) {
        fprintf(stderr, "Missing keycode!\n");
        return EBADMSG;
    }

    uint8_t type = evt.is_press ? XCB_KEY_PRESS : XCB_KEY_RELEASE;
    uint32_t delay = XCB_CURRENT_TIME + ntohl(evt.delay);
    xcb_test_fake_input(con, type, keycode, delay, XCB_NONE, 0, 0, 0);
    
    return 0;
}

int handle_fake_button_request(int sock, xcb_connection_t* con)
{
    ax11_msg_fake_button_event evt = {};
    if (read_all(sock, &evt.header + 1, sizeof(evt) - sizeof(evt.header))) {
        return ECONNRESET;
    }

    uint8_t type = evt.is_press ? XCB_BUTTON_PRESS : XCB_BUTTON_RELEASE;
    uint32_t delay = XCB_CURRENT_TIME + ntohl(evt.delay);
    xcb_test_fake_input(con, type, ntohs(evt.button), delay, XCB_NONE, 0, 0, 0);
    
    return 0;
}

int handle_fake_motion_request(int sock, xcb_connection_t* con)
{
    ax11_msg_fake_motion_event evt = {};
    if (read_all(sock, &evt.header + 1, sizeof(evt) - sizeof(evt.header))) {
        return ECONNRESET;
    }

    uint32_t delay = XCB_CURRENT_TIME + ntohl(evt.delay);
    xcb_test_fake_input(con, XCB_MOTION_NOTIFY, 0, delay, XCB_NONE, ntohs(evt.x), ntohs(evt.y), 0);
    
    return 0;
}

int check_sysv_shm()
{
    return shmget(IPC_PRIVATE, 1, IPC_CREAT | 0666) >= 0;
}

int check_mit_shm(xcb_connection_t* con)
{
    xcb_query_extension_cookie_t extension_cookie = xcb_query_extension(con,
			strlen(MIT_SHM_EXTENSION_NAME), MIT_SHM_EXTENSION_NAME);
	xcb_query_extension_reply_t* extension_reply = xcb_query_extension_reply(con, extension_cookie, NULL);
	free(extension_reply);
    return extension_reply ? 1 : 0;
}

int main()
{
    void* p;
    ax11_connect(NULL, &p);

	const char* displayEnv = getenv("DISPLAY");
    xcb_connection_t* con = xcb_connect(displayEnv ? displayEnv : ":0.0", NULL);
	if(xcb_connection_has_error(con))
	{
		printf("Cannot open display\n");
		exit(1);
	}

    const char* use_shm = getenv("AX11_SHMEM");
    int check_shm = !use_shm || strcmp(use_shm, "1") == 0;

    if (check_shm) {
        g_supportsVShm = check_sysv_shm();
        printf("Supports System V shared memory: %d\n", g_supportsVShm);
        if (!g_supportsVShm) {
            perror("Caused by shmget");
        }

        g_supportsMitShm = check_mit_shm(con);
        printf("Supports MIT-SHM: %d\n", g_supportsMitShm);
    }
    else {
        printf("System V shared memory disabled by AX11_SHMEM environment variable!\n");
    }

    printf("Using XShmGetImage (speedup): %d\n", g_supportsVShm && g_supportsMitShm);

    struct sockaddr_in address = create_socket_address(AX11_PORT);
    int server_sock = create_socket_server(&address);

    xcb_screen_t* scr = xcb_setup_roots_iterator(xcb_get_setup(con)).data;
    g_syms = xcb_key_symbols_alloc(con);
	xcb_image_t* image = NULL;

    while (1)
    {
        int client_sock;
        socklen_t addrlen = sizeof(address);

        reconnect:
        if ((client_sock = accept(server_sock, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) 
        {
            perror("accept"); 
            exit(EXIT_FAILURE); 
        }

        int buf_size = 16 * 1024 * 1024;
        setsockopt(client_sock, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
        
        int no_delay = 1;
        setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay));
        printf("Connection established!\n");

        while (1)
        {
            ax11_msg msg;
            if (read_all(client_sock, &msg, sizeof(msg))) {
                goto connreset;
            }

            switch (ntohl(msg.type))
            {
                case AX11_MSG_REQUEST_REFRESH:
                    assert(ntohl(msg.size) == 0);
                    int err = handle_refresh_request(client_sock, con, scr, &image);
                    if (err == EAGAIN) {
                        fprintf(stderr, "Failed to capture screen!\n");
                        continue;
                    } else if (err) {
                        goto connreset;
                    }
                    break;
                case AX11_MSG_FAKE_KEY_EVENT:
                    assert(ntohl(msg.size) == sizeof(ax11_msg_fake_key_event) - sizeof(ax11_msg));
                    if (handle_fake_key_request(client_sock, con)) {
                        goto connreset;
                    }
                    break;
                case AX11_MSG_FAKE_BUTTON_EVENT:
                    assert(ntohl(msg.size) == sizeof(ax11_msg_fake_button_event) - sizeof(ax11_msg));
                    if (handle_fake_button_request(client_sock, con)) {
                        goto connreset;
                    }
                    break;
                case AX11_MSG_FAKE_MOTION_EVENT:
                    assert(ntohl(msg.size) == sizeof(ax11_msg_fake_motion_event) - sizeof(ax11_msg));
                    if (handle_fake_motion_request(client_sock, con)) {
                        goto connreset;
                    }
                    break;
                default:
                    fprintf(stderr, "Unknown request type %d! Ignoring...\n", ntohl(msg.type));
                    break;
            }

            continue;
            
            connreset:
            fprintf(stderr, "Connection reset! Reconnecting...\n");
            goto reconnect;
        }
    }

    return 0;
}