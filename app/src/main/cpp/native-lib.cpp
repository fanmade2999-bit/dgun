#include <jni.h>
#include <android/log.h>
#include <GLES2/gl2.h>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <cstring>
#include <type_traits>
#include <memory>

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

static constexpr float WORLD_SIZE=1024.0f;
static constexpr float WORLD_HALF=WORLD_SIZE*0.5f;
static constexpr float WORLD_TILE_SIZE=2.0f;
static constexpr int WORLD_TILE_COUNT=int(WORLD_SIZE/WORLD_TILE_SIZE);

// Chunk layer: 16 × 16 tiles per chunk, 32 world units per chunk.
// The world therefore contains a deterministic 32 × 32 chunk torus.
static constexpr int CHUNK_TILE_COUNT=16;
static constexpr float CHUNK_WORLD_SIZE=WORLD_TILE_SIZE*float(CHUNK_TILE_COUNT);
static constexpr int WORLD_CHUNK_COUNT=WORLD_TILE_COUNT/CHUNK_TILE_COUNT;
static constexpr int WORLD_CHUNK_STATE_COUNT=WORLD_CHUNK_COUNT*WORLD_CHUNK_COUNT;
static constexpr float ECO_CYCLE_SECONDS=240.0f;
static constexpr float ECO_DAY_START=0.20f;
static constexpr float ECO_NIGHT_START=0.78f;

static int floorTile(float v);
static int positiveMod(int v,int m);

static constexpr float BASE_X=0.0f;
static constexpr float BASE_Z=0.0f;
static constexpr float FACILITY_X=11.0f;
static constexpr float FACILITY_Z=-6.0f;
static constexpr float BOSS_X=14.0f;
static constexpr float BOSS_Z=8.0f;

static float px=0.0f, pz=0.0f;
static float yaw=0.0f, aimPitch=0.18f;

static float wrapWorld(float v) {
    while(v>=WORLD_HALF) v-=WORLD_SIZE;
    while(v< -WORLD_HALF) v+=WORLD_SIZE;
    return v;
}

static float wrappedDelta(float from, float to) {
    float d=to-from;
    if(d>WORLD_HALF) d-=WORLD_SIZE;
    if(d< -WORLD_HALF) d+=WORLD_SIZE;
    return d;
}

static float nearestWorldImage(float value,float reference) {
    return reference+wrappedDelta(reference,value);
}

static int floorTile(float v) {
    return int(std::floor(v/WORLD_TILE_SIZE));
}

static int positiveMod(int v,int m) {
    const int r=v%m;
    return r<0?r+m:r;
}

static uint32_t worldHash(int tx,int tz) {
    const uint32_t x=uint32_t(positiveMod(tx,WORLD_TILE_COUNT));
    const uint32_t z=uint32_t(positiveMod(tz,WORLD_TILE_COUNT));
    uint32_t h=2166136261u;
    h^=x+0x9e3779b9u; h*=16777619u;
    h^=z+0x85ebca6bu; h*=16777619u;
    h^=(x<<16)|(z&0xffffu); h*=16777619u;
    h^=h>>13; h*=1274126177u; h^=h>>16;
    return h;
}

static int chunkCoord(float worldValue) {
    return int(std::floor(wrapWorld(worldValue)/CHUNK_WORLD_SIZE));
}

static int chunkLocalTile(float worldValue) {
    const int tile=floorTile(worldValue);
    return positiveMod(tile,CHUNK_TILE_COUNT);
}

static uint32_t chunkHash(int chunkX,int chunkZ) {
    const uint32_t x=uint32_t(positiveMod(chunkX,WORLD_CHUNK_COUNT));
    const uint32_t z=uint32_t(positiveMod(chunkZ,WORLD_CHUNK_COUNT));
    uint32_t h=0x9747b28cu;
    h^=x+0x9e3779b9u; h*=0x85ebca6bu;
    h^=z+0xc2b2ae35u; h*=0x27d4eb2fu;
    h^=h>>15; h*=0x2c1b3c6du; h^=h>>12;
    return h;
}

struct EcoChunkState {
    float food=0.0f;
    float water=0.0f;
    float shelter=0.0f;
    float danger=0.0f;
    int population=0;

    // Population LOD: creatures outside the active simulation bubble are
    // represented as counts instead of consuming individual actor slots.
    // This lets the 1024x1024 world contain a much larger living population
    // without making every creature a full real-time object.
    uint16_t dormant[3]{};

    // Fractional progress keeps aggregate births/deaths from being rounded
    // away on every 0.20s simulation tick.
    float dormantRemainder[3]{};
};

static EcoChunkState ecoChunks[WORLD_CHUNK_STATE_COUNT]{};

static int chunkStateIndex(int cx,int cz) {
    const int x=positiveMod(cx,WORLD_CHUNK_COUNT);
    const int z=positiveMod(cz,WORLD_CHUNK_COUNT);
    return z*WORLD_CHUNK_COUNT+x;
}

static bool chunkBiomeIsIndustrial(int chunkX,int chunkZ) {
    return int(chunkHash(chunkX,chunkZ)%7u)==0;
}

static float ecoDayPhase(float clock) {
    const float cycle=std::fmod(std::max(0.0f,clock),ECO_CYCLE_SECONDS)/ECO_CYCLE_SECONDS;
    return cycle;
}

static bool ecoIsNight(float clock) {
    const float p=ecoDayPhase(clock);
    return p<ECO_DAY_START || p>=ECO_NIGHT_START;
}

static float ecoRainIntensity(float clock) {
    const float p=ecoDayPhase(clock);
    const float front=std::sin((p*6.0f+0.17f)*2.0f*PI);
    const float storm=std::sin((p*19.0f+0.61f)*2.0f*PI);
    return std::clamp(0.18f+0.52f*std::max(0.0f,front)+0.18f*std::max(0.0f,storm),0.0f,1.0f);
}

static float tileHeight(int tx,int tz) {
    const uint32_t h=worldHash(tx,tz);
    const int band=int((h>>5)&0x0fu);
    if(band<3) return 0.08f;
    if(band>12) return 0.30f;
    return 0.16f;
}

static bool proceduralSolidTile(int tx,int tz) {
    const uint32_t h=worldHash(tx,tz);
    const int biome=int((h>>9)&0x0fu);
    // Sparse deterministic ruins/rock clusters. The exact world repeats only
    // after the full torus circumference, while the local view is generated
    // on demand from wrapped tile coordinates.
    if(biome<2) return true;
    if(((h>>17)&0xffu)==0x2au) return true;
    return false;
}

static bool nearPoint(float x,float z,float tx,float tz,float radius);

static bool proceduralSolidAt(float x,float z) {
    const int tx=floorTile(x);
    const int tz=floorTile(z);
    const float centerX=(float(tx)+0.5f)*WORLD_TILE_SIZE;
    const float centerZ=(float(tz)+0.5f)*WORLD_TILE_SIZE;
    const float dx=wrappedDelta(x,centerX);
    const float dz=wrappedDelta(z,centerZ);
    if(std::fabs(dx)>0.92f || std::fabs(dz)>0.92f) return false;

    // Keep the key test landmarks navigable.
    if(nearPoint(x,z,BASE_X,BASE_Z,4.0f)
        || nearPoint(x,z,FACILITY_X,FACILITY_Z,4.0f)
        || nearPoint(x,z,BOSS_X,BOSS_Z,4.0f)) {
        return false;
    }

    return proceduralSolidTile(tx,tz);
}

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

// Identified equipment is deliberately meaningful rather than cosmetic.
// Each recovered UNKNOWN EQUIPMENT piece upgrades the iconic laser platform.
static int laserEquipmentLevel=0;
static bool laserLanceEquipped=false;

static float fabricationTimer=0.0f;
static int fabricationSlot=-1;

enum class BaseMode : uint8_t {
    LAND,
    ORBITAL
};
static BaseMode baseMode=BaseMode::LAND;

static bool actionPointerActive=false;
static int actionPointer=-1;
static int swapPointer=-1;

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
static int mapPointer=-1;
static bool mapExpanded=false;
static float joyX=0.0f, joyY=0.0f;
static float lastAimX=0.0f, lastAimY=0.0f;
static constexpr float PLAYER_RADIUS=0.62f;

static float enemyX=5.0f, enemyZ=-7.0f;
static float enemyYaw=0.0f;
static float enemyHp=40.0f;
static float enemyRespawn=0.0f;
static float enemyAttackTimer=0.7f;

// Landmark boss: persistent in the world until defeated.
static float bossX=BOSS_X, bossZ=BOSS_Z;
static float bossYaw=3.14f;
static float bossHp=180.0f;
static float bossAttackTimer=2.0f;
static float bossLaserT=0.0f;
static float bossShotDirX=0.0f, bossShotDirZ=0.0f;
static float bossHitFlash=0.0f;
static bool bossDefeated=false;

// Autonomous ecosystem. These actors belong to the world, not to the player.
// Their simulation continues regardless of the player's distance, camera,
// death state, or current map view.
enum EcosystemKind : uint8_t {
    ECO_GRAZER=0,
    ECO_SCAVENGER=1,
    ECO_HUNTER=2
};

struct EcoActor {
    bool alive=false;
    int kind=ECO_GRAZER;
    int groupId=-1;
    float x=0.0f, z=0.0f;
    float yaw=0.0f;
    float hp=1.0f;
    float hunger=0.0f;
    float energy=1.0f;
    float age=0.0f;
    float breedCooldown=0.0f;
    float brain=0.0f;
    float corpseTimer=0.0f;
    float fear=0.0f;
    float thirst=0.0f;
    float alert=0.0f;
    float loyalty=0.5f;
    float attackCooldown=0.0f;
    float denX=0.0f, denZ=0.0f;
    int lastPlayerChunkX=0;
    int lastPlayerChunkZ=0;
    int target=-1;
    int homeChunkX=0;
    int homeChunkZ=0;
    uint32_t seed=0;
};

static constexpr int MAX_ECO_ACTORS=96;
static constexpr int INITIAL_ECO_ACTORS=48;
static constexpr float ECO_PLAYER_HEARING_RADIUS=12.0f;
static constexpr float ECO_WRECK_SCAVENGE_RADIUS=18.0f;
static EcoActor ecoActors[MAX_ECO_ACTORS]{};
static float ecosystemClock=0.0f;
static float ecosystemAccumulator=0.0f;
static int ecosystemPopulation=0;

static constexpr int ECO_EVENT_HISTORY=16;
struct EcoEventRecord {
    int type=0; // 1 birth, 2 predation, 3 death, 4 scavenging, 5 cycle, 6 player kill
    int kind=0;
    int chunkX=0;
    int chunkZ=0;
    float stamp=0.0f;
};
static EcoEventRecord ecoEventHistory[ECO_EVENT_HISTORY]{};
static int ecoEventWrite=0;

static int ecosystemBirths=0;
static int ecosystemDeaths=0;
static int ecosystemKills=0;
static int ecosystemLastEvent=0;
static float ecosystemLastEventTimer=0.0f; // 1=birth, 2=predation, 3=starvation, 4=scavenge
static int ecosystemCycle=0;
// Aggregate migration is stepped on the global ecology clock rather than on
// player proximity, so distant populations reorganize even when never seen.
static int ecosystemMigrationStep=0;
static float worldNoise=0.0f;

static void recordEcoEvent(int type,int kind,float x,float z) {
    EcoEventRecord& e=ecoEventHistory[ecoEventWrite];
    e.type=type;
    e.kind=kind;
    e.chunkX=chunkCoord(x);
    e.chunkZ=chunkCoord(z);
    e.stamp=ecosystemClock;
    ecoEventWrite=(ecoEventWrite+1)%ECO_EVENT_HISTORY;
}

static float playerHitFlash=0.0f;
static float enemyHitFlash=0.0f;
static float laserT=0.0f;
static float enemyLaserT=0.0f;
static float enemyShotDirX=0.0f, enemyShotDirZ=0.0f;
static double lastTime=0.0;
static std::string savePath;
static float autosaveTimer=0.0f;
static constexpr uint32_t SAVE_MAGIC=0x44475356u;
static constexpr uint32_t SAVE_VERSION=4u;

static double nowSeconds() {
    static double t=0.0;
    t += 1.0/60.0;
    return t;
}

// Forward declarations for systems whose definitions live later in this
// single-file prototype.
static void initializeBodyPool();
static void initializeEcosystem();
static bool beginFabrication();
static int findOtherBodySlot();
static void saveActiveBodyToPool();
static void loadBodyFromSlot(int slot);
static bool saveGame();
static bool loadGame();


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
    initializeEcosystem();
    loadGame();
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

static void drawProceduralTerrain(const Mat4& vp) {
    const int centerTx=floorTile(px);
    const int centerTz=floorTile(pz);
    constexpr int R=10;

    for(int oz=-R;oz<=R;oz++) {
        for(int ox=-R;ox<=R;ox++) {
            const int tx=centerTx+ox;
            const int tz=centerTz+oz;
            const float centerX=(float(tx)+0.5f)*WORLD_TILE_SIZE;
            const float centerZ=(float(tz)+0.5f)*WORLD_TILE_SIZE;
            // Render the tile's canonical center at its nearest torus image,
            // then offset the neighboring tiles around that center.
            const float localX=nearestWorldImage(centerX,px);
            const float localZ=nearestWorldImage(centerZ,pz);
            const uint32_t h=worldHash(tx,tz);

            // Skip most tiles to keep the draw cost low, but vary all visible
            // ground pieces deterministically so the world reads as generated.
            const int biome=int((h>>9)&0x0fu);
            const float y=tileHeight(tx,tz)-0.10f;

            float r=0.07f + 0.02f*float((h>>1)&7u);
            float g=0.10f + 0.025f*float((h>>4)&7u);
            float b=0.11f + 0.020f*float((h>>7)&7u);
            if(biome<3) {
                r*=0.70f; g*=0.90f; b*=1.12f;
            } else if(biome>12) {
                r*=1.35f; g*=0.92f; b*=0.72f;
            }

            // Use the player-relative placement so the seam never exposes an
            // artificial empty strip when X/Z wrap from +64 to -64.
            drawCube(vp,{localX,y,localZ},{0.96f,0.08f+tileHeight(tx,tz)*0.20f,0.96f},
                     float((h>>22)&3u)*0.5f,r,g,b);

            if(proceduralSolidTile(tx,tz)) {
                const float top=0.25f+0.25f*float((h>>24)&3u);
                drawCube(vp,{localX,top,localZ},{0.60f,top,0.60f},
                         float((h>>18)&3u)*0.4f,
                         0.12f+0.03f*float((h>>26)&3u),
                         0.14f+0.03f*float((h>>28)&3u),
                         0.15f+0.02f*float((h>>30)&3u));
            }
        }
    }
}

static void drawRain(const Mat4& vp) {
    const float rain=ecoRainIntensity(ecosystemClock);
    if(rain<0.40f) return;

    constexpr int DROPS=54;
    float verts[DROPS*2*3]{};
    int n=0;
    const int baseTx=floorTile(px);
    const int baseTz=floorTile(pz);

    for(int i=0;i<DROPS;i++) {
        const uint32_t h=worldHash(baseTx+i*3,baseTz-i*5)^uint32_t(i*0x9E3779B9u);
        const float ox=(float(int((h>>4)&255u))-127.0f)/127.0f*13.0f;
        const float oz=(float(int((h>>12)&255u))-127.0f)/127.0f*10.0f;
        const float startY=3.8f+float((h>>20)&31u)*0.035f;
        const float lenDrop=0.35f+rain*0.65f;

        const float x=px+ox;
        const float z=pz+oz;
        const float dx=0.05f+rain*0.04f;
        const float dy=-lenDrop;

        verts[n++]=x;        verts[n++]=startY;      verts[n++]=z;
        verts[n++]=x+dx;     verts[n++]=startY+dy;   verts[n++]=z+0.02f;
    }

    drawLines(vp,verts,n/3,0.38f,0.58f,0.72f,0.20f+rain*0.34f);
}

static void drawChunkBorders(const Mat4& vp) {
    const int centerChunkX=chunkCoord(px);
    const int centerChunkZ=chunkCoord(pz);
    const float localChunkOriginX=std::floor(wrapWorld(px)/CHUNK_WORLD_SIZE)*CHUNK_WORLD_SIZE;
    const float localChunkOriginZ=std::floor(wrapWorld(pz)/CHUNK_WORLD_SIZE)*CHUNK_WORLD_SIZE;

    float verts[4*2*3]{};
    int n=0;
    for(int i=-2;i<=2;i++) {
        const float x=localChunkOriginX+float(i)*CHUNK_WORLD_SIZE;
        verts[n++]=x; verts[n++]=0.02f; verts[n++]=nearestWorldImage(localChunkOriginZ,pz);
        verts[n++]=x; verts[n++]=0.02f; verts[n++]=nearestWorldImage(localChunkOriginZ+CHUNK_WORLD_SIZE*5.0f,pz);
    }
    for(int i=-2;i<=2;i++) {
        const float z=localChunkOriginZ+float(i)*CHUNK_WORLD_SIZE;
        verts[n++]=nearestWorldImage(localChunkOriginX,px); verts[n++]=0.025f; verts[n++]=z;
        verts[n++]=nearestWorldImage(localChunkOriginX+CHUNK_WORLD_SIZE*5.0f,px); verts[n++]=0.025f; verts[n++]=z;
    }
    (void)centerChunkX;
    (void)centerChunkZ;
    drawLines(vp,verts,n/3,0.20f,0.28f,0.34f,0.55f);
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
    // Legacy test cover, rendered at the nearest torus image.
    drawCube(vp,{nearestWorldImage(-7.0f,px),0.65f,nearestWorldImage(-4.0f,pz)},
             {1.4f,0.65f,1.2f},0.0f,0.12f,0.20f,0.26f);
    drawCube(vp,{nearestWorldImage(-6.0f,px),0.9f,nearestWorldImage(6.0f,pz)},
             {2.0f,0.9f,1.0f},0.08f,0.15f,0.24f,0.20f);
    drawCube(vp,{nearestWorldImage(7.0f,px),0.8f,nearestWorldImage(5.0f,pz)},
             {1.2f,0.8f,1.2f},-0.24f,0.22f,0.17f,0.12f);
    drawCube(vp,{nearestWorldImage(8.0f,px),0.5f,nearestWorldImage(-2.5f,pz)},
             {0.8f,0.5f,2.2f},0.5f,0.12f,0.17f,0.22f);
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

static float laserDamagePerSecond() {
    if(!laserLanceEquipped) return 18.0f;
    return 18.0f + 12.0f*float(laserEquipmentLevel);
}

static float laserHeatRate() {
    if(!laserLanceEquipped) return 0.92f;
    return std::max(0.50f,0.92f-0.12f*float(laserEquipmentLevel));
}

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

static float ecoDistance(int a,int b) {
    return std::sqrt(
        std::max(0.0001f,
            wrappedDelta(ecoActors[a].x,ecoActors[b].x)*
            wrappedDelta(ecoActors[a].x,ecoActors[b].x) +
            wrappedDelta(ecoActors[a].z,ecoActors[b].z)*
            wrappedDelta(ecoActors[a].z,ecoActors[b].z)
        )
    );
}

static float ecoDistanceTo(float x,float z,float tx,float tz) {
    const float dx=wrappedDelta(x,tx);
    const float dz=wrappedDelta(z,tz);
    return std::sqrt(std::max(0.0001f,dx*dx+dz*dz));
}

static int ecoFindEmptySlot() {
    for(int i=0;i<MAX_ECO_ACTORS;i++) {
        if(!ecoActors[i].alive && ecoActors[i].corpseTimer<=0.0f) return i;
    }
    return -1;
}

static void ecoWrapActor(EcoActor& a) {
    a.x=wrapWorld(a.x);
    a.z=wrapWorld(a.z);
}

static void ecoSpawnActor(int slot,int kind,uint32_t seed,float x,float z) {
    EcoActor& a=ecoActors[slot];
    a=EcoActor{};
    a.alive=true;
    a.kind=kind;
    a.seed=seed;
    a.x=wrapWorld(x);
    a.z=wrapWorld(z);
    a.yaw=float(seed%6283u)/1000.0f;
    a.hp=(kind==ECO_HUNTER)?90.0f:((kind==ECO_SCAVENGER)?58.0f:42.0f);
    a.hunger=0.12f+float((seed>>7)&31u)/220.0f;
    a.thirst=0.08f+float((seed>>10)&31u)/260.0f;
    a.energy=0.65f+float((seed>>13)&31u)/100.0f;
    a.fear=0.0f;
    a.alert=0.0f;
    a.loyalty=0.35f+float((seed>>17)&63u)/100.0f;
    a.groupId=(kind==ECO_HUNTER)?int((seed>>12)&3u):int((seed>>18)&7u);
    a.attackCooldown=float((seed>>27)&31u)/31.0f;
    a.age=5.0f+float((seed>>18)&31u);
    a.homeChunkX=chunkCoord(a.x);
    a.homeChunkZ=chunkCoord(a.z);
    a.denX=wrapWorld((float(a.homeChunkX)+0.5f)*CHUNK_WORLD_SIZE
                     +float(int((seed>>21)&15u)-7)*1.2f);
    a.denZ=wrapWorld((float(a.homeChunkZ)+0.5f)*CHUNK_WORLD_SIZE
                     +float(int((seed>>25)&15u)-7)*1.2f);
    a.brain=0.4f+float((seed>>23)&31u)/30.0f;
    ecosystemPopulation++;
}

static void initializeEcosystem() {
    ecosystemPopulation=0;
    ecosystemClock=0.0f;
    ecosystemAccumulator=0.0f;
    ecosystemBirths=0;
    ecosystemDeaths=0;
    ecosystemKills=0;
    ecosystemLastEvent=0;
    ecosystemLastEventTimer=0.0f;
    ecosystemCycle=0;
    ecosystemMigrationStep=0;
    ecoEventWrite=0;
    for(int i=0;i<ECO_EVENT_HISTORY;i++) ecoEventHistory[i]=EcoEventRecord{};

    for(int i=0;i<WORLD_CHUNK_STATE_COUNT;i++) {
        const int cx=(i%WORLD_CHUNK_COUNT)-WORLD_CHUNK_COUNT/2;
        const int cz=(i/WORLD_CHUNK_COUNT)-WORLD_CHUNK_COUNT/2;
        const uint32_t h=chunkHash(cx,cz);
        EcoChunkState& chunk=ecoChunks[i];
        chunk.food=0.35f+float((h>>3)&63u)/100.0f;
        chunk.water=0.25f+float((h>>9)&63u)/120.0f;
        chunk.shelter=0.20f+float((h>>15)&63u)/100.0f;
        chunk.danger=0.08f+float((h>>21)&31u)/180.0f;
        chunk.population=0;
        chunk.dormant[ECO_GRAZER]=2+int((h>>3)&3u);
        chunk.dormant[ECO_SCAVENGER]=1+int((h>>8)&2u);
        chunk.dormant[ECO_HUNTER]=int((h>>13)&1u);
        for(int kind=0;kind<3;kind++) chunk.dormantRemainder[kind]=0.0f;
    }

    for(int i=0;i<MAX_ECO_ACTORS;i++) ecoActors[i]=EcoActor{};

    // Place independent populations across the whole torus. No spawn decision
    // references player coordinates, camera position, or current chunk.
    for(int i=0;i<INITIAL_ECO_ACTORS;i++) {
        const uint32_t seed=worldHash(i*17-31,i*29+7)^(0xA511E9B3u+uint32_t(i)*0x9E3779B9u);
        const int cx=positiveMod(i*7+3,WORLD_CHUNK_COUNT)-WORLD_CHUNK_COUNT/2;
        const int cz=positiveMod(i*13+11,WORLD_CHUNK_COUNT)-WORLD_CHUNK_COUNT/2;
        const float fx=0.18f+float((seed>>3)&255u)/255.0f*0.64f;
        const float fz=0.18f+float((seed>>11)&255u)/255.0f*0.64f;
        const float x=(float(cx)+fx)*CHUNK_WORLD_SIZE;
        const float z=(float(cz)+fz)*CHUNK_WORLD_SIZE;
        const int kind=(i%10<5)?ECO_GRAZER:((i%10<8)?ECO_SCAVENGER:ECO_HUNTER);
        ecoSpawnActor(i,kind,seed,x,z);
    }
}

static EcoChunkState& ecoChunkAt(float x,float z) {
    return ecoChunks[chunkStateIndex(chunkCoord(x),chunkCoord(z))];
}

static Vec3 ecoChunkCenterFromPacked(int packed);

static float ecoChunkSuitability(int cx,int cz) {
    const EcoChunkState& chunk=ecoChunks[chunkStateIndex(cx,cz)];
    return chunk.food*1.15f + chunk.water*0.70f + chunk.shelter*0.20f - chunk.danger*0.35f;
}

static int ecoChunkKindPopulation(int cx,int cz,int kind) {
    int count=0;
    for(int i=0;i<MAX_ECO_ACTORS;i++) {
        const EcoActor& a=ecoActors[i];
        if(a.alive && a.kind==kind &&
           chunkCoord(a.x)==cx && chunkCoord(a.z)==cz) count++;
    }
    return count;
}

static int ecoChunkCapacity(int cx,int cz,int kind) {
    const EcoChunkState& chunk=ecoChunks[chunkStateIndex(cx,cz)];
    const float base=(kind==ECO_GRAZER)?5.0f:
                     ((kind==ECO_SCAVENGER)?3.0f:1.8f);
    const float quality=0.35f+chunk.food*0.55f+chunk.water*0.30f+
                        chunk.shelter*0.22f-chunk.danger*0.30f;
    return std::clamp(int(std::round(base*quality)),1,8);
}

static int ecoDormantCapacity(int kind,const EcoChunkState& chunk) {
    const float base=(kind==ECO_GRAZER)?18.0f:
                     ((kind==ECO_SCAVENGER)?10.0f:5.0f);
    const float quality=0.25f+chunk.food*0.60f+chunk.water*0.35f+
                        chunk.shelter*0.12f-chunk.danger*0.40f;
    return std::clamp(int(std::round(base*quality)),1,24);
}


static float ecoDormantMigrationScore(int cx,int cz,int kind) {
    const EcoChunkState& chunk=ecoChunks[chunkStateIndex(cx,cz)];
    const float prey=float(chunk.dormant[ECO_GRAZER]+chunk.dormant[ECO_SCAVENGER]);

    if(kind==ECO_GRAZER) {
        return chunk.food*1.25f+chunk.water*0.60f+
               chunk.shelter*0.12f-chunk.danger*0.55f;
    }
    if(kind==ECO_SCAVENGER) {
        return chunk.food*0.80f+chunk.water*0.42f+
               chunk.shelter*0.18f-chunk.danger*0.42f;
    }

    // Hunters migrate toward prey-rich chunks, while avoiding severe danger.
    return prey*0.075f+chunk.water*0.18f+
           chunk.shelter*0.18f-chunk.danger*0.48f;
}

static void ecoMigrateDormantPopulation() {
    const int step=int(std::floor(std::max(0.0f,ecosystemClock)/12.0f));
    if(step<=ecosystemMigrationStep) return;
    ecosystemMigrationStep=step;

    // Calculate all moves from the pre-migration state, then apply them. This
    // prevents a population from chaining through several chunks in one pass.
    int migration[WORLD_CHUNK_STATE_COUNT][3]{};

    for(int i=0;i<WORLD_CHUNK_STATE_COUNT;i++) {
        const int sx=(i%WORLD_CHUNK_COUNT)-WORLD_CHUNK_COUNT/2;
        const int sz=(i/WORLD_CHUNK_COUNT)-WORLD_CHUNK_COUNT/2;
        const EcoChunkState& source=ecoChunks[i];

        for(int kind=0;kind<3;kind++) {
            if(source.dormant[kind]<2) continue;

            // Only a subset of chunks moves on each migration step. This keeps
            // the aggregate world dynamic without causing the whole ecosystem
            // to reshuffle every few seconds.
            const uint32_t gate=chunkHash(sx,sz) ^
                uint32_t(kind*0x9E3779B9u) ^
                uint32_t(step*0x85EBCA6Bu);
            if((gate&7u)!=0u) continue;

            const float sourceScore=ecoDormantMigrationScore(sx,sz,kind);
            float bestScore=sourceScore;
            int bestCx=sx;
            int bestCz=sz;

            static constexpr int DX[8]={-1,0,1,-1,1,-1,0,1};
            static constexpr int DZ[8]={-1,-1,-1,0,0,1,1,1};

            for(int n=0;n<8;n++) {
                const int cx=sx+DX[n];
                const int cz=sz+DZ[n];
                const float destinationScore=
                    ecoDormantMigrationScore(cx,cz,kind)-
                    0.055f*float(std::abs(DX[n])+std::abs(DZ[n]));

                if(destinationScore>bestScore+0.085f) {
                    bestScore=destinationScore;
                    bestCx=cx;
                    bestCz=cz;
                }
            }

            if(bestCx==sx && bestCz==sz) continue;

            const EcoChunkState& destination=
                ecoChunks[chunkStateIndex(bestCx,bestCz)];
            const int cap=ecoDormantCapacity(kind,destination);
            const int pending=std::max(0,migration[chunkStateIndex(bestCx,bestCz)][kind]);
            if(int(destination.dormant[kind])+pending>=cap) continue;

            migration[i][kind]--;
            migration[chunkStateIndex(bestCx,bestCz)][kind]++;
        }
    }

    bool movedAny=false;
    for(int i=0;i<WORLD_CHUNK_STATE_COUNT;i++) {
        for(int kind=0;kind<3;kind++) {
            const int delta=migration[i][kind];
            if(delta==0) continue;

            const int next=std::clamp(
                int(ecoChunks[i].dormant[kind])+delta,0,
                int(ecoDormantCapacity(kind,ecoChunks[i])));
            ecoChunks[i].dormant[kind]=uint16_t(next);
            movedAny=true;
        }
    }

    if(movedAny) {
        ecosystemLastEvent=7;
        ecosystemLastEventTimer=2.5f;
    }
}

static void ecoSimulateDormantPopulation(float dt) {
    // Aggregate ecology runs in chunks that have no detailed actors. A small
    // fractional accumulator is retained so population change is not lost
    // every time the 0.20s simulation tick rounds to an integer.
    for(int i=0;i<WORLD_CHUNK_STATE_COUNT;i++) {
        EcoChunkState& chunk=ecoChunks[i];

        const int dormantGrazers=int(chunk.dormant[ECO_GRAZER]);
        const int dormantScavengers=int(chunk.dormant[ECO_SCAVENGER]);
        const int dormantHunters=int(chunk.dormant[ECO_HUNTER]);

        const float quality=std::clamp(
            chunk.food*0.62f+chunk.water*0.28f+chunk.shelter*0.10f-
            chunk.danger*0.45f,0.0f,1.0f);

        // Distant grazers consume the same finite chunk resources as active
        // grazers, so a forgotten region can genuinely be overgrazed.
        const float grazingPressure=float(dormantGrazers)*dt*0.00024f;
        chunk.food=std::max(0.0f,chunk.food-grazingPressure);

        // Distant hunters still form a food chain. Their pressure is applied
        // to aggregate grazer/scavenger populations instead of creating fake
        // off-screen actors.
        const float predatorPressure=float(dormantHunters)*dt*0.0085f;

        for(int kind=0;kind<3;kind++) {
            const int cap=ecoDormantCapacity(kind,chunk);
            float value=float(chunk.dormant[kind])+chunk.dormantRemainder[kind];

            float birthRate=0.0045f+quality*0.015f;
            float deathRate=0.0035f+(1.0f-quality)*0.022f;

            if(kind==ECO_SCAVENGER) {
                // Scavengers benefit slightly from overall organic activity,
                // but too much danger still suppresses their recovery.
                birthRate+=std::min(0.006f,chunk.food*0.004f);
            }
            if(kind==ECO_HUNTER) {
                // Hunters need prey density; barren chunks slowly lose them.
                const float prey=float(dormantGrazers+dormantScavengers);
                birthRate*=std::clamp(prey/6.0f,0.25f,1.0f);
                deathRate+=std::max(0.0f,0.35f-prey*0.015f)*0.020f;
            }

            float delta=dt*(birthRate-deathRate);

            if(kind==ECO_GRAZER) {
                delta-=predatorPressure*0.78f;
                if(chunk.food<0.08f) {
                    delta-=dt*0.055f;
                }
            } else if(kind==ECO_SCAVENGER) {
                delta-=predatorPressure*0.22f;
                if(chunk.water<0.05f) {
                    delta-=dt*0.028f;
                }
            } else if(kind==ECO_HUNTER && chunk.danger>0.55f) {
                delta-=dt*0.020f;
            }

            value=std::clamp(value+delta,0.0f,float(cap));
            const int next=int(std::floor(value));
            chunk.dormant[kind]=uint16_t(std::clamp(next,0,cap));
            chunk.dormantRemainder[kind]=std::clamp(
                value-float(chunk.dormant[kind]),0.0f,0.9999f);
        }
    }
}

static void ecoDemoteDistantActors() {
    constexpr float ACTIVE_RADIUS=150.0f;

    for(int i=0;i<MAX_ECO_ACTORS;i++) {
        EcoActor& a=ecoActors[i];
        if(!a.alive) continue;

        if(ecoDistanceTo(a.x,a.z,px,pz)<=ACTIVE_RADIUS) continue;

        EcoChunkState& chunk=ecoChunkAt(a.x,a.z);
        const int kind=std::clamp(a.kind,0,2);
        const int cap=ecoDormantCapacity(kind,chunk);

        if(int(chunk.dormant[kind])<cap) {
            chunk.dormant[kind]++;
        } else if(a.kind==ECO_HUNTER && chunk.food>0.30f) {
            // Hunters have a lower carrying capacity; if their aggregate
            // bucket is full, simply keep this actor active for now.
            continue;
        }

        a.alive=false;
        a.hp=0.0f;
        a.target=-1;
        a.corpseTimer=0.0f;
        ecosystemPopulation=std::max(0,ecosystemPopulation-1);
    }
}

static void ecoPromoteNearbyPopulation() {
    constexpr int RADIUS_CHUNKS=2;

    const int pcx=chunkCoord(px);
    const int pcz=chunkCoord(pz);

    for(int dz=-RADIUS_CHUNKS;dz<=RADIUS_CHUNKS;dz++) {
        for(int dx=-RADIUS_CHUNKS;dx<=RADIUS_CHUNKS;dx++) {
            const int cx=pcx+dx;
            const int cz=pcz+dz;
            EcoChunkState& chunk=ecoChunks[chunkStateIndex(cx,cz)];

            for(int kind=0;kind<3;kind++) {
                if(chunk.dormant[kind]==0) continue;

                const int slot=ecoFindEmptySlot();
                if(slot<0 || ecosystemPopulation>=MAX_ECO_ACTORS) return;

                const uint32_t h=chunkHash(cx,cz) ^
                    (uint32_t(kind)*0x9E3779B9u) ^
                    uint32_t(ecosystemCycle*0x85EBCA6Bu) ^
                    uint32_t(slot*0xC2B2AE35u);

                const float ox=(float(int((h>>4)&255u))-127.0f)/255.0f*
                               CHUNK_WORLD_SIZE*0.42f;
                const float oz=(float(int((h>>12)&255u))-127.0f)/255.0f*
                               CHUNK_WORLD_SIZE*0.42f;

                ecoSpawnActor(slot,kind,h,
                    wrapWorld((float(cx)+0.5f)*CHUNK_WORLD_SIZE+ox),
                    wrapWorld((float(cz)+0.5f)*CHUNK_WORLD_SIZE+oz));

                chunk.dormant[kind]--;
            }
        }
    }
}

static int ecoFindWaterChunk(int slot) {
    const EcoActor& a=ecoActors[slot];
    const int baseX=chunkCoord(a.x);
    const int baseZ=chunkCoord(a.z);
    float best=-9999.0f;
    int bestPacked=0;
    for(int dz=-3;dz<=3;dz++) {
        for(int dx=-3;dx<=3;dx++) {
            const int cx=baseX+dx;
            const int cz=baseZ+dz;
            const EcoChunkState& chunk=ecoChunks[chunkStateIndex(cx,cz)];
            const float score=chunk.water*1.50f+chunk.shelter*0.15f-
                              chunk.danger*0.30f-
                              0.06f*float(std::abs(dx)+std::abs(dz));
            if(score>best) {
                best=score;
                bestPacked=(positiveMod(cx,WORLD_CHUNK_COUNT)<<16)|
                           positiveMod(cz,WORLD_CHUNK_COUNT);
            }
        }
    }
    return bestPacked;
}

static int ecoFindFoodChunk(int slot) {
    const EcoActor& a=ecoActors[slot];
    const int baseX=chunkCoord(a.x);
    const int baseZ=chunkCoord(a.z);
    float best=-9999.0f;
    int bestPacked=-1;

    for(int dz=-2;dz<=2;dz++) {
        for(int dx=-2;dx<=2;dx++) {
            const int cx=baseX+dx;
            const int cz=baseZ+dz;
            const float score=ecoChunkSuitability(cx,cz)
                -0.08f*float(std::abs(dx)+std::abs(dz));
            if(score>best) {
                best=score;
                bestPacked=(positiveMod(cx,WORLD_CHUNK_COUNT)<<16)
                           | positiveMod(cz,WORLD_CHUNK_COUNT);
            }
        }
    }
    return bestPacked;
}

static Vec3 ecoChunkCenterFromPacked(int packed) {
    const int ux=(packed>>16)&0xffff;
    const int uz=packed&0xffff;
    const int cx=ux>=WORLD_CHUNK_COUNT/2 ? ux-WORLD_CHUNK_COUNT : ux;
    const int cz=uz>=WORLD_CHUNK_COUNT/2 ? uz-WORLD_CHUNK_COUNT : uz;
    return {
        (float(cx)+0.5f)*CHUNK_WORLD_SIZE,
        0.0f,
        (float(cz)+0.5f)*CHUNK_WORLD_SIZE
    };
}

static int ecoChoosePrey(int hunterSlot) {
    float best=999999.0f;
    int bestSlot=-1;
    const EcoActor& h=ecoActors[hunterSlot];

    for(int i=0;i<MAX_ECO_ACTORS;i++) {
        if(i==hunterSlot || !ecoActors[i].alive) continue;
        if(ecoActors[i].kind==ECO_HUNTER) continue;

        const float d=ecoDistance(hunterSlot,i);
        if(d<best && d<28.0f) {
            best=d;
            bestSlot=i;
        }
    }
    return bestSlot;
}

static int ecoChooseCorpse(int scavengerSlot) {
    float best=999999.0f;
    int bestSlot=-1;
    const EcoActor& s=ecoActors[scavengerSlot];

    for(int i=0;i<MAX_ECO_ACTORS;i++) {
        if(i==scavengerSlot || ecoActors[i].alive || ecoActors[i].corpseTimer<=0.0f) continue;
        const float d=ecoDistanceTo(s.x,s.z,ecoActors[i].x,ecoActors[i].z);
        if(d<best && d<30.0f) {
            best=d;
            bestSlot=i;
        }
    }
    return bestSlot;
}

static int ecoFindMate(int slot) {
    const EcoActor& a=ecoActors[slot];
    if(a.kind==ECO_HUNTER) return -1;

    float best=999999.0f;
    int bestSlot=-1;
    for(int i=0;i<MAX_ECO_ACTORS;i++) {
        if(i==slot || !ecoActors[i].alive || ecoActors[i].kind!=a.kind) continue;
        if(ecoActors[i].breedCooldown>0.0f || ecoActors[i].hunger>0.32f || ecoActors[i].energy<0.45f) continue;

        const float d=ecoDistance(slot,i);
        if(d<best && d<2.8f) {
            best=d;
            bestSlot=i;
        }
    }
    return bestSlot;
}

static void ecoKillActor(int slot) {
    if(slot<0 || slot>=MAX_ECO_ACTORS || !ecoActors[slot].alive) return;
    EcoActor& a=ecoActors[slot];
    a.alive=false;
    a.hp=0.0f;
    a.target=-1;
    a.corpseTimer=30.0f+float((a.seed>>5)&31u);
    ecosystemPopulation=std::max(0,ecosystemPopulation-1);
    ecosystemDeaths++;
    ecosystemLastEvent=3;
    ecosystemLastEventTimer=2.5f;
    recordEcoEvent(3,a.kind,a.x,a.z);
}

static void ecoReproduce(int aSlot,int bSlot) {
    if(aSlot<0 || bSlot<0 || ecosystemPopulation>=MAX_ECO_ACTORS) return;
    EcoActor& a=ecoActors[aSlot];
    EcoActor& b=ecoActors[bSlot];
    if(!a.alive || !b.alive || a.kind!=b.kind) return;

    const int slot=ecoFindEmptySlot();
    if(slot<0) return;

    const uint32_t seed=(a.seed*1664525u)+(b.seed*1013904223u)+uint32_t(slot*2654435761u);
    const float ox=(float(int((seed>>2)&63u))-31.0f)*0.018f;
    const float oz=(float(int((seed>>10)&63u))-31.0f)*0.018f;
    ecoSpawnActor(slot,a.kind,seed,wrapWorld((a.x+b.x)*0.5f+ox),wrapWorld((a.z+b.z)*0.5f+oz));
    ecoActors[slot].age=0.0f;
    ecoActors[slot].hunger=0.08f;
    ecoActors[slot].thirst=0.06f;
    ecoActors[slot].energy=0.55f;
    ecoActors[slot].groupId=a.groupId;
    ecoActors[slot].loyalty=std::clamp((a.loyalty+b.loyalty)*0.5f,0.25f,0.95f);
    ecoActors[slot].denX=wrapWorld((a.denX+b.denX)*0.5f);
    ecoActors[slot].denZ=wrapWorld((a.denZ+b.denZ)*0.5f);
    a.breedCooldown=18.0f;
    b.breedCooldown=18.0f;
    ecosystemBirths++;
    ecosystemLastEvent=1;
    ecosystemLastEventTimer=2.5f;
    recordEcoEvent(1,a.kind,a.x,a.z);
}

static void simulateEcosystemTick(float dt) {
    const int oldCycle=ecosystemCycle;
    ecosystemClock+=dt;
    worldNoise=std::max(0.0f,worldNoise-dt*0.45f);
    ecosystemCycle=int(std::floor(ecosystemClock/ECO_CYCLE_SECONDS));
    ecosystemLastEventTimer=std::max(0.0f,ecosystemLastEventTimer-dt);

    const float rain=ecoRainIntensity(ecosystemClock);
    for(int i=0;i<WORLD_CHUNK_STATE_COUNT;i++) {
        EcoChunkState& chunk=ecoChunks[i];
        const uint32_t h=chunkHash((i%WORLD_CHUNK_COUNT)-WORLD_CHUNK_COUNT/2,
                                    (i/WORLD_CHUNK_COUNT)-WORLD_CHUNK_COUNT/2);

        // Food regrows globally, with rain accelerating recovery. Industrial
        // chunks recover more slowly and provide more shelter.
        const float industrial=chunkBiomeIsIndustrial(
            (i%WORLD_CHUNK_COUNT)-WORLD_CHUNK_COUNT/2,
            (i/WORLD_CHUNK_COUNT)-WORLD_CHUNK_COUNT/2
        )?1.0f:0.0f;
        chunk.food=std::clamp(chunk.food+dt*(0.0022f+rain*0.006f)*(1.0f-industrial*0.35f),0.0f,1.0f);
        chunk.water=std::clamp(chunk.water+rain*dt*0.014f-dt*0.001f,0.0f,1.0f);
        chunk.danger=std::clamp(chunk.danger-dt*0.004f,0.02f,0.75f);
        chunk.shelter=std::clamp(0.20f+float((h>>15)&63u)/100.0f+(industrial*0.22f),0.05f,1.0f);
        chunk.population=0;
    }

    for(int i=0;i<MAX_ECO_ACTORS;i++) {
        if(ecoActors[i].alive) {
            ecoChunks[chunkStateIndex(chunkCoord(ecoActors[i].x),chunkCoord(ecoActors[i].z))].population++;
        }
    }

    if(wreck.active) {
        float pressure=0.0f;
        for(int i=0;i<MAX_ECO_ACTORS;i++) {
            const EcoActor& a=ecoActors[i];
            if(!a.alive || a.kind!=ECO_SCAVENGER) continue;
            if(ecoDistanceTo(a.x,a.z,wreck.x,wreck.z)<ECO_WRECK_SCAVENGE_RADIUS) {
                pressure+=0.00055f*dt;
            }
        }
        if(pressure>0.0f) {
            wreck.salvagePercent=std::max(0.0f,
                wreck.salvagePercent-pressure*100.0f);
            for(int p=0;p<PART_COUNT;p++) {
                if(wreck.parts[p].hp>0.0f) {
                    wreck.parts[p].hp=std::max(0.0f,
                        wreck.parts[p].hp-pressure*wreck.parts[p].maxHp);
                }
            }
        }
    }

    if(oldCycle!=ecosystemCycle && ecosystemCycle>0) {
        ecosystemLastEvent=5;
        ecosystemLastEventTimer=2.5f;
    }

    ecoSimulateDormantPopulation(dt);
    ecoMigrateDormantPopulation();
    ecoDemoteDistantActors();
    ecoPromoteNearbyPopulation();

    for(int i=0;i<MAX_ECO_ACTORS;i++) {
        EcoActor& a=ecoActors[i];

        if(!a.alive) {
            if(a.corpseTimer>0.0f) {
                a.corpseTimer=std::max(0.0f,a.corpseTimer-dt);
            }
            continue;
        }

        a.age+=dt;
        a.hunger=std::min(1.2f,a.hunger+dt*(a.kind==ECO_HUNTER?0.024f:0.018f));
        a.thirst=std::min(1.2f,a.thirst+dt*0.014f);
        const bool night=ecoIsNight(ecosystemClock);
        a.energy=std::max(0.0f,a.energy-dt*(night?0.004f:0.007f));
        a.breedCooldown=std::max(0.0f,a.breedCooldown-dt);
        a.fear=std::max(0.0f,a.fear-dt*0.035f);
        a.alert=std::max(0.0f,a.alert-dt*0.020f);
        a.attackCooldown=std::max(0.0f,a.attackCooldown-dt);

        EcoChunkState& currentChunk=ecoChunkAt(a.x,a.z);

        const float playerDx=wrappedDelta(a.x,px);
        const float playerDz=wrappedDelta(a.z,pz);
        const float playerDistance=std::sqrt(std::max(0.0001f,
            playerDx*playerDx+playerDz*playerDz));
        const bool playerDetected=
            playerDistance<(ECO_PLAYER_HEARING_RADIUS+worldNoise*7.0f);

        if(playerDetected) {
            a.lastPlayerChunkX=chunkCoord(px);
            a.lastPlayerChunkZ=chunkCoord(pz);
            a.alert=std::min(1.0f,
                a.alert+dt*(0.18f+worldNoise*0.50f));
            if(a.kind!=ECO_HUNTER) {
                a.fear=std::min(1.0f,
                    a.fear+dt*(0.26f+worldNoise*0.60f));
            }

            // Alarm propagation: one creature noticing a disturbance can warn
            // nearby members of its own group. This creates emergent reactions
            // without creating a player-centric scripted event.
            for(int j=0;j<MAX_ECO_ACTORS;j++) {
                if(j==i || !ecoActors[j].alive ||
                   ecoActors[j].kind!=a.kind ||
                   ecoActors[j].groupId!=a.groupId) continue;
                const float gx=wrappedDelta(a.x,ecoActors[j].x);
                const float gz=wrappedDelta(a.z,ecoActors[j].z);
                if(gx*gx+gz*gz<49.0f) {
                    ecoActors[j].alert=std::min(1.0f,ecoActors[j].alert+dt*0.35f);
                    if(ecoActors[j].kind!=ECO_HUNTER) {
                        ecoActors[j].fear=std::min(1.0f,ecoActors[j].fear+dt*0.22f);
                    }
                }
            }
        }
        if(night && currentChunk.shelter>0.68f) {
            a.energy=std::min(1.0f,a.energy+dt*0.014f);
            a.alert=std::max(0.0f,a.alert-dt*0.030f);
        }
        const float rain=ecoRainIntensity(ecosystemClock);
        currentChunk.water=std::clamp(currentChunk.water+rain*dt*0.018f-dt*0.002f,0.0f,1.0f);

        if(a.hunger>=1.0f || a.thirst>=1.0f || a.age>1800.0f) {
            ecoKillActor(i);
            continue;
        }

        float moveX=0.0f;
        float moveZ=0.0f;
        const float seedPhase=float(a.seed%628u)*0.01f;

        if(a.thirst>0.74f) {
            const int packed=ecoFindWaterChunk(i);
            const Vec3 water=ecoChunkCenterFromPacked(packed);
            const float dx=wrappedDelta(a.x,water.x);
            const float dz=wrappedDelta(a.z,water.z);
            const float d=std::sqrt(std::max(0.0001f,dx*dx+dz*dz));
            if(d>2.0f) {
                moveX=dx/d;
                moveZ=dz/d;
                a.yaw=std::atan2(moveX,-moveZ);
            } else {
                EcoChunkState& waterHere=ecoChunkAt(a.x,a.z);
                a.thirst=std::max(0.0f,a.thirst-dt*0.18f);
                waterHere.water=std::max(0.0f,waterHere.water-dt*0.004f);
                a.energy=std::min(1.0f,a.energy+dt*0.008f);
            }
        } else if(a.kind==ECO_HUNTER) {
            if(a.target<0 || !ecoActors[a.target].alive ||
               ecoDistance(i,a.target)>34.0f) {
                a.target=ecoChoosePrey(i);
            }

            if(a.target<0 && a.loyalty>0.62f) {
                for(int j=0;j<MAX_ECO_ACTORS;j++) {
                    if(j==i || !ecoActors[j].alive ||
                       ecoActors[j].kind!=ECO_HUNTER ||
                       ecoActors[j].groupId!=a.groupId) continue;
                    const int shared=ecoActors[j].target;
                    if(shared>=0 && shared<MAX_ECO_ACTORS &&
                       ecoActors[shared].alive) {
                        a.target=shared;
                        break;
                    }
                }
            }

            if(a.target>=0) {
                const float dx=wrappedDelta(a.x,ecoActors[a.target].x);
                const float dz=wrappedDelta(a.z,ecoActors[a.target].z);
                const float d=std::sqrt(std::max(0.0001f,dx*dx+dz*dz));
                moveX=dx/d;
                moveZ=dz/d;
                a.yaw=std::atan2(dx,-dz);

                if(d<1.6f) {
                    EcoActor& prey=ecoActors[a.target];
                    prey.hp-=8.0f*dt;
                    a.hunger=std::max(0.0f,a.hunger-0.110f*dt);
                    a.thirst=std::max(0.0f,a.thirst-0.015f*dt);
                    a.energy=std::min(1.0f,a.energy+0.030f*dt);
                    a.fear=std::max(0.0f,a.fear-0.08f*dt);
                    if(prey.hp<=0.0f) {
                        ecosystemKills++;
                        recordEcoEvent(2,prey.kind,prey.x,prey.z);
                        ecoKillActor(a.target);
                        ecosystemLastEvent=2;
                        ecosystemLastEventTimer=2.5f;
                        a.target=-1;
                    }
                }
            } else {
                if(ecoIsNight(ecosystemClock) && a.hunger<0.55f) {
                    const float dx=wrappedDelta(a.x,a.denX);
                    const float dz=wrappedDelta(a.z,a.denZ);
                    const float d=std::sqrt(std::max(0.0001f,dx*dx+dz*dz));
                    if(d>1.8f) {
                        moveX=dx/d;
                        moveZ=dz/d;
                        a.yaw=std::atan2(moveX,-moveZ);
                    } else {
                        a.energy=std::min(1.0f,a.energy+dt*0.022f);
                        moveX=std::sin(a.yaw)*0.12f;
                        moveZ=-std::cos(a.yaw)*0.12f;
                    }
                } else {
                    a.yaw+=std::sin(ecosystemClock*0.35f+seedPhase)*dt*0.45f;
                    moveX=std::sin(a.yaw);
                    moveZ=-std::cos(a.yaw);
                }
            }
        } else if(a.kind==ECO_GRAZER) {
            const int packed=ecoFindFoodChunk(i);
            const Vec3 foodCenter=ecoChunkCenterFromPacked(packed);
            const float dx=wrappedDelta(a.x,foodCenter.x);
            const float dz=wrappedDelta(a.z,foodCenter.z);
            const float d=std::sqrt(std::max(0.0001f,dx*dx+dz*dz));
            EcoChunkState& here=ecoChunkAt(a.x,a.z);

            if(here.food>0.18f && d<18.0f) {
                here.food=std::max(0.0f,here.food-dt*0.018f);
                a.hunger=std::max(0.0f,a.hunger-dt*0.100f);
                a.thirst=std::max(0.0f,a.thirst-dt*0.030f*here.water);
                a.energy=std::min(1.0f,a.energy+dt*0.018f);
                moveX=dx/d*0.18f;
                moveZ=dz/d*0.18f;
            } else {
                moveX=dx/d;
                moveZ=dz/d;
            }

            // Grazers avoid dangerous chunks and reduce fear during shelter.
            a.fear=std::min(1.0f,a.fear+here.danger*dt*0.08f);
            if(here.shelter>0.62f) {
                a.energy=std::min(1.0f,a.energy+dt*0.010f);
            }

            float herdX=0.0f,herdZ=0.0f;
            int herdCount=0;
            for(int j=0;j<MAX_ECO_ACTORS;j++) {
                if(j==i || !ecoActors[j].alive ||
                   ecoActors[j].kind!=a.kind ||
                   ecoActors[j].groupId!=a.groupId) continue;
                const float hx=wrappedDelta(a.x,ecoActors[j].x);
                const float hz=wrappedDelta(a.z,ecoActors[j].z);
                if(hx*hx+hz*hz<64.0f) {
                    herdX+=hx;
                    herdZ+=hz;
                    herdCount++;
                }
            }
            if(herdCount>0 && a.fear<0.55f) {
                moveX+=herdX*0.020f*a.loyalty;
                moveZ+=herdZ*0.020f*a.loyalty;
            }

            const float side=std::sin(ecosystemClock*0.12f+seedPhase)*0.35f;
            moveX+=std::cos(a.yaw)*side;
            moveZ+=std::sin(a.yaw)*side;
            a.yaw=std::atan2(moveX,-moveZ);
        } else {
            int corpse=ecoChooseCorpse(i);
            if(corpse>=0) {
                const float dx=wrappedDelta(a.x,ecoActors[corpse].x);
                const float dz=wrappedDelta(a.z,ecoActors[corpse].z);
                const float d=std::sqrt(std::max(0.0001f,dx*dx+dz*dz));
                moveX=dx/d;
                moveZ=dz/d;
                a.yaw=std::atan2(dx,-dz);

                if(d<1.7f) {
                    a.hunger=std::max(0.0f,a.hunger-dt*0.120f);
                    a.thirst=std::max(0.0f,a.thirst-dt*0.022f);
                    a.energy=std::min(1.0f,a.energy+dt*0.030f);
                    ecoActors[corpse].corpseTimer=std::max(0.0f,ecoActors[corpse].corpseTimer-dt*3.0f);
                    ecosystemLastEvent=4;
                    ecosystemLastEventTimer=2.5f;
                    recordEcoEvent(4,a.kind,a.x,a.z);
                }
            } else {
                a.yaw+=std::sin(ecosystemClock*0.18f+seedPhase)*dt*0.8f;
                moveX=std::sin(a.yaw);
                moveZ=-std::cos(a.yaw);
            }
        }

        float speed=(a.kind==ECO_HUNTER)?1.35f:((a.kind==ECO_GRAZER)?0.62f:0.86f);
        if(ecoIsNight(ecosystemClock) && a.kind==ECO_GRAZER) speed*=0.45f;
        if(a.fear>0.65f) speed*=1.18f;

        const float magnitude=std::sqrt(std::max(0.0001f,moveX*moveX+moveZ*moveZ));
        moveX/=magnitude;
        moveZ/=magnitude;

        const float nextX=wrapWorld(a.x+moveX*speed*dt);
        const float nextZ=wrapWorld(a.z+moveZ*speed*dt);

        // Ecosystem agents can route around generated solid tiles. If a step is
        // blocked, they turn rather than freezing forever.
        if(!proceduralSolidAt(nextX,a.z)) a.x=nextX;
        else a.yaw+=1.10f;

        if(!proceduralSolidAt(a.x,nextZ)) a.z=nextZ;
        else a.yaw+=0.70f;

        if(playerDetected && a.kind!=ECO_HUNTER &&
           (playerDistance<ECO_PLAYER_HEARING_RADIUS || worldNoise>0.28f)) {
            const float d=std::max(0.001f,playerDistance);
            moveX=playerDx/d;
            moveZ=playerDz/d;
            a.yaw=std::atan2(moveX,-moveZ);
            a.alert=std::min(1.0f,a.alert+dt*0.20f);
        }

        ecoWrapActor(a);

        EcoChunkState& landed=ecoChunkAt(a.x,a.z);
        const int landedCx=chunkCoord(a.x);
        const int landedCz=chunkCoord(a.z);
        const int localKindCount=ecoChunkKindPopulation(landedCx,landedCz,a.kind);
        const int localCapacity=ecoChunkCapacity(landedCx,landedCz,a.kind);

        if(a.kind==ECO_HUNTER) {
            landed.danger=std::min(0.90f,landed.danger+dt*0.035f);
        } else if(a.fear>0.45f) {
            a.energy=std::max(0.0f,a.energy-dt*0.004f);
        }

        if(localKindCount>localCapacity && a.hunger<0.70f && a.thirst<0.70f) {
            const int packed=ecoFindFoodChunk(i);
            const Vec3 destination=ecoChunkCenterFromPacked(packed);
            const float dx=wrappedDelta(a.x,destination.x);
            const float dz=wrappedDelta(a.z,destination.z);
            const float d=std::sqrt(std::max(0.0001f,dx*dx+dz*dz));
            if(d>5.0f) {
                moveX=dx/d;
                moveZ=dz/d;
                a.alert=std::min(1.0f,a.alert+dt*0.15f);
            }
        }

        const int mate=ecoFindMate(i);
        if(mate>=0 && localKindCount<localCapacity &&
           ((uint32_t(int(a.age*10.0f))^a.seed)&31u)==0u) {
            ecoReproduce(i,mate);
        }
    }

    // LOD transitions happen before detailed actor logic, so the active
    // population can drift this tick. Refresh the chunk counters afterward
    // for accurate HUD/minimap reporting.
    refreshEcoChunkActivePopulation();
}

static void updateEcosystem(float dt) {
    ecosystemAccumulator+=dt;
    constexpr float TICK=0.20f;

    int safety=0;
    while(ecosystemAccumulator>=TICK && safety<4) {
        ecosystemAccumulator-=TICK;
        simulateEcosystemTick(TICK);
        safety++;
    }
}

static int ecosystemChunkPopulation(int cx,int cz) {
    int count=0;
    for(int i=0;i<MAX_ECO_ACTORS;i++) {
        if(!ecoActors[i].alive) continue;
        if(chunkCoord(ecoActors[i].x)==cx && chunkCoord(ecoActors[i].z)==cz) count++;
    }
    return count;
}

static int ecoChunkTotalPopulation(int cx,int cz) {
    const EcoChunkState& chunk=ecoChunks[chunkStateIndex(cx,cz)];
    return ecosystemChunkPopulation(cx,cz)
        + int(chunk.dormant[ECO_GRAZER])
        + int(chunk.dormant[ECO_SCAVENGER])
        + int(chunk.dormant[ECO_HUNTER]);
}

static int ecoDormantTotalPopulation() {
    int total=0;
    for(int i=0;i<WORLD_CHUNK_STATE_COUNT;i++) {
        const EcoChunkState& chunk=ecoChunks[i];
        total+=int(chunk.dormant[ECO_GRAZER]);
        total+=int(chunk.dormant[ECO_SCAVENGER]);
        total+=int(chunk.dormant[ECO_HUNTER]);
    }
    return total;
}

static int ecoTotalPopulation() {
    return ecosystemPopulation+ecoDormantTotalPopulation();
}

static void refreshEcoChunkActivePopulation() {
    for(int i=0;i<WORLD_CHUNK_STATE_COUNT;i++) {
        ecoChunks[i].population=0;
    }
    for(int i=0;i<MAX_ECO_ACTORS;i++) {
        if(!ecoActors[i].alive) continue;
        ecoChunks[chunkStateIndex(chunkCoord(ecoActors[i].x),chunkCoord(ecoActors[i].z))].population++;
    }
}

static void drawEcosystem(const Mat4& vp) {
    for(int i=0;i<MAX_ECO_ACTORS;i++) {
        const EcoActor& a=ecoActors[i];
        if(!a.alive) {
            if(a.corpseTimer<=0.0f) continue;
            const float d=ecoDistanceTo(px,pz,a.x,a.z);
            if(d>42.0f) continue;

            const float cx=nearestWorldImage(a.x,px);
            const float cz=nearestWorldImage(a.z,pz);
            drawCube(vp,{cx,0.16f,cz},{0.28f,0.08f,0.28f},a.yaw,
                     0.16f,0.12f,0.10f,0.80f);
            continue;
        }

        const float d=ecoDistanceTo(px,pz,a.x,a.z);
        if(d>42.0f) continue;

        const float x=nearestWorldImage(a.x,px);
        const float z=nearestWorldImage(a.z,pz);
        const float size=(a.kind==ECO_HUNTER)?0.45f:((a.kind==ECO_SCAVENGER)?0.32f:0.28f);
        const float y=size+0.10f;
        const float r=(a.kind==ECO_HUNTER)?0.72f:((a.kind==ECO_SCAVENGER)?0.54f:0.22f);
        const float g=(a.kind==ECO_HUNTER)?0.20f:((a.kind==ECO_SCAVENGER)?0.48f:0.62f);
        const float b=(a.kind==ECO_HUNTER)?0.12f:((a.kind==ECO_SCAVENGER)?0.62f:0.34f);

        drawCube(vp,{x,y,z},{size,size*0.72f,size},a.yaw,r,g,b);

        if(a.alert>0.45f) {
            drawCube(vp,{x,y+size*0.85f,z},
                     {size*0.35f,0.05f,size*0.35f},a.yaw,
                     0.64f,0.50f,0.16f,0.60f);
        }

        // Hunger/condition cue: weak actors become visually dimmer/smaller.
        const float condition=std::clamp(1.0f-a.hunger*0.65f,0.35f,1.0f);
        if(condition<0.58f) {
            drawCube(vp,{x,y+size*0.72f,z},{size*0.42f,0.08f,size*0.42f},a.yaw,
                     0.50f,0.18f,0.10f,0.55f);
        }
    }
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
    const float wrappedX=wrapWorld(x);
    const float wrappedZ=wrapWorld(z);

    // Procedural solids live on the toroidal tile field.
    if(proceduralSolidAt(wrappedX,wrappedZ)) return true;

    for (const auto& b : OBSTACLES) {
        // Test the obstacle in the nearest torus image. This keeps collision
        // correct even when the player approaches a landmark across the seam.
        const float obstacleX=wrapWorld((b.minX+b.maxX)*0.5f);
        const float obstacleZ=wrapWorld((b.minZ+b.maxZ)*0.5f);
        const float halfX=(b.maxX-b.minX)*0.5f;
        const float halfZ=(b.maxZ-b.minZ)*0.5f;
        const float centerDx=wrappedDelta(wrappedX,obstacleX);
        const float centerDz=wrappedDelta(wrappedZ,obstacleZ);
        const float cx=std::clamp(centerDx,-halfX,halfX);
        const float cz=std::clamp(centerDz,-halfZ,halfZ);
        const float dx=centerDx-cx;
        const float dz=centerDz-cz;
        if(dx*dx+dz*dz<radius*radius) return true;
    }

    return false;
}

static void movePlayer(float dx, float dz) {
    const float nextX=wrapWorld(px+dx);
    if(!collidesObstacle(nextX,pz,PLAYER_RADIUS)) {
        px=nextX;
    }

    const float nextZ=wrapWorld(pz+dz);
    if(!collidesObstacle(px,nextZ,PLAYER_RADIUS)) {
        pz=nextZ;
    }
}

static void moveEnemy(float dx, float dz) {
    const float nextX=wrapWorld(enemyX+dx);
    if(!collidesObstacle(nextX,enemyZ,0.65f)) {
        enemyX=nextX;
    }

    const float nextZ=wrapWorld(enemyZ+dz);
    if(!collidesObstacle(enemyX,nextZ,0.65f)) {
        enemyZ=nextZ;
    }
}

static bool nearPoint(float x,float z,float tx,float tz,float radius) {
    const float dx=wrappedDelta(x,tx);
    const float dz=wrappedDelta(z,tz);
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
        laserEquipmentLevel=std::min(3,laserEquipmentLevel+1);
        laserLanceEquipped=true;
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

static int closestPlayerPartOnRay(Vec3 start,Vec3 dir,float maxDistance);

static void drawEnemy(const Mat4& vp) {
    const float ex=nearestWorldImage(enemyX,px);
    const float ez=nearestWorldImage(enemyZ,pz);
    const float r = enemyHitFlash>0 ? 0.95f : 0.55f;
    const float g = enemyHitFlash>0 ? 0.85f : 0.18f;
    const float b = enemyHitFlash>0 ? 0.20f : 0.14f;

    drawCube(vp,{ex,0.95f,ez},{0.90f,0.85f,0.90f},enemyYaw,r,g,b);
    drawCube(vp,{ex,1.70f,ez},{0.45f,0.35f,0.45f},enemyYaw,0.45f,0.15f,0.12f);
    drawCube(vp,localOffset({ex,0,ez},{0,1.40f,-0.95f},enemyYaw),
             {0.18f,0.14f,0.70f},enemyYaw,0.78f,0.25f,0.18f);
}

static void drawBoss(const Mat4& vp) {
    if(bossDefeated) return;

    const float bx=nearestWorldImage(bossX,px);
    const float bz=nearestWorldImage(bossZ,pz);
    const float r = bossHitFlash>0 ? 1.0f : 0.34f;
    const float g = bossHitFlash>0 ? 0.65f : 0.12f;
    const float b = bossHitFlash>0 ? 0.16f : 0.08f;

    // Heavy core.
    drawCube(vp,{bx,1.20f,bz},{1.35f,1.10f,1.20f},bossYaw,r,g,b);
    drawCube(vp,localOffset({bx,0,bz},{0,2.55f,0.0f},bossYaw),
             {0.72f,0.48f,0.72f},bossYaw,0.50f,0.14f,0.10f);

    // Oversized shoulder / arm blocks.
    drawCube(vp,localOffset({bx,0,bz},{-1.55f,1.30f,0.0f},bossYaw),
             {0.52f,0.72f,0.62f},bossYaw,0.28f,0.10f,0.08f);
    drawCube(vp,localOffset({bx,0,bz},{ 1.55f,1.30f,0.0f},bossYaw),
             {0.52f,0.72f,0.62f},bossYaw,0.28f,0.10f,0.08f);

    // Legs.
    drawCube(vp,localOffset({bx,0,bz},{-0.62f,-0.10f,0.0f},bossYaw),
             {0.45f,0.72f,0.52f},bossYaw,0.24f,0.09f,0.07f);
    drawCube(vp,localOffset({bx,0,bz},{ 0.62f,-0.10f,0.0f},bossYaw),
             {0.45f,0.72f,0.52f},bossYaw,0.24f,0.09f,0.07f);

    // Large front cannon.
    drawCube(vp,localOffset({bx,0,bz},{0,1.65f,-1.45f},bossYaw),
             {0.25f,0.24f,1.10f},bossYaw,0.72f,0.20f,0.12f);

    // Landmark beacon.
    drawCube(vp,{bossX,3.15f,bossZ},{0.10f,0.35f,0.10f},0.0f,
             0.90f,0.24f,0.08f,0.75f);
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

static void drawBossLaser(const Mat4& vp) {
    if(bossLaserT<=0.0f || bossDefeated) return;

    const Vec3 start={nearestWorldImage(bossX,px),1.65f,
                      nearestWorldImage(bossZ,pz)};
    const Vec3 end={
        start.x + bossShotDirX*16.0f,
        1.20f,
        start.z + bossShotDirZ*16.0f
    };
    const float verts[]={
        start.x,start.y,start.z,
        end.x,end.y,end.z
    };
    drawLines(vp,verts,2,1.0f,0.08f,0.06f,0.95f);
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
    static constexpr uint8_t X[7] = {17,10,4,4,10,17,17};
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
        case 'X': return X[row];
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
    const float baseDrawX=nearestWorldImage(BASE_X,px);
    const float baseDrawZ=nearestWorldImage(BASE_Z,pz);
    drawCube(vp,{baseDrawX,baseY,baseDrawZ},{2.0f,0.85f,2.0f},0.0f,
             0.13f,0.24f,0.33f);
    drawCube(vp,{baseDrawX,baseY+1.15f,baseDrawZ},{1.0f,0.25f,1.0f},0.0f,
             0.26f,0.55f,0.70f);

    // Discovery facility.
    const float facilityDrawX=nearestWorldImage(FACILITY_X,px);
    const float facilityDrawZ=nearestWorldImage(FACILITY_Z,pz);
    drawCube(vp,{facilityDrawX,1.0f,facilityDrawZ},{1.5f,1.0f,1.5f},0.1f,
             0.26f,0.20f,0.14f);
    drawCube(vp,{facilityDrawX,2.25f,facilityDrawZ},{0.75f,0.25f,0.75f},0.1f,
             0.50f,0.34f,0.12f);

    // Unknown equipment pod outside the facility.
    if(unknownEquipment>0) {
        drawCube(vp,{nearestWorldImage(FACILITY_X+2.0f,px),0.35f,
                     nearestWorldImage(FACILITY_Z,pz)},
                 {0.30f,0.35f,0.30f},0.2f,
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
                vp,
                {nearestWorldImage(p.x,px),p.y,nearestWorldImage(p.z,pz)},
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

static bool pointInMapButton(float x,float y,float w,float h) {
    const float cx=w*0.92f;
    const float cy=h*0.12f;
    const float radius=h*0.055f;
    const float dx=x-cx, dy=y-cy;
    return dx*dx+dy*dy<=radius*radius;
}

static void drawMinimap(const Mat4& hud,bool expanded) {
    const int chunkCount=WORLD_CHUNK_COUNT;
    const int centerX=chunkCoord(px);
    const int centerZ=chunkCoord(pz);

    if(!expanded) {
        const float size=viewportH*0.22f;
        const float x0=viewportW-size-18.0f;
        const float y0=viewportH*0.05f;
        drawRect2D(hud,x0,y0,x0+size,y0+size,0.025f,0.04f,0.055f,0.82f);

        constexpr int visible=7;
        const float cell=size/float(visible);
        for(int mz=0;mz<visible;mz++) {
            for(int mx=0;mx<visible;mx++) {
                const int cx=centerX+mx-visible/2;
                const int cz=centerZ+mz-visible/2;
                const uint32_t h=chunkHash(cx,cz);
                const int population=ecoChunkTotalPopulation(cx,cz);
                const bool industrial=chunkBiomeIsIndustrial(cx,cz);
                const float life=std::clamp(float(population)/18.0f,0.0f,1.0f);
                const float r=industrial?(0.38f+life*0.10f):(0.09f+0.03f*float((h>>3)&3u)+life*0.10f);
                const float g=industrial?(0.25f+life*0.18f):(0.16f+0.025f*float((h>>6)&3u)+life*0.18f);
                const float b=industrial?(0.15f+life*0.08f):(0.20f+0.03f*float((h>>9)&3u)+life*0.08f);
                drawRect2D(hud,x0+mx*cell+1.0f,y0+mz*cell+1.0f,
                           x0+(mx+1)*cell-1.0f,y0+(mz+1)*cell-1.0f,
                           r,g,b,0.92f);
            }
        }
        const float pcx=x0+(visible/2+0.5f)*cell;
        const float pcy=y0+(visible/2+0.5f)*cell;
        drawCircle2D(hud,pcx,pcy,std::max(3.0f,cell*0.28f),0.95f,0.95f,0.85f,0.95f);

        // Stable landmark markers in the local minimap window.
        auto marker=[&](int cx,int cz,float r,float g,float b) {
            const int dx=cx-centerX;
            const int dz=cz-centerZ;
            if(std::abs(dx)>visible/2 || std::abs(dz)>visible/2) return;
            const float mx= x0+(float(dx+visible/2)+0.5f)*cell;
            const float my= y0+(float(dz+visible/2)+0.5f)*cell;
            drawCircle2D(hud,mx,my,std::max(2.5f,cell*0.18f),r,g,b,0.95f);
        };
        marker(chunkCoord(BASE_X),chunkCoord(BASE_Z),0.30f,0.80f,1.0f);
        marker(chunkCoord(FACILITY_X),chunkCoord(FACILITY_Z),0.90f,0.60f,0.20f);
        if(!bossDefeated) marker(chunkCoord(BOSS_X),chunkCoord(BOSS_Z),0.95f,0.25f,0.15f);
        return;
    }

    // Expanded map: entire finite torus, with chunk grid and persistent landmarks.
    const float size=std::min(viewportW*0.82f,viewportH*0.82f);
    const float x0=(viewportW-size)*0.50f;
    const float y0=(viewportH-size)*0.50f;
    drawRect2D(hud,x0-10.0f,y0-30.0f,x0+size+10.0f,y0+size+10.0f,
               0.015f,0.025f,0.040f,0.96f);

    const float cell=size/float(chunkCount);
    for(int mz=0;mz<chunkCount;mz++) {
        for(int mx=0;mx<chunkCount;mx++) {
            const int signedX=mx-(chunkCount/2);
            const int signedZ=mz-(chunkCount/2);
            const uint32_t h=chunkHash(signedX,signedZ);
            const int population=ecoChunkTotalPopulation(signedX,signedZ);
            const bool industrial=chunkBiomeIsIndustrial(signedX,signedZ);
            const float life=std::clamp(float(population)/18.0f,0.0f,1.0f);
            const float r=industrial?(0.40f+life*0.08f):(0.07f+0.025f*float((h>>3)&3u)+life*0.10f);
            const float g=industrial?(0.24f+life*0.16f):(0.12f+0.024f*float((h>>6)&3u)+life*0.18f);
            const float b=industrial?(0.14f+life*0.08f):(0.18f+0.028f*float((h>>9)&3u)+life*0.08f);
            drawRect2D(hud,x0+mx*cell+1.0f,y0+mz*cell+1.0f,
                       x0+(mx+1)*cell-1.0f,y0+(mz+1)*cell-1.0f,
                       r,g,b,0.92f);
        }
    }

    // Player position in canonical map space.
    const float mapX=wrapWorld(px)+WORLD_HALF;
    const float mapZ=wrapWorld(pz)+WORLD_HALF;
    const float pcx=x0+(mapX/WORLD_SIZE)*size;
    const float pcy=y0+(mapZ/WORLD_SIZE)*size;
    drawCircle2D(hud,pcx,pcy,6.0f,0.95f,0.95f,0.85f,1.0f);

    auto landmarkAbs=[&](float wx,float wz,float r,float g,float b) {
        const float mx=x0+((wrapWorld(wx)+WORLD_HALF)/WORLD_SIZE)*size;
        const float my=y0+((wrapWorld(wz)+WORLD_HALF)/WORLD_SIZE)*size;
        drawCircle2D(hud,mx,my,5.0f,r,g,b,0.95f);
    };
    landmarkAbs(BASE_X,BASE_Z,0.30f,0.80f,1.0f);
    landmarkAbs(FACILITY_X,FACILITY_Z,0.90f,0.60f,0.20f);
    if(!bossDefeated) landmarkAbs(BOSS_X,BOSS_Z,0.95f,0.25f,0.15f);

    // Recent ecosystem events leave temporary strategic traces. The traces are
    // generated by the world simulation, not by player proximity.
    for(int i=0;i<ECO_EVENT_HISTORY;i++) {
        const EcoEventRecord& e=ecoEventHistory[i];
        if(e.stamp<=0.0f) continue;
        const float age=ecosystemClock-e.stamp;
        if(age<0.0f || age>60.0f) continue;

        const float mx=x0+
            ((wrapWorld((float(e.chunkX)+0.5f)*CHUNK_WORLD_SIZE)+WORLD_HALF)
             /WORLD_SIZE)*size;
        const float my=y0+
            ((wrapWorld((float(e.chunkZ)+0.5f)*CHUNK_WORLD_SIZE)+WORLD_HALF)
             /WORLD_SIZE)*size;
        const float alpha=std::clamp(1.0f-age/60.0f,0.15f,0.82f);

        if(e.type==2) {
            drawCircle2D(hud,mx,my,5.5f,0.90f,0.25f,0.14f,alpha);
        } else if(e.type==6) {
            drawCircle2D(hud,mx,my,5.0f,0.86f,0.62f,0.18f,alpha);
        } else if(e.type==3) {
            drawCircle2D(hud,mx,my,4.0f,0.68f,0.30f,0.20f,alpha);
        } else if(e.type==1) {
            drawCircle2D(hud,mx,my,4.0f,0.30f,0.78f,0.42f,alpha);
        } else if(e.type==4) {
            drawCircle2D(hud,mx,my,3.5f,0.72f,0.62f,0.28f,alpha);
        } else if(e.type==7) {
            drawCircle2D(hud,mx,my,4.5f,0.40f,0.68f,0.92f,alpha);
        }
    }

    // Current chunk highlight.
    const float cpx=std::floor(mapX/CHUNK_WORLD_SIZE/WORLD_CHUNK_COUNT*WORLD_CHUNK_COUNT);
    const float cpz=std::floor(mapZ/CHUNK_WORLD_SIZE);
    const float hx=x0+(cpx/float(WORLD_CHUNK_COUNT))*size;
    const float hy=y0+(cpz/float(WORLD_CHUNK_COUNT))*size;
    const float outline[]={
        hx,hy,0, hx+cell,hy,0,
        hx+cell,hy,0, hx+cell,hy+cell,0,
        hx+cell,hy+cell,0, hx,hy+cell,0,
        hx,hy+cell,0, hx,hy,0
    };
    drawLines(hud,outline,8,0.92f,0.92f,0.70f,0.85f);

    char mapTitle[32]{};
    const EcoChunkState& currentEcoChunk=ecoChunkAt(px,pz);
    std::snprintf(mapTitle,sizeof(mapTitle),"MAP C%d,%d",centerX,centerZ);
    drawText2D(hud,mapTitle,x0,y0-18.0f,2.7f,0.86f,0.92f,0.96f,0.95f);

    char chunkLife[32]{};
    std::snprintf(chunkLife,sizeof(chunkLife),"FOOD%d WATER%d",
                  int(std::round(currentEcoChunk.food*9.0f)),
                  int(std::round(currentEcoChunk.water*9.0f)));
    drawText2D(hud,chunkLife,x0,y0+size+16.0f,2.15f,
               0.55f,0.74f,0.68f,0.84f);
    drawText2D(hud,"MAP",viewportW*0.88f,viewportH*0.105f,3.0f,
               1.0f,1.0f,1.0f,0.95f);
}

static void drawHud() {
    const Mat4 hud=ortho(0,float(viewportW),float(viewportH),0);

    glDisable(GL_DEPTH_TEST);

    // Compact minimap stays behind the gameplay HUD. The expanded map is
    // composited last so it behaves like a real map overlay.
    if(!mapExpanded) drawMinimap(hud,false);

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

    if(laserLanceEquipped) {
        drawText2D(hud,"LANCE",viewportW*0.47f,pad+44.0f,2.2f,
                   0.35f,0.90f,1.0f,0.92f);
        char lvl[8]{};
        std::snprintf(lvl,sizeof(lvl),"%d",laserEquipmentLevel);
        drawText2D(hud,lvl,viewportW*0.60f,pad+44.0f,2.6f,
                   1.0f,0.90f,0.38f,0.95f);
    }

    if(!bossDefeated) {
        const float bw=viewportW*0.26f;
        const float bx=viewportW*0.37f;
        const float by=viewportH*0.07f;
        drawText2D(hud,"BOSS",bx,by-22.0f,3.0f,
                   1.0f,0.55f,0.30f,0.90f);
        drawRect2D(hud,bx,by,bx+bw,by+12.0f,0.08f,0.03f,0.03f,0.85f);
        drawRect2D(hud,bx,by,bx+bw*std::clamp(bossHp/180.0f,0.0f,1.0f),
                   by+12.0f,0.90f,0.18f,0.08f,0.92f);
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

    // Wrapped world coordinates + chunk/local-tile coordinates.
    char xText[32]{};
    char zText[32]{};
    char yText[32]{};
    char cText[32]{};
    const int worldY=int(std::round(tileHeight(floorTile(px),floorTile(pz))));
    std::snprintf(xText,sizeof(xText),"X%d",(int)std::round(wrapWorld(px)));
    std::snprintf(yText,sizeof(yText),"Y%d",worldY);
    std::snprintf(zText,sizeof(zText),"Z%d",(int)std::round(wrapWorld(pz)));
    std::snprintf(cText,sizeof(cText),"C%d,%d T%d,%d",
                  chunkCoord(px),chunkCoord(pz),
                  chunkLocalTile(px),chunkLocalTile(pz));
    drawText2D(hud,xText,viewportW*0.02f,viewportH*0.90f,2.4f,
               0.72f,0.86f,0.92f,0.80f);
    drawText2D(hud,yText,viewportW*0.08f,viewportH*0.90f,2.4f,
               0.72f,0.86f,0.92f,0.80f);
    drawText2D(hud,zText,viewportW*0.14f,viewportH*0.90f,2.4f,
               0.72f,0.86f,0.92f,0.80f);
    drawText2D(hud,cText,viewportW*0.15f,viewportH*0.90f,2.15f,
               0.64f,0.80f,0.88f,0.78f);

    char ecoText[32]{};
    std::snprintf(ecoText,sizeof(ecoText),"ECO%d/%d",
                  ecosystemPopulation,ecoTotalPopulation());
    drawText2D(hud,ecoText,viewportW*0.30f,viewportH*0.90f,2.15f,
               0.54f,0.76f,0.68f,0.80f);

    char cycleText[32]{};
    std::snprintf(cycleText,sizeof(cycleText),"C%d",ecosystemCycle);
    drawText2D(hud,cycleText,viewportW*0.39f,viewportH*0.90f,2.05f,
               0.55f,0.72f,0.80f,0.72f);

    const bool night=ecoIsNight(ecosystemClock);
    drawText2D(hud,night?"NIGHT":"DAY",viewportW*0.44f,viewportH*0.90f,2.05f,
               0.70f,0.76f,0.86f,0.72f);

    const float rain=ecoRainIntensity(ecosystemClock);
    if(rain>0.48f) {
        drawText2D(hud,"RAIN",viewportW*0.54f,viewportH*0.90f,2.05f,
                   0.50f,0.70f,0.86f,0.72f);
    }

    if(ecosystemLastEventTimer>0.0f) {
        const char* eventText=(ecosystemLastEvent==1)?"BIRTH":
                              (ecosystemLastEvent==2)?"HUNT":
                              (ecosystemLastEvent==3)?"DEATH":
                              (ecosystemLastEvent==4)?"FOOD":
                              (ecosystemLastEvent==5)?"CYCLE":
                              (ecosystemLastEvent==6)?"SHOT":
                              (ecosystemLastEvent==7)?"MOVE":"";
        if(eventText[0]) {
            drawText2D(hud,eventText,viewportW*0.63f,viewportH*0.90f,2.05f,
                       0.60f,0.80f,0.68f,0.72f);
        }
    }

    // The HUD exposes local ecology, not a player-centric quest state.
    char localEcoText[32]{};
    const EcoChunkState& localChunk=ecoChunkAt(px,pz);
    const int localTotal=ecoChunkTotalPopulation(chunkCoord(px),chunkCoord(pz));
    std::snprintf(localEcoText,sizeof(localEcoText),"E%d/%d F%d W%d",
                  localChunk.population,
                  localTotal,
                  int(std::round(localChunk.food*9.0f)),
                  int(std::round(localChunk.water*9.0f)));
    drawText2D(hud,localEcoText,viewportW*0.75f,viewportH*0.935f,1.85f,
               0.45f,0.68f,0.58f,0.72f);

    const float mapX=viewportW*0.92f;
    const float mapY=viewportH*0.12f;
    const float mapR=viewportH*0.055f;
    drawCircle2D(hud,mapX,mapY,mapR,
                 mapExpanded?0.72f:0.34f,
                 mapExpanded?0.82f:0.54f,
                 mapExpanded?0.42f:0.52f,
                 0.75f);
    drawText2D(hud,"MAP",mapX-22.0f,mapY-10.0f,2.4f,1.0f,1.0f,1.0f,0.95f);


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

    if(mapExpanded) {
        drawMinimap(hud,true);
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
    // A normal chassis remains a salvageable wreck. A temporary recovery bot is
    // not an assembled chassis and must not magically create a new body slot.
    if(!miniBotMode) saveActiveBodyToPool();
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
    worldNoise=std::min(1.0f,worldNoise+0.8f);
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


struct SaveSnapshot {
    float px=0.0f, pz=0.0f, yaw=0.0f, aimPitch=0.18f;
    float heat=0.0f, playerHp=100.0f, respawnTimer=0.0f, chassisIntegrity=100.0f;
    BodyPartState playerParts[PART_COUNT]{};

    StoredBody bodySlots[MAX_BODY_SLOTS]{};
    int activeBodySlot=0;
    int bodyGeneration=1;
    uint8_t bodyPoolInitialized=0;
    uint8_t miniBotMode=0;

    int scrap=0, circuits=0, unknownEquipment=0, identifiedEquipment=0;
    int laserEquipmentLevel=0;
    uint8_t laserLanceEquipped=0;

    float fabricationTimer=0.0f;
    int fabricationSlot=-1;
    uint8_t baseMode=0;

    WreckState wreck{};

    float enemyX=0.0f, enemyZ=0.0f, enemyYaw=0.0f;
    float enemyHp=40.0f, enemyRespawn=0.0f, enemyAttackTimer=0.7f;

    float bossX=0.0f, bossZ=0.0f, bossYaw=0.0f;
    float bossHp=180.0f, bossAttackTimer=2.0f, bossLaserT=0.0f;
    float bossShotDirX=0.0f, bossShotDirZ=0.0f, bossHitFlash=0.0f;
    uint8_t bossDefeated=0;

    float ecosystemClock=0.0f, ecosystemAccumulator=0.0f;
    int ecosystemPopulation=0;
    int ecosystemBirths=0, ecosystemDeaths=0, ecosystemKills=0;
    int ecosystemLastEvent=0;
    float ecosystemLastEventTimer=0.0f;
    int ecosystemCycle=0;
    int ecosystemMigrationStep=0;
    float worldNoise=0.0f;

    EcoEventRecord ecoEventHistory[ECO_EVENT_HISTORY]{};
    int ecoEventWrite=0;

    EcoChunkState ecoChunks[WORLD_CHUNK_STATE_COUNT]{};
    EcoActor ecoActors[MAX_ECO_ACTORS]{};
};

static std::string saveFilePath() {
    if(savePath.empty()) return {};
    if(savePath.back()=='/') return savePath+"dgun_save.bin";
    return savePath+"/dgun_save.bin";
}

static uint32_t saveChecksum(const std::vector<uint8_t>& data) {
    uint32_t h=2166136261u;
    for(uint8_t b:data) {
        h^=b;
        h*=16777619u;
    }
    return h;
}

template<typename T>
static void savePod(std::vector<uint8_t>& data,const T& value) {
    static_assert(std::is_trivially_copyable<T>::value,"savePod requires POD");
    const size_t oldSize=data.size();
    data.resize(oldSize+sizeof(T));
    std::memcpy(data.data()+oldSize,&value,sizeof(T));
}

static void saveBool(std::vector<uint8_t>& data,bool value) {
    const uint8_t v=value?1u:0u;
    savePod(data,v);
}

template<typename T>
static bool loadPod(const std::vector<uint8_t>& data,size_t& cursor,T& value) {
    static_assert(std::is_trivially_copyable<T>::value,"loadPod requires POD");
    if(cursor>data.size() || data.size()-cursor<sizeof(T)) return false;
    std::memcpy(&value,data.data()+cursor,sizeof(T));
    cursor+=sizeof(T);
    return true;
}

static bool loadBool(const std::vector<uint8_t>& data,size_t& cursor,uint8_t& value) {
    return loadPod(data,cursor,value);
}

static void writeBodyPart(std::vector<uint8_t>& data,const BodyPartState& part) {
    savePod(data,part.hp);
    savePod(data,part.maxHp);
}

static bool readBodyPart(const std::vector<uint8_t>& data,size_t& cursor,BodyPartState& part) {
    return loadPod(data,cursor,part.hp) &&
           loadPod(data,cursor,part.maxHp);
}

static void writeStoredBody(std::vector<uint8_t>& data,const StoredBody& body) {
    saveBool(data,body.occupied);
    savePod(data,body.generation);
    for(int i=0;i<PART_COUNT;i++) writeBodyPart(data,body.parts[i]);
}

static bool readStoredBody(const std::vector<uint8_t>& data,size_t& cursor,StoredBody& body) {
    uint8_t occupied=0;
    if(!loadBool(data,cursor,occupied) ||
       !loadPod(data,cursor,body.generation)) return false;
    body.occupied=occupied!=0;
    for(int i=0;i<PART_COUNT;i++) {
        if(!readBodyPart(data,cursor,body.parts[i])) return false;
    }
    return true;
}

static void writeWreck(std::vector<uint8_t>& data,const WreckState& value) {
    saveBool(data,value.active);
    savePod(data,value.x);
    savePod(data,value.z);
    savePod(data,value.yaw);
    savePod(data,value.salvagePercent);
    for(int i=0;i<PART_COUNT;i++) writeBodyPart(data,value.parts[i]);
}

static bool readWreck(const std::vector<uint8_t>& data,size_t& cursor,WreckState& value) {
    uint8_t active=0;
    if(!loadBool(data,cursor,active) ||
       !loadPod(data,cursor,value.x) ||
       !loadPod(data,cursor,value.z) ||
       !loadPod(data,cursor,value.yaw) ||
       !loadPod(data,cursor,value.salvagePercent)) return false;
    value.active=active!=0;
    for(int i=0;i<PART_COUNT;i++) {
        if(!readBodyPart(data,cursor,value.parts[i])) return false;
    }
    return true;
}

static void writeEcoActor(std::vector<uint8_t>& data,const EcoActor& actor) {
    saveBool(data,actor.alive);
    savePod(data,actor.kind);
    savePod(data,actor.groupId);
    savePod(data,actor.x);
    savePod(data,actor.z);
    savePod(data,actor.yaw);
    savePod(data,actor.hp);
    savePod(data,actor.hunger);
    savePod(data,actor.energy);
    savePod(data,actor.age);
    savePod(data,actor.breedCooldown);
    savePod(data,actor.brain);
    savePod(data,actor.corpseTimer);
    savePod(data,actor.fear);
    savePod(data,actor.thirst);
    savePod(data,actor.alert);
    savePod(data,actor.loyalty);
    savePod(data,actor.attackCooldown);
    savePod(data,actor.denX);
    savePod(data,actor.denZ);
    savePod(data,actor.lastPlayerChunkX);
    savePod(data,actor.lastPlayerChunkZ);
    savePod(data,actor.target);
    savePod(data,actor.homeChunkX);
    savePod(data,actor.homeChunkZ);
    savePod(data,actor.seed);
}

static bool readEcoActor(const std::vector<uint8_t>& data,size_t& cursor,EcoActor& actor) {
    uint8_t alive=0;
    if(!loadBool(data,cursor,alive) ||
       !loadPod(data,cursor,actor.kind) ||
       !loadPod(data,cursor,actor.groupId) ||
       !loadPod(data,cursor,actor.x) ||
       !loadPod(data,cursor,actor.z) ||
       !loadPod(data,cursor,actor.yaw) ||
       !loadPod(data,cursor,actor.hp) ||
       !loadPod(data,cursor,actor.hunger) ||
       !loadPod(data,cursor,actor.energy) ||
       !loadPod(data,cursor,actor.age) ||
       !loadPod(data,cursor,actor.breedCooldown) ||
       !loadPod(data,cursor,actor.brain) ||
       !loadPod(data,cursor,actor.corpseTimer) ||
       !loadPod(data,cursor,actor.fear) ||
       !loadPod(data,cursor,actor.thirst) ||
       !loadPod(data,cursor,actor.alert) ||
       !loadPod(data,cursor,actor.loyalty) ||
       !loadPod(data,cursor,actor.attackCooldown) ||
       !loadPod(data,cursor,actor.denX) ||
       !loadPod(data,cursor,actor.denZ) ||
       !loadPod(data,cursor,actor.lastPlayerChunkX) ||
       !loadPod(data,cursor,actor.lastPlayerChunkZ) ||
       !loadPod(data,cursor,actor.target) ||
       !loadPod(data,cursor,actor.homeChunkX) ||
       !loadPod(data,cursor,actor.homeChunkZ) ||
       !loadPod(data,cursor,actor.seed)) return false;
    actor.alive=alive!=0;
    return true;
}

static void writeEcoChunk(std::vector<uint8_t>& data,const EcoChunkState& chunk) {
    savePod(data,chunk.food);
    savePod(data,chunk.water);
    savePod(data,chunk.shelter);
    savePod(data,chunk.danger);
    savePod(data,chunk.population);
    for(int kind=0;kind<3;kind++) savePod(data,chunk.dormant[kind]);
    for(int kind=0;kind<3;kind++) savePod(data,chunk.dormantRemainder[kind]);
}

static bool readEcoChunk(const std::vector<uint8_t>& data,size_t& cursor,EcoChunkState& chunk) {
    if(!loadPod(data,cursor,chunk.food) ||
       !loadPod(data,cursor,chunk.water) ||
       !loadPod(data,cursor,chunk.shelter) ||
       !loadPod(data,cursor,chunk.danger) ||
       !loadPod(data,cursor,chunk.population)) return false;
    for(int kind=0;kind<3;kind++) {
        if(!loadPod(data,cursor,chunk.dormant[kind])) return false;
    }
    for(int kind=0;kind<3;kind++) {
        if(!loadPod(data,cursor,chunk.dormantRemainder[kind])) return false;
    }
    return true;
}

static void writeEcoEvent(std::vector<uint8_t>& data,const EcoEventRecord& event) {
    savePod(data,event.type);
    savePod(data,event.kind);
    savePod(data,event.chunkX);
    savePod(data,event.chunkZ);
    savePod(data,event.stamp);
}

static bool readEcoEvent(const std::vector<uint8_t>& data,size_t& cursor,EcoEventRecord& event) {
    return loadPod(data,cursor,event.type) &&
           loadPod(data,cursor,event.kind) &&
           loadPod(data,cursor,event.chunkX) &&
           loadPod(data,cursor,event.chunkZ) &&
           loadPod(data,cursor,event.stamp);
}

static bool saveGame() {
    const std::string path=saveFilePath();
    if(path.empty()) return false;

    std::vector<uint8_t> payload;
    payload.reserve(128*1024);

    savePod(payload,px);
    savePod(payload,pz);
    savePod(payload,yaw);
    savePod(payload,aimPitch);
    savePod(payload,heat);
    savePod(payload,playerHp);
    savePod(payload,respawnTimer);
    savePod(payload,chassisIntegrity);

    for(int i=0;i<PART_COUNT;i++) writeBodyPart(payload,playerParts[i]);

    for(int i=0;i<MAX_BODY_SLOTS;i++) writeStoredBody(payload,bodySlots[i]);
    savePod(payload,activeBodySlot);
    savePod(payload,bodyGeneration);
    saveBool(payload,bodyPoolInitialized);
    saveBool(payload,miniBotMode);

    savePod(payload,scrap);
    savePod(payload,circuits);
    savePod(payload,unknownEquipment);
    savePod(payload,identifiedEquipment);
    savePod(payload,laserEquipmentLevel);
    saveBool(payload,laserLanceEquipped);

    savePod(payload,fabricationTimer);
    savePod(payload,fabricationSlot);
    const uint8_t baseModeValue=baseMode==BaseMode::ORBITAL?1u:0u;
    savePod(payload,baseModeValue);

    writeWreck(payload,wreck);

    savePod(payload,enemyX);
    savePod(payload,enemyZ);
    savePod(payload,enemyYaw);
    savePod(payload,enemyHp);
    savePod(payload,enemyRespawn);
    savePod(payload,enemyAttackTimer);

    savePod(payload,bossX);
    savePod(payload,bossZ);
    savePod(payload,bossYaw);
    savePod(payload,bossHp);
    savePod(payload,bossAttackTimer);
    savePod(payload,bossLaserT);
    savePod(payload,bossShotDirX);
    savePod(payload,bossShotDirZ);
    savePod(payload,bossHitFlash);
    saveBool(payload,bossDefeated);

    savePod(payload,ecosystemClock);
    savePod(payload,ecosystemAccumulator);
    savePod(payload,ecosystemPopulation);
    savePod(payload,ecosystemBirths);
    savePod(payload,ecosystemDeaths);
    savePod(payload,ecosystemKills);
    savePod(payload,ecosystemLastEvent);
    savePod(payload,ecosystemLastEventTimer);
    savePod(payload,ecosystemCycle);
    savePod(payload,ecosystemMigrationStep);
    savePod(payload,worldNoise);

    savePod(payload,ecoEventWrite);
    for(int i=0;i<ECO_EVENT_HISTORY;i++) writeEcoEvent(payload,ecoEventHistory[i]);

    for(int i=0;i<WORLD_CHUNK_STATE_COUNT;i++) writeEcoChunk(payload,ecoChunks[i]);
    for(int i=0;i<MAX_ECO_ACTORS;i++) writeEcoActor(payload,ecoActors[i]);

    const uint32_t checksum=saveChecksum(payload);
    const uint32_t payloadSize=uint32_t(payload.size());

    std::ofstream out(path+".tmp",std::ios::binary|std::ios::trunc);
    if(!out) {
        __android_log_print(ANDROID_LOG_ERROR,"DG-0016",
                            "Unable to open save temp file: %s",path.c_str());
        return false;
    }

    out.write(reinterpret_cast<const char*>(&SAVE_MAGIC),sizeof(SAVE_MAGIC));
    out.write(reinterpret_cast<const char*>(&SAVE_VERSION),sizeof(SAVE_VERSION));
    out.write(reinterpret_cast<const char*>(&payloadSize),sizeof(payloadSize));
    out.write(reinterpret_cast<const char*>(&checksum),sizeof(checksum));
    out.write(reinterpret_cast<const char*>(payload.data()),
              std::streamsize(payload.size()));
    out.flush();
    if(!out) {
        out.close();
        std::remove((path+".tmp").c_str());
        return false;
    }
    out.close();

    // POSIX/Android rename replaces the destination in one filesystem
    // operation, keeping the save update atomic-ish instead of deleting the
    // valid save before the replacement is ready.
    if(std::rename((path+".tmp").c_str(),path.c_str())!=0) {
        std::remove((path+".tmp").c_str());
        return false;
    }

    autosaveTimer=0.0f;
    return true;
}

static bool loadGame() {
    const std::string path=saveFilePath();
    if(path.empty()) return false;

    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if(!in) return false;

    const std::streamoff fileSize=in.tellg();
    constexpr std::streamoff HEADER_SIZE=16;
    if(fileSize<HEADER_SIZE || fileSize>8*1024*1024) return false;

    in.seekg(0,std::ios::beg);
    uint32_t magic=0,version=0,payloadSize=0,checksum=0;
    in.read(reinterpret_cast<char*>(&magic),sizeof(magic));
    in.read(reinterpret_cast<char*>(&version),sizeof(version));
    in.read(reinterpret_cast<char*>(&payloadSize),sizeof(payloadSize));
    in.read(reinterpret_cast<char*>(&checksum),sizeof(checksum));

    if(!in || magic!=SAVE_MAGIC || version!=SAVE_VERSION ||
       payloadSize!=uint32_t(fileSize-HEADER_SIZE)) {
        __android_log_print(ANDROID_LOG_WARN,"DG-0016",
                            "Ignoring incompatible or truncated save");
        return false;
    }

    std::vector<uint8_t> payload(payloadSize);
    if(payloadSize>0) {
        in.read(reinterpret_cast<char*>(payload.data()),std::streamsize(payloadSize));
    }
    if(!in || saveChecksum(payload)!=checksum) {
        __android_log_print(ANDROID_LOG_WARN,"DG-0016",
                            "Ignoring corrupt save payload");
        return false;
    }

    std::unique_ptr<SaveSnapshot> snapshot(new SaveSnapshot{});
    size_t cursor=0;
    auto readAll=[&]() -> bool {
        if(!loadPod(payload,cursor,snapshot->px) ||
           !loadPod(payload,cursor,snapshot->pz) ||
           !loadPod(payload,cursor,snapshot->yaw) ||
           !loadPod(payload,cursor,snapshot->aimPitch) ||
           !loadPod(payload,cursor,snapshot->heat) ||
           !loadPod(payload,cursor,snapshot->playerHp) ||
           !loadPod(payload,cursor,snapshot->respawnTimer) ||
           !loadPod(payload,cursor,snapshot->chassisIntegrity)) return false;

        for(int i=0;i<PART_COUNT;i++) {
            if(!readBodyPart(payload,cursor,snapshot->playerParts[i])) return false;
        }
        for(int i=0;i<MAX_BODY_SLOTS;i++) {
            if(!readStoredBody(payload,cursor,snapshot->bodySlots[i])) return false;
        }

        if(!loadPod(payload,cursor,snapshot->activeBodySlot) ||
           !loadPod(payload,cursor,snapshot->bodyGeneration) ||
           !loadBool(payload,cursor,snapshot->bodyPoolInitialized) ||
           !loadBool(payload,cursor,snapshot->miniBotMode)) return false;

        if(!loadPod(payload,cursor,snapshot->scrap) ||
           !loadPod(payload,cursor,snapshot->circuits) ||
           !loadPod(payload,cursor,snapshot->unknownEquipment) ||
           !loadPod(payload,cursor,snapshot->identifiedEquipment) ||
           !loadPod(payload,cursor,snapshot->laserEquipmentLevel) ||
           !loadBool(payload,cursor,snapshot->laserLanceEquipped) ||
           !loadPod(payload,cursor,snapshot->fabricationTimer) ||
           !loadPod(payload,cursor,snapshot->fabricationSlot) ||
           !loadPod(payload,cursor,snapshot->baseMode)) return false;

        if(!readWreck(payload,cursor,snapshot->wreck)) return false;

        if(!loadPod(payload,cursor,snapshot->enemyX) ||
           !loadPod(payload,cursor,snapshot->enemyZ) ||
           !loadPod(payload,cursor,snapshot->enemyYaw) ||
           !loadPod(payload,cursor,snapshot->enemyHp) ||
           !loadPod(payload,cursor,snapshot->enemyRespawn) ||
           !loadPod(payload,cursor,snapshot->enemyAttackTimer) ||
           !loadPod(payload,cursor,snapshot->bossX) ||
           !loadPod(payload,cursor,snapshot->bossZ) ||
           !loadPod(payload,cursor,snapshot->bossYaw) ||
           !loadPod(payload,cursor,snapshot->bossHp) ||
           !loadPod(payload,cursor,snapshot->bossAttackTimer) ||
           !loadPod(payload,cursor,snapshot->bossLaserT) ||
           !loadPod(payload,cursor,snapshot->bossShotDirX) ||
           !loadPod(payload,cursor,snapshot->bossShotDirZ) ||
           !loadPod(payload,cursor,snapshot->bossHitFlash) ||
           !loadBool(payload,cursor,snapshot->bossDefeated)) return false;

        if(!loadPod(payload,cursor,snapshot->ecosystemClock) ||
           !loadPod(payload,cursor,snapshot->ecosystemAccumulator) ||
           !loadPod(payload,cursor,snapshot->ecosystemPopulation) ||
           !loadPod(payload,cursor,snapshot->ecosystemBirths) ||
           !loadPod(payload,cursor,snapshot->ecosystemDeaths) ||
           !loadPod(payload,cursor,snapshot->ecosystemKills) ||
           !loadPod(payload,cursor,snapshot->ecosystemLastEvent) ||
           !loadPod(payload,cursor,snapshot->ecosystemLastEventTimer) ||
           !loadPod(payload,cursor,snapshot->ecosystemCycle) ||
           !loadPod(payload,cursor,snapshot->ecosystemMigrationStep) ||
           !loadPod(payload,cursor,snapshot->worldNoise)) return false;

        if(!loadPod(payload,cursor,snapshot->ecoEventWrite)) return false;
        for(int i=0;i<ECO_EVENT_HISTORY;i++) {
            if(!readEcoEvent(payload,cursor,snapshot->ecoEventHistory[i])) return false;
        }

        for(int i=0;i<WORLD_CHUNK_STATE_COUNT;i++) {
            if(!readEcoChunk(payload,cursor,snapshot->ecoChunks[i])) return false;
        }
        for(int i=0;i<MAX_ECO_ACTORS;i++) {
            if(!readEcoActor(payload,cursor,snapshot->ecoActors[i])) return false;
        }

        return cursor==payload.size();
    };

    if(!readAll()) {
        __android_log_print(ANDROID_LOG_WARN,"DG-0016",
                            "Ignoring malformed save payload");
        return false;
    }

    if(snapshot->activeBodySlot<0 || snapshot->activeBodySlot>=MAX_BODY_SLOTS ||
       snapshot->fabricationSlot<-1 || snapshot->fabricationSlot>=MAX_BODY_SLOTS ||
       snapshot->laserEquipmentLevel<0 || snapshot->laserEquipmentLevel>3 ||
       snapshot->ecoEventWrite<0 || snapshot->ecoEventWrite>=ECO_EVENT_HISTORY ||
       snapshot->baseMode>1 ||
       snapshot->ecosystemPopulation<0 ||
       snapshot->ecosystemCycle<0 ||
       snapshot->ecosystemMigrationStep<0) {
        return false;
    }

    px=wrapWorld(snapshot->px);
    pz=wrapWorld(snapshot->pz);
    yaw=snapshot->yaw;
    aimPitch=std::clamp(snapshot->aimPitch,-0.25f,0.38f);
    heat=std::clamp(snapshot->heat,0.0f,1.0f);
    playerHp=std::clamp(snapshot->playerHp,0.0f,100.0f);
    respawnTimer=std::max(0.0f,snapshot->respawnTimer);
    chassisIntegrity=std::clamp(snapshot->chassisIntegrity,0.0f,100.0f);

    for(int i=0;i<PART_COUNT;i++) playerParts[i]=snapshot->playerParts[i];
    for(int i=0;i<MAX_BODY_SLOTS;i++) bodySlots[i]=snapshot->bodySlots[i];
    activeBodySlot=snapshot->activeBodySlot;
    bodyGeneration=std::max(1,snapshot->bodyGeneration);
    bodyPoolInitialized=snapshot->bodyPoolInitialized!=0;
    miniBotMode=snapshot->miniBotMode!=0;

    scrap=std::max(0,snapshot->scrap);
    circuits=std::max(0,snapshot->circuits);
    unknownEquipment=std::max(0,snapshot->unknownEquipment);
    identifiedEquipment=std::max(0,snapshot->identifiedEquipment);
    laserEquipmentLevel=std::clamp(snapshot->laserEquipmentLevel,0,3);
    laserLanceEquipped=snapshot->laserLanceEquipped!=0;

    fabricationTimer=std::max(0.0f,snapshot->fabricationTimer);
    fabricationSlot=snapshot->fabricationSlot;
    baseMode=snapshot->baseMode?BaseMode::ORBITAL:BaseMode::LAND;

    wreck=snapshot->wreck;
    enemyX=wrapWorld(snapshot->enemyX);
    enemyZ=wrapWorld(snapshot->enemyZ);
    enemyYaw=snapshot->enemyYaw;
    enemyHp=std::clamp(snapshot->enemyHp,0.0f,40.0f);
    enemyRespawn=std::max(0.0f,snapshot->enemyRespawn);
    enemyAttackTimer=snapshot->enemyAttackTimer;
    bossX=wrapWorld(snapshot->bossX);
    bossZ=wrapWorld(snapshot->bossZ);
    bossYaw=snapshot->bossYaw;
    bossHp=std::clamp(snapshot->bossHp,0.0f,180.0f);
    bossAttackTimer=snapshot->bossAttackTimer;
    bossLaserT=std::max(0.0f,snapshot->bossLaserT);
    bossShotDirX=snapshot->bossShotDirX;
    bossShotDirZ=snapshot->bossShotDirZ;
    bossHitFlash=std::max(0.0f,snapshot->bossHitFlash);
    bossDefeated=snapshot->bossDefeated!=0;

    ecosystemClock=std::max(0.0f,snapshot->ecosystemClock);
    ecosystemAccumulator=std::clamp(snapshot->ecosystemAccumulator,0.0f,1.0f);
    ecosystemPopulation=std::clamp(snapshot->ecosystemPopulation,0,MAX_ECO_ACTORS);
    ecosystemBirths=std::max(0,snapshot->ecosystemBirths);
    ecosystemDeaths=std::max(0,snapshot->ecosystemDeaths);
    ecosystemKills=std::max(0,snapshot->ecosystemKills);
    ecosystemLastEvent=snapshot->ecosystemLastEvent;
    ecosystemLastEventTimer=std::max(0.0f,snapshot->ecosystemLastEventTimer);
    ecosystemCycle=snapshot->ecosystemCycle;
    ecosystemMigrationStep=std::max(0,snapshot->ecosystemMigrationStep);
    worldNoise=std::clamp(snapshot->worldNoise,0.0f,1.0f);

    ecoEventWrite=snapshot->ecoEventWrite;
    for(int i=0;i<ECO_EVENT_HISTORY;i++) ecoEventHistory[i]=snapshot->ecoEventHistory[i];
    for(int i=0;i<WORLD_CHUNK_STATE_COUNT;i++) ecoChunks[i]=snapshot->ecoChunks[i];
    for(int i=0;i<MAX_ECO_ACTORS;i++) ecoActors[i]=snapshot->ecoActors[i];

    refreshEcoChunkActivePopulation();

    movePointer=-1;
    aimPointer=-1;
    firePointer=-1;
    mapPointer=-1;
    actionPointer=-1;
    swapPointer=-1;
    joyX=0.0f;
    joyY=0.0f;
    mapExpanded=false;
    lastAimX=0.0f;
    lastAimY=0.0f;
    laserT=0.0f;
    enemyLaserT=0.0f;
    bossLaserT=0.0f;
    autosaveTimer=0.0f;
    lastTime=0.0;

    __android_log_print(ANDROID_LOG_INFO,"DG-0016",
                        "Loaded persistent save: %u bytes",payloadSize);
    return true;
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

    const float dx=wrappedDelta(enemyX,px);
    const float dz=wrappedDelta(enemyZ,pz);
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

        const Vec3 shotStart={nearestWorldImage(enemyX,px),1.45f,
                              nearestWorldImage(enemyZ,pz)};
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

static void updateBoss(float dt) {
    if(bossDefeated) return;

    const float dx=wrappedDelta(bossX,px);
    const float dz=wrappedDelta(bossZ,pz);
    const float distance=std::sqrt(std::max(0.0001f,dx*dx+dz*dz));
    bossYaw=std::atan2(dx,-dz);

    if(distance>7.0f) {
        const float speed=0.72f;
        const float nx=wrapWorld(bossX+(dx/distance)*speed*dt);
        const float nz=wrapWorld(bossZ+(dz/distance)*speed*dt);
        if(!collidesObstacle(nx,bossZ,1.25f)) bossX=nx;
        if(!collidesObstacle(bossX,nz,1.25f)) bossZ=nz;
    }

    bossAttackTimer-=dt;
    if(bossAttackTimer<=0.0f && distance<15.0f) {
        bossAttackTimer=1.65f;
        bossLaserT=0.16f;
        bossShotDirX=dx/std::max(0.001f,distance);
        bossShotDirZ=dz/std::max(0.001f,distance);

        const Vec3 shotStart={bossX,1.65f,bossZ};
        const Vec3 target={px,1.10f,pz};
        const Vec3 shotDir=norm(sub(target,shotStart));
        const int detectedPart=closestPlayerPartOnRay(shotStart,shotDir,distance+1.0f);
        const int hitPart=miniBotMode ? PART_CORE : (detectedPart>=0 ? detectedPart : PART_CORE);

        const float partDamage=miniBotMode ? 12.0f : 9.0f;
        playerParts[hitPart].hp=
            std::max(0.0f,playerParts[hitPart].hp-partDamage);

        const float structuralDamage=miniBotMode ? 10.0f : 3.25f;
        chassisIntegrity=std::max(0.0f,chassisIntegrity-structuralDamage);
        playerHp=std::min(calculatePlayerHp(),chassisIntegrity);
        playerHitFlash=0.16f;

        if(playerHp<=0.0f || chassisIntegrity<=0.0f) {
            beginPlayerDeath();
        }
    }
}

static void update(float dt) {
    autosaveTimer+=dt;
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
            px=wrapWorld(BASE_X);
            pz=wrapWorld(BASE_Z);
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
            worldNoise=std::min(1.0f,worldNoise+dt*3.5f);
            heat=std::min(1.0f,heat+dt*laserHeatRate());
            laserT=0.08f;
        } else {
            heat=std::max(0.0f,heat-dt*0.42f);
            laserT=std::max(0.0f,laserT-dt);
        }
    }

    updateEcosystem(dt);
    updateEnemy(dt);
    updateBoss(dt);

    if(!savePath.empty() && autosaveTimer>=5.0f) {
        saveGame();
    }

    enemyLaserT=std::max(0.0f,enemyLaserT-dt);
    bossLaserT=std::max(0.0f,bossLaserT-dt);
    playerHitFlash=std::max(0.0f,playerHitFlash-dt);
    enemyHitFlash=std::max(0.0f,enemyHitFlash-dt);
    bossHitFlash=std::max(0.0f,bossHitFlash-dt);

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

        const Vec3 enemyImage={
            nearestWorldImage(enemyX,px),0.95f,nearestWorldImage(enemyZ,pz)
        };
        const Vec3 toEnemy=sub(enemyImage,start);
        const float along=dot(toEnemy,dir);

        if(along>0.0f && along<=maxBeamDistance+0.05f && along<LASER_MAX_RANGE) {
            const Vec3 closest=add(start,mul(dir,along));
            const Vec3 enemyCenter={enemyX,0.95f,enemyZ};
            const float d2=dot(sub(enemyCenter,closest),sub(enemyCenter,closest));
            if(d2<1.65f) {
                enemyHp-=laserDamagePerSecond()*trace.energy*dt*60.0f;
                enemyHitFlash=0.08f;
                if(enemyHp<=0.0f) {
                    enemyHp=0.0f;
                    enemyRespawn=2.0f;
                    enemyHitFlash=0.35f;
                }
            }
        }

        if(!bossDefeated) {
            const Vec3 bossImage={
                nearestWorldImage(bossX,px),1.35f,nearestWorldImage(bossZ,pz)
            };
            const Vec3 toBoss=sub(bossImage,start);
            const float bossAlong=dot(toBoss,dir);
            if(bossAlong>0.0f && bossAlong<=maxBeamDistance+0.05f && bossAlong<LASER_MAX_RANGE) {
                const Vec3 closestBoss=add(start,mul(dir,bossAlong));
                const Vec3 bossCenter={bossX,1.35f,bossZ};
                const float bossD2=dot(sub(bossCenter,closestBoss),sub(bossCenter,closestBoss));
                if(bossD2<3.10f) {
                    bossHp-=laserDamagePerSecond()*trace.energy*dt*60.0f;
                    bossHitFlash=0.08f;
                    if(bossHp<=0.0f) {
                        bossHp=0.0f;
                        bossDefeated=true;
                        bossHitFlash=0.45f;

                        // Landmark bosses are a source of named discovery,
                        // not a respawning farm: defeating this one unlocks
                        // another UNKNOWN EQUIPMENT piece and components.
                        ++unknownEquipment;
                        scrap+=5;
                        circuits+=2;
                    }
                }
            }
        }

        // Wildlife is a real part of the world, so the player's weapon can
        // intersect it. We choose the nearest actor along the beam rather than
        // damaging every creature the beam crosses.
        int hitEco=-1;
        float nearestEco=LASER_MAX_RANGE+1.0f;
        for(int i=0;i<MAX_ECO_ACTORS;i++) {
            if(!ecoActors[i].alive) continue;

            const Vec3 ecoImage={
                nearestWorldImage(ecoActors[i].x,px),
                (ecoActors[i].kind==ECO_HUNTER)?0.45f:0.32f,
                nearestWorldImage(ecoActors[i].z,pz)
            };
            const Vec3 toEco=sub(ecoImage,start);
            const float alongEco=dot(toEco,dir);
            if(alongEco<=0.0f || alongEco>maxBeamDistance+0.05f ||
               alongEco>=nearestEco) continue;

            const Vec3 closestEco=add(start,mul(dir,alongEco));
            const float radius=(ecoActors[i].kind==ECO_HUNTER)?0.72f:0.55f;
            const Vec3 deltaEco=sub(ecoImage,closestEco);
            if(dot(deltaEco,deltaEco)<radius*radius) {
                nearestEco=alongEco;
                hitEco=i;
            }
        }

        if(hitEco>=0) {
            EcoActor& animal=ecoActors[hitEco];
            const float damage=(animal.kind==ECO_HUNTER)?20.0f:12.0f;
            animal.hp-=damage*trace.energy*dt*60.0f;
            animal.fear=std::min(1.0f,animal.fear+dt*3.0f);
            animal.alert=std::min(1.0f,animal.alert+dt*2.0f);
            if(animal.hp<=0.0f) {
                recordEcoEvent(6,animal.kind,animal.x,animal.z);
                ecoKillActor(hitEco);
                ecosystemLastEvent=6;
                ecosystemLastEventTimer=2.5f;
                animal.target=-1;
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

    const bool night=ecoIsNight(ecosystemClock);
    const float rain=ecoRainIntensity(ecosystemClock);
    const float skyR=night?0.010f:(0.025f-0.004f*rain);
    const float skyG=night?0.016f:(0.035f-0.008f*rain);
    const float skyB=night?0.030f:(0.050f-0.010f*rain);
    glClearColor(skyR,skyG,skyB,1.0f);
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

    drawProceduralTerrain(vp);
    drawChunkBorders(vp);
    drawGrid(vp);
    drawArenaBlocks(vp);
    drawTestWorldStructures(vp);
    drawRain(vp);
    drawEcosystem(vp);
    if(wreck.active) drawWreck(vp);
    if(enemyRespawn<=0.0f) drawEnemy(vp);
    if(!bossDefeated) drawBoss(vp);

    if(respawnTimer<=0.0f && !miniBotMode) {
        drawMech(vp);
        drawLaser(vp);
    } else {
        drawMiniBot(vp);
    }

    drawEnemyLaser(vp);
    drawBossLaser(vp);
    drawHud();
}

static void touch(int pointerId,int action,float x,float y,float w,float h) {
    const bool leftZone = x < w*0.45f;

    if(action==ACTION_DOWN || action==ACTION_POINTER_DOWN) {
        if(!leftZone && pointInMapButton(x,y,w,h) && mapPointer<0) {
            mapPointer=pointerId;
            mapExpanded=!mapExpanded;
            return;
        }

        if(mapExpanded) return;
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
        if(pointerId==mapPointer || action==ACTION_CANCEL) {
            mapPointer=-1;
        }
        if(mapExpanded && action!=ACTION_CANCEL) return;

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
Java_com_fanmade_dg_MainActivity_00024NativeBridge_init(
        JNIEnv* env,jclass,jstring path) {
    if(path) {
        const char* chars=env->GetStringUTFChars(path,nullptr);
        if(chars) {
            dg::savePath=chars;
            env->ReleaseStringUTFChars(path,chars);
        }
    }
    dg::initGL();
    __android_log_print(ANDROID_LOG_INFO,"DG-0014",
                        "Native renderer initialized; persistent save configured");
}

extern "C" JNIEXPORT void JNICALL
Java_com_fanmade_dg_MainActivity_00024NativeBridge_save(JNIEnv*,jclass) {
    dg::saveGame();
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
