// curtkr.c: Tracks mouse, draws visual trail (red on click), prints coords.
// Compile with: gcc curtkr.c -o curtkr -lX11 -lXfixes -lXext -lcairo -lm

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>     // For usleep
#include <signal.h>     // For signal handling (Ctrl+C)
#include <string.h>     // For memset
#include <math.h>       // For fade calculation (optional)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h> // Needed for ShapeInput

#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>

// --- Configuration ---
#define TRAIL_LENGTH 50      // Number of points in the trail
#define TRAIL_RADIUS 3.0     // Radius of the circles in the trail
#define UPDATE_INTERVAL 16666 // Microseconds (16666 approx = 60 FPS)
// Trail Color (Red, Green, Blue - values 0.0 to 1.0)
#define TRAIL_R 0.2
#define TRAIL_G 0.5
#define TRAIL_B 1.0
// Click Color (Red)
#define CLICK_R 1.0
#define CLICK_G 0.0
#define CLICK_B 0.0
// --- End Configuration ---

// Structure to hold a point
typedef struct {
    int x;
    int y;
    int valid;   // Flag to indicate if this point contains actual data
    int clicked; // <<<< NEW: Flag to indicate if mouse button was pressed
} TrailPoint;

// Global array for trail points (circular buffer)
TrailPoint trail_points[TRAIL_LENGTH];
int trail_head = 0; // Index of the next spot to write to

// Global flag to control the main loop
volatile sig_atomic_t keep_running = 1;

// Signal handler for SIGINT (Ctrl+C)
void handle_sigint(int sig) {
    printf("\nCaught signal %d. Exiting gracefully...\n", sig);
    keep_running = 0;
}

// Function to draw the trail onto the Cairo surface
void draw_trail(cairo_t *cr, int width, int height) {
    // 1. Clear the entire surface to fully transparent
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    // 2. Draw the trail points
    for (int i = 0; i < TRAIL_LENGTH; ++i) {
        int current_index = (trail_head - 1 - i + TRAIL_LENGTH) % TRAIL_LENGTH;

        if (!trail_points[current_index].valid) {
            continue;
        }

        double alpha = 1.0 - ((double)i / TRAIL_LENGTH);
        if (alpha < 0.05) continue;

        // <<<< MODIFIED: Set color based on 'clicked' flag >>>>
        if (trail_points[current_index].clicked) {
            // Clicked point: Use Red (adjust alpha slightly if desired)
            cairo_set_source_rgba(cr, CLICK_R, CLICK_G, CLICK_B, alpha * 0.9);
        } else {
            // Normal point: Use configured trail color
            cairo_set_source_rgba(cr, TRAIL_R, TRAIL_G, TRAIL_B, alpha * 0.8);
        }
        // <<<< END MODIFIED >>>>

        // Draw a circle at the point
        cairo_arc(cr,
                  trail_points[current_index].x,
                  trail_points[current_index].y,
                  TRAIL_RADIUS, 0, 2 * M_PI);
        cairo_fill(cr);
    }
}


int main() {
    Display *display;
    Window root_window;
    Window overlay_window;
    int screen;
    int width, height;
    XSetWindowAttributes attrs;
    Visual *visual;
    int depth;
    Colormap colormap;

    // Variables for XQueryPointer
    Window root_return, child_return;
    int root_x_return, root_y_return;
    int win_x_return, win_y_return;
    unsigned int mask_return; // <<<< This holds the button state >>>>

    // Cairo variables
    cairo_surface_t *cairo_surface = NULL;
    cairo_t *cr = NULL;

    // --- Initialize Trail Buffer ---
    // Note: memset also correctly initializes the new 'clicked' flag to 0
    memset(trail_points, 0, sizeof(trail_points));

    // --- Setup Signal Handler ---
    signal(SIGINT, handle_sigint);

    // --- Connect to the X Server ---
    display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Error: Could not open X display\n");
        return 1;
    }

    screen = DefaultScreen(display);
    root_window = RootWindow(display, screen);
    width = DisplayWidth(display, screen);
    height = DisplayHeight(display, screen);

    // --- Find a 32-bit visual ---
    XVisualInfo vinfo_template;
    vinfo_template.screen = screen; vinfo_template.depth = 32; vinfo_template.class = TrueColor;
    int nitems;
    XVisualInfo *vinfo_list = XGetVisualInfo(display, VisualScreenMask | VisualDepthMask | VisualClassMask, &vinfo_template, &nitems);
    if (!vinfo_list || nitems == 0) { /* ... error handling ... */
        fprintf(stderr, "Error: No 32-bit TrueColor visual found. Is a compositor running?\n");
        XCloseDisplay(display); return 1;
    }
    visual = vinfo_list[0].visual; depth = vinfo_list[0].depth;

    // --- Create Colormap ---
    colormap = XCreateColormap(display, root_window, visual, AllocNone);

    // --- Set Window Attributes ---
    attrs.override_redirect = True; attrs.colormap = colormap;
    attrs.background_pixel = 0; attrs.border_pixel = 0;
    unsigned long valuemask = CWOverrideRedirect | CWColormap | CWBackPixel | CWBorderPixel;

    // --- Create Overlay Window ---
    overlay_window = XCreateWindow(display, root_window, 0, 0, width, height, 0,
                                   depth, InputOutput, visual, valuemask, &attrs);

    // --- Set EWMH Properties ---
    Atom wm_state = XInternAtom(display, "_NET_WM_STATE", False);
    Atom wm_state_above = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
    if (wm_state != None && wm_state_above != None) {
        XChangeProperty(display, overlay_window, wm_state, XA_ATOM, 32, PropModeReplace, (unsigned char *)&wm_state_above, 1);
    } // else { fprintf(stderr, "Warning: Could not set _NET_WM_STATE_ABOVE.\n"); }
    Atom wm_window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom wm_window_type_dock = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DOCK", False);
    if (wm_window_type != None && wm_window_type_dock != None) {
         XChangeProperty(display, overlay_window, wm_window_type, XA_ATOM, 32, PropModeReplace, (unsigned char *)&wm_window_type_dock, 1);
    }

    // --- Enable Click-Through ---
    int fix_event_base, fix_error_base;
    if (XFixesQueryExtension(display, &fix_event_base, &fix_error_base)) {
        XserverRegion region = XFixesCreateRegion(display, NULL, 0);
        XFixesSetWindowShapeRegion(display, overlay_window, ShapeInput, 0, 0, region);
        XFixesDestroyRegion(display, region);
    } else {
        fprintf(stderr, "Warning: XFixes extension not available. Overlay will not be click-through.\n");
    }

    // --- Map Window & Flush ---
    XMapWindow(display, overlay_window);
    XFlush(display);

    // --- Setup Cairo ---
    cairo_surface = cairo_xlib_surface_create(display, overlay_window, visual, width, height);
    if (!cairo_surface || cairo_surface_status(cairo_surface) != CAIRO_STATUS_SUCCESS) { /* ... error handling ... */
         fprintf(stderr, "Error creating Cairo surface: %s\n", cairo_status_to_string(cairo_surface_status(cairo_surface)));
         XFree(vinfo_list); XDestroyWindow(display, overlay_window); XFreeColormap(display, colormap); XCloseDisplay(display); return 1;
    }
    cr = cairo_create(cairo_surface);
    if (!cr || cairo_status(cr) != CAIRO_STATUS_SUCCESS) { /* ... error handling ... */
        fprintf(stderr, "Error creating Cairo context: %s\n", cairo_status_to_string(cairo_status(cr)));
        if (cairo_surface) cairo_surface_destroy(cairo_surface);
        XFree(vinfo_list); XDestroyWindow(display, overlay_window); XFreeColormap(display, colormap); XCloseDisplay(display); return 1;
    }

    // --- Free Visual Info ---
    XFree(vinfo_list); vinfo_list = NULL;

    printf("Mouse trail overlay started. Press Ctrl+C to exit.\n");
    printf("\rMouse Coordinates: X=     Y=     "); fflush(stdout);

    // --- Main Loop ---
    while (keep_running) {
        // 1. Get current mouse position AND button state
        Bool result = XQueryPointer(display, root_window,
                                    &root_return, &child_return,
                                    &root_x_return, &root_y_return,
                                    &win_x_return, &win_y_return,
                                    &mask_return); // Contains button state

        if (result) {
            // Print coordinates
            printf("\rMouse Coordinates: X=%-5d Y=%-5d", root_x_return, root_y_return);
            fflush(stdout);

            // 2. Store the new point in the circular buffer
            trail_points[trail_head].x = root_x_return;
            trail_points[trail_head].y = root_y_return;
            trail_points[trail_head].valid = 1;

            // <<<< NEW: Check button state and set clicked flag >>>>
            // Check if any of Button1Mask to Button5Mask are set in mask_return
            if (mask_return & (Button1Mask | Button2Mask | Button3Mask | Button4Mask | Button5Mask)) {
                trail_points[trail_head].clicked = 1; // A button was pressed
            } else {
                trail_points[trail_head].clicked = 0; // No button was pressed
            }
            // <<<< END NEW >>>>

            trail_head = (trail_head + 1) % TRAIL_LENGTH; // Move head

            // 3. Draw the entire trail onto the Cairo surface
            draw_trail(cr, width, height);

            // 4. Flush drawing to the screen
            cairo_surface_flush(cairo_surface);

        } else {
            // Handle query failure
             printf("\rMouse Coordinates: Query Failed!   "); fflush(stdout);
             fprintf(stderr, "\nWarning: XQueryPointer failed.\n");
             usleep(100000); // Sleep longer
        }

        // 5. Pause briefly
        usleep(UPDATE_INTERVAL);

    } // End main loop

    // --- Cleanup ---
    printf("\nCleaning up resources...\n");
    if (cr) cairo_destroy(cr);
    if (cairo_surface) cairo_surface_destroy(cairo_surface);
    XFreeColormap(display, colormap);
    XUnmapWindow(display, overlay_window);
    XDestroyWindow(display, overlay_window);
    XCloseDisplay(display);

    printf("Exiting.\n");
    return 0;
}
