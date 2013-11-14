///////////////////////////////////////////////////////////////////////////////
// main.cpp
// ========
// testing Pixel Buffer Object for unpacking (uploading) pixel data to PBO
// using GL_ARB_pixel_buffer_object extension
// It uses 2 PBOs to optimize uploading pipeline; application to PBO, and PBO to
// texture object.
//
//  AUTHOR: Song Ho Ahn (song.ahn@gmail.com)
// CREATED: 2007-10-22
// UPDATED: 2012-06-07
///////////////////////////////////////////////////////////////////////////////

// in order to get function prototypes from glext.h, define GL_GLEXT_PROTOTYPES before including glext.h
#define GL_GLEXT_PROTOTYPES

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#include <cstdlib> // posix_memalign
#include <malloc.h> // _aligned_malloc on Windows
#include <cstdio>
#include <cstdint> // uintptr_t
#include <cstring>
#include <cassert>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <vector>

#if defined(_WIN32)
#include <windows.h> // GetSystemInfo
#elif defined (__gnu_linux__)
#include <unistd.h> // sysconf
#endif

#include "glInfo.h" // glInfo struct
#include "Timer.h"
#include "glext.h"
#define GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD 0x9160

using std::stringstream;
using std::cout;
using std::endl;
using std::ends;

// GLUT CALLBACK functions ////////////////////////////////////////////////////
void displayCB();

void toOrtho();
void toPerspective();
void reshapeCB(int w, int h);

void timerCB(int millisec);
void idleCB();
void keyboardCB(unsigned char key, int x, int y);
void mouseCB(int button, int stat, int x, int y);
void mouseMotionCB(int x, int y);

void exitCB(); // CALLBACK function when exit() is called

// Program functions //////////////////////////////////////////////////////////
void initGL();
int  initGLUT(int argc, char **argv);
bool initSharedMem();
void clearSharedMem();
void initLights();
void setCamera(float posX, float posY, float posZ, float targetX, float targetY, float targetZ);
void updatePixels(GLubyte* dst, int size);
void drawString(const char *str, int x, int y, float color[4], void *font);
void drawString3D(const char *str, float pos[3], float color[4], void *font);
void showInfo();
void showTransferRate();
void printTransferRate();
void resetTransferRate();

/* 'alignment' must be a power of 2. */
void* alignedMalloc(size_t alignment, size_t size);
void alignedFree(void* ptr);
void setPboCount(int count);

// Program Constants //////////////////////////////////////////////////////////
//const int    SCREEN_WIDTH    = 400;
//const int    SCREEN_HEIGHT   = 300;
const int    SCREEN_WIDTH    = 800;
const int    SCREEN_HEIGHT   = 600;
const float  CAMERA_DISTANCE = 3.0f;
const int    TEXT_WIDTH      = 8;
const int    TEXT_HEIGHT     = 13;
//const int    IMAGE_WIDTH = 1024;
//const int    IMAGE_HEIGHT = 1024;
//const int    IMAGE_WIDTH = 8192;
//const int    IMAGE_HEIGHT = 8192; // 8192*8192*4 = 256 MB each frame
const int    IMAGE_WIDTH = 4096;
const int    IMAGE_HEIGHT = 4096; // 4096*4096*4 = 64 MB each frame
const int    DATA_SIZE = IMAGE_WIDTH * IMAGE_HEIGHT * 4;
const GLenum PIXEL_FORMAT = GL_BGRA;

// Global Variables ///////////////////////////////////////////////////////////
void* font = GLUT_BITMAP_8_BY_13;
GLuint textureId;                   // ID of texture
GLubyte* imageData = NULL;             // pointer to texture buffer
int screenWidth;
int screenHeight;
bool mouseLeftDown;
bool mouseRightDown;
float mouseX, mouseY;
float cameraAngleX;
float cameraAngleY;
float cameraDistance;

// Performance measurement
int drawMode = 0;
Timer timer, t1, t2;
float copyTime, updateTime;

// See resetTransferRate()
static int rateDiscarded = 3; // Discard first measurements
static int rateCount = 0;
static double transferRateSum = 0;
static double frameRateSum = 0;

bool pboSupported = false;
bool amdSupported = false;
long int systemPageSize = 4096; // Default value, will be checked at runtime
int pboCount = 0; // Amount of Pixel Buffer Objects used
std::vector<GLuint> pboIds; // IDs of Pixel Buffer Objects
std::vector<GLsync> pboFences; // Sync Fences used for the UNSYNCH_FENCES method
std::vector<GLubyte*> alignedBuffers; // Buffers used for the AMD_pinned_memory method

/* Texture Streaming methods:
 * 0: No streaming at all. Just load texture data from the System Memory.
 * 1: Use basic Buffer Re-specification ("Orphaning").
 * 2: Use Unsynchronized Buffer Update with Orphaning.
 * 3: Use Unsynchronized Buffer Update with Fences synchronization.
 * 4: Use 'AMD_pinned_memory' extension.
 */
enum PboMethod { NONE, ORPHAN, UNSYNCH_ORPHAN, UNSYNCH_FENCES, AMD };
PboMethod pboMethod = NONE;

// Function pointers for PBO Extension ////////////////////////////////////////
// Windows needs to get function pointers from ICD OpenGL drivers,
// because opengl32.dll does not support extensions higher than v1.1.
#ifdef _WIN32
PFNGLGENBUFFERSARBPROC pglGenBuffersARB = 0;                     // VBO Name Generation Procedure
PFNGLBINDBUFFERARBPROC pglBindBufferARB = 0;                     // VBO Bind Procedure
PFNGLBUFFERDATAARBPROC pglBufferDataARB = 0;                     // VBO Data Loading Procedure
PFNGLBUFFERSUBDATAARBPROC pglBufferSubDataARB = 0;               // VBO Sub Data Loading Procedure
PFNGLDELETEBUFFERSARBPROC pglDeleteBuffersARB = 0;               // VBO Deletion Procedure
PFNGLGETBUFFERPARAMETERIVARBPROC pglGetBufferParameterivARB = 0; // return various parameters of VBO
PFNGLMAPBUFFERARBPROC pglMapBufferARB = 0;                       // map VBO procedure
PFNGLUNMAPBUFFERARBPROC pglUnmapBufferARB = 0;                   // unmap VBO procedure
#define glGenBuffersARB           pglGenBuffersARB
#define glBindBufferARB           pglBindBufferARB
#define glBufferDataARB           pglBufferDataARB
#define glBufferSubDataARB        pglBufferSubDataARB
#define glDeleteBuffersARB        pglDeleteBuffersARB
#define glGetBufferParameterivARB pglGetBufferParameterivARB
#define glMapBufferARB            pglMapBufferARB
#define glUnmapBufferARB          pglUnmapBufferARB
#endif

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv)
{
    initSharedMem();

    // register exit callback
    atexit(exitCB);

    // init GLUT and GL
    initGLUT(argc, argv);
    initGL();

    // get OpenGL info
    glInfo glInfo;
    glInfo.getInfo();
    //glInfo.printSelf();

    // init 2 texture objects
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, IMAGE_WIDTH, IMAGE_HEIGHT, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, (GLvoid*)imageData);
    glBindTexture(GL_TEXTURE_2D, 0);

#if defined(_WIN32)
    // check PBO is supported by your video card
    if(glInfo.isExtensionSupported("GL_ARB_pixel_buffer_object"))
    {
        // get pointers to GL functions
        glGenBuffersARB = (PFNGLGENBUFFERSARBPROC)wglGetProcAddress("glGenBuffersARB");
        glBindBufferARB = (PFNGLBINDBUFFERARBPROC)wglGetProcAddress("glBindBufferARB");
        glBufferDataARB = (PFNGLBUFFERDATAARBPROC)wglGetProcAddress("glBufferDataARB");
        glBufferSubDataARB = (PFNGLBUFFERSUBDATAARBPROC)wglGetProcAddress("glBufferSubDataARB");
        glDeleteBuffersARB = (PFNGLDELETEBUFFERSARBPROC)wglGetProcAddress("glDeleteBuffersARB");
        glGetBufferParameterivARB = (PFNGLGETBUFFERPARAMETERIVARBPROC)wglGetProcAddress("glGetBufferParameterivARB");
        glMapBufferARB = (PFNGLMAPBUFFERARBPROC)wglGetProcAddress("glMapBufferARB");
        glUnmapBufferARB = (PFNGLUNMAPBUFFERARBPROC)wglGetProcAddress("glUnmapBufferARB");

        // check once again PBO extension
        if(glGenBuffersARB && glBindBufferARB && glBufferDataARB && glBufferSubDataARB &&
                glMapBufferARB && glUnmapBufferARB && glDeleteBuffersARB && glGetBufferParameterivARB)
        {
            pboSupported = true;
            pboMode = 1;    // using 1 PBO
            cout << "Video card supports GL_ARB_pixel_buffer_object." << endl;
        }
        else
        {
            pboSupported = false;
            pboMode = 0;    // without PBO
            cout << "Video card does NOT support GL_ARB_pixel_buffer_object." << endl;
        }
    }

    // Query the system memory page size and update the default value
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    if (si.dwPageSize > 0) {
        systemPageSize = si.dwPageSize;
    }
#elif defined (__gnu_linux__)
    // for linux, do not need to get function pointers, it is up-to-date
    if (glInfo.isExtensionSupported("GL_ARB_pixel_buffer_object")) {
        pboSupported = true;
        cout << "Video card supports GL_ARB_pixel_buffer_object" << endl;
    }
    else {
        cout << "Video card does NOT support GL_ARB_pixel_buffer_object" << endl;
    }

    if(glInfo.isExtensionSupported("GL_AMD_pinned_memory")) {
        amdSupported = true;
        cout << "Video card supports GL_AMD_pinned_memory" << endl;
    }
    else {
        cout << "Video card does NOT support GL_AMD_pinned_memory" << endl;
    }

    // Query the system memory page size and update the default value
    if (sysconf(_SC_PAGE_SIZE) > 0) {
        systemPageSize = sysconf(_SC_PAGE_SIZE);
    }
#endif

    cout << "System memory page size: " << systemPageSize << " bytes" << endl;
    cout << "Texture data size: " << DATA_SIZE << " bytes" << endl;

    // Moved to setPboCount()
    //    if (pboSupported)
    //    {
    //        // create 2 pixel buffer objects, you need to delete them when program exits.
    //        // glBufferDataARB with NULL pointer reserves only memory space.
    //        glGenBuffersARB(2, pboIds);
    //        glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, pboIds[0]);
    //        glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, DATA_SIZE, 0, GL_STREAM_DRAW_ARB);
    //        glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, pboIds[1]);
    //        glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, DATA_SIZE, 0, GL_STREAM_DRAW_ARB);
    //        glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
    //    }

    // start timer, the elapsed time will be used for updateVertices()
    timer.start();

    // the last GLUT call (LOOP)
    // window will be shown and display callback is triggered by events
    // NOTE: this call never return main().
    glutMainLoop(); /* Start GLUT event-processing loop */

    return 0;
}


//=============================================================================
// CALLBACKS
//=============================================================================

void displayCB()
{
    if (pboMethod == NONE) {
        /*
         * Update data in System Memory.
         */
        t1.start();
        updatePixels(imageData, DATA_SIZE);
        t1.stop();
        updateTime = t1.getElapsedTimeInMilliSec();

        /*
         * Copy data from System Memory to texture object.
         */
        t1.start();
        glBindTexture(GL_TEXTURE_2D, textureId);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, PIXEL_FORMAT, GL_UNSIGNED_BYTE, (GLvoid*)imageData);
        t1.stop();
        copyTime = t1.getElapsedTimeInMilliSec();
    }
    else {
        /*
         * Update buffer indices used in data upload & copy.
         *
         * "uploadIdx": index used to upload pixels to a Pixel Buffer Object.
         * "copyIdx": index used to copy pixels from a Pixel Buffer Object to a GPU texture.
         *
         * When (pboCount > 1), this will allow to perform
         * simultaneous upload & copy, by using alternative buffers.
         * That is a good thing, unless the double buffering is being already
         * done somewhere else in the code.
         */
        static int copyIdx = 0;
        copyIdx = (copyIdx + 1) % pboCount;
        int uploadIdx = (copyIdx + 1) % pboCount;

        /*
         * Upload new data to a Pixel Buffer Object.
         */
        t1.start();

        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboIds[uploadIdx]); // Access the Pixel Buffer Object and bind it

        if (pboMethod == ORPHAN) {
            glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, DATA_SIZE, NULL, GL_STREAM_DRAW_ARB);
            GLubyte* ptr = (GLubyte*)glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, GL_WRITE_ONLY_ARB);
            if (NULL == ptr) {
                cout << "ERROR [displayCB] (glMapBufferARB): " << (char*)gluErrorString(glGetError()) << endl;
                return;
            }
            else {
                // update data directly on the mapped buffer
                updatePixels(ptr, DATA_SIZE);
                // release pointer to mapping buffer
                if (!glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB)) {
                    cout << "ERROR [displayCB] (glUnmapBufferARB): " << (char*)gluErrorString(glGetError()) << endl;
                }
            }
        }
        else if (pboMethod == UNSYNCH_ORPHAN || pboMethod == UNSYNCH_FENCES) {
            if (pboMethod == UNSYNCH_FENCES) {
                if (glIsSync(pboFences[uploadIdx])) {
                    GLenum result = glClientWaitSync(pboFences[uploadIdx], 0, GL_TIMEOUT_IGNORED);
                    switch (result) {
                    case GL_ALREADY_SIGNALED:
                        // Transfer was already done when trying to use buffer
                        //cout << "DEBUG (glClientWaitSync): ALREADY_SIGNALED (good timing!) uploadIdx: " << uploadIdx << endl;
                        break;
                    case GL_CONDITION_SATISFIED:
                        // This means that we had to wait for the fence to synchronize us after using all the buffers,
                        // which implies that the GPU command queue is full and that we are GPU-bound (DMA transfers aren't fast enough).
                        //cout << "WARNING (glClientWaitSync): CONDITION_SATISFIED (had to wait for the sync) uploadIdx: " << uploadIdx << endl;
                        break;
                    case GL_TIMEOUT_EXPIRED:
                        cout << "WARNING (glClientWaitSync): TIMEOUT_EXPIRED (DMA transfers are too slow!) uploadIdx: " << uploadIdx << endl;
                        break;
                    case GL_WAIT_FAILED:
                        cout << "ERROR (glClientWaitSync): WAIT_FAILED: " << (char*)gluErrorString(glGetError()) << endl;
                        break;
                    }
                    glDeleteSync(pboFences[uploadIdx]); pboFences[uploadIdx] = NULL;
                }
            }
            else if (pboMethod == UNSYNCH_ORPHAN) {
                glBufferData(GL_PIXEL_UNPACK_BUFFER, DATA_SIZE, NULL, GL_STREAM_DRAW); // Buffer re-specification (orphaning)
            }
            GLubyte* ptr = (GLubyte*)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, DATA_SIZE, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
            if (NULL == ptr) {
                cout << "ERROR [displayCB] (glMapBufferRange): " << (char*)gluErrorString(glGetError()) << endl;
                return;
            }
            else {
                updatePixels(ptr, DATA_SIZE); // Update data directly on the mapped buffer
                if (!glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER)) {
                    cout << "ERROR [displayCB] (glUnmapBuffer): " << (char*)gluErrorString(glGetError()) << endl;
                }
            }
        }
        else if (pboMethod == AMD) {
            if (glIsSync(pboFences[uploadIdx])) {
                GLenum result = glClientWaitSync(pboFences[uploadIdx], 0, GL_TIMEOUT_IGNORED);
                switch (result) {
                case GL_ALREADY_SIGNALED:
                    // Transfer was already done when trying to use buffer
                    //cout << "DEBUG (glClientWaitSync): ALREADY_SIGNALED (good timing!) uploadIdx: " << uploadIdx << endl;
                    break;
                case GL_CONDITION_SATISFIED:
                    // This means that we had to wait for the fence to synchronize us after using all the buffers,
                    // which implies that the GPU command queue is full and that we are GPU-bound (DMA transfers aren't fast enough).
                    //cout << "WARNING (glClientWaitSync): CONDITION_SATISFIED (had to wait for the sync) uploadIdx: " << uploadIdx << endl;
                    break;
                case GL_TIMEOUT_EXPIRED:
                    cout << "WARNING (glClientWaitSync): TIMEOUT_EXPIRED (DMA transfers are too slow!) uploadIdx: " << uploadIdx << endl;
                    break;
                case GL_WAIT_FAILED:
                    cout << "ERROR (glClientWaitSync): WAIT_FAILED: " << (char*)gluErrorString(glGetError()) << endl;
                    break;
                }
                glDeleteSync(pboFences[uploadIdx]); pboFences[uploadIdx] = NULL;
            }
            updatePixels(alignedBuffers[uploadIdx], DATA_SIZE); // Update data directly on the mapped buffer
        }

        t1.stop();
        updateTime = t1.getElapsedTimeInMilliSec();

        /*
         * Protect each Pixel Buffer Object against being overwritten.
         *
         * Tipically the data upload will be slower than our main loop, so this
         * function will be called again before the previous frame was uploaded
         * and processed. The main bottleneck is the PCI bus transfer speed,
         * which limits how fast the DMA (System Memory --> VRAM) can work.
         *
         * OpenGL Sync Fences will block until the PBO is released.
         */
        if (pboMethod == UNSYNCH_FENCES || pboMethod == AMD) {
            pboFences[uploadIdx] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        }

        /*
         * Copy data from a Pixel Buffer Object to a GPU texture.
         * glTexSubImage2D() will copy pixels to the corresponding texture in the GPU.
         */
        t1.start();

        glBindTexture(GL_TEXTURE_2D, textureId); // Bind the texture
        glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, pboIds[copyIdx]); // Access the Pixel Buffer Object and bind it

        // Use offset instead of pointer
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, PIXEL_FORMAT, GL_UNSIGNED_BYTE, 0);

        t1.stop();
        copyTime = t1.getElapsedTimeInMilliSec();

        // it is good idea to release PBOs with ID 0 after use.
        // Once bound with 0, all pixel operations behave normal ways.
        glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
    }

    // clear buffer
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    // save the initial ModelView matrix before modifying ModelView matrix
    glPushMatrix();

    // tramsform camera
    glTranslatef(0, 0, -cameraDistance);
    glRotatef(cameraAngleX, 1, 0, 0); // pitch
    glRotatef(cameraAngleY, 0, 1, 0); // heading

    // draw a point with texture
    glBindTexture(GL_TEXTURE_2D, textureId);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);
    glTexCoord2f(0.0f, 0.0f);   glVertex3f(-1.0f, -1.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f);   glVertex3f( 1.0f, -1.0f, 0.0f);
    glTexCoord2f(1.0f, 1.0f);   glVertex3f( 1.0f,  1.0f, 0.0f);
    glTexCoord2f(0.0f, 1.0f);   glVertex3f(-1.0f,  1.0f, 0.0f);
    glEnd();

    // unbind texture
    glBindTexture(GL_TEXTURE_2D, 0);

    // draw info messages
    showInfo();
    //showTransferRate();
    printTransferRate();

    glPopMatrix();

    glutSwapBuffers();
}

///////////////////////////////////////////////////////////////////////////////
// set projection matrix as orthogonal
///////////////////////////////////////////////////////////////////////////////
void toOrtho()
{
    // set viewport to be the entire window
    glViewport(0, 0, (GLsizei)screenWidth, (GLsizei)screenHeight);

    // set orthographic viewing frustum
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, screenWidth, 0, screenHeight, -1, 1);

    // switch to modelview matrix in order to set scene
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

///////////////////////////////////////////////////////////////////////////////
// set the projection matrix as perspective
///////////////////////////////////////////////////////////////////////////////
void toPerspective()
{
    // set viewport to be the entire window
    glViewport(0, 0, (GLsizei)screenWidth, (GLsizei)screenHeight);

    // set perspective viewing frustum
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60.0f, (float)(screenWidth)/screenHeight, 1.0f, 1000.0f); // FOV, AspectRatio, NearClip, FarClip

    // switch to modelview matrix in order to set scene
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void reshapeCB(int width, int height)
{
    screenWidth = width;
    screenHeight = height;
    toPerspective();
}

void timerCB(int millisec)
{
    glutTimerFunc(millisec, timerCB, millisec);
    glutPostRedisplay();
}

void idleCB()
{
    glutPostRedisplay();
}

void keyboardCB(unsigned char key, int x, int y)
{
    switch(key)
    {
    case 27: // ESCAPE
        exit(0);
        break;

    case ' ':
        pboMethod = (PboMethod)(((int)pboMethod + 1) % (amdSupported ? 5 : 4));
        cout << "PBO Method: " << pboMethod << endl;
        setPboCount(1);
        resetTransferRate();
        break;

    case 'd': // switch rendering modes (fill -> wire -> point)
    case 'D':
        drawMode = (drawMode + 1) % 3;
        if(drawMode == 0) {
            // fill mode
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
        } else if(drawMode == 1) {
            // wireframe mode
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
        } else {
            // point mode
            glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
        }
        break;

    default:
        break;
    }

    if (key >= '0' && key <= '9') {
        setPboCount((int)key - (int)'0');
        resetTransferRate();
    }
}

void mouseCB(int button, int state, int x, int y)
{
    mouseX = x;
    mouseY = y;

    if(button == GLUT_LEFT_BUTTON)
    {
        if(state == GLUT_DOWN)
        {
            mouseLeftDown = true;
        }
        else if(state == GLUT_UP)
            mouseLeftDown = false;
    }

    else if(button == GLUT_RIGHT_BUTTON)
    {
        if(state == GLUT_DOWN)
        {
            mouseRightDown = true;
        }
        else if(state == GLUT_UP)
            mouseRightDown = false;
    }
}

void mouseMotionCB(int x, int y)
{
    if(mouseLeftDown)
    {
        cameraAngleY += (x - mouseX);
        cameraAngleX += (y - mouseY);
        mouseX = x;
        mouseY = y;
    }
    if(mouseRightDown)
    {
        cameraDistance -= (y - mouseY) * 0.2f;
        if(cameraDistance < 2.0f)
            cameraDistance = 2.0f;

        mouseY = y;
    }
}

void exitCB()
{
    clearSharedMem();
}


///////////////////////////////////////////////////////////////////////////////
// initialize OpenGL
// disable unused features
///////////////////////////////////////////////////////////////////////////////
void initGL()
{
    //@glShadeModel(GL_SMOOTH);                    // shading mathod: GL_SMOOTH or GL_FLAT
    glShadeModel(GL_FLAT);                      // shading mathod: GL_SMOOTH or GL_FLAT
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);      // 4-byte pixel alignment

    // enable /disable features
    //@glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    //glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    //glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);
    glEnable(GL_DEPTH_TEST);
    //@glEnable(GL_LIGHTING);
    glDisable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_CULL_FACE);

    // track material ambient and diffuse from surface color, call it before glEnable(GL_COLOR_MATERIAL)
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_COLOR_MATERIAL);

    glClearColor(0, 0, 0, 0);                   // background color
    glClearStencil(0);                          // clear stencil buffer
    glClearDepth(1.0f);                         // 0 is near, 1 is far
    glDepthFunc(GL_LEQUAL);

    //@initLights();
}

///////////////////////////////////////////////////////////////////////////////
// initialize GLUT for windowing
///////////////////////////////////////////////////////////////////////////////
int initGLUT(int argc, char **argv)
{
    // GLUT stuff for windowing
    // initialization openGL window.
    // it is called before any other GLUT routine
    glutInit(&argc, argv);

    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_ALPHA); // display mode

    //glutInitWindowSize(400, 300);               // window size
    glutInitWindowSize(SCREEN_WIDTH, SCREEN_HEIGHT);               // window size

    glutInitWindowPosition(100, 100);           // window location

    // finally, create a window with openGL context
    // Window will not displayed until glutMainLoop() is called
    // it returns a unique ID
    int handle = glutCreateWindow(argv[0]);     // param is the title of window

    // register GLUT callback functions
    glutDisplayFunc(displayCB);
    //glutTimerFunc(33, timerCB, 33);             // redraw only every given millisec
    glutIdleFunc(idleCB);                       // redraw only every given millisec
    glutReshapeFunc(reshapeCB);
    glutKeyboardFunc(keyboardCB);
    glutMouseFunc(mouseCB);
    glutMotionFunc(mouseMotionCB);

    return handle;
}

///////////////////////////////////////////////////////////////////////////////
// initialize global variables
///////////////////////////////////////////////////////////////////////////////
bool initSharedMem()
{
    screenWidth = SCREEN_WIDTH;
    screenHeight = SCREEN_HEIGHT;

    mouseLeftDown = mouseRightDown = false;
    mouseX = mouseY = 0;

    cameraAngleX = cameraAngleY = 0;
    cameraDistance = CAMERA_DISTANCE;

    drawMode = 0; // 0:fill, 1: wireframe, 2:points

    // allocate texture buffer
    imageData = new GLubyte[DATA_SIZE];
    memset(imageData, 0, DATA_SIZE);

    return true;
}

///////////////////////////////////////////////////////////////////////////////
// clean up shared memory
///////////////////////////////////////////////////////////////////////////////
void clearSharedMem()
{
    // deallocate texture buffer
    delete [] imageData; imageData = NULL;

    // clean up texture
    glDeleteTextures(1, &textureId);

    // clean up PBOs
    setPboCount(0);
}

///////////////////////////////////////////////////////////////////////////////
// initialize lights
///////////////////////////////////////////////////////////////////////////////
void initLights()
{
    // set up light colors (ambient, diffuse, specular)
    GLfloat lightKa[] = {.2f, .2f, .2f, 1.0f};  // ambient light
    GLfloat lightKd[] = {.7f, .7f, .7f, 1.0f};  // diffuse light
    GLfloat lightKs[] = {1, 1, 1, 1};           // specular light
    glLightfv(GL_LIGHT0, GL_AMBIENT, lightKa);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightKd);
    glLightfv(GL_LIGHT0, GL_SPECULAR, lightKs);

    // position the light
    float lightPos[4] = {0, 0, 20, 1}; // positional light
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);

    glEnable(GL_LIGHT0);                        // MUST enable each light source after configuration
}

///////////////////////////////////////////////////////////////////////////////
// set camera position and lookat direction
///////////////////////////////////////////////////////////////////////////////
void setCamera(float posX, float posY, float posZ, float targetX, float targetY, float targetZ)
{
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(posX, posY, posZ, targetX, targetY, targetZ, 0, 1, 0); // eye(x,y,z), focal(x,y,z), up(x,y,z)
}

///////////////////////////////////////////////////////////////////////////////
// copy an image data to texture buffer
///////////////////////////////////////////////////////////////////////////////
void updatePixels(GLubyte* dst, int size)
{
    static int color = 0;

    if(!dst)
        return;

    int* ptr = (int*)dst;

    // copy 4 bytes at once
    for(int i = 0; i < IMAGE_HEIGHT; ++i)
    {
        for(int j = 0; j < IMAGE_WIDTH; ++j)
        {
            *ptr = color;
            ++ptr;
        }
        color += 257;   // add an arbitary number (no meaning)
    }
    ++color;            // scroll down
}

///////////////////////////////////////////////////////////////////////////////
// write 2d text using GLUT
// The projection matrix must be set to orthogonal before call this function.
///////////////////////////////////////////////////////////////////////////////
void drawString(const char *str, int x, int y, float color[4], void *font)
{
    glPushAttrib(GL_LIGHTING_BIT | GL_CURRENT_BIT); // lighting and color mask
    glDisable(GL_LIGHTING);     // need to disable lighting for proper text color
    glDisable(GL_TEXTURE_2D);

    glColor4fv(color);          // set text color
    glRasterPos2i(x, y);        // place text position

    // loop all characters in the string
    while(*str)
    {
        glutBitmapCharacter(font, *str);
        ++str;
    }

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
    glPopAttrib();
}

///////////////////////////////////////////////////////////////////////////////
// draw a string in 3D space
///////////////////////////////////////////////////////////////////////////////
void drawString3D(const char *str, float pos[3], float color[4], void *font)
{
    glPushAttrib(GL_LIGHTING_BIT | GL_CURRENT_BIT); // lighting and color mask
    glDisable(GL_LIGHTING);     // need to disable lighting for proper text color
    glDisable(GL_TEXTURE_2D);

    glColor4fv(color);          // set text color
    glRasterPos3fv(pos);        // place text position

    // loop all characters in the string
    while(*str)
    {
        glutBitmapCharacter(font, *str);
        ++str;
    }

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
    glPopAttrib();
}

///////////////////////////////////////////////////////////////////////////////
// display info messages
///////////////////////////////////////////////////////////////////////////////
void showInfo()
{
    // backup current model-view matrix
    glPushMatrix();                     // save current modelview matrix
    glLoadIdentity();                   // reset modelview matrix

    // set to 2D orthogonal projection
    glMatrixMode(GL_PROJECTION);     // switch to projection matrix
    glPushMatrix();                  // save current projection matrix
    glLoadIdentity();                // reset projection matrix
    gluOrtho2D(0, screenWidth, 0, screenHeight); // set to orthogonal projection

    float color[4] = {1, 1, 1, 1};

    stringstream ss;
    ss << "PBO Count: ";
    if (pboCount == 0)
        ss << "off" << ends;
    else
        ss << pboCount << " PBO(s)" << ends;
    drawString(ss.str().c_str(), 1, screenHeight-TEXT_HEIGHT, color, font);
    ss.str(""); // clear buffer

    ss << "PBO Method: ";
    switch (pboMethod) {
    case NONE:
        ss << "None (direct transfer)" << ends; break;
    case ORPHAN:
        ss << "Orphaning" << ends; break;
    case UNSYNCH_ORPHAN:
        ss << "Unsynchronized with orphaning" << ends; break;
    case UNSYNCH_FENCES:
        ss << "Unsynchronized with fences synchronization" << ends; break;
    case AMD:
        ss << "AMD_pinned_memory" << ends; break;
    default: break;
    }
    drawString(ss.str().c_str(), 1, screenHeight-(2*TEXT_HEIGHT), color, font);
    ss.str("");

    ss << std::fixed << std::setprecision(3);
    ss << "Updating Time: " << updateTime << " ms" << ends;
    drawString(ss.str().c_str(), 1, screenHeight-(3*TEXT_HEIGHT), color, font);
    ss.str("");

    ss << "Copying Time: " << copyTime << " ms" << ends;
    drawString(ss.str().c_str(), 1, screenHeight-(4*TEXT_HEIGHT), color, font);
    ss.str("");

    ss << "Press SPACE key to toggle PBO on/off." << ends;
    drawString(ss.str().c_str(), 1, 1, color, font);

    // unset floating format
    ss << std::resetiosflags(std::ios_base::fixed | std::ios_base::floatfield);

    // restore projection matrix
    glPopMatrix();                   // restore to previous projection matrix

    // restore modelview matrix
    glMatrixMode(GL_MODELVIEW);      // switch to modelview matrix
    glPopMatrix();                   // restore to previous modelview matrix
}

///////////////////////////////////////////////////////////////////////////////
// display transfer rates
///////////////////////////////////////////////////////////////////////////////
void showTransferRate()
{
    static Timer timer;
    static int count = 0;
    static stringstream ss;
    double elapsedTime;

    // backup current model-view matrix
    glPushMatrix();                     // save current modelview matrix
    glLoadIdentity();                   // reset modelview matrix

    // set to 2D orthogonal projection
    glMatrixMode(GL_PROJECTION);        // switch to projection matrix
    glPushMatrix();                     // save current projection matrix
    glLoadIdentity();                   // reset projection matrix
    //gluOrtho2D(0, IMAGE_WIDTH, 0, IMAGE_HEIGHT); // set to orthogonal projection
    gluOrtho2D(0, screenWidth, 0, screenHeight); // set to orthogonal projection

    float color[4] = {1, 1, 0, 1};

    // update fps every second
    elapsedTime = timer.getElapsedTime();
    if(elapsedTime < 1.0)
    {
        ++count;
    }
    else
    {
        ss.str("");
        ss << std::fixed << std::setprecision(1);
        ss << "Transfer Rate: " << (count / elapsedTime) * DATA_SIZE / (1024 * 1024) << " MB" << ends; // update fps string
        ss << std::resetiosflags(std::ios_base::fixed | std::ios_base::floatfield);
        count = 0;                      // reset counter
        timer.start();                  // restart timer
    }
    drawString(ss.str().c_str(), 200, 286, color, font);

    // restore projection matrix
    glPopMatrix();                      // restore to previous projection matrix

    // restore modelview matrix
    glMatrixMode(GL_MODELVIEW);         // switch to modelview matrix
    glPopMatrix();                      // restore to previous modelview matrix
}

///////////////////////////////////////////////////////////////////////////////
// print transfer rates
///////////////////////////////////////////////////////////////////////////////
void printTransferRate()
{
    static const double INV_MEGA = 1.0 / (1024 * 1024);
    static Timer timer;
    static int count = 0;

    // loop until 1 sec passed
    double elapsedTime = timer.getElapsedTime();
    if (elapsedTime < 1.0) {
        ++count;
    }
    else {
        if (rateDiscarded > 0) {
            --rateDiscarded;
        }
        else {
            ++rateCount;

            double transferRate = (count / elapsedTime) * DATA_SIZE * INV_MEGA;
            transferRateSum += transferRate;
            double transferRateAvg = transferRateSum / rateCount;

            double frameRate = count / elapsedTime;
            frameRateSum += frameRate;
            double frameRateAvg = frameRateSum / rateCount;

            cout << std::fixed << std::setprecision(1);
            cout << "Transfer Rate: " << transferRate
                 << " MB/s @ " << frameRate
                 << " FPS -- Average: " << transferRateAvg
                 << " MB/s @ " << frameRateAvg << " FPS";
            cout << std::resetiosflags(std::ios_base::fixed | std::ios_base::floatfield);
            cout << endl;
        }
        count = 0;     // reset counter
        timer.start(); // restart timer
    }
}

void resetTransferRate()
{
    rateDiscarded = 3; // Discard first measurements
    rateCount = 0;
    transferRateSum = 0;
    frameRateSum = 0;
}

void* alignedMalloc(size_t alignment, size_t size)
{
    // Check that alignment is power of 2
    assert((alignment & (alignment - 1)) == 0);

#if defined(__GNUC__) && 1
    void* ptr = NULL;
    if (posix_memalign(&ptr, alignment, size)) {
        cout << "ERROR [alignedMalloc] (posix_memalign) size: " << size << " alignment: " << alignment << endl;
        return NULL;
    }
    memset(ptr, 0, size);
#elif defined(_MSC_VER) && 1
    void* ptr = NULL;
    ptr = _aligned_malloc(size, alignment);
    if (!ptr) {
        cout << "ERROR [alignedMalloc] (_aligned_malloc) size: " << size << " alignment: " << alignment << endl;
        return NULL;
    }
    memset(ptr, 0, size);
#else // Other compilers
    // Aligned memory solution adapted from here:
    // http://stackoverflow.com/questions/227897/solve-the-memory-alignment-in-c-interview-question-that-stumped-me
    size = size + (alignment - 1) + sizeof(void*);
    void* mem = malloc(size);
    if (!mem) {
        cout << "ERROR [alignedMalloc] (malloc) size: " << size << endl;
        return NULL;
    }
    memset(mem, 0, size);
    uintptr_t mask = ~(uintptr_t)(alignment - 1);
    void** ptr = (void**)(((uintptr_t)mem + (alignment - 1) + sizeof(void*)) & mask);
    ptr[-1] = mem;
#endif

    // Check that returned pointer is properly aligned
    assert(((uintptr_t)ptr & (alignment - 1)) == 0);
    return ptr;
}

void alignedFree(void* ptr)
{
#if (defined(__GNUC__) || defined(_MSC_VER)) && 1
    free(ptr);
#else // Other compilers
    free(((void**)ptr)[-1]);
#endif
}

void setPboCount(int count)
{
    if (!pboSupported)
        return;

    if (count > pboCount) {
        if (pboMethod != AMD) {
            // Generate each Pixel Buffer object and allocate memory for it
            // Hopefully, PBOs will get allocated in VRAM

            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0); // Unbind any buffer object previously bound
            for (int i = pboCount; i < count; ++i) {
                GLuint pboId;
                glGenBuffers(1, &pboId); // Generate new Buffer Object ID
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboId); // Create a zero-sized memory Pixel Buffer Object and bind it
                glBufferData(GL_PIXEL_UNPACK_BUFFER, DATA_SIZE, NULL, GL_STREAM_DRAW); // Reserve the memory space for the PBO
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0); // Release the PBO binding

                pboIds.push_back(pboId); // Update our list of PBO IDs
                pboFences.push_back(NULL);

                cout << "Created PBO buffer #" << i << " of size: " << DATA_SIZE << endl;
            }
            pboCount = pboIds.size();
            assert(GL_NO_ERROR == glGetError());
        }
        else {
            // Generate each Pixel Buffer object and allocate memory for it
            // PBOs will get allocated in System RAM, and GPU will access it through DMA

            glBindBuffer(GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD, 0); // Unbind any buffer object previously bound
            for (int i = pboCount; i < count; ++i) {
                GLuint pboId;
                glGenBuffers(1, &pboId); // Generate new Buffer Object ID
                glBindBuffer(GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD, pboId); // Create a zero-sized memory Pixel Buffer Object and bind it
                assert(GL_NO_ERROR == glGetError());

                // Memory alignment functions are compiler-specific
                GLubyte* ptAlignedBuffer = (GLubyte*)alignedMalloc(systemPageSize, DATA_SIZE);
                if (NULL == ptAlignedBuffer) {
                    cout << "ERROR [setPboCount] (alignedMalloc) size: " << DATA_SIZE << " alignment: " << systemPageSize << endl;
                    break;
                }
                cout << "Created memory buffer #" << i << " of size: " << DATA_SIZE << " alignment: " << systemPageSize << endl;

                glBufferData(GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD, DATA_SIZE, ptAlignedBuffer, GL_STREAM_DRAW); // Take control of the memory space for the PBO
                GLenum error = glGetError();
                if (GL_NO_ERROR != error) {
                    cout << "ERROR [setPboCount] (glBufferData): " << (char*)gluErrorString(error) << endl;
                    alignedFree(ptAlignedBuffer);
                    cout << "Freed memory buffer #" << i << endl;
                    break;
                }
                glBindBuffer(GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD, 0); // Release the PBO binding
                assert(GL_NO_ERROR == glGetError());

                pboIds.push_back(pboId); // Update our list of PBO IDs
                pboFences.push_back(NULL);
                alignedBuffers.push_back((GLubyte*)ptAlignedBuffer);

                cout << "Created PBO buffer #" << i << endl;
            }
            pboCount = pboIds.size();
            assert(GL_NO_ERROR == glGetError());
        }
    }
    else if (count < pboCount) {
        if (pboMethod != AMD) {
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0); // Unbind any buffer object previously bound
            for (int i = pboCount - 1; i >= count; --i) {
                glDeleteSync(pboFences.back());
                pboFences.pop_back();

                GLuint pboId = pboIds.back();
                glDeleteBuffers(1, &pboId);
                pboIds.pop_back(); // Update our list of PBO IDs

                cout << "Deleted PBO buffer #" << i << endl;
            }
            pboCount = pboIds.size();
            assert(GL_NO_ERROR == glGetError());
        }
        else {
            glBindBuffer(GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD, 0); // Unbind any buffer object previously bound
            for (int i = pboCount - 1; i >= count; --i) {
                glDeleteSync(pboFences.back());
                pboFences.pop_back();

                GLuint pboId = pboIds.back();
                glDeleteBuffers(1, &pboId);
                pboIds.pop_back(); // Update our list of PBO IDs

                cout << "Deleted PBO buffer #" << i << endl;

                alignedFree(alignedBuffers.back());
                alignedBuffers.pop_back();

                cout << "Freed memory buffer #" << i << endl;
            }
            pboCount = pboIds.size();
            assert(GL_NO_ERROR == glGetError());
        }
    }

    cout << "PBO Count: " << pboCount << endl;
}
