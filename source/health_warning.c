#include "health_warning.h"
#include "health_warning_policy.h"
#include <switch.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <math.h>

static int published_health=-1;
static EGLContext owner;
static GLuint program,vao;
static GLint strength_uniform;
static unsigned opacity;
static u64 last_tick;
static int failed;
int health_warning_pending(void) {
 int health=__atomic_load_n(&published_health,__ATOMIC_ACQUIRE);
 int pending=opacity || (health>=0 && health<300);
 if(!pending)last_tick=armGetSystemTick();
 return pending;
}
void health_warning_context_lost(void *context) {
 if(owner==(EGLContext)context) {owner=EGL_NO_CONTEXT;program=vao=0;failed=0;}
}
void health_warning_set(int health_permille) {
 if(health_permille>1000)health_permille=1000;
 __atomic_store_n(&published_health,health_permille,__ATOMIC_RELEASE);
}
static GLuint compile(GLenum type,const char *text) {
 GLuint shader=glCreateShader(type);if(!shader)return 0;
 glShaderSource(shader,1,&text,NULL);glCompileShader(shader);
 GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
 if(!ok) {glDeleteShader(shader);return 0;}
 return shader;
}
static int prepare(void) {
 const char *vertex="#version 300 es\n"
 "out vec2 position;void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));position=p*2.0-1.0;gl_Position=vec4(position,0.0,1.0);}";
 const char *fragment="#version 300 es\nprecision mediump float;"
 "in vec2 position;uniform float strength;out vec4 color;void main(){"
 "vec2 edge=smoothstep(vec2(0.35),vec2(1.0),abs(position));"
 "float mask=1.0-(1.0-edge.x)*(1.0-edge.y);"
 "color=vec4(0.65,0.015,0.01,mask*strength);}";
 GLuint v=compile(GL_VERTEX_SHADER,vertex),f=compile(GL_FRAGMENT_SHADER,fragment);
 if(!v || !f) {if(v)glDeleteShader(v);if(f)glDeleteShader(f);return -1;}
 program=glCreateProgram();
 if(!program) {glDeleteShader(v);glDeleteShader(f);return -1;}
 glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);
 glDeleteShader(v);glDeleteShader(f);GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
 if(!ok) {glDeleteProgram(program);program=0;return -1;}
 strength_uniform=glGetUniformLocation(program,"strength");glGenVertexArrays(1,&vao);
 if(!vao || strength_uniform<0) {if(vao)glDeleteVertexArrays(1,&vao);glDeleteProgram(program);program=vao=0;return -1;}
 return 0;
}
static void toggle(GLenum cap,GLboolean enabled) {if(enabled)glEnable(cap);else glDisable(cap);}
void health_warning_draw(int width,int height) {
 int health=__atomic_load_n(&published_health,__ATOMIC_ACQUIRE);
 u64 now=armGetSystemTick(),freq=armGetSystemTickFreq();
 unsigned elapsed=last_tick ? (unsigned)((now-last_tick)*1000/freq) : 16;last_tick=now;
 if(health<0) {opacity=0;return;}
 opacity=health_warning_fade(opacity,health_warning_alpha((unsigned)health),elapsed);
 if(!opacity || width<=0 || height<=0)return;
 EGLContext context=eglGetCurrentContext();if(context==EGL_NO_CONTEXT)return;
 if(owner!=context) {owner=context;program=vao=0;failed=0;}
 if(failed)return;
 if(!program && prepare()) {failed=1;return;}
 /* No texture/buffer bindings are changed. Restore every state touched here. */
 GLint old_program,old_vao,framebuffer,viewport[4],src_rgb,dst_rgb,src_alpha,dst_alpha,eq_rgb,eq_alpha;
 GLboolean mask[4];
 glGetIntegerv(GL_CURRENT_PROGRAM,&old_program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&old_vao);
 glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&framebuffer);glGetIntegerv(GL_VIEWPORT,viewport);
 glGetIntegerv(GL_BLEND_SRC_RGB,&src_rgb);glGetIntegerv(GL_BLEND_DST_RGB,&dst_rgb);
 glGetIntegerv(GL_BLEND_SRC_ALPHA,&src_alpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&dst_alpha);
 glGetIntegerv(GL_BLEND_EQUATION_RGB,&eq_rgb);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&eq_alpha);
 glGetBooleanv(GL_COLOR_WRITEMASK,mask);
 const GLenum caps[]={GL_DEPTH_TEST,GL_STENCIL_TEST,GL_SCISSOR_TEST,GL_CULL_FACE,GL_RASTERIZER_DISCARD,GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_COVERAGE,GL_BLEND};
 GLboolean enabled[8];for(unsigned i=0;i<8;++i)enabled[i]=glIsEnabled(caps[i]);
 glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);glViewport(0,0,width,height);
 for(unsigned i=0;i<7;++i)glDisable(caps[i]);
 glEnable(GL_BLEND);
 glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
 glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
 /* Exactly one smooth bright/dim cycle per second, never fully off. */
 float cycle=(float)(now%freq)/(float)freq;
 float pulse=0.80f-0.20f*cosf(cycle*6.28318530718f);
 glUseProgram(program);glBindVertexArray(vao);glUniform1f(strength_uniform,(float)opacity/1000.0f*pulse);
 glDrawArrays(GL_TRIANGLES,0,3);
 glBindVertexArray((GLuint)old_vao);glUseProgram((GLuint)old_program);
 glBlendEquationSeparate((GLenum)eq_rgb,(GLenum)eq_alpha);
 glBlendFuncSeparate((GLenum)src_rgb,(GLenum)dst_rgb,(GLenum)src_alpha,(GLenum)dst_alpha);
 glColorMask(mask[0],mask[1],mask[2],mask[3]);for(unsigned i=0;i<8;++i)toggle(caps[i],enabled[i]);
 glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)framebuffer);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
}
