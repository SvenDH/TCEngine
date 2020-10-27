/*==========================================================*/
/*							GRAPHICS						*/
/*==========================================================*/

#include "tcgl.h"
#include "tclog.h"
#include "tcmath.h"

/* Constants and macros: */

#ifndef MAX_TRANSFORMSTACK
#define MAX_TRANSFORMSTACK 32
#endif

// Number of buffers inside a stream index for async index editing
#ifndef MAX_VERTICES
#define MAX_VERTICES 65535
#endif

/* Type declaration: */

typedef struct {
    mat3* currentmat;                   // Current matrix being transformed
    mat3 modelview;                     // Transformation in model space
    mat3 project;                       // Transformation to view space
    mat3 transform;
    mat3 stack[MAX_TRANSFORMSTACK];     // Transform stack (top is current matirx)
    int stackcount;                     // Top of transform stack

    rid_t defaulttex;                   // Default texture object
    rid_t defaultshader;                // Default shader program object
    rid_t currentshader;                // Currently used render program
    ivec2 rendersize;                   // Size of current FBO

    struct {
        int drawcalls;                  // Number of draw calls in this frame
        int shaderswitches;             // Number of shader switches in this frame
    } stats;
} State;


/* Forward function declarations: */



/* Global state: */
static State state = { 0 };


/* Function definitions */

void gfx_init(int width, int height) {
    //gl_init_device();
}

void gfx_close(void) {
    //gl_close();
}

void gfx_ortho(double left, double right, double bottom, double top) { 
    mat_ortho(*state.currentmat, left, right, bottom, top); 
}

