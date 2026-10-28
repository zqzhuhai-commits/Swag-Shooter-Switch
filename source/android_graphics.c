#include "android_graphics.h"
#include <switch.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <string.h>
#include <stdlib.h>
/* Single-display backend. A Java Surface is projected onto the default
 * Switch window; Android window objects never cross into Mesa unchanged. */
static unsigned window_refs;
static int valid_window(void *window) { return window==nwindowGetDefault(); }
static void window_acquire(void *window) {
    if (valid_window(window)) __atomic_add_fetch(&window_refs,1,__ATOMIC_RELAXED);
}
static void window_release(void *window) {
    if (!valid_window(window)) return;
    unsigned count=__atomic_load_n(&window_refs,__ATOMIC_RELAXED);
    while (count && !__atomic_compare_exchange_n(&window_refs,&count,count-1,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)) {}
}
static void *window_from_surface(void *env,void *surface) {
    if (!env || !surface) return NULL;
    NWindow *window=nwindowGetDefault(); window_acquire(window); return window;
}
static int window_dimension(void *pointer,int height) {
    if (!valid_window(pointer)) return -22;
    NWindow *window=pointer; mutexLock(&window->mutex);
    int value=height ? window->height : window->width;
    if (!value) value=height ? window->default_height : window->default_width;
    mutexUnlock(&window->mutex); return value;
}
static int window_width(void *window) { return window_dimension(window,0); }
static int window_height(void *window) { return window_dimension(window,1); }
static int window_geometry(void *pointer,int width,int height,int format) {
    if (!valid_window(pointer) || width<0 || height<0 || (!!width != !!height)) return -22;
    NWindow *window=pointer;
    mutexLock(&window->mutex);
    int busy=window->slots_configured!=0;
    unsigned old_format=window->format;
    mutexUnlock(&window->mutex);
    if (format && (unsigned)format!=old_format) return -95;
    if (!width) return 0;
    if (busy) return window_width(window)==width && window_height(window)==height ? 0 : -16;
    return R_SUCCEEDED(nwindowSetDimensions(window,width,height)) ? 0 : -22;
}
static __eglMustCastToProperFunctionPointerType guest_get_proc(const char *name);
typedef struct { const char *name; uintptr_t function; } Binding;
#define GRAPHICS_BIND(name,fn) {name,(uintptr_t)(fn)}
static const Binding bindings[]={
    GRAPHICS_BIND("ANativeWindow_fromSurface",window_from_surface), GRAPHICS_BIND("ANativeWindow_acquire",window_acquire),
    GRAPHICS_BIND("ANativeWindow_release",window_release), GRAPHICS_BIND("ANativeWindow_getWidth",window_width),
    GRAPHICS_BIND("ANativeWindow_getHeight",window_height), GRAPHICS_BIND("ANativeWindow_setBuffersGeometry",window_geometry),
    GRAPHICS_BIND("eglChooseConfig",eglChooseConfig), GRAPHICS_BIND("eglCreateContext",eglCreateContext),
    GRAPHICS_BIND("eglCreatePbufferSurface",eglCreatePbufferSurface), GRAPHICS_BIND("eglCreateWindowSurface",eglCreateWindowSurface),
    GRAPHICS_BIND("eglDestroyContext",eglDestroyContext), GRAPHICS_BIND("eglDestroySurface",eglDestroySurface),
    GRAPHICS_BIND("eglGetConfigAttrib",eglGetConfigAttrib), GRAPHICS_BIND("eglGetCurrentContext",eglGetCurrentContext),
    GRAPHICS_BIND("eglGetCurrentSurface",eglGetCurrentSurface), GRAPHICS_BIND("eglGetDisplay",eglGetDisplay),
    GRAPHICS_BIND("eglGetError",eglGetError), GRAPHICS_BIND("eglGetProcAddress",guest_get_proc),
    GRAPHICS_BIND("eglInitialize",eglInitialize), GRAPHICS_BIND("eglMakeCurrent",eglMakeCurrent),
    GRAPHICS_BIND("eglQueryString",eglQueryString), GRAPHICS_BIND("eglQuerySurface",eglQuerySurface),
    GRAPHICS_BIND("eglSwapBuffers",eglSwapBuffers), GRAPHICS_BIND("eglSwapInterval",eglSwapInterval),
    GRAPHICS_BIND("eglTerminate",eglTerminate), GRAPHICS_BIND("eglBindAPI",eglBindAPI),
    GRAPHICS_BIND("eglGetCurrentDisplay",eglGetCurrentDisplay), GRAPHICS_BIND("eglQueryContext",eglQueryContext)
};
uintptr_t android_graphics_resolve(const char *name) {
    for (size_t i=0;i<sizeof bindings/sizeof *bindings;++i)
        if (!strcmp(name,bindings[i].name)) return bindings[i].function;
    if (name[0]=='g' && name[1]=='l' && name[2]>='A' && name[2]<='Z')
        return (uintptr_t)eglGetProcAddress(name);
    return 0;
}
static __eglMustCastToProperFunctionPointerType guest_get_proc(const char *name) {
    if (!name) return NULL;
    uintptr_t wrapper=android_graphics_resolve(name);
    /* Do not leak Mesa's Android-specific extension entry points. */
    if (wrapper) return (__eglMustCastToProperFunctionPointerType)wrapper;
    if (strncmp(name,"gl",2)) return NULL;
    return eglGetProcAddress(name);
}
#define EGL_CHECK(expression) do { int bad=!!(expression); errors+=bad; fprintf(log,"EGL_CHECK line=%d status=%s expression=%s egl_error=%x\n",__LINE__,bad ? "FAIL" : "PASS",#expression,eglGetError()); } while(0)
int android_graphics_selftest(FILE *log) {
    EGLDisplay display=eglGetCurrentDisplay(); EGLContext saved=eglGetCurrentContext();
    EGLSurface draw=eglGetCurrentSurface(EGL_DRAW),read=eglGetCurrentSurface(EGL_READ);
    EGLContext context=EGL_NO_CONTEXT; EGLSurface surface=EGL_NO_SURFACE;
    int errors=0, made_current=0, readback=0, restored=0, borrowed_surface=0; EGLint config_id=0,count=0; EGLConfig config;
    if (display==EGL_NO_DISPLAY || saved==EGL_NO_CONTEXT) { fprintf(log,"EGL_STEP current_context FAIL display=%p context=%p error=%x\n",display,saved,eglGetError()); return -1; }
    EGL_CHECK(guest_get_proc("ANativeWindow_getWidth")!=(__eglMustCastToProperFunctionPointerType)window_width);
    EGL_CHECK(guest_get_proc("eglGetProcAddress")!=(__eglMustCastToProperFunctionPointerType)guest_get_proc);
    EGL_CHECK(guest_get_proc("glClear")==NULL || guest_get_proc("eglUnknownAndroidExtension")!=NULL);
    EGL_CHECK(window_width(nwindowGetDefault())<=0 || window_height(nwindowGetDefault())<=0);
    EGL_CHECK(!eglQueryContext(display,saved,EGL_CONFIG_ID,&config_id));
    const EGLint choose[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    const EGLint size[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
    const EGLint version[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLBoolean (*choose_config)(EGLDisplay,const EGLint *,EGLConfig *,EGLint,EGLint *)=
        (EGLBoolean (*)(EGLDisplay,const EGLint *,EGLConfig *,EGLint,EGLint *))android_graphics_resolve("eglChooseConfig");
    EGLBoolean selected=choose_config(display,choose,&config,1,&count);
    if (!selected) { ++errors; fprintf(log,"EGL_SELECT_FAIL error=%x\n",eglGetError()); goto done; }
    if (!count) {
        fprintf(log,"EGL_PBUFFER_UNAVAILABLE matching_configs=0 fallback=existing_window_surface pbuffer_support=NOT_VALIDATED\n");
        const EGLint window_config[]={EGL_CONFIG_ID,config_id,EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_NONE};
        if (!choose_config(display,window_config,&config,1,&count) || count!=1) {
            ++errors; fprintf(log,"EGL_WINDOW_CONFIG_FAIL configs=%d error=%x\n",count,eglGetError()); goto done;
        }
        surface=draw; borrowed_surface=1;
    } else {
        surface=eglCreatePbufferSurface(display,config,size);
        fprintf(log,"EGL_STEP pbuffer surface=%p error=%x\n",surface,eglGetError());
    }
    context=eglCreateContext(display,config,EGL_NO_CONTEXT,version);
    fprintf(log,"EGL_STEP context context=%p error=%x\n",context,eglGetError());
    if (surface==EGL_NO_SURFACE || context==EGL_NO_CONTEXT || !eglMakeCurrent(display,surface,surface,context)) { ++errors; fprintf(log,"EGL_STEP create_or_select FAIL line=%d configs=%d surface=%p context=%p error=%x\n",__LINE__,count,surface,context,eglGetError()); goto done; }
    made_current=1;
    glViewport(0,0,16,16); glClearColor(0.25f,0.5f,0.75f,1); glClear(GL_COLOR_BUFFER_BIT);
    unsigned char pixel[4]={0}; glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    EGL_CHECK(abs((int)pixel[0]-64)>3 || abs((int)pixel[1]-128)>3 || abs((int)pixel[2]-191)>3 || pixel[3]!=255 || glGetError()!=GL_NO_ERROR);
    readback=1;
done:
    if (made_current) { restored=eglMakeCurrent(display,draw,read,saved); EGL_CHECK(!restored); }
    if (context!=EGL_NO_CONTEXT) EGL_CHECK(!eglDestroyContext(display,context));
    if (!borrowed_surface && surface!=EGL_NO_SURFACE) EGL_CHECK(!eglDestroySurface(display,surface));
    fprintf(log,"ANDROID_GRAPHICS_DONE errors=%d context_current=%d readback_executed=%d context_restored=%d borrowed_window_surface=%d window_lifecycle=NOT_EXERCISED engine_started=0\n",errors,made_current,readback,restored,borrowed_surface);
    return errors ? -1 : 0;
}

#undef EGL_CHECK
