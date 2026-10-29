#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

using namespace glm;

// Constants
static const int   WIN_W        = 800;
static const int   WIN_H        = 600;
static const float SPRITE_PX    = 13.0f;
static const float SPRITE_SCALE = 4.0f;
static const float SPRITE_SIZE  = SPRITE_PX * SPRITE_SCALE;
static const float CHAR_SIZE    = 80.0f;
static const float PLAYER_Y     = 545.0f;
static const float DANGER_Y     = 490.0f;
static const float PLAYER_SPEED = 250.0f; 
static const float BULLET_SPEED = 450.0f; 
static const float ENEMY_SPEED  = (DANGER_Y + SPRITE_SIZE) / 15.0f;
static const float SHOOT_CD     = 0.5f;
static const int   ENEMY_MAX_HP = 3;
static const int   MAX_ENEMIES  = 5;
static const int   MAX_BULLETS  = 10;
static const int   PLAYER_LIVES = 3;
static const float SPAWN_MIN    = 2.0f;
static const float SPAWN_MAX    = 5.0f;
static const float ANIM_FPS     = 6.0f;

// Game state
enum GameState { STATE_MENU, STATE_PLAYING, STATE_GAMEOVER };
// 
struct Player { float x; float shootTimer; int frame; float animTimer; };
struct Bullet  { float x, y; bool active; };
struct Enemy   { float x, y; int hp; bool active; int frame; float animTimer; };

static GameState gState;
static Player    gPlayer;
static Bullet    gBullets[MAX_BULLETS];
static Enemy     gEnemies[MAX_ENEMIES];
static int       gLives;
static int       gScore;
static float     gSpawnTimer, gNextSpawn;
static float     gBgOff[3];
static bool      gKeyLeft, gKeyRight, gKeyShoot;

// Shaders
//vertex
static const char* VS = R"glsl(
#version 330 core
layout(location=0) in vec2 pos;
layout(location=1) in vec2 uv;
uniform mat4 projection;
uniform mat4 model;
uniform vec2 uvOff;
uniform vec2 uvScl;
out vec2 vUV;
void main(){
    gl_Position = projection * model * vec4(pos,0,1);
    vUV = uv * uvScl + uvOff;
}
)glsl";


//fragment
static const char* FS = R"glsl(
#version 330 core
in vec2 vUV;
uniform sampler2D tex;
uniform vec4 tint;
uniform bool useTex;
out vec4 outColor;
void main(){
    if(useTex){
        vec4 c = texture(tex, vUV);
        if(c.a < 0.05) discard;
        outColor = c * tint;
    } else {
        outColor = tint;
    }
}
)glsl";

// OpenGL handles
static GLuint gShader, gVAO, gVBO;
static GLint  uProj, uModel, uUVOff, uUVScl, uTint, uTex, uUseTex;
static mat4   gProj;

static GLuint gTexPlayer, gTexZombie, gTexBullet, gTexBg[3];

// Compile
static GLuint compileShader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if(!ok){ char log[512]; glGetShaderInfoLog(s,512,nullptr,log); fprintf(stderr,"%s\n",log); }
    return s;
}
static GLuint buildProgram()
{
    GLuint vs = compileShader(GL_VERTEX_SHADER, VS);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, FS);
    GLuint p  = glCreateProgram();
    glAttachShader(p,vs); glAttachShader(p,fs);
    glLinkProgram(p);
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

// VAO
static void setupQuad()
{
    float v[] = {
        0,0,0,0,   1,0,1,0,   1,1,1,1,
        0,0,0,0,   1,1,1,1,   0,1,0,1,
    };
    glGenVertexArrays(1,&gVAO);
    glGenBuffers(1,&gVBO);
    glBindVertexArray(gVAO);
    glBindBuffer(GL_ARRAY_BUFFER,gVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(v),v,GL_STATIC_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

// Texture
static GLuint makeTex(const unsigned char* rgba, int w, int h)
{
    GLuint t;
    glGenTextures(1,&t);
    glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    return t;
}

// Background gradient
static void buildBgTile(unsigned char* buf, int w, int h,
                        int r0,int g0,int b0, int r1,int g1,int b1)
{
    for(int y=0; y<h; y++){
        float t = (float)y/(h-1);
        unsigned char r=(unsigned char)(r0+t*(r1-r0));
        unsigned char g=(unsigned char)(g0+t*(g1-g0));
        unsigned char b=(unsigned char)(b0+t*(b1-b0));
        for(int x=0; x<w; x++){
            int i=(y*w+x)*4;
            buf[i]=r; buf[i+1]=g; buf[i+2]=b; buf[i+3]=0xFF;
        }
    }
}

static void loadTextures()
{
    auto loadPNG = [](const char* path) -> GLuint {
        int w, h, ch;
        unsigned char* data = stbi_load(path, &w, &h, &ch, 4);
        if(!data){ fprintf(stderr, "Failed to load texture: %s\n", path); return 0; }
        GLuint t = makeTex(data, w, h);
        stbi_image_free(data);
        return t;
    };

    gTexPlayer = loadPNG(ASSETS_DIR "survivor.png");
    gTexZombie = loadPNG(ASSETS_DIR "zombie.png");
    gTexBullet = loadPNG(ASSETS_DIR "bullet.png");

    static unsigned char bg0[4*64*4], bg1[4*64*4], bg2[4*64*4];
    buildBgTile(bg0,4,64, 0x08,0x08,0x08, 0x0D,0x0D,0x0D);
    buildBgTile(bg1,4,64, 0x0E,0x0E,0x0E, 0x14,0x14,0x14);
    buildBgTile(bg2,4,64, 0x14,0x14,0x14, 0x1A,0x1A,0x1A);
    gTexBg[0] = makeTex(bg0,4,64);
    gTexBg[1] = makeTex(bg1,4,64);
    gTexBg[2] = makeTex(bg2,4,64);
}

static void setModel(float px, float py, float pw, float ph)
{
    mat4 m = translate(mat4(1.f), vec3(px,py,0));
    m = scale(m, vec3(pw,ph,1));
    glUniformMatrix4fv(uModel,1,GL_FALSE,value_ptr(m));
}

// textured sprite
static void drawSprite(GLuint tex, float px, float py, float sz,
                       int frame, int nFrames)
{
    glUniform1i(uUseTex,1);
    glUniform1i(uTex,0);
    glUniform4f(uTint,1,1,1,1);
    glUniform2f(uUVOff, (float)frame/nFrames, 0);
    glUniform2f(uUVScl, 1.0f/nFrames, 1);
    setModel(px,py,sz,sz);
    glBindTexture(GL_TEXTURE_2D,tex);
    glBindVertexArray(gVAO);
    glDrawArrays(GL_TRIANGLES,0,6);
}

// solid colored rectangle
static void drawRect(float px, float py, float pw, float ph,
                     float r, float g, float b, float a=1)
{
    glUniform1i(uUseTex,0);
    glUniform4f(uTint,r,g,b,a);
    setModel(px,py,pw,ph);
    glBindVertexArray(gVAO);
    glDrawArrays(GL_TRIANGLES,0,6);
}

// tiling+scrolling background layer
static void drawBackground(int idx, float offsetPx)
{
    glUniform1i(uUseTex,1);
    glUniform1i(uTex,0);
    glUniform4f(uTint,1,1,1,1);
    // tile the 4×64 texture to fill 800×600; scroll by offsetting UV.y
    glUniform2f(uUVOff, 0, offsetPx/64.0f);
    glUniform2f(uUVScl, (float)WIN_W/4.0f, (float)WIN_H/64.0f);
    setModel(0,0,(float)WIN_W,(float)WIN_H);
    glBindTexture(GL_TEXTURE_2D,gTexBg[idx]);
    glBindVertexArray(gVAO);
    glDrawArrays(GL_TRIANGLES,0,6);
}

// font for HUD/menu text
static const unsigned char FONT_AZ[26][15] = {
    {0,1,0, 1,0,1, 1,1,1, 1,0,1, 1,0,1}, // A
    {1,1,0, 1,0,1, 1,1,0, 1,0,1, 1,1,0}, // B
    {0,1,1, 1,0,0, 1,0,0, 1,0,0, 0,1,1}, // C
    {1,1,0, 1,0,1, 1,0,1, 1,0,1, 1,1,0}, // D
    {1,1,1, 1,0,0, 1,1,0, 1,0,0, 1,1,1}, // E
    {1,1,1, 1,0,0, 1,1,0, 1,0,0, 1,0,0}, // F
    {0,1,1, 1,0,0, 1,0,1, 1,0,1, 0,1,1}, // G
    {1,0,1, 1,0,1, 1,1,1, 1,0,1, 1,0,1}, // H
    {1,1,1, 0,1,0, 0,1,0, 0,1,0, 1,1,1}, // I
    {0,1,1, 0,0,1, 0,0,1, 1,0,1, 0,1,1}, // J
    {1,0,1, 1,0,1, 1,1,0, 1,0,1, 1,0,1}, // K
    {1,0,0, 1,0,0, 1,0,0, 1,0,0, 1,1,1}, // L
    {1,0,1, 1,1,1, 1,0,1, 1,0,1, 1,0,1}, // M
    {1,0,1, 1,1,0, 1,0,1, 1,0,1, 1,0,1}, // N
    {1,1,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1}, // O
    {1,1,0, 1,0,1, 1,1,0, 1,0,0, 1,0,0}, // P
    {1,1,1, 1,0,1, 1,0,1, 1,1,1, 0,0,1}, // Q
    {1,1,0, 1,0,1, 1,1,0, 1,0,1, 1,0,1}, // R
    {1,1,1, 1,0,0, 1,1,1, 0,0,1, 1,1,1}, // S
    {1,1,1, 0,1,0, 0,1,0, 0,1,0, 0,1,0}, // T
    {1,0,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1}, // U
    {1,0,1, 1,0,1, 1,0,1, 1,0,1, 0,1,0}, // V
    {1,0,1, 1,0,1, 1,1,1, 1,1,1, 1,0,1}, // W
    {1,0,1, 1,0,1, 0,1,0, 1,0,1, 1,0,1}, // X
    {1,0,1, 1,0,1, 0,1,0, 0,1,0, 0,1,0}, // Y
    {1,1,1, 0,0,1, 0,1,0, 1,0,0, 1,1,1}, // Z
};
static const unsigned char FONT_09[10][15] = {
    {1,1,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1}, // 0
    {0,1,0, 1,1,0, 0,1,0, 0,1,0, 1,1,1}, // 1
    {1,1,1, 0,0,1, 1,1,1, 1,0,0, 1,1,1}, // 2
    {1,1,1, 0,0,1, 0,1,1, 0,0,1, 1,1,1}, // 3
    {1,0,1, 1,0,1, 1,1,1, 0,0,1, 0,0,1}, // 4
    {1,1,1, 1,0,0, 1,1,1, 0,0,1, 1,1,1}, // 5
    {1,1,1, 1,0,0, 1,1,1, 1,0,1, 1,1,1}, // 6
    {1,1,1, 0,0,1, 0,0,1, 0,1,0, 0,1,0}, // 7
    {1,1,1, 1,0,1, 1,1,1, 1,0,1, 1,1,1}, // 8
    {1,1,1, 1,0,1, 1,1,1, 0,0,1, 1,1,1}, // 9
};

static void drawChar(char c, float px, float py, float cs, float r, float g, float b)
{
    const unsigned char* bits = nullptr;
    if(c >= 'A' && c <= 'Z') bits = FONT_AZ[c-'A'];
    else if(c >= '0' && c <= '9') bits = FONT_09[c-'0'];
    else if(c == ':'){
        // simple colon
        drawRect(px+cs, py+cs,    cs,cs, r,g,b);
        drawRect(px+cs, py+3*cs,  cs,cs, r,g,b);
        return;
    } else return; // space / unknown = skip
    for(int row=0;row<5;row++)
        for(int col=0;col<3;col++)
            if(bits[row*3+col])
                drawRect(px+col*cs, py+row*cs, cs,cs, r,g,b);
}

static void drawText(const char* str, float px, float py, float cs,
                     float r=1,float g=1,float b=1)
{
    float cx = px;
    for(int i=0; str[i]; i++){
        drawChar(str[i], cx, py, cs, r,g,b);
        cx += 4*cs; // 3px wide + 1px gap
    }
}

// Game logic
static float randF(float lo, float hi){
    return lo + ((float)rand()/RAND_MAX)*(hi-lo);
}

static void resetGame()
{
    gPlayer = {(float)WIN_W*0.5f, 0.f, 0, 0.f};
    for(auto& b : gBullets) b.active = false;
    for(auto& e : gEnemies) e.active = false;
    gLives      = PLAYER_LIVES;
    gScore      = 0;
    gSpawnTimer = 0;
    gNextSpawn  = randF(SPAWN_MIN, SPAWN_MAX);
    gBgOff[0]=gBgOff[1]=gBgOff[2]=0;
}

// spawn enemies randomly in a inteval
static void spawnEnemy()
{
    for(auto& e : gEnemies){
        if(!e.active){
            e.active    = true;
            e.x         = randF(CHAR_SIZE*0.5f, WIN_W-CHAR_SIZE*0.5f);
            e.y         = -CHAR_SIZE;
            e.hp        = ENEMY_MAX_HP;
            e.frame     = 0;
            e.animTimer = 0;
            return;
        }
    }
}

static int enemyCount(){
    int c=0; for(auto& e:gEnemies) if(e.active) c++; return c;
}

static void update(float dt)
{
    // scroll
    gBgOff[0] += 5.f  * dt;
    gBgOff[1] += 15.f * dt;
    gBgOff[2] += 30.f * dt;

    if(gState != STATE_PLAYING) return;

    // Player movement
    if(gKeyLeft)  gPlayer.x -= PLAYER_SPEED * dt;
    if(gKeyRight) gPlayer.x += PLAYER_SPEED * dt;
    float half = SPRITE_SIZE*0.5f;
    if(gPlayer.x < half)        gPlayer.x = half;
    if(gPlayer.x > WIN_W-half)  gPlayer.x = WIN_W-half;

    // Shoot
    if(gPlayer.shootTimer > 0) gPlayer.shootTimer -= dt;
    if(gKeyShoot && gPlayer.shootTimer <= 0){
        gPlayer.shootTimer = SHOOT_CD;
        gPlayer.frame      = 1;
        gPlayer.animTimer  = 0.2f;
        for(auto& b : gBullets){
            if(!b.active){
                b.active = true;
                b.x = gPlayer.x;
                b.y = PLAYER_Y - SPRITE_SIZE;
                break;
            }
        }
    }
    gKeyShoot = false;

    // Player animation:
    if(gPlayer.frame == 1){
        gPlayer.animTimer -= dt;
        if(gPlayer.animTimer <= 0) gPlayer.frame = 0;
    }

    // Bullets movement
    for(auto& b : gBullets){
        if(!b.active) continue;
        b.y -= BULLET_SPEED * dt;
        if(b.y < -SPRITE_SIZE) b.active = false;
    }

    // Enemies: move + animate
    for(auto& e : gEnemies){
        if(!e.active) continue;
        e.y += ENEMY_SPEED * dt;
        e.animTimer += dt;
        if(e.animTimer >= 1.f/ANIM_FPS){
            e.animTimer = 0;
            e.frame = (e.frame+1)%4;
        }
        // Crossed danger line
        if(e.y + CHAR_SIZE >= DANGER_Y){
            e.active = false;
            if(--gLives <= 0){ gLives=0; gState=STATE_GAMEOVER; return; }
        }
    }

    // Bullet × Enemy AABB collisions
    for(auto& b : gBullets){
        if(!b.active) continue;
        //bullet hitbox
        float bx1=b.x-half, bx2=bx1+SPRITE_SIZE;
        float by1=b.y,       by2=by1+SPRITE_SIZE;
        for(auto& e : gEnemies){
            if(!e.active) continue;
            //enemies hitbox
            float eHalf = CHAR_SIZE*0.5f;
            float ex1=e.x-eHalf, ex2=ex1+CHAR_SIZE;
            float ey1=e.y,        ey2=ey1+CHAR_SIZE;
            if(bx1<ex2 && bx2>ex1 && by1<ey2 && by2>ey1){
                b.active = false;
                if(--e.hp <= 0){ e.active = false; gScore++; }
                break;
            }
        }
    }

    // Spawn enemies
    if(enemyCount() < MAX_ENEMIES){
        gSpawnTimer += dt;
        if(gSpawnTimer >= gNextSpawn){
            gSpawnTimer = 0;
            gNextSpawn  = randF(SPAWN_MIN, SPAWN_MAX);
            spawnEnemy();
        }
    }
}

// Render
static void render()
{
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(gShader);
    glUniformMatrix4fv(uProj,1,GL_FALSE,value_ptr(gProj));
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

    // Background layers — always visible
    drawBackground(0, gBgOff[0]);
    drawBackground(1, gBgOff[1]);
    drawBackground(2, gBgOff[2]);

    float blink = (float)fmod(glfwGetTime()*2.0,2.0)<1.0f ? 1.f : 0.3f;

    if(gState == STATE_MENU){
        // Title panel
        drawRect(200,190, 400,60, 0.05f,0.1f,0.05f);
        drawText("ZOMBIE",    210, 200, 5.f, 0.2f,1.f,0.2f);
        drawText("SHOOTER",   210, 230, 5.f, 0.9f,0.9f,0.2f);
        // Instruction
        drawRect(220,300, 360,40, 0.f,0.f,0.f,0.6f);
        drawText("PRESS ENTER",222,310,5.f, blink,blink*0.8f,0.f);
        // Controls hint
        drawText("A D  MOVE",  270,370,3.f, 0.5f,0.5f,0.5f);
        drawText("SPACE SHOOT",240,390,3.f, 0.5f,0.5f,0.5f);
        return;
    }

    if(gState == STATE_GAMEOVER){
        drawRect(160,220, 480,70, 0.15f,0.f,0.f);
        drawText("GAME OVER", 175,230, 6.f, 1.f,0.15f,0.15f);
        char scoreStr[16]; sprintf(scoreStr, "%d", gScore);
        drawText("SCORE:", 270,295, 4.f, 1,1,1);
        drawText(scoreStr,  374,295, 4.f, 0.2f,1.f,0.2f);
        drawRect(220,320, 360,40, 0.f,0.f,0.f,0.6f);
        drawText("PRESS ENTER",222,330,5.f, blink,blink*0.8f,0.f);
        return;
    }

    // --- PLAYING ---

    // Danger line (white)
    drawRect(0, DANGER_Y, (float)WIN_W, 2.f, 1,1,1);

    // Bullets
    for(auto& b : gBullets){
        if(!b.active) continue;
        drawSprite(gTexBullet, b.x-SPRITE_SIZE*0.5f, b.y, SPRITE_SIZE, 0, 1);
    }

    // Enemies + HP bars
    for(auto& e : gEnemies){
        if(!e.active) continue;
        float ex = e.x - CHAR_SIZE*0.5f;
        drawSprite(gTexZombie, ex, e.y, CHAR_SIZE, 0, 1);
        // HP bar
        float bw = CHAR_SIZE, bh=4.f, bx=ex, by=e.y-7.f;
        drawRect(bx, by, bw, bh, 0.3f,0.f,0.f);
        drawRect(bx, by, bw*(float)e.hp/ENEMY_MAX_HP, bh, 0.f,0.85f,0.f);
    }

    // Player
    drawSprite(gTexPlayer,
               gPlayer.x - CHAR_SIZE*0.5f,
               PLAYER_Y  - CHAR_SIZE*0.5f,
               CHAR_SIZE, 0, 1);

    // HUD: lives
    drawText("LIVES:",10,10,3.f, 1,1,1);
    for(int i=0;i<PLAYER_LIVES;i++){
        float r = (i < gLives) ? 1.f : 0.2f;
        float g = 0.1f;
        drawRect(90+i*18.f, 10, 14, 22, r, g, g);
    }

    // HUD: score
    char scoreStr[16];
    sprintf(scoreStr, "%d", gScore);
    drawText("SCORE:", 530, 10, 3.f, 1,1,1);
    drawText(scoreStr,  622, 10, 3.f, 0.2f,1.f,0.2f);
}

// GLFW callbacks
static void onKey(GLFWwindow* win, int key, int /*sc*/, int action, int /*mods*/)
{
    if(action==GLFW_PRESS && key==GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(win,GLFW_TRUE);

    if(action==GLFW_PRESS && key==GLFW_KEY_ENTER)
        if(gState==STATE_MENU || gState==STATE_GAMEOVER){
            resetGame();
            gState = STATE_PLAYING;
        }

    if(key==GLFW_KEY_LEFT  || key==GLFW_KEY_A) gKeyLeft  = (action!=GLFW_RELEASE);
    if(key==GLFW_KEY_RIGHT || key==GLFW_KEY_D) gKeyRight = (action!=GLFW_RELEASE);
    if(action==GLFW_PRESS  && key==GLFW_KEY_SPACE) gKeyShoot = true;
}

// main
int main()
{
    srand((unsigned)time(nullptr));

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE,GLFW_FALSE);

    GLFWwindow* win = glfwCreateWindow(WIN_W,WIN_H,"ZombieShooter",nullptr,nullptr);
    glfwMakeContextCurrent(win);
    glfwSetKeyCallback(win, onKey);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    gShader = buildProgram();
    gProj   = ortho(0.f,(float)WIN_W,(float)WIN_H,0.f,-1.f,1.f);
    setupQuad();
    loadTextures();

    uProj   = glGetUniformLocation(gShader,"projection");
    uModel  = glGetUniformLocation(gShader,"model");
    uUVOff  = glGetUniformLocation(gShader,"uvOff");
    uUVScl  = glGetUniformLocation(gShader,"uvScl");
    uTint   = glGetUniformLocation(gShader,"tint");
    uTex    = glGetUniformLocation(gShader,"tex");
    uUseTex = glGetUniformLocation(gShader,"useTex");

    gState = STATE_MENU;
    resetGame();

    double last = glfwGetTime();
    while(!glfwWindowShouldClose(win)){
        double now = glfwGetTime();
        float dt = (float)(now-last); last=now;
        if(dt>0.05f) dt=0.05f;

        glfwPollEvents();
        update(dt);
        render();
        glfwSwapBuffers(win);
    }

    glfwTerminate();
    return 0;
}
