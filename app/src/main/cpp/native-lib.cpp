#include <jni.h>
#include <android/log.h>
#include <GLES2/gl2.h>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace dg {

constexpr float PI = 3.14159265358979323846f;
constexpr int ACTION_DOWN = 0;
constexpr int ACTION_UP = 1;
constexpr int ACTION_MOVE = 2;
constexpr int ACTION_CANCEL = 3;
constexpr int ACTION_POINTER_DOWN = 5;
constexpr int ACTION_POINTER_UP = 6;

struct Vec3 { float x, y, z; };

static Vec3 add(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
static Vec3 sub(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
static Vec3 mul(Vec3 a, float s) { return {a.x*s, a.y*s, a.z*s}; }
static float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
static float len(Vec3 a) { return std::sqrt(std::max(0.000001f, dot(a,a))); }
static Vec3 norm(Vec3 a) { return mul(a, 1.0f/len(a)); }
static Vec3 cross(Vec3 a, Vec3 b) {
    return {
        a.y*b.z-a.z*b.y,
        a.z*b.x-a.x*b.z,
        a.x*b.y-a.y*b.x
    };
}

struct Mat4 { float m[16]{}; };

static Mat4 identity() {
    Mat4 r{};
    r.m[0]=r.m[5]=r.m[10]=r.m[15]=1.0f;
    return r;
}

static Mat4 mulM(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c=0;c<4;c++) {
        for (int row=0;row<4;row++) {
            r.m[c*4+row] =
                a.m[0*4+row]*b.m[c*4+0] +
                a.m[1*4+row]*b.m[c*4+1] +
                a.m[2*4+row]*b.m[c*4+2] +
                a.m[3*4+row]*b.m[c*4+3];
        }
    }
    return r;
}

static Mat4 perspective(float fovY, float aspect, float zn, float zf) {
    const float f = 1.0f/std::tan(fovY*0.5f);
    Mat4 r{};
    r.m[0]=f/aspect;
    r.m[5]=f;
    r.m[10]=(zf+zn)/(zn-zf);
    r.m[11]=-1.0f;
    r.m[14]=(2.0f*zf*zn)/(zn-zf);
    return r;
}

static Mat4 ortho(float left, float right, float bottom, float top) {
    Mat4 r=identity();
    r.m[0]=2.0f/(right-left);
    r.m[5]=2.0f/(top-bottom);
    r.m[10]=-1.0f;
    r.m[12]=-(right+left)/(right-left);
    r.m[13]=-(top+bottom)/(top-bottom);
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
    r.m[12]=-dot(s,eye);
    r.m[13]=-dot(u,eye);
    r.m[14]=dot(f,eye);
    return r;
}

static Mat4 translate(Vec3 p) {
    Mat4 r=identity();
    r.m[12]=p.x; r.m[13]=p.y; r.m[14]=p.z;
    return r;
}

static Mat4 scale(Vec3 s) {
    Mat4 r{};
    r.m[0]=s.x; r.m[5]=s.y; r.m[10]=s.z; r.m[15]=1.0f;
    return r;
}

static Mat4 rotateY(float a) {
    Mat4 r=identity();
    const float c=std::cos(a), s=std::sin(a);
    r.m[0]=c; r.m[2]=s;
    r.m[8]=-s; r.m[10]=c;
    return r;
}

static Vec3 localOffset(Vec3 origin, Vec3 offset, float yaw) {
    const float c=std::cos(yaw), s=std::sin(yaw);
    return {
        origin.x + offset.x*c - offset.z*s,
        origin.y + offset.y,
        origin.z + offset.x*s + offset.z*c
    };
}

static GLuint program = 0;
static GLint uMvp=-1;
static GLint uColor=-1;
static GLuint cubeVbo=0;
static int viewportW=1, viewportH=1;

static float px=0.0f, pz=0.0f;
static float yaw=0.0f, aimPitch=0.72f;

static float heat=0.0f;
static float playerHp=100.0f;
static float respawnTimer=0.0f;

static int movePointer=-1;
static int firePointer=-1;
static float joyX=0.0f, joyY=0.0f;
static float lastAimX=0.0f;

static float enemyX=5.0f, enemyZ=-7.0f;
static float enemyYaw=0.0f;
static float enemyHp=40.0f;
static float enemyRespawn=0.0f;
static float enemyAttackTimer=0.7f;

static float playerHitFlash=0.0f;
static float enemyHitFlash=0.0f;
static float laserT=0.0f;
static float enemyLaserT=0.0f;
static float enemyShotDirX=0.0f, enemyShotDirZ=0.0f;
static double lastTime=0.0;

static double nowSeconds() {
    static double t=0.0;
    t += 1.0/60.0;
    return t;
}

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s=glCreateShader(type);
    glShaderSource(s,1,&src,nullptr);
    glCompileShader(s);

    GLint ok=0;
    glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok) {
        char log[512]{};
        glGetShaderInfoLog(s,sizeof(log),nullptr,log);
        __android_log_print(ANDROID_LOG_ERROR,"DG-0002","Shader compile: %s",log);
    }
    return s;
}

static void initGL() {
    const char* vs =
        "attribute vec3 aPos;"
        "uniform mat4 uMvp;"
        "void main(){ gl_Position=uMvp*vec4(aPos,1.0); }";

    const char* fs =
        "precision mediump float;"
        "uniform vec4 uColor;"
        "void main(){ gl_FragColor=uColor; }";

    GLuint v=compileShader(GL_VERTEX_SHADER,vs);
    GLuint f=compileShader(GL_FRAGMENT_SHADER,fs);

    program=glCreateProgram();
    glAttachShader(program,v);
    glAttachShader(program,f);
    glBindAttribLocation(program,0,"aPos");
    glLinkProgram(program);

    GLint linked=0;
    glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if(!linked) {
        char log[512]{};
        glGetProgramInfoLog(program,sizeof(log),nullptr,log);
        __android_log_print(ANDROID_LOG_ERROR,"DG-0002","Program link: %s",log);
    }

    glDeleteShader(v);
    glDeleteShader(f);

    uMvp=glGetUniformLocation(program,"uMvp");
    uColor=glGetUniformLocation(program,"uColor");

    const float cube[] = {
        -1,-1,-1,  1,-1,-1,  1, 1,-1,  -1, 1,-1,
        -1,-1, 1,  1,-1, 1,  1, 1, 1,  -1, 1, 1
    };
    const uint16_t idx[] = {
        0,1,2, 0,2,3, 4,6,5, 4,7,6,
        0,4,5, 0,5,1, 3,2,6, 3,6,7,
        1,5,6, 1,6,2, 0,3,7, 0,7,4
    };

    glGenBuffers(1,&cubeVbo);
    glBindBuffer(GL_ARRAY_BUFFER,cubeVbo);

    float expanded[36*3];
    int out=0;
    for (int i: idx) {
        expanded[out++]=cube[i*3];
        expanded[out++]=cube[i*3+1];
        expanded[out++]=cube[i*3+2];
    }
    glBufferData(GL_ARRAY_BUFFER,sizeof(expanded),expanded,GL_STATIC_DRAW);

    glUseProgram(program);
    glEnableVertexAttribArray(0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.025f,0.035f,0.05f,1.0f);
}

static void drawCube(
        const Mat4& vp,
        Vec3 p,
        Vec3 s,
        float yawRad,
        float r,float g,float b,float a=1.0f) {
    Mat4 model=mulM(
        translate(p),
        mulM(rotateY(yawRad),scale(s))
    );
    Mat4 mvp=mulM(vp,model);

    glBindBuffer(GL_ARRAY_BUFFER,cubeVbo);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,nullptr);
    glUniformMatrix4fv(uMvp,1,GL_FALSE,mvp.m);
    glUniform4f(uColor,r,g,b,a);
    glDrawArrays(GL_TRIANGLES,0,36);
}

static void drawLines(const Mat4& vp, const float* verts, int count,
                      float r,float g,float b,float a=1.0f) {
    glBindBuffer(GL_ARRAY_BUFFER,0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,verts);
    glUniformMatrix4fv(uMvp,1,GL_FALSE,vp.m);
    glUniform4f(uColor,r,g,b,a);
    glDrawArrays(GL_LINES,0,count);
}

static void drawGrid(const Mat4& vp) {
    static float lines[41*2*2*3];
    static bool ready=false;

    if(!ready) {
        int n=0;
        for(int i=-20;i<=20;i++) {
            lines[n++]=float(i); lines[n++]=0.0f; lines[n++]=-20.0f;
            lines[n++]=float(i); lines[n++]=0.0f; lines[n++]=20.0f;
        }
        for(int z=-20;z<=20;z++) {
            lines[n++]=-20.0f; lines[n++]=0.0f; lines[n++]=float(z);
            lines[n++]=20.0f; lines[n++]=0.0f; lines[n++]=float(z);
        }
        ready=true;
    }

    drawLines(vp,lines,41*2*2,0.10f,0.13f,0.16f,1.0f);
}

static void drawArenaBlocks(const Mat4& vp) {
    // Low-cost cover/landmark silhouettes. These become real facilities/terrain later.
    drawCube(vp,{-7.0f,0.65f,-4.0f},{1.4f,0.65f,1.2f},0.0f,0.12f,0.20f,0.26f);
    drawCube(vp,{-6.0f,0.9f,6.0f},{2.0f,0.9f,1.0f},0.08f,0.15f,0.24f,0.20f);
    drawCube(vp,{7.0f,0.8f,5.0f},{1.2f,0.8f,1.2f},-0.24f,0.22f,0.17f,0.12f);
    drawCube(vp,{8.0f,0.5f,-2.5f},{0.8f,0.5f,2.2f},0.5f,0.12f,0.17f,0.22f);
}

static void drawMech(const Mat4& vp) {
    const float bodyR = playerHitFlash>0 ? 0.85f : 0.18f;
    const float bodyG = playerHitFlash>0 ? 0.25f : 0.37f;
    const float bodyB = playerHitFlash>0 ? 0.20f : 0.58f;

    // Torso + head.
    drawCube(vp,{px,0.95f,pz},{0.80f,0.62f,0.60f},yaw,bodyR,bodyG,bodyB);
    drawCube(vp,localOffset({px,0,pz},{0,1.85f,-0.03f},yaw),
             {0.42f,0.34f,0.42f},yaw,0.22f,0.46f,0.70f);

    // Shoulders and arms.
    drawCube(vp,localOffset({px,0,pz},{-1.05f,1.05f,0.0f},yaw),
             {0.30f,0.40f,0.42f},yaw,0.12f,0.28f,0.43f);
    drawCube(vp,localOffset({px,0,pz},{1.05f,1.05f,0.0f},yaw),
             {0.30f,0.40f,0.42f},yaw,0.12f,0.28f,0.43f);
    drawCube(vp,localOffset({px,0,pz},{-1.08f,0.40f,0.0f},yaw),
             {0.28f,0.48f,0.30f},yaw,0.10f,0.23f,0.36f);
    drawCube(vp,localOffset({px,0,pz},{1.08f,0.40f,0.0f},yaw),
             {0.28f,0.48f,0.30f},yaw,0.10f,0.23f,0.36f);

    // Legs.
    drawCube(vp,localOffset({px,0,pz},{-0.40f,-0.10f,0.0f},yaw),
             {0.30f,0.60f,0.38f},yaw,0.12f,0.23f,0.32f);
    drawCube(vp,localOffset({px,0,pz},{0.40f,-0.10f,0.0f},yaw),
             {0.30f,0.60f,0.38f},yaw,0.12f,0.23f,0.32f);

    // Main laser cannon mounted at the front.
    drawCube(vp,localOffset({px,0,pz},{0,1.45f,-0.95f},yaw),
             {0.13f,0.13f,0.85f},yaw,0.38f,0.55f,0.78f);
}

static void drawEnemy(const Mat4& vp) {
    const float r = enemyHitFlash>0 ? 0.95f : 0.55f;
    const float g = enemyHitFlash>0 ? 0.85f : 0.18f;
    const float b = enemyHitFlash>0 ? 0.20f : 0.14f;

    drawCube(vp,{enemyX,0.95f,enemyZ},{0.90f,0.85f,0.90f},enemyYaw,r,g,b);
    drawCube(vp,{enemyX,1.70f,enemyZ},{0.45f,0.35f,0.45f},enemyYaw,0.45f,0.15f,0.12f);
    drawCube(vp,localOffset({enemyX,0,enemyZ},{0,1.40f,-0.95f},enemyYaw),
             {0.18f,0.14f,0.70f},enemyYaw,0.78f,0.25f,0.18f);
}

static void drawLaser(const Mat4& vp) {
    if(laserT<=0.0f) return;

    const Vec3 start=localOffset({px,0,pz},{0,1.45f,-1.70f},yaw);
    const float horiz=std::cos(aimPitch);
    const Vec3 dir={
        std::sin(yaw)*horiz,
        -std::sin(aimPitch),
        -std::cos(yaw)*horiz
    };
    const Vec3 end=add(start,mul(dir,14.0f));

    const float verts[]={
        start.x,start.y,start.z,
        end.x,end.y,end.z
    };
    drawLines(vp,verts,2,0.25f,0.95f,1.0f,0.98f);
}

static void drawEnemyLaser(const Mat4& vp) {
    if(enemyLaserT<=0.0f) return;

    const float targetY=1.20f;
    const Vec3 start={enemyX,1.45f,enemyZ};
    const Vec3 end={px,targetY,pz};
    const float verts[]={
        start.x,start.y,start.z,
        end.x,end.y,end.z
    };
    drawLines(vp,verts,2,1.0f,0.20f,0.12f,0.92f);
}

static void drawRect2D(const Mat4& vp,float x1,float y1,float x2,float y2,
                       float r,float g,float b,float a=1.0f) {
    const float verts[]={
        x1,y1,0, x2,y1,0, x2,y2,0,
        x1,y1,0, x2,y2,0, x1,y2,0
    };
    glBindBuffer(GL_ARRAY_BUFFER,0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,verts);
    glUniformMatrix4fv(uMvp,1,GL_FALSE,vp.m);
    glUniform4f(uColor,r,g,b,a);
    glDrawArrays(GL_TRIANGLES,0,6);
}

static void drawCircle2D(const Mat4& vp,float cx,float cy,float radius,
                         float r,float g,float b,float a=0.85f) {
    constexpr int SEG=32;
    float verts[(SEG+1)*3];
    for(int i=0;i<=SEG;i++) {
        const float t=float(i)/float(SEG)*2.0f*PI;
        verts[i*3+0]=cx+std::cos(t)*radius;
        verts[i*3+1]=cy+std::sin(t)*radius;
        verts[i*3+2]=0;
    }
    glBindBuffer(GL_ARRAY_BUFFER,0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,verts);
    glUniformMatrix4fv(uMvp,1,GL_FALSE,vp.m);
    glUniform4f(uColor,r,g,b,a);
    glDrawArrays(GL_LINE_STRIP,0,SEG+1);
}

static void drawHud() {
    const Mat4 hud=ortho(0,float(viewportW),float(viewportH),0);

    glDisable(GL_DEPTH_TEST);

    // HP and heat bars.
    const float pad=22.0f;
    const float barW=viewportW*0.28f;
    const float barH=18.0f;
    drawRect2D(hud,pad,pad,pad+barW,pad+barH,0.03f,0.04f,0.05f,0.85f);
    drawRect2D(hud,pad,pad,pad+barW*(playerHp/100.0f),pad+barH,0.18f,0.78f,0.30f,0.92f);

    const float heatY=pad+barH+10.0f;
    drawRect2D(hud,pad,heatY,pad+barW,heatY+12.0f,0.03f,0.04f,0.05f,0.85f);
    drawRect2D(hud,pad,heatY,pad+barW*std::min(1.0f,heat),heatY+12.0f,
               0.18f,0.72f,0.95f,0.92f);

    // Simple targeting reticle.
    const float cx=viewportW*0.50f;
    const float cy=viewportH*0.47f;
    drawCircle2D(hud,cx,cy,20.0f,0.6f,0.8f,0.9f,0.70f);

    // Touch guides.
    const float joyBaseX=viewportW*0.18f;
    const float joyBaseY=viewportH*0.78f;
    const float joyR=viewportH*0.20f;
    const float joyKnobX=joyBaseX+joyX*joyR;
    const float joyKnobY=joyBaseY+joyY*joyR;
    drawCircle2D(hud,joyBaseX,joyBaseY,joyR,0.45f,0.58f,0.68f,0.42f);
    drawCircle2D(hud,joyKnobX,joyKnobY,joyR*0.42f,0.65f,0.82f,0.95f,0.70f);

    const float fireX=viewportW*0.84f;
    const float fireY=viewportH*0.78f;
    const float fireR=viewportH*0.13f;
    drawCircle2D(hud,fireX,fireY,fireR,
                 firePointer>=0?0.25f:0.55f,
                 firePointer>=0?0.90f:0.65f,
                 firePointer>=0?1.00f:0.72f,
                 firePointer>=0?0.78f:0.40f);

    // Aim hint: a small line near the fire control shows the active yaw direction.
    const float aimLen=viewportH*0.11f;
    const float ax=fireX+std::sin(yaw)*aimLen;
    const float ay=fireY-std::cos(yaw)*aimLen;
    const float guide[]={
        fireX,fireY,0,
        ax,ay,0
    };
    drawLines(hud,guide,2,0.70f,0.88f,1.0f,0.65f);

    glEnable(GL_DEPTH_TEST);
}

static void updateAim(float x, float y) {
    const float dx=x-lastAimX;
    yaw += dx*0.0075f;
    lastAimX=x;

    // A small amount of vertical aim motion changes the cannon elevation.
    const float centerY=viewportH*0.50f;
    aimPitch += (y-centerY)*0.0008f;
    aimPitch=std::clamp(aimPitch,0.15f,1.15f);
}

static void updateEnemy(float dt) {
    if(enemyRespawn>0.0f) {
        enemyRespawn=std::max(0.0f,enemyRespawn-dt);
        if(enemyRespawn<=0.0f) {
            enemyX=6.5f;
            enemyZ=-8.0f;
            enemyHp=40.0f;
            enemyAttackTimer=0.7f;
        }
        return;
    }

    const float dx=px-enemyX;
    const float dz=pz-enemyZ;
    const float distance=std::sqrt(std::max(0.0001f,dx*dx+dz*dz));

    enemyYaw=std::atan2(dx,-dz);

    if(distance>3.2f) {
        const float speed=1.10f;
        enemyX += (dx/distance)*speed*dt;
        enemyZ += (dz/distance)*speed*dt;
    } else {
        const float orbit=0.55f;
        const float sideX=std::cos(enemyYaw);
        const float sideZ=std::sin(enemyYaw);
        enemyX += sideX*orbit*dt;
        enemyZ += sideZ*orbit*dt;
    }

    enemyAttackTimer-=dt;
    if(enemyAttackTimer<=0.0f && distance<11.0f) {
        enemyAttackTimer=1.10f;
        enemyLaserT=0.12f;
        enemyShotDirX=dx/std::max(0.001f,distance);
        enemyShotDirZ=dz/std::max(0.001f,distance);
        playerHp=std::max(0.0f,playerHp-7.0f);
        playerHitFlash=0.12f;
        if(playerHp<=0.0f) respawnTimer=1.35f;
    }
}

static void update(float dt) {
    if(respawnTimer>0.0f) {
        respawnTimer=std::max(0.0f,respawnTimer-dt);
        heat=std::max(0.0f,heat-dt*0.7f);
        if(respawnTimer<=0.0f) {
            px=0.0f;
            pz=0.0f;
            yaw=0.0f;
            playerHp=100.0f;
            movePointer=-1;
            firePointer=-1;
            joyX=joyY=0.0f;
        }
    } else {
        const float dead=0.15f;
        float mx=(std::fabs(joyX)>dead)?joyX:0.0f;
        float my=(std::fabs(joyY)>dead)?joyY:0.0f;

        const float moveLen=std::sqrt(mx*mx+my*my);
        if(moveLen>1.0f) {
            mx/=moveLen;
            my/=moveLen;
        }

        const Vec3 forward={std::sin(yaw),0,-std::cos(yaw)};
        const Vec3 right={std::cos(yaw),0,std::sin(yaw)};
        const Vec3 move=add(mul(forward,-my),mul(right,mx));

        const float speed=3.6f;
        px += move.x*speed*dt;
        pz += move.z*speed*dt;

        px=std::clamp(px,-18.0f,18.0f);
        pz=std::clamp(pz,-18.0f,18.0f);

        if(firePointer>=0 && heat<0.92f) {
            heat=std::min(1.0f,heat+dt*0.92f);
            laserT=0.08f;
        } else {
            heat=std::max(0.0f,heat-dt*0.42f);
            laserT=std::max(0.0f,laserT-dt);
        }
    }

    updateEnemy(dt);

    enemyLaserT=std::max(0.0f,enemyLaserT-dt);
    playerHitFlash=std::max(0.0f,playerHitFlash-dt);
    enemyHitFlash=std::max(0.0f,enemyHitFlash-dt);

    // Laser hit test: horizontal ray against enemy's small collision sphere.
    if(laserT>0.0f && enemyRespawn<=0.0f && respawnTimer<=0.0f) {
        const Vec3 start=localOffset({px,0,pz},{0,1.45f,-1.70f},yaw);
        const float dirX=std::sin(yaw);
        const float dirZ=-std::cos(yaw);
        const float ex=enemyX-start.x;
        const float ez=enemyZ-start.z;
        const float along=ex*dirX+ez*dirZ;

        if(along>0.0f && along<15.0f) {
            const float closestX=start.x+dirX*along;
            const float closestZ=start.z+dirZ*along;
            const float hx=enemyX-closestX;
            const float hz=enemyZ-closestZ;
            if(hx*hx+hz*hz<1.25f) {
                enemyHp-=18.0f*dt*60.0f;
                enemyHitFlash=0.08f;
                if(enemyHp<=0.0f) {
                    enemyHp=0.0f;
                    enemyRespawn=2.0f;
                    enemyHitFlash=0.35f;
                }
            }
        }
    }
}

static void frame() {
    const double t=nowSeconds();
    float dt=0.016f;

    if(lastTime>0.0) {
        dt=float(std::clamp(t-lastTime,0.001,0.033));
    }
    lastTime=t;
    update(dt);

    glViewport(0,0,viewportW,viewportH);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glUseProgram(program);

    const float aspect=viewportH?float(viewportW)/float(viewportH):1.0f;
    const Mat4 proj=perspective(62.0f*PI/180.0f,aspect,0.1f,60.0f);

    const Vec3 target={px,0.90f,pz};
    const float cameraDistance=6.8f;
    const Vec3 eye={
        px-cameraDistance*std::sin(yaw),
        4.6f,
        pz+cameraDistance*std::cos(yaw)
    };
    const Mat4 vp=mulM(proj,lookAt(eye,target,{0,1,0}));

    drawGrid(vp);
    drawArenaBlocks(vp);
    if(enemyRespawn<=0.0f) drawEnemy(vp);
    drawMech(vp);
    drawLaser(vp);
    drawEnemyLaser(vp);
    drawHud();
}

static void touch(int pointerId,int action,float x,float y,float w,float h) {
    const bool leftZone = x < w*0.45f;

    if(action==ACTION_DOWN || action==ACTION_POINTER_DOWN) {
        if(leftZone && movePointer<0) {
            movePointer=pointerId;
            const float baseX=w*0.18f;
            const float baseY=h*0.78f;
            const float radius=h*0.20f;
            joyX=std::clamp((x-baseX)/radius,-1.0f,1.0f);
            joyY=std::clamp((y-baseY)/radius,-1.0f,1.0f);
        } else if(!leftZone && firePointer<0) {
            firePointer=pointerId;
            lastAimX=x;
        }
        return;
    }

    if(action==ACTION_MOVE) {
        if(pointerId==movePointer) {
            const float baseX=w*0.18f;
            const float baseY=h*0.78f;
            const float radius=h*0.20f;
            joyX=std::clamp((x-baseX)/radius,-1.0f,1.0f);
            joyY=std::clamp((y-baseY)/radius,-1.0f,1.0f);
        } else if(pointerId==firePointer) {
            updateAim(x,y);
        }
        return;
    }

    if(action==ACTION_UP || action==ACTION_POINTER_UP || action==ACTION_CANCEL) {
        if(pointerId==movePointer || action==ACTION_CANCEL) {
            movePointer=-1;
            joyX=0.0f;
            joyY=0.0f;
        }
        if(pointerId==firePointer || action==ACTION_CANCEL) {
            firePointer=-1;
            laserT=0.0f;
        }
    }
}

} // namespace dg

extern "C" JNIEXPORT void JNICALL
Java_com_fanmade_dg_MainActivity_00024NativeBridge_init(JNIEnv*,jclass) {
    dg::initGL();
    __android_log_print(ANDROID_LOG_INFO,"DG-0002","Native renderer initialized");
}

extern "C" JNIEXPORT void JNICALL
Java_com_fanmade_dg_MainActivity_00024NativeBridge_resize(JNIEnv*,jclass,jint w,jint h) {
    dg::viewportW=std::max(1,(int)w);
    dg::viewportH=std::max(1,(int)h);
}

extern "C" JNIEXPORT void JNICALL
Java_com_fanmade_dg_MainActivity_00024NativeBridge_frame(JNIEnv*,jclass) {
    dg::frame();
}

extern "C" JNIEXPORT void JNICALL
Java_com_fanmade_dg_MainActivity_00024NativeBridge_touch(
        JNIEnv*,jclass,jint pointerId,jint action,
        jfloat x,jfloat y,jint w,jint h) {
    dg::touch(pointerId,action,x,y,(float)w,(float)h);
}
