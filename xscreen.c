#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <limits.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/Xlibint.h>
#include <X11/Xproto.h>
#include <X11/Xlib.h> //-lX11
#include <X11/extensions/XTest.h> // -Xtst
#include <X11/extensions/XShm.h>
#include <X11/Xutil.h>
#include <X11/ImUtil.h>
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

#define AX11_USE_SPLIT_UPDATING 0

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

static int g_supportsXShm = 0;
static int g_supportsVShm = 0;

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

void fetch_ximage_useshm(Display* dis, Screen* scr, Drawable drawable, XImage** pImage)
{
    XShmSegmentInfo shminfo;
    XImage* image = *pImage = XShmCreateImage(dis, DefaultVisual(dis, 0), 24, ZPixmap, NULL, &shminfo, 100, 100);
    
    int shmid = shminfo.shmid = shmget(IPC_PRIVATE,
      image->bytes_per_line * image->height,
      IPC_CREAT | 0777);
    if (shmid < 0) {
        perror("shmget");
        exit(1);
    }

    void* shmaddr = shminfo.shmaddr = image->data = shmat(shminfo.shmid, 0, 0);
    if (shmaddr == (void*)-1) {
        perror("shmat");
        exit(1);
    }
    shminfo.readOnly = False;

    XShmAttach(dis, &shminfo);

    XShmGetImage(dis,
        RootWindow(dis, 0),
        image,
        0,
        0,
        AllPlanes);
}

extern void *_XGetRequest(Display *dpy, CARD8 type, size_t len);

extern Status _XReply(
    Display*	/* dpy */,
    xReply*	/* rep */,
    int		/* extra */,
    Bool	/* discard */
);

#define GetReqSized(name, sz, req) \
	req = (x##name##Req *) _XGetRequest(dpy, X_##name, sz)

#define GetReq(name, req) \
	GetReqSized(name, SIZEOF(x##name##Req), req)

static inline int
Ones(Mask mask)
{
    register Mask y;

    y = (mask >> 1) & 033333333333;
    y = mask - y - ((y >>1) & 033333333333);
    return (((y + (y >> 3)) & 030707070707) % 077);
}

XImage *XUpdateImage(
     register Display *dpy,
     Drawable d,
     int x,
     int y,
     unsigned int width,
     unsigned int height,
     unsigned long plane_mask,
     int format,
     char* data,
     int data_size)	/* either XYPixmap or ZPixmap */
{
	xGetImageReply rep;
	register xGetImageReq *req;
	unsigned long nbytes;
	XImage *image;
	int planes;
	LockDisplay(dpy);
	GetReq (GetImage, req);
	/*
	 * first set up the standard stuff in the request
	 */
	req->drawable = d;
	req->x = x;
	req->y = y;
	req->width = width;
	req->height = height;
	req->planeMask = plane_mask;
	req->format = format;

	if (_XReply (dpy, (xReply *) &rep, 0, xFalse) == 0 ||
	    rep.length == 0) {
		UnlockDisplay(dpy);
		SyncHandle();
		return (XImage *)NULL;
	}

	if (rep.length < (INT_MAX >> 2)) {
	    nbytes = (unsigned long)rep.length << 2;
        if (data_size != nbytes) {
            printf("ERR data size\n");
            //data = NULL;
        }
	} else
	    data = NULL;
	if (!data) {
	    _XEatDataWords(dpy, rep.length);
	    UnlockDisplay(dpy);
	    SyncHandle();
	    return (XImage *) NULL;
	}
        _XReadPad (dpy, data, nbytes);
        if (format == XYPixmap) {
	    image = XCreateImage(dpy, _XVIDtoVisual(dpy, rep.visual),
		Ones (plane_mask &
		    (((unsigned long)0xFFFFFFFF) >> (32 - rep.depth))),
		format, 0, data, width, height, dpy->bitmap_pad, 0);
	    planes = image->depth;
	} else { /* format == ZPixmap */
            image = XCreateImage (dpy, _XVIDtoVisual(dpy, rep.visual),
		rep.depth, ZPixmap, 0, data, width, height,
		    _XGetScanlinePad(dpy, (int) rep.depth), 0);
	    planes = 1;
	}

	if (image) {
        if (planes < 1 || image->height < 1 || image->bytes_per_line < 1 ||
            INT_MAX / image->height <= image->bytes_per_line ||
            INT_MAX / planes <= image->height * image->bytes_per_line ||
            nbytes < planes * image->height * image->bytes_per_line) {
            image = NULL;
        }
	}
	UnlockDisplay(dpy);
	SyncHandle();
	return (image);
}

void fetch_ximage_noshm(Display* dis, Screen* scr, Drawable drawable, XImage** pImage)
{
    static long frameCount = 0;
    frameCount++;

    if (*pImage == NULL) {
        *pImage = XGetImage(dis, drawable, 0, 0, scr->width, scr->height, AllPlanes, ZPixmap);
    }
    else {
        #if AX11_USE_SPLIT_UPDATING
            int mw, mh;
            switch (frameCount % 4) {
                case 0: mw = 0; mh = 0; break;
                case 1: mw = 0; mh = 1; break;
                case 2: mw = 1; mh = 0; break;
                case 3: mw = 1; mh = 1; break;
            }
            int w = scr->width / 2 * mw;
            int h = scr->height / 2 * mh;
            *pImage = XGetSubImage(dis, drawable, w, h, scr->width / 2, scr->height / 2, AllPlanes, ZPixmap, *pImage, w, h);
        #else
            *pImage = XUpdateImage(dis, drawable, 0, 0, scr->width, scr->height, AllPlanes, ZPixmap, (*pImage)->data, 0xDEADBEEF);
            //*pImage = XGetSubImage(dis, drawable, 0, 0, scr->width, scr->height, AllPlanes, ZPixmap, *pImage, 0, 0);
        #endif
        if (*pImage == NULL) {
            fprintf(stderr, "Failed to reuse image buffer!\n");
            fetch_ximage_noshm(dis, scr, drawable, pImage);
        }
    }
}

void fetch_ximage(Display* dis, Screen* scr, Drawable drawable, XImage** pImage)
{
    static clock_t captureLast = 0;
    static clock_t captureStart = 0;

    captureStart = clock(); 
    if (g_supportsVShm && g_supportsXShm) {
        fetch_ximage_useshm(dis, scr, drawable, pImage);
    }
    else {
        fetch_ximage_noshm(dis, scr, drawable, pImage);
    }

    clock_t now = clock();
    double captureTime = (double)(now - captureStart) / (double)(CLOCKS_PER_SEC);
    double streamTime = (double)(now - captureLast) / (double)(CLOCKS_PER_SEC);
    printf("Capture FPS: %f | Stream FPS: %f\n", 1.0 / captureTime, 1.0 / streamTime);
    captureLast = now;
}

int handle_refresh_request(int sock, Display* dis, Screen* scr, Drawable drawable, XImage** pImage)
{
    fetch_ximage(dis, scr, drawable, pImage);
    XImage* image = *pImage;
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

    return 0;
}

int handle_fake_key_request(int sock, Display* dis)
{
    ax11_msg_fake_key_event evt = {};
    if (read_all(sock, &evt.header + 1, sizeof(evt) - sizeof(evt.header))) {
        return ECONNRESET;
    }

    KeySym sym = ntohl(evt.key_sym);
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

#define THREAD_PRIORITY_URGENT_DISPLAY (-8)

int main(void)
{
    // Increase thread priority
    setpriority(PRIO_PROCESS, 0, THREAD_PRIORITY_URGENT_DISPLAY);

    Display* dis = XOpenDisplay((char*)0);
    if (!dis) {
        fprintf(stderr, "Failed to open display!\n");
        return -1;
    }

    g_supportsVShm = shmget(IPC_PRIVATE, 1, IPC_CREAT | 0666) >= 0;
    printf("Supports System V shared memory: %d\n", g_supportsVShm);
    if (!g_supportsVShm) {
        perror("shmget");
    }

    g_supportsXShm = XShmQueryExtension(dis);
    printf("Supports MIT-XSHM: %d\n", g_supportsXShm);

    printf("Using XShmGetImage (speedup): %d\n", g_supportsVShm && g_supportsXShm);

    Screen* scr = XDefaultScreenOfDisplay(dis);
    Drawable drawable = XDefaultRootWindow(dis);

    struct sockaddr_in address = create_socket_address(AX11_PORT);
    int server_sock = create_socket_server(&address);

    XImage* image = NULL;

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
                    int err = handle_refresh_request(client_sock, dis, scr, drawable, &image);
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

    XDestroyImage(image);
    XCloseDisplay(dis);
    return 0;
}