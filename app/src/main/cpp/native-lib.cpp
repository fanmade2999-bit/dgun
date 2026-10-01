#include <jni.h>
#include <android/log.h>
#include <GLES2/gl2.h>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstdio>

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
static float yaw=0.0f, aimPitch=0.18f;

static float heat=0.0f;
static float playerHp=100.0f;
static float respawnTimer=0.0f;

// Overall chassis integrity prevents the displayed HP from stalling simply
// because the current attack angle keeps landing on an already-destroyed limb.
// Part damage remains authoritative for salvage and local component condition.
static float chassisIntegrity=100.0f;

// ULTRON-style remote body continuity prototype.
enum PlayerPart : int {
    PART_CORE=0,
    PART_HEAD,
    PART_LEFT_ARM,
    PART_RIGHT_ARM,
    PART_LEFT_LEG,
    PART_RIGHT_LEG,
    PART_WEAPON,
    PART_COUNT
};

struct BodyPartState {
    float hp;
    float maxHp;
};

struct BodyPartDef {
    Vec3 localCenter;
    float radius;
    float maxHp;
};

static constexpr BodyPartDef PLAYER_PART_DEFS[PART_COUNT] = {
    {{ 0.00f, 0.95f,  0.00f}, 0.95f, 34.0f}, // core
    {{ 0.00f, 1.85f, -0.03f}, 0.50f, 12.0f}, // head
    {{-1.08f, 0.55f,  0.00f}, 0.52f, 10.0f}, // left arm
    {{ 1.08f, 0.55f,  0.00f}, 0.52f, 10.0f}, // right arm
    {{-0.40f,-0.10f,  0.00f}, 0.62f, 12.0f}, // left leg
    {{ 0.40f,-0.10f,  0.00f}, 0.62f, 12.0f}, // right leg
    {{ 0.00f, 1.45f, -1.00f}, 0.82f, 10.0f}  // weapon
};

static BodyPartState playerParts[PART_COUNT] = {
    {34.0f,34.0f}, {12.0f,12.0f},
    {10.0f,10.0f}, {10.0f,10.0f},
    {12.0f,12.0f}, {12.0f,12.0f},
    {10.0f,10.0f}
};

static constexpr int MAX_BODY_SLOTS=6;

struct StoredBody {
    bool occupied=false;
    BodyPartState parts[PART_COUNT]{};
    int generation=0;
};

static StoredBody bodySlots[MAX_BODY_SLOTS]{};
static int activeBodySlot=0;
static int bodyGeneration=1;
static bool bodyPoolInitialized=false;
static bool miniBotMode=false;

// Test-world resources and progression.
static int scrap=3;
static int circuits=1;
static int unknownEquipment=1;
static int identifiedEquipment=0;

static float fabricationTimer=0.0f;
static int fabricationSlot=-1;

enum class BaseMode : uint8_t {
    LAND,
    ORBITAL
};
static BaseMode baseMode=BaseMode::LAND;

static constexpr float BASE_X=0.0f;
static constexpr float BASE_Z=0.0f;
static constexpr float FACILITY_X=11.0f;
static constexpr float FACILITY_Z=-6.0f;

static bool actionPointerActive=false;
static int actionPointer=-1;
static int swapPointer=-1;
static bool actionPointerActive=false;

struct WreckState {
    bool active=false;
    float x=0.0f, z=0.0f, yaw=0.0f;
    float salvagePercent=0.0f;
    BodyPartState parts[PART_COUNT]{};
};
static WreckState wreck;

static int movePointer=-1;
static int aimPointer=-1;
static int firePointer=-1;
static float joyX=0.0f, joyY=0.0f;
static float lastAimX=0.0f, lastAimY=0.0f;
static constexpr float PLAYER_RADIUS=0.62f;

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
    initializeBodyPool();
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

struct BeamObstacle {
    float minX, maxX;
    float minZ, maxZ;
    float resistance;
};

static constexpr BeamObstacle BEAM_OBSTACLES[] = {
    // Light debris / thin cover: laser can punch through with little loss.
    {-8.40f,-5.60f,-5.20f,-2.80f, 0.35f},
    // Dense concrete-like block.
    {-8.00f,-4.00f, 5.00f, 7.00f, 1.20f},
    // Heavy industrial structure.
    { 5.80f, 8.20f, 3.80f, 6.20f, 2.00f},
    // Thick metal barrier.
    { 7.20f, 8.80f,-4.70f,-0.30f, 2.60f}
};

static constexpr float LASER_PENETRATION=2.20f;
static constexpr float LASER_MAX_RANGE=18.0f;

static float rayBoxEntryDistance(Vec3 start, Vec3 dir, const BeamObstacle& b) {
    float tmin=0.0f;
    float tmax=LASER_MAX_RANGE;

    const float ox=start.x;
    const float oz=start.z;
    const float dx=dir.x;
    const float dz=dir.z;

    auto slab = [](float o,float d,float minV,float maxV,float& lo,float& hi) {
        if(std::fabs(d)<0.00001f) {
            return o>=minV && o<=maxV;
        }
        float a=(minV-o)/d;
        float b=(maxV-o)/d;
        if(a>b) std::swap(a,b);
        lo=std::max(lo,a);
        hi=std::min(hi,b);
        return lo<=hi;
    };

    if(!slab(ox,dx,b.minX,b.maxX,tmin,tmax)) return -1.0f;
    if(!slab(oz,dz,b.minZ,b.maxZ,tmin,tmax)) return -1.0f;
    if(tmax<0.0f || tmin>LASER_MAX_RANGE) return -1.0f;
    return std::max(0.0f,tmin);
}

struct LaserTrace {
    float distance=LASER_MAX_RANGE;
    float remainingPenetration=LASER_PENETRATION;
    float energy=1.0f;
};

static LaserTrace traceLaser(Vec3 start,Vec3 dir) {
    LaserTrace result{};
    float nearestCut=LASER_MAX_RANGE;

    // Sort-like repeated selection without heap allocation. There are only four
    // current obstacles, so a small fixed pass keeps the mobile path cheap.
    bool used[sizeof(BEAM_OBSTACLES)/sizeof(BEAM_OBSTACLES[0])]{};
    float cursor=0.0f;
    float penetration=LASER_PENETRATION;
    float energy=1.0f;

    for(size_t pass=0; pass<sizeof(BEAM_OBSTACLES)/sizeof(BEAM_OBSTACLES[0]); pass++) {
        int best=-1;
        float bestT=LASER_MAX_RANGE+1.0f;

        for(size_t i=0;i<sizeof(BEAM_OBSTACLES)/sizeof(BEAM_OBSTACLES[0]);i++) {
            if(used[i]) continue;
            float t=rayBoxEntryDistance(start,dir,BEAM_OBSTACLES[i]);
            if(t<0.0f || t<cursor-0.001f) continue;
            if(t<bestT) {
                bestT=t;
                best=(int)i;
            }
        }

        if(best<0) break;
        used[best]=true;

        const float resistance=BEAM_OBSTACLES[best].resistance;
        if(penetration < resistance) {
            nearestCut=bestT+0.04f;
            penetration=0.0f;
            energy*=0.12f;
            break;
        }

        penetration-=resistance;
        energy*=std::max(0.18f,1.0f-resistance*0.22f);
        cursor=bestT+0.05f;
    }

    result.distance=std::clamp(nearestCut,0.5f,LASER_MAX_RANGE);
    result.remainingPenetration=penetration;
    result.energy=std::clamp(energy,0.0f,1.0f);
    return result;
}

static void drawMech(const Mat4& vp) {
    const float bodyR = playerHitFlash>0 ? 0.85f : 0.18f;
    const float bodyG = playerHitFlash>0 ? 0.25f : 0.37f;
    const float bodyB = playerHitFlash>0 ? 0.20f : 0.58f;

    // Torso + head.
    drawCube(vp,{px,0.95f,pz},{0.80f,0.62f,0.60f},yaw,bodyR,bodyG,bodyB);
    drawCube(vp,localOffset({px,0,pz},{0,1.85f,-0.03f},yaw),
             {0.42f,0.34f,0.42f},yaw,0.22f,0.46f,0.70f);

    auto partTint=[](int part,float r,float g,float b)->Vec3 {
        const float frac=PLAYER_PART_DEFS[part].maxHp>0.0f
            ? std::clamp(playerParts[part].hp/PLAYER_PART_DEFS[part].maxHp,0.0f,1.0f)
            : 0.0f;
        const float factor=0.32f+0.68f*frac;
        return {r*factor,g*factor,b*factor};
    };

    const Vec3 lShoulder=partTint(PART_LEFT_ARM,0.12f,0.28f,0.43f);
    const Vec3 rShoulder=partTint(PART_RIGHT_ARM,0.12f,0.28f,0.43f);
    const Vec3 lArm=partTint(PART_LEFT_ARM,0.10f,0.23f,0.36f);
    const Vec3 rArm=partTint(PART_RIGHT_ARM,0.10f,0.23f,0.36f);
    const Vec3 lLeg=partTint(PART_LEFT_LEG,0.12f,0.23f,0.32f);
    const Vec3 rLeg=partTint(PART_RIGHT_LEG,0.12f,0.23f,0.32f);
    const Vec3 weapon=partTint(PART_WEAPON,0.38f,0.55f,0.78f);

    // Shoulders and arms.
    drawCube(vp,localOffset({px,0,pz},{-1.05f,1.05f,0.0f},yaw),
             {0.30f,0.40f,0.42f},yaw,lShoulder.x,lShoulder.y,lShoulder.z);
    drawCube(vp,localOffset({px,0,pz},{1.05f,1.05f,0.0f},yaw),
             {0.30f,0.40f,0.42f},yaw,rShoulder.x,rShoulder.y,rShoulder.z);
    drawCube(vp,localOffset({px,0,pz},{-1.08f,0.40f,0.0f},yaw),
             {0.28f,0.48f,0.30f},yaw,lArm.x,lArm.y,lArm.z);
    drawCube(vp,localOffset({px,0,pz},{1.08f,0.40f,0.0f},yaw),
             {0.28f,0.48f,0.30f},yaw,rArm.x,rArm.y,rArm.z);

    // Legs.
    drawCube(vp,localOffset({px,0,pz},{-0.40f,-0.10f,0.0f},yaw),
             {0.30f,0.60f,0.38f},yaw,lLeg.x,lLeg.y,lLeg.z);
    drawCube(vp,localOffset({px,0,pz},{0.40f,-0.10f,0.0f},yaw),
             {0.30f,0.60f,0.38f},yaw,rLeg.x,rLeg.y,rLeg.z);

    // Main laser cannon mounted at the front.
    drawCube(vp,localOffset({px,0,pz},{0,1.45f,-0.95f},yaw),
             {0.13f,0.13f,0.85f},yaw,weapon.x,weapon.y,weapon.z);
}

struct ObstacleBox {
    float minX, maxX;
    float minZ, maxZ;
};

static constexpr ObstacleBox OBSTACLES[] = {
    {-8.40f,-5.60f,-5.20f,-2.80f},
    {-8.00f,-4.00f, 5.00f, 7.00f},
    { 5.80f, 8.20f, 3.80f, 6.20f},
    { 7.20f, 8.80f,-4.70f,-0.30f}
};

static bool collidesObstacle(float x, float z, float radius) {
    for (const auto& b : OBSTACLES) {
        const float cx = std::clamp(x,b.minX,b.maxX);
        const float cz = std::clamp(z,b.minZ,b.maxZ);
        const float dx = x-cx;
        const float dz = z-cz;
        if (dx*dx+dz*dz < radius*radius) return true;
    }
    return false;
}

static void movePlayer(float dx, float dz) {
    const float nextX = std::clamp(px+dx,-18.0f,18.0f);
    if (!collidesObstacle(nextX,pz,PLAYER_RADIUS)) {
        px=nextX;
    }

    const float nextZ = std::clamp(pz+dz,-18.0f,18.0f);
    if (!collidesObstacle(px,nextZ,PLAYER_RADIUS)) {
        pz=nextZ;
    }
}

static void moveEnemy(float dx, float dz) {
    const float nextX = std::clamp(enemyX+dx,-18.0f,18.0f);
    if (!collidesObstacle(nextX,enemyZ,0.65f)) {
        enemyX=nextX;
    }

    const float nextZ = std::clamp(enemyZ+dz,-18.0f,18.0f);
    if (!collidesObstacle(enemyX,nextZ,0.65f)) {
        enemyZ=nextZ;
    }
}

static bool nearPoint(float x,float z,float tx,float tz,float radius) {
    const float dx=x-tx;
    const float dz=z-tz;
    return dx*dx+dz*dz<=radius*radius;
}

static bool pointInSwapButton(float x,float y,float w,float h) {
    const float cx=w*0.84f;
    const float cy=h*0.25f;
    const float radius=h*0.095f;
    const float dx=x-cx, dy=y-cy;
    return dx*dx+dy*dy<=radius*radius;
}

static bool pointInActionButton(float x,float y,float w,float h) {
    const float cx=w*0.68f;
    const float cy=h*0.78f;
    const float radius=h*0.115f;
    const float dx=x-cx, dy=y-cy;
    return dx*dx+dy*dy<=radius*radius;
}

static bool performBodySwap() {
    if(!nearPoint(px,pz,BASE_X,BASE_Z,2.8f)) return false;

    const int other=findOtherBodySlot();
    if(other<0 || fabricationTimer>0.0f) return false;

    saveActiveBodyToPool();
    loadBodyFromSlot(other);
    return true;
}

static bool performContextAction() {
    if(nearPoint(px,pz,wreck.x,wreck.z,2.0f) && wreck.active) {
        // Salvage is intentionally condition-dependent: destroyed parts yield
        // little, intact parts yield much more. Recovery is a conversion into
        // components; later this becomes a proper salvage inventory UI.
        int recoveredScrap=0;
        int recoveredCircuits=0;

        for(int i=0;i<PART_COUNT;i++) {
            const BodyPartDef& d=PLAYER_PART_DEFS[i];
            const float frac=d.maxHp>0.0f
                ? std::clamp(wreck.parts[i].hp/d.maxHp,0.0f,1.0f)
                : 0.0f;

            if(frac>0.03f) {
                recoveredScrap += 1 + int(std::floor(frac*3.0f));
                if(i==PART_CORE || i==PART_WEAPON || frac>0.75f) {
                    recoveredCircuits += 1;
                }
            }
        }

        scrap+=recoveredScrap;
        circuits+=recoveredCircuits;
        wreck.active=false;
        wreck.salvagePercent=0.0f;
        return true;
    }

    if(nearPoint(px,pz,FACILITY_X,FACILITY_Z,2.6f) && unknownEquipment>0) {
        // Identification is deliberately location-driven instead of pure RNG:
        // the facility reveals one hidden piece of equipment.
        --unknownEquipment;
        ++identifiedEquipment;
        return true;
    }

    if(nearPoint(px,pz,BASE_X,BASE_Z,2.8f)) {
        return beginFabrication();
    }

    return false;
}

static bool pointInFireButton(float x, float y, float w, float h) {
    const float cx=w*0.84f;
    const float cy=h*0.78f;
    const float radius=h*0.135f;
    const float dx=x-cx, dy=y-cy;
    return dx*dx+dy*dy <= radius*radius;
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
    if(laserT<=0.0f || firePointer<0) return;

    const Vec3 start=localOffset({px,0,pz},{0,1.45f,-1.70f},yaw);
    const float horiz=std::cos(aimPitch);
    const Vec3 dir={
        std::sin(yaw)*horiz,
        -std::sin(aimPitch),
        -std::cos(yaw)*horiz
    };

    const LaserTrace trace=traceLaser(start,dir);

    float visibleDistance=trace.distance;
    if(dir.y<0.0f) {
        const float groundDistance=start.y/(-dir.y);
        if(groundDistance>0.02f) {
            visibleDistance=std::min(visibleDistance,groundDistance);
        }
    }

    const Vec3 end=add(start,mul(dir,visibleDistance));

    // Multiple cheap lines make the beam visibly persistent even on GLES
    // implementations that clamp glLineWidth() to a single pixel.
    const float mainVerts[]={
        start.x,start.y,start.z,
        end.x,end.y,end.z
    };
    drawLines(vp,mainVerts,2,0.25f,0.95f,1.0f,0.98f);

    const float offsets[][2]={
        {-0.030f,-0.030f},
        { 0.030f, 0.030f},
        {-0.045f, 0.000f},
        { 0.045f, 0.000f}
    };
    const float glow=0.22f+0.36f*trace.energy;
    for(const auto& o:offsets) {
        const float glowVerts[]={
            start.x+o[0],start.y,start.z+o[1],
            end.x+o[0],end.y,end.z+o[1]
        };
        drawLines(vp,glowVerts,2,0.05f,0.55f,0.90f,glow);
    }

    // Small muzzle pulse.
    const float pulse=0.10f+0.12f*std::sin(float(nowSeconds()*60.0));
    const Vec3 muzzleEnd=add(start,mul(dir,0.55f+pulse));
    const float muzzle[]={
        start.x,start.y,start.z,
        muzzleEnd.x,muzzleEnd.y,muzzleEnd.z
    };
    drawLines(vp,muzzle,2,0.60f,1.0f,1.0f,0.95f);
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

static uint8_t glyphBits(char ch,int row) {
    static constexpr uint8_t H[7] = {17,17,17,31,17,17,17};
    static constexpr uint8_t P[7] = {30,17,17,30,16,16,16};
    static constexpr uint8_t E[7] = {31,16,16,30,16,16,31};
    static constexpr uint8_t A[7] = {14,17,17,31,17,17,17};
    static constexpr uint8_t T[7] = {31,4,4,4,4,4,4};
    static constexpr uint8_t U[7] = {17,17,17,17,17,17,14};
    static constexpr uint8_t F[7] = {31,16,16,30,16,16,16};
    static constexpr uint8_t I[7] = {31,4,4,4,4,4,31};
    static constexpr uint8_t R[7] = {30,17,17,30,20,18,17};
    static constexpr uint8_t M[7] = {17,27,21,17,17,17,17};
    static constexpr uint8_t O[7] = {14,17,17,17,17,17,14};
    static constexpr uint8_t V[7] = {17,17,17,17,17,10,4};
    static constexpr uint8_t W[7] = {17,17,17,21,21,21,10};
    static constexpr uint8_t B[7] = {30,17,17,30,17,17,30};
    static constexpr uint8_t D[7] = {30,17,17,17,17,17,30};
    static constexpr uint8_t Y[7] = {17,17,10,4,4,4,4};
    static constexpr uint8_t S[7] = {15,16,16,14,1,1,30};
    static constexpr uint8_t L[7] = {16,16,16,16,16,16,31};
    static constexpr uint8_t G[7] = {14,17,16,23,17,17,14};
    static constexpr uint8_t N[7] = {17,25,21,19,17,17,17};
    static constexpr uint8_t C[7] = {14,17,16,16,16,17,14};
    static constexpr uint8_t D0[7] = {14,17,19,21,25,17,14};
    static constexpr uint8_t D1[7] = {4,12,4,4,4,4,14};
    static constexpr uint8_t D2[7] = {14,17,1,2,4,8,31};
    static constexpr uint8_t D3[7] = {30,1,1,14,1,1,30};
    static constexpr uint8_t D4[7] = {2,6,10,18,31,2,2};
    static constexpr uint8_t D5[7] = {31,16,16,30,1,1,30};
    static constexpr uint8_t D6[7] = {14,16,16,30,17,17,14};
    static constexpr uint8_t D7[7] = {31,1,2,4,8,8,8};
    static constexpr uint8_t D8[7] = {14,17,17,14,17,17,14};
    static constexpr uint8_t D9[7] = {14,17,17,15,1,1,14};

    if(row<0 || row>=7) return 0;
    switch(ch) {
        case 'H': return H[row];
        case 'P': return P[row];
        case 'E': return E[row];
        case 'A': return A[row];
        case 'T': return T[row];
        case 'U': return U[row];
        case 'F': return F[row];
        case 'I': return I[row];
        case 'R': return R[row];
        case 'M': return M[row];
        case 'O': return O[row];
        case 'V': return V[row];
        case 'W': return W[row];
        case 'B': return B[row];
        case 'D': return D[row];
        case 'Y': return Y[row];
        case 'S': return S[row];
        case 'L': return L[row];
        case 'G': return G[row];
        case 'N': return N[row];
        case 'C': return C[row];
        case '0': return D0[row];
        case '1': return D1[row];
        case '2': return D2[row];
        case '3': return D3[row];
        case '4': return D4[row];
        case '5': return D5[row];
        case '6': return D6[row];
        case '7': return D7[row];
        case '8': return D8[row];
        case '9': return D9[row];
        default: return 0;
    }
}

static void drawText2D(const Mat4& hud,const char* text,float x,float y,
                       float scale,float r,float g,float b,float a=1.0f) {
    float verts[10000]{};
    int n=0;
    float cursor=x;

    for(int ci=0;text[ci] && n<9900;ci++) {
        const char ch=text[ci];
        for(int row=0;row<7;row++) {
            const uint8_t bits=glyphBits(ch,row);
            for(int col=0;col<5;col++) {
                if((bits & (1u<<(4-col)))==0) continue;

                const float x1=cursor+float(col)*scale;
                const float y1=y+float(row)*scale;
                const float x2=x1+scale;
                const float y2=y1+scale;

                const float q[]={
                    x1,y1,0, x2,y1,0, x2,y2,0,
                    x1,y1,0, x2,y2,0, x1,y2,0
                };
                for(float v:q) verts[n++]=v;
            }
        }
        cursor += 7.0f*scale;
    }

    glBindBuffer(GL_ARRAY_BUFFER,0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,verts);
    glUniformMatrix4fv(uMvp,1,GL_FALSE,hud.m);
    glUniform4f(uColor,r,g,b,a);
    glDrawArrays(GL_TRIANGLES,0,n/3);
}

static void drawTestWorldStructures(const Mat4& vp) {
    // Home base: land prototype now; the base abstraction is already separate
    // so an orbital/mobile base can replace this visual later.
    const float baseY=baseMode==BaseMode::ORBITAL ? 4.0f : 0.85f;
    drawCube(vp,{BASE_X,baseY,BASE_Z},{2.0f,0.85f,2.0f},0.0f,
             0.13f,0.24f,0.33f);
    drawCube(vp,{BASE_X,baseY+1.15f,BASE_Z},{1.0f,0.25f,1.0f},0.0f,
             0.26f,0.55f,0.70f);

    // Discovery facility.
    drawCube(vp,{FACILITY_X,1.0f,FACILITY_Z},{1.5f,1.0f,1.5f},0.1f,
             0.26f,0.20f,0.14f);
    drawCube(vp,{FACILITY_X,2.25f,FACILITY_Z},{0.75f,0.25f,0.75f},0.1f,
             0.50f,0.34f,0.12f);

    // Unknown equipment pod outside the facility.
    if(unknownEquipment>0) {
        drawCube(vp,{FACILITY_X+2.0f,0.35f,FACILITY_Z},{0.30f,0.35f,0.30f},0.2f,
                 0.34f,0.52f,0.65f);
    }
}

static void drawWreck(const Mat4& vp) {
    if(wreck.active) {
        for(int i=0;i<PART_COUNT;i++) {
            const BodyPartDef& d=PLAYER_PART_DEFS[i];
            const float fraction=wreck.parts[i].maxHp>0.0f
                ? std::clamp(wreck.parts[i].hp/wreck.parts[i].maxHp,0.0f,1.0f)
                : 0.0f;
            if(fraction<=0.03f) continue;

            const Vec3 p=localOffset(
                {wreck.x,0,wreck.z},
                d.localCenter,
                wreck.yaw
            );
            const float damaged=1.0f-fraction;
            const float s=0.72f+0.18f*fraction;

            drawCube(
                vp,p,
                {d.radius*s*0.68f,d.radius*s*0.48f,d.radius*s*0.68f},
                wreck.yaw,
                0.08f+0.10f*fraction,
                0.10f+0.08f*fraction,
                0.11f+0.06f*fraction
            );

            // Highly damaged parts are visibly offset, communicating salvage severity.
            if(damaged>0.45f) {
                drawCube(
                    vp,
                    add(p,{0.08f*std::sin(float(i)),0.06f,0.08f*std::cos(float(i))}),
                    {0.16f,0.05f,0.16f},
                    wreck.yaw,
                    0.18f,0.12f,0.09f,0.75f
                );
            }
        }
    }
}

static void drawMiniBot(const Mat4& vp) {
    if(!miniBotMode || respawnTimer>0.0f) return;

    drawCube(vp,{px,0.45f,pz},{0.25f,0.25f,0.25f},yaw,
             0.35f,0.48f,0.55f);
    drawCube(vp,{px,0.78f,pz},{0.16f,0.12f,0.16f},yaw,
             0.42f,0.66f,0.78f);
}

static void drawHud() {
    const Mat4 hud=ortho(0,float(viewportW),float(viewportH),0);

    glDisable(GL_DEPTH_TEST);

    const float pad=22.0f;
    const float barW=viewportW*0.28f;
    const float barH=18.0f;

    // Green = HP. Blue = weapon heat.
    drawRect2D(hud,pad,pad,pad+barW,pad+barH,0.03f,0.04f,0.05f,0.85f);
    drawRect2D(hud,pad,pad,pad+barW*(playerHp/100.0f),pad+barH,0.18f,0.78f,0.30f,0.92f);
    drawText2D(hud,"HP",pad+6.0f,pad+2.0f,3.0f,0.85f,1.0f,0.90f,0.95f);

    const float heatY=pad+barH+10.0f;
    drawRect2D(hud,pad,heatY,pad+barW,heatY+16.0f,0.03f,0.04f,0.05f,0.85f);
    drawRect2D(hud,pad,heatY,pad+barW*std::min(1.0f,heat),heatY+16.0f,
               0.18f,0.72f,0.95f,0.92f);
    drawText2D(hud,"HEAT",pad+6.0f,heatY+1.0f,3.0f,0.80f,0.95f,1.0f,0.95f);

    // Targeting reticle.
    const float cx=viewportW*0.50f;
    const float cy=viewportH*0.47f;
    drawCircle2D(hud,cx,cy,20.0f,0.6f,0.8f,0.9f,0.70f);

    // Left-side movement control.
    const float joyBaseX=viewportW*0.18f;
    const float joyBaseY=viewportH*0.78f;
    const float joyR=viewportH*0.20f;
    const float joyKnobX=joyBaseX+joyX*joyR;
    const float joyKnobY=joyBaseY+joyY*joyR;
    drawCircle2D(hud,joyBaseX,joyBaseY,joyR,0.45f,0.58f,0.68f,0.42f);
    drawCircle2D(hud,joyKnobX,joyKnobY,joyR*0.42f,0.65f,0.82f,0.95f,0.70f);
    drawText2D(hud,"MOVE",joyBaseX-35.0f,joyBaseY-joyR-23.0f,3.0f,0.75f,0.88f,0.95f,0.82f);

    // Right-side aim region: swipe here, but it does not fire.
    const float aimLabelX=viewportW*0.68f;
    const float aimLabelY=viewportH*0.17f;
    drawText2D(hud,"AIM",aimLabelX,aimLabelY,3.0f,0.70f,0.86f,0.96f,0.70f);

    // Body swap is only available at the home base.
    const float swapX=viewportW*0.84f;
    const float swapY=viewportH*0.25f;
    const float swapR=viewportH*0.095f;
    const bool canSwap=nearPoint(px,pz,BASE_X,BASE_Z,2.8f)
        && findOtherBodySlot()>=0
        && fabricationTimer<=0.0f;
    drawCircle2D(hud,swapX,swapY,swapR,
                 swapPointer>=0?0.70f:0.36f,
                 swapPointer>=0?0.84f:0.55f,
                 swapPointer>=0?0.42f:0.50f,
                 canSwap?(swapPointer>=0?0.90f:0.55f):0.18f);
    drawText2D(hud,"SWAP",swapX-30.0f,swapY-10.0f,3.0f,
               1.0f,1.0f,1.0f,canSwap?0.95f:0.35f);

    // Context action button: salvage at a wreck, identify at a facility,
    // fabricate at base when enough components are available.
    const float actionX=viewportW*0.68f;
    const float actionY=viewportH*0.78f;
    const float actionR=viewportH*0.115f;
    drawCircle2D(hud,actionX,actionY,actionR,
                 actionPointer>=0?0.55f:0.42f,
                 actionPointer>=0?0.86f:0.58f,
                 actionPointer>=0?0.55f:0.50f,
                 actionPointer>=0?0.85f:0.48f);
    drawText2D(hud,"ACT",actionX-22.0f,actionY-10.0f,3.0f,1.0f,1.0f,1.0f,0.95f);

    // Explicit fire button. Swiping elsewhere on the right no longer fires.
    const float fireX=viewportW*0.84f;
    const float fireY=viewportH*0.78f;
    const float fireR=viewportH*0.135f;
    drawCircle2D(hud,fireX,fireY,fireR,
                 firePointer>=0?0.20f:0.48f,
                 firePointer>=0?0.84f:0.64f,
                 firePointer>=0?1.00f:0.80f,
                 firePointer>=0?0.85f:0.48f);
    drawText2D(hud,"FIRE",fireX-39.0f,fireY-10.0f,3.0f,1.0f,1.0f,1.0f,0.95f);

    // Body continuity readout.
    const float bodyX=viewportW*0.73f;
    const float bodyY=pad+8.0f;
    char bodyText[32]{};
    std::snprintf(bodyText,sizeof(bodyText),"BODY");
    drawText2D(hud,bodyText,bodyX,bodyY,3.0f,0.70f,0.88f,0.96f,0.82f);
    char countText[8]{};
    std::snprintf(countText,sizeof(countText),"%d",countReadyBodies());
    drawText2D(hud,countText,bodyX+35.0f,bodyY,3.0f,1.0f,1.0f,1.0f,0.95f);

    char resText[32]{};
    std::snprintf(resText,sizeof(resText),"%d",scrap);
    drawText2D(hud,"SCRAP",viewportW*0.47f,pad+11.0f,2.4f,
               0.72f,0.82f,0.86f,0.78f);
    drawText2D(hud,resText,viewportW*0.56f,pad+11.0f,2.8f,
               1.0f,1.0f,1.0f,0.95f);

    char circText[32]{};
    std::snprintf(circText,sizeof(circText),"%d",circuits);
    drawText2D(hud,"CIRCUIT",viewportW*0.62f,pad+11.0f,2.15f,
               0.72f,0.82f,0.86f,0.78f);
    drawText2D(hud,circText,viewportW*0.75f,pad+11.0f,2.8f,
               1.0f,1.0f,1.0f,0.95f);

    char eqText[32]{};
    std::snprintf(eqText,sizeof(eqText),"%d",identifiedEquipment);
    drawText2D(hud,"ID",viewportW*0.82f,pad+46.0f,2.5f,
               0.98f,0.86f,0.35f,0.95f);
    drawText2D(hud,eqText,viewportW*0.87f,pad+46.0f,2.8f,
               1.0f,0.92f,0.60f,0.95f);

    if(fabricationTimer>0.0f) {
        char fabText[32]{};
        std::snprintf(fabText,sizeof(fabText),"%.0f",std::ceil(fabricationTimer));
        drawText2D(hud,fabText,viewportW*0.47f,pad+44.0f,3.0f,
                   0.65f,0.92f,0.98f,0.95f);
    }

    if(wreck.active) {
        char salvage[32]{};
        std::snprintf(salvage,sizeof(salvage),"SALVAGE");
        drawText2D(hud,salvage,viewportW*0.73f,pad+32.0f,2.6f,
                   0.70f,0.78f,0.72f,0.82f);
        char pct[16]{};
        std::snprintf(pct,sizeof(pct),"%.0f",wreck.salvagePercent);
        drawText2D(hud,pct,viewportW*0.73f+48.0f,pad+32.0f,2.6f,
                   0.85f,0.92f,0.82f,0.95f);
    }

    if(miniBotMode) {
        drawText2D(hud,"BOT",viewportW*0.47f,viewportH*0.80f,3.0f,
                   0.70f,0.86f,0.92f,0.75f);
    }

    // Context hint: the ACT button only does something when a relevant
    // interaction is nearby.
    if(nearPoint(px,pz,wreck.x,wreck.z,2.0f) && wreck.active) {
        drawText2D(hud,"SALVAGE",viewportW*0.34f,viewportH*0.18f,2.6f,
                   0.80f,0.92f,0.80f,0.82f);
    } else if(nearPoint(px,pz,FACILITY_X,FACILITY_Z,2.6f) && unknownEquipment>0) {
        drawText2D(hud,"IDENTIFY",viewportW*0.36f,viewportH*0.18f,2.6f,
                   0.80f,0.90f,0.98f,0.82f);
    } else if(nearPoint(px,pz,BASE_X,BASE_Z,2.8f) && fabricationSlot<0) {
        drawText2D(hud,"BODY",viewportW*0.42f,viewportH*0.18f,2.6f,
                   0.75f,0.88f,0.96f,0.82f);
    }

    glEnable(GL_DEPTH_TEST);
}

static void updateAim(float x, float y) {
    const float dx=x-lastAimX;
    const float dy=y-lastAimY;

    yaw += dx*0.0075f;
    aimPitch += dy*0.0040f;

    lastAimX=x;
    lastAimY=y;
    // Prevent the cannon from being dragged nearly straight into the ground.
    // Negative values aim slightly upward; positive values aim downward.
    aimPitch=std::clamp(aimPitch,-0.25f,0.38f);
}

static Vec3 playerPartCenter(int part) {
    return localOffset({px,0,pz},PLAYER_PART_DEFS[part].localCenter,yaw);
}

static int closestPlayerPartOnRay(Vec3 start,Vec3 dir,float maxDistance) {
    int best=-1;
    float bestT=maxDistance+1.0f;
    for(int i=0;i<PART_COUNT;i++) {
        if(playerParts[i].hp<=0.0f) continue;
        const Vec3 center=playerPartCenter(i);
        const Vec3 rel=sub(center,start);
        const float t=dot(rel,dir);
        if(t<0.0f || t>maxDistance) continue;
        const Vec3 closest=add(start,mul(dir,t));
        const float d2=dot(sub(center,closest),sub(center,closest));
        const float radius=PLAYER_PART_DEFS[i].radius;
        if(d2<=radius*radius && t<bestT) {
            bestT=t;
            best=i;
        }
    }
    return best;
}

static float calculatePlayerHp() {
    float total=0.0f;
    float maxTotal=0.0f;
    for(int i=0;i<PART_COUNT;i++) {
        total+=std::max(0.0f,playerParts[i].hp);
        maxTotal+=playerParts[i].maxHp;
    }
    return maxTotal>0.0f ? total*100.0f/maxTotal : 0.0f;
}

static void resetPlayerBody();

static void saveActiveBodyToPool() {
    StoredBody& b=bodySlots[activeBodySlot];
    b.occupied=true;
    b.generation=bodyGeneration;
    for(int i=0;i<PART_COUNT;i++) b.parts[i]=playerParts[i];
}

static void loadBodyFromSlot(int slot) {
    activeBodySlot=slot;
    StoredBody& b=bodySlots[slot];
    b.occupied=true;
    if(b.generation<=0) {
        b.generation=++bodyGeneration;
    } else {
        bodyGeneration=b.generation;
    }
    for(int i=0;i<PART_COUNT;i++) {
        playerParts[i]=b.parts[i];
    }
    playerHp=calculatePlayerHp();
    chassisIntegrity=playerHp;
    heat=0.0f;
    aimPitch=0.0f;
}

static int countReadyBodies() {
    int count=0;
    for(int i=0;i<MAX_BODY_SLOTS;i++) {
        if(i==activeBodySlot) continue;
        if(bodySlots[i].occupied) count++;
    }
    return count;
}

static int findOtherBodySlot() {
    for(int i=0;i<MAX_BODY_SLOTS;i++) {
        if(i!=activeBodySlot && bodySlots[i].occupied) return i;
    }
    return -1;
}

static int findEmptyBodySlot() {
    for(int i=0;i<MAX_BODY_SLOTS;i++) {
        if(!bodySlots[i].occupied) return i;
    }
    return -1;
}

static void initializeBodyPool() {
    if(bodyPoolInitialized) return;
    bodyPoolInitialized=true;

    for(int i=0;i<MAX_BODY_SLOTS;i++) {
        bodySlots[i]=StoredBody{};
    }

    resetPlayerBody();

    // Three initially assembled chassis make the scarcity/fabrication loop
    // testable without making the first run punishing.
    bodySlots[0].occupied=true;
    bodySlots[0].generation=1;
    for(int p=0;p<PART_COUNT;p++) bodySlots[0].parts[p]=playerParts[p];

    for(int slot=1;slot<=2;slot++) {
        bodySlots[slot].occupied=true;
        bodySlots[slot].generation=slot+1;
        for(int p=0;p<PART_COUNT;p++) bodySlots[slot].parts[p]=playerParts[p];
    }

    activeBodySlot=0;
    bodyGeneration=1;
}

static void completeFabrication() {
    if(fabricationSlot<0) return;

    StoredBody& b=bodySlots[fabricationSlot];
    b.occupied=true;
    b.generation=++bodyGeneration;
    for(int i=0;i<PART_COUNT;i++) {
        b.parts[i].maxHp=PLAYER_PART_DEFS[i].maxHp;
        b.parts[i].hp=PLAYER_PART_DEFS[i].maxHp;
    }

    fabricationSlot=-1;
    fabricationTimer=0.0f;
}

static bool beginFabrication() {
    if(fabricationSlot>=0) return false;
    const int slot=findEmptyBodySlot();
    if(slot<0 || scrap<6 || circuits<2) return false;

    scrap-=6;
    circuits-=2;
    fabricationSlot=slot;
    fabricationTimer=6.0f;
    return true;
}

static int currentBodySalvageClass() {
    float avg=0.0f;
    for(int i=0;i<PART_COUNT;i++) {
        avg += playerParts[i].maxHp>0.0f
            ? std::clamp(playerParts[i].hp/playerParts[i].maxHp,0.0f,1.0f)
            : 0.0f;
    }
    avg/=float(PART_COUNT);
    return int(std::round(avg*100.0f));
}

static float calculatePlayerHp();

static void resetPlayerBody() {
    for(int i=0;i<PART_COUNT;i++) {
        playerParts[i].maxHp=PLAYER_PART_DEFS[i].maxHp;
        playerParts[i].hp=PLAYER_PART_DEFS[i].maxHp;
    }
    playerHp=100.0f;
    chassisIntegrity=100.0f;
    heat=0.0f;
    aimPitch=0.18f;
}

static void storeDestroyedWreck() {
    saveActiveBodyToPool();
    wreck.active=true;
    wreck.x=px;
    wreck.z=pz;
    wreck.yaw=yaw;
    wreck.salvagePercent=0.0f;

    float remaining=0.0f;
    float maxTotal=0.0f;
    for(int i=0;i<PART_COUNT;i++) {
        wreck.parts[i]=playerParts[i];
        remaining+=std::max(0.0f,playerParts[i].hp);
        maxTotal+=std::max(0.0f,playerParts[i].maxHp);
    }

    if(maxTotal>0.0f) {
        // Severe local damage ruins more salvage than simple linear health loss.
        wreck.salvagePercent=std::pow(std::clamp(remaining/maxTotal,0.0f,1.0f),1.35f)*100.0f;
    }
}

static void beginPlayerDeath() {
    storeDestroyedWreck();

    // The chassis has physically ceased to be an available body.
    bodySlots[activeBodySlot].occupied=false;

    miniBotMode=false;
    respawnTimer=2.20f;
    laserT=0.0f;
    firePointer=-1;
    aimPointer=-1;
    actionPointer=-1;
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
        moveEnemy((dx/distance)*speed*dt,(dz/distance)*speed*dt);
    } else {
        const float orbit=0.55f;
        const float sideX=std::cos(enemyYaw);
        const float sideZ=std::sin(enemyYaw);
        moveEnemy(sideX*orbit*dt,sideZ*orbit*dt);
    }

    enemyAttackTimer-=dt;
    if(enemyAttackTimer<=0.0f && distance<11.0f) {
        enemyAttackTimer=1.10f;
        enemyLaserT=0.12f;
        enemyShotDirX=dx/std::max(0.001f,distance);
        enemyShotDirZ=dz/std::max(0.001f,distance);

        const Vec3 shotStart={enemyX,1.45f,enemyZ};
        const Vec3 target={px,1.15f,pz};
        const Vec3 shotDir=norm(sub(target,shotStart));
        const int hitPart=closestPlayerPartOnRay(shotStart,shotDir,distance+1.0f);

        const int appliedPart = miniBotMode ? PART_CORE : (hitPart>=0 ? hitPart : PART_CORE);
        const float partDamage = miniBotMode ? 9.0f : (appliedPart==PART_HEAD ? 10.0f : 7.0f);

        // Localized damage: the struck component takes the full hit.
        playerParts[appliedPart].hp=
            std::max(0.0f,playerParts[appliedPart].hp-partDamage);

        // A small amount of structural shock always reaches the chassis.
        // This keeps the overall HP responsive even when an external part has
        // already been destroyed and is skipped by the hitbox selector.
        const float structuralDamage = miniBotMode ? 8.0f : 2.25f;
        chassisIntegrity=std::max(0.0f,chassisIntegrity-structuralDamage);

        playerHp=std::min(calculatePlayerHp(),chassisIntegrity);
        playerHitFlash=0.12f;

        if(playerHp<=0.0f || chassisIntegrity<=0.0f) {
            beginPlayerDeath();
        }
    }
}

static void update(float dt) {
    if(fabricationTimer>0.0f) {
        fabricationTimer=std::max(0.0f,fabricationTimer-dt);
        if(fabricationTimer<=0.0f) completeFabrication();
    }

    const bool disabled = respawnTimer>0.0f;

    if(disabled) {
        respawnTimer=std::max(0.0f,respawnTimer-dt);
        heat=std::max(0.0f,heat-dt*0.7f);
        laserT=0.0f;
        firePointer=-1;

        if(respawnTimer<=0.0f) {
            px=0.0f;
            pz=0.0f;
            yaw=0.0f;
            aimPitch=0.18f;

            const int nextBody=findOtherBodySlot();

            if(nextBody>=0) {
                // Consciousness reaches the main base and transfers into an
                // already assembled chassis. The destroyed chassis remains at
                // its world position as the wreck we recorded above.
                miniBotMode=false;
                loadBodyFromSlot(nextBody);
                bodySlots[nextBody].occupied=true;
            } else {
                // No complete chassis remains. A tiny recovery bot is deployed
                // by the base and becomes the temporary body.
                miniBotMode=true;
                resetPlayerBody();
                playerHp=55.0f;
                chassisIntegrity=55.0f;
            }

            // Keep movement pointer alive. If the player's thumb is still on the
            // joystick, movement resumes immediately after deployment.
            actionPointer=-1;
            actionPointerActive=false;
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

        const float speed=miniBotMode ? 2.2f : 3.6f;
        movePlayer(move.x*speed*dt,move.z*speed*dt);

        if(!miniBotMode && firePointer>=0 && heat<0.92f) {
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

    // Laser hit test uses the same obstruction trace as the visual beam.
    // A low-resistance obstacle can be penetrated; a high-resistance obstacle
    // stops the beam and prevents the target behind it from taking full damage.
    if(laserT>0.0f && firePointer>=0 && enemyRespawn<=0.0f && respawnTimer<=0.0f) {
        const Vec3 start=localOffset({px,0,pz},{0,1.45f,-1.70f},yaw);
        const float horiz=std::cos(aimPitch);
        const Vec3 dir={
            std::sin(yaw)*horiz,
            -std::sin(aimPitch),
            -std::cos(yaw)*horiz
        };
        const LaserTrace trace=traceLaser(start,dir);
        float maxBeamDistance=trace.distance;
        if(dir.y<0.0f) {
            const float groundDistance=start.y/(-dir.y);
            if(groundDistance>0.02f) maxBeamDistance=std::min(maxBeamDistance,groundDistance);
        }

        const Vec3 toEnemy={enemyX-start.x,0.95f-start.y,enemyZ-start.z};
        const float along=dot(toEnemy,dir);

        if(along>0.0f && along<=maxBeamDistance+0.05f && along<LASER_MAX_RANGE) {
            const Vec3 closest=add(start,mul(dir,along));
            const Vec3 enemyCenter={enemyX,0.95f,enemyZ};
            const float d2=dot(sub(enemyCenter,closest),sub(enemyCenter,closest));
            if(d2<1.65f) {
                enemyHp-=18.0f*trace.energy*dt*60.0f;
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
    drawTestWorldStructures(vp);
    if(wreck.active) drawWreck(vp);
    if(enemyRespawn<=0.0f) drawEnemy(vp);

    if(respawnTimer<=0.0f && !miniBotMode) {
        drawMech(vp);
        drawLaser(vp);
    } else {
        drawMiniBot(vp);
    }

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
        } else if(!leftZone && pointInSwapButton(x,y,w,h) && swapPointer<0) {
            swapPointer=pointerId;
            performBodySwap();
        } else if(!leftZone && pointInActionButton(x,y,w,h) && actionPointer<0) {
            actionPointer=pointerId;
            actionPointerActive=performContextAction();
        } else if(!leftZone && pointInFireButton(x,y,w,h) && firePointer<0) {
            // Only a press inside the explicit FIRE button starts firing.
            firePointer=pointerId;
            laserT=0.08f;
        } else if(!leftZone && aimPointer<0) {
            // Any other press on the right is an aim swipe, never a fire press.
            aimPointer=pointerId;
            lastAimX=x;
            lastAimY=y;
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
        } else if(pointerId==aimPointer) {
            updateAim(x,y);
        } else if(pointerId!=firePointer && pointerId!=actionPointer && pointerId!=swapPointer && !leftZone) {
            // An unclaimed right-side MOVE can become an aim gesture, but a
            // dedicated action/fire pointer never changes role mid-gesture.
            if(pointInFireButton(x,y,w,h) && firePointer<0) {
                firePointer=pointerId;
            } else if(pointInActionButton(x,y,w,h) && actionPointer<0) {
                actionPointer=pointerId;
            } else if(aimPointer<0) {
                aimPointer=pointerId;
                lastAimX=x;
                lastAimY=y;
            }
        }
        return;
    }

    if(action==ACTION_UP || action==ACTION_POINTER_UP || action==ACTION_CANCEL) {
        if(pointerId==movePointer || action==ACTION_CANCEL) {
            movePointer=-1;
            joyX=0.0f;
            joyY=0.0f;
        }
        if(pointerId==aimPointer || action==ACTION_CANCEL) {
            aimPointer=-1;
        }
        if(pointerId==actionPointer || action==ACTION_CANCEL) {
            actionPointer=-1;
            actionPointerActive=false;
        }
        if(pointerId==swapPointer || action==ACTION_CANCEL) {
            swapPointer=-1;
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
    __android_log_print(ANDROID_LOG_INFO,"DG-0005","Native renderer initialized");
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
