#include <jni.h>
#include <android/log.h>
#include <GLES2/gl2.h>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace dg {

constexpr float PI = 3.14159265358979323846f;

struct Vec3 { float x, y, z; };

static Vec3 add(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
static Vec3 sub(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
static Vec3 mul(Vec3 a, float s) { return {a.x*s, a.y*s, a.z*s}; }
static float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
static Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
static Vec3 norm(Vec3 a) {
    const float n = std::sqrt(std::max(0.000001f, dot(a,a)));
    return mul(a, 1.0f/n);
}

struct Mat4 { float m[16]{}; };

static Mat4 identity() {
    Mat4 r{}; r.m[0]=r.m[5]=r.m[10]=r.m[15]=1.0f; return r;
}

static Mat4 mulM(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c=0;c<4;c++) for (int row=0;row<4;row++) {
        r.m[c*4+row] = a.m[0*4+row]*b.m[c*4+0]
                     + a.m[1*4+row]*b.m[c*4+1]
                     + a.m[2*4+row]*b.m[c*4+2]
                     + a.m[3*4+row]*b.m[c*4+3];
    }
    return r;
}

static Mat4 perspective(float fovY, float aspect, float zn, float zf) {
    const float f = 1.0f/std::tan(fovY*0.5f);
    Mat4 r{};
    r.m[0]=f/aspect; r.m[5]=f;
    r.m[10]=(zf+zn)/(zn-zf);
    r.m[11]=-1.0f;
    r.m[14]=(2.0f*zf*zn)/(zn-zf);
    return r;
}

static Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
    const Vec3 f = norm(sub(center,eye));
    const Vec3 s = norm(cross(f,up));
    const Vec3 u = cross(s,f);
    Mat4 r = identity();
    r.m[0]=s.x; r.m[4]=s.y; r.m[8]=s.z;
    r.m[1]=u.x; r.m[5]=u.y; r.m[9]=u.z;
    r.m[2]=-f.x; r.m[6]=-f.y; r.m[10]=-f.z;
    r.m[12]=-dot(s,eye); r.m[13]=-dot(u,eye); r.m[14]=dot(f,eye);
    return r;
}

static Mat4 translate(Vec3 p) {
    Mat4 r=identity(); r.m[12]=p.x; r.m[13]=p.y; r.m[14]=p.z; return r;
}
static Mat4 scale(Vec3 s) {
    Mat4 r{}; r.m[0]=s.x; r.m[5]=s.y; r.m[10]=s.z; r.m[15]=1.0f; return r;
}
static Mat4 rotateY(float a) {
    Mat4 r=identity(); const float c=std::cos(a), s=std::sin(a);
    r.m[0]=c; r.m[2]=s; r.m[8]=-s; r.m[10]=c; return r;
}

static GLuint program = 0;
static GLint uMvp=-1;
static GLint uColor=-1;
static GLuint cubeVbo=0;
static GLuint gridVbo=0;
static int viewportW=1, viewportH=1;
static float px=0.0f, pz=0.0f, yaw=0.0f;
static float heat=0.0f;
static bool joyActive=false, fireActive=false;
static float joyX=0.0f, joyY=0.0f;
static float enemyX=3.0f, enemyZ=-6.0f;
static float laserT=0.0f;
static double lastTime=0.0;

static double nowSeconds() {
    static double t=0.0;
    t += 1.0/60.0;
    return t;
}

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s=glCreateShader(type); glShaderSource(s,1,&src,nullptr); glCompileShader(s); return s;
}

static void initGL() {
    const char* vs =
        "attribute vec3 aPos; uniform mat4 uMvp;"
        "void main(){ gl_Position=uMvp*vec4(aPos,1.0); }";
    const char* fs =
        "precision mediump float; uniform vec4 uColor;"
        "void main(){ gl_FragColor=uColor; }";
    GLuint v=compileShader(GL_VERTEX_SHADER,vs), f=compileShader(GL_FRAGMENT_SHADER,fs);
    program=glCreateProgram(); glAttachShader(program,v); glAttachShader(program,f);
    glBindAttribLocation(program,0,"aPos"); glLinkProgram(program);
    glDeleteShader(v); glDeleteShader(f);
    uMvp=glGetUniformLocation(program,"uMvp"); uColor=glGetUniformLocation(program,"uColor");

    const float cube[] = {
        -1,-1,-1,  1,-1,-1,  1, 1,-1,  -1, 1,-1,
        -1,-1, 1,  1,-1, 1,  1, 1, 1,  -1, 1, 1
    };
    const uint16_t idx[] = {
        0,1,2, 0,2,3, 4,6,5, 4,7,6,
        0,4,5, 0,5,1, 3,2,6, 3,6,7,
        1,5,6, 1,6,2, 0,3,7, 0,7,4
    };
    glGenBuffers(1,&cubeVbo); glBindBuffer(GL_ARRAY_BUFFER,cubeVbo);
    float expanded[36*3]; int out=0;
    for (int i: idx) { expanded[out++]=cube[i*3]; expanded[out++]=cube[i*3+1]; expanded[out++]=cube[i*3+2]; }
    glBufferData(GL_ARRAY_BUFFER,sizeof(expanded),expanded,GL_STATIC_DRAW);

    float grid[41*41*2]; int k=0;
    constexpr int N=20;
    for(int i=-N;i<=N;i++){ grid[k++]=float(i); grid[k++]=-20; grid[k++]=float(i); grid[k++]=20; }
    for(int z=-N;z<=N;z++){ grid[k++]=-20; grid[k++]=float(z); grid[k++]=20; grid[k++]=float(z); }
    glGenBuffers(1,&gridVbo); glBindBuffer(GL_ARRAY_BUFFER,gridVbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(grid),grid,GL_STATIC_DRAW);

    glUseProgram(program); glEnableVertexAttribArray(0);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDisable(GL_CULL_FACE);
    glClearColor(0.03f,0.04f,0.06f,1.0f);
}

static void drawCube(const Mat4& vp, Vec3 p, Vec3 s, float yawRad, float r,float g,float b) {
    Mat4 model=mulM(translate(p),mulM(rotateY(yawRad),scale(s)));
    Mat4 mvp=mulM(vp,model);
    glBindBuffer(GL_ARRAY_BUFFER,cubeVbo); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,nullptr);
    glUniformMatrix4fv(uMvp,1,GL_FALSE,mvp.m); glUniform4f(uColor,r,g,b,1.0f);
    glDrawArrays(GL_TRIANGLES,0,36);
}

static void drawGrid(const Mat4& vp) {
    Mat4 model=identity(), mvp=mulM(vp,model);
    static float lines[41*41*2*3]; static bool ready=false;
    if(!ready){
        int n=0; for(int i=-20;i<=20;i++){lines[n++]=i;lines[n++]=0;lines[n++]=-20;lines[n++]=i;lines[n++]=0;lines[n++]=20;}
        for(int z=-20;z<=20;z++){lines[n++]=-20;lines[n++]=0;lines[n++]=z;lines[n++]=20;lines[n++]=0;lines[n++]=z;} ready=true;
    }
    glBindBuffer(GL_ARRAY_BUFFER,0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,lines);
    glUniformMatrix4fv(uMvp,1,GL_FALSE,mvp.m); glUniform4f(uColor,0.12f,0.15f,0.18f,1.0f);
    glDrawArrays(GL_LINES,0,41*2*2);
}

static void drawLaser(const Mat4& vp) {
    if(laserT<=0) return;
    const Vec3 start={px,1.5f,pz};
    const Vec3 dir={std::sin(yaw),0,-std::cos(yaw)};
    const Vec3 end=add(start,mul(dir,10.0f));
    float verts[]={start.x,start.y,start.z,end.x,end.y,end.z};
    glBindBuffer(GL_ARRAY_BUFFER,0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,verts);
    glUniformMatrix4fv(uMvp,1,GL_FALSE,vp.m); glUniform4f(uColor,0.2f,0.95f,1.0f,1.0f);
    glLineWidth(4.0f); glDrawArrays(GL_LINES,0,2);
}

static void update(float dt) {
    const float dead=0.15f;
    float mx=(std::fabs(joyX)>dead)?joyX:0.0f;
    float my=(std::fabs(joyY)>dead)?joyY:0.0f;
    const float len=std::sqrt(mx*mx+my*my);
    if(len>1.0f){mx/=len;my/=len;}
    const Vec3 forward={std::sin(yaw),0,-std::cos(yaw)};
    const Vec3 right={std::cos(yaw),0,std::sin(yaw)};
    const Vec3 move=add(mul(forward,-my),mul(right,mx));
    const float speed=3.2f;
    px += move.x*speed*dt; pz += move.z*speed*dt;
    px=std::clamp(px,-18.0f,18.0f); pz=std::clamp(pz,-18.0f,18.0f);
    yaw += move.x*0.015f;

    if(fireActive && heat<0.9f){
        heat += dt*0.75f;
        laserT=0.07f;
        if(heat<0.2f){ laserT=0.12f; }
    } else {
        heat=std::max(0.0f,heat-dt*0.33f);
        laserT=std::max(0.0f,laserT-dt);
    }
}

static void frame() {
    const double t=nowSeconds(); float dt=0.016f;
    if(lastTime>0) dt=float(std::clamp(t-lastTime,0.001,0.033));
    lastTime=t; update(dt);

    glViewport(0,0,viewportW,viewportH);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glUseProgram(program);
    const float aspect=viewportH?float(viewportW)/float(viewportH):1.0f;
    Mat4 proj=perspective(62.0f*PI/180.0f,aspect,0.1f,60.0f);
    Vec3 target={px,0.7f,pz};
    Vec3 eye=add(target,{ -6.5f*std::sin(yaw),4.4f, 6.5f*std::cos(yaw) });
    Mat4 vp=mulM(proj,lookAt(eye,target,{0,1,0}));

    drawGrid(vp);
    drawCube(vp,{px,0.8f,pz},{0.7f,0.8f,0.7f},yaw,0.16f,0.35f,0.58f);
    drawCube(vp,{px,1.7f,pz},{0.45f,0.35f,0.45f},yaw,0.22f,0.45f,0.72f);
    Vec3 cannon=add({px,1.65f,pz},{0,0,-0.9f});
    drawCube(vp,cannon,{0.12f,0.12f,0.75f},yaw,0.35f,0.5f,0.72f);
    drawCube(vp,{enemyX,0.9f,enemyZ},{0.85f,0.9f,0.85f},0.3f,0.52f,0.18f,0.16f);
    drawLaser(vp);

    if(laserT>0){
        const Vec3 dir={std::sin(yaw),0,-std::cos(yaw)};
        const Vec3 end=add({px,1.5f,pz},mul(dir,10.0f));
        const float ex=end.x-enemyX, ez=end.z-enemyZ;
        if(ex*ex+ez*ez<1.7f){ enemyX=6.0f; enemyZ=-8.0f; }
    }
}

static void touch(int action,float x,float y,float w,float h) {
    const bool down = action==0 || action==2;
    if(x < w*0.45f){
        if(down){
            joyActive=true;
            const float baseX=w*0.18f, baseY=h*0.78f, radius=h*0.20f;
            joyX=std::clamp((x-baseX)/radius,-1.0f,1.0f);
            joyY=std::clamp((y-baseY)/radius,-1.0f,1.0f);
        } else { joyActive=false; joyX=joyY=0; }
    } else {
        fireActive=down;
        if(!down) laserT=0.0f;
    }
}

} // namespace dg

extern "C" JNIEXPORT void JNICALL
Java_com_fanmade_dg_MainActivity_00024NativeBridge_init(JNIEnv*, jclass) {
    dg::initGL();
    __android_log_print(ANDROID_LOG_INFO,"DG-0001","Native renderer initialized");
}

extern "C" JNIEXPORT void JNICALL
Java_com_fanmade_dg_MainActivity_00024NativeBridge_resize(JNIEnv*, jclass, jint w, jint h) {
    dg::viewportW=std::max(1,(int)w); dg::viewportH=std::max(1,(int)h);
}

extern "C" JNIEXPORT void JNICALL
Java_com_fanmade_dg_MainActivity_00024NativeBridge_frame(JNIEnv*, jclass) { dg::frame(); }

extern "C" JNIEXPORT void JNICALL
Java_com_fanmade_dg_MainActivity_00024NativeBridge_touch(JNIEnv*, jclass, jint, jint action,
                                                          jfloat x,jfloat y,jint w,jint h) {
    dg::touch(action,x,y,(float)w,(float)h);
}
