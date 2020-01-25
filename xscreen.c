#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <sys/types.h>
#include <X11/Xlib.h> //-lX11
#include <X11/extensions/XTest.h> // -Xtst
#include <X11/Xutil.h>
#include <X11/Xmd.h>
#include <X11/Xatom.h>
#include <errno.h>

#define AX11_OK 0

#define AX11_PORT 28384

#define AX11_MSG_FRAMEBUFFER 1
#define AX11_MSG_REQUEST_REFRESH 2
#define AX11_MSG_FAKE_KEY_EVENT 3
#define AX11_MSG_FAKE_BUTTON_EVENT 4
#define AX11_MSG_FAKE_MOTION_EVENT 5

#define AX11_PACK_STRUCT __attribute__((__packed__))

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
    char string[32];
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
    address.sin_addr.s_addr = INADDR_ANY;
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

int handle_refresh_request(int sock, Display* dis, Screen* scr, Drawable* drawable)
{
    XImage* image = XGetImage(dis, *drawable, 0, 0, scr->width, scr->height, AllPlanes, ZPixmap);
    if (!image) {
        return EAGAIN;
    }
    int image_size = image->height * image->bytes_per_line;

    ax11_msg_framebuffer resp = {
        {
            htonl(AX11_MSG_FRAMEBUFFER),
            htonl(image_size + sizeof(resp) - sizeof(ax11_msg)),
        },
        htons(image->format),
        htons(image->depth),
        htons(image->byte_order),
        htons(image->bits_per_pixel),
        htons(image->bytes_per_line),
        htons(image->width),
        htons(image->height),
    };

    send(sock, &resp, sizeof(resp), 0);
    send(sock, image->data, image_size, 0);

    XDestroyImage(image);
    return 0;
}

int handle_fake_key_request(int sock, Display* dis)
{
    ax11_msg_fake_key_event evt = {};
    if (read_all(sock, &evt.header + 1, sizeof(evt) - sizeof(evt.header))) {
        return ECONNRESET;
    }

    KeySym sym = XStringToKeysym(evt.string);
    KeyCode code = XKeysymToKeycode(dis, sym);
    XTestFakeKeyEvent(dis, code, evt.is_press ? True : False, CurrentTime + ntohl(evt.delay));
    XFlush(dis);
    return 0;
}

int handle_fake_button_request(int sock, Display* dis)
{
    ax11_msg_fake_button_event evt = {};
    if (read_all(sock, &evt.header + 1, sizeof(evt) - sizeof(evt.header))) {
        return ECONNRESET;
    }

    XTestFakeButtonEvent(dis, ntohs(evt.button), evt.is_press ? True : False, CurrentTime + ntohl(evt.delay));
    XFlush(dis);
    return 0;
}

int handle_fake_motion_request(int sock, Display* dis)
{
    ax11_msg_fake_motion_event evt = {};
    if (read_all(sock, &evt.header + 1, sizeof(evt) - sizeof(evt.header))) {
        return ECONNRESET;
    }

    XTestFakeMotionEvent(dis, -1, ntohs(evt.x), ntohs(evt.y), CurrentTime + ntohl(evt.delay));
    XFlush(dis);
    return 0;
}

int main(void)
{
    Display* dis = XOpenDisplay((char*)0);
    if (!dis) {
        fprintf(stderr, "Failed to open display!\n");
        return -1;
    }

    Screen* scr = XDefaultScreenOfDisplay(dis);
    Drawable drawable = XDefaultRootWindow(dis);

    struct sockaddr_in address = create_socket_address(AX11_PORT);
    int server_sock = create_socket_server(&address);

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
                    int err = handle_refresh_request(client_sock, dis, scr, &drawable);
                    if (err == EAGAIN) {
                        fprintf(stderr, "Failed to capture screen!\n");
                        continue;
                    } else if (err) {
                        goto connreset;
                    }
                    break;
                case AX11_MSG_FAKE_KEY_EVENT:
                    assert(ntohl(msg.size) == sizeof(ax11_msg_fake_key_event) - sizeof(ax11_msg));
                    if (handle_fake_key_request(client_sock, dis)) {
                        goto connreset;
                    }
                    break;
                case AX11_MSG_FAKE_BUTTON_EVENT:
                    assert(ntohl(msg.size) == sizeof(ax11_msg_fake_button_event) - sizeof(ax11_msg));
                    if (handle_fake_button_request(client_sock, dis)) {
                        goto connreset;
                    }
                    break;
                case AX11_MSG_FAKE_MOTION_EVENT:
                    assert(ntohl(msg.size) == sizeof(ax11_msg_fake_motion_event) - sizeof(ax11_msg));
                    if (handle_fake_motion_request(client_sock, dis)) {
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

    XCloseDisplay(dis);
    return 0;
}