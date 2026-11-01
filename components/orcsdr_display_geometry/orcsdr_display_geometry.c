#include "orcsdr_display_geometry.h"

static bool valid(const orcsdr_display_geometry_t *g)
{
    return g && g->native_width > 0 && g->native_width <= 32768 &&
        g->native_height > 0 && g->native_height <= 32768 &&
        (g->rotation == 0 || g->rotation == 90 || g->rotation == 180 || g->rotation == 270) &&
        g->width == ((g->rotation % 180) ? g->native_height : g->native_width) &&
        g->height == ((g->rotation % 180) ? g->native_width : g->native_height);
}

bool orcsdr_geometry_init(orcsdr_display_geometry_t *out, int32_t w, int32_t h,
                         int32_t rotation, int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    if (!out || w <= 0 || h <= 0 || w > 32768 || h > 32768 ||
        (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) ||
        left < 0 || top < 0 || right < 0 || bottom < 0 ||
        (int64_t)left + right >= w || (int64_t)top + bottom >= h) return false;
    orcsdr_display_geometry_t g = {w, h, rotation, w, h, {left, top, w-left-right, h-top-bottom}};
    if (rotation % 180) { g.width = h; g.height = w; }
    switch (rotation) {
    case 90: g.safe = (orcsdr_rect_t){top, right, h-top-bottom, w-left-right}; break;
    case 180: g.safe = (orcsdr_rect_t){right, bottom, w-left-right, h-top-bottom}; break;
    case 270: g.safe = (orcsdr_rect_t){bottom, left, h-top-bottom, w-left-right}; break;
    default: break;
    }
    *out = g;
    return true;
}

bool orcsdr_view_to_native(const orcsdr_display_geometry_t *g, orcsdr_point_t p, orcsdr_point_t *out)
{
    if (!out || !valid(g) || p.x < 0 || p.y < 0 || p.x >= g->width || p.y >= g->height) return false;
    switch (g->rotation) {
    case 90: *out = (orcsdr_point_t){g->native_width-1-p.y, p.x}; break;
    case 180: *out = (orcsdr_point_t){g->native_width-1-p.x, g->native_height-1-p.y}; break;
    case 270: *out = (orcsdr_point_t){p.y, g->native_height-1-p.x}; break;
    default: *out = p; break;
    }
    return true;
}

bool orcsdr_native_to_view(const orcsdr_display_geometry_t *g, orcsdr_point_t p, orcsdr_point_t *out)
{
    if (!out || !valid(g) || p.x < 0 || p.y < 0 || p.x >= g->native_width || p.y >= g->native_height) return false;
    switch (g->rotation) {
    case 90: *out = (orcsdr_point_t){p.y, g->native_width-1-p.x}; break;
    case 180: *out = (orcsdr_point_t){g->native_width-1-p.x, g->native_height-1-p.y}; break;
    case 270: *out = (orcsdr_point_t){g->native_height-1-p.y, p.x}; break;
    default: *out = p; break;
    }
    return true;
}

bool orcsdr_view_rect_to_native(const orcsdr_display_geometry_t *g, orcsdr_rect_t r, orcsdr_rect_t *out)
{
    if (!out || !valid(g) || r.width <= 0 || r.height <= 0) return false;
    int64_t x1 = (int64_t)r.x + r.width, y1 = (int64_t)r.y + r.height;
    int32_t x0 = r.x < 0 ? 0 : r.x, y0 = r.y < 0 ? 0 : r.y;
    if (x1 > g->width) x1 = g->width;
    if (y1 > g->height) y1 = g->height;
    if (x1 <= x0 || y1 <= y0) return false;
    orcsdr_point_t a, b;
    if (!orcsdr_view_to_native(g, (orcsdr_point_t){x0,y0}, &a) ||
        !orcsdr_view_to_native(g, (orcsdr_point_t){(int32_t)x1-1,(int32_t)y1-1}, &b)) return false;
    int32_t min_x = a.x < b.x ? a.x : b.x, min_y = a.y < b.y ? a.y : b.y;
    int32_t max_x = a.x > b.x ? a.x : b.x, max_y = a.y > b.y ? a.y : b.y;
    *out = (orcsdr_rect_t){min_x,min_y,max_x-min_x+1,max_y-min_y+1};
    return true;
}

bool orcsdr_touch_to_view(const orcsdr_display_geometry_t *g, const orcsdr_touch_calibration_t *c,
                          orcsdr_point_t p, orcsdr_point_t *out)
{
    if (!out || !valid(g) || !c || c->x_max <= c->x_min || c->y_max <= c->y_min ||
        p.x < c->x_min || p.x > c->x_max || p.y < c->y_min || p.y > c->y_max) return false;
    /* int64 arithmetic also handles negative raw origins without overflow. */
    int64_t x = (int64_t)p.x-c->x_min, y = (int64_t)p.y-c->y_min;
    int64_t dx = (int64_t)c->x_max-c->x_min, dy = (int64_t)c->y_max-c->y_min;
    if (c->swap_axes) {
        int64_t t=x; x=y; y=t; t=dx; dx=dy; dy=t;
    }
    int32_t nx = (int32_t)(x*(g->native_width-1)/dx);
    int32_t ny = (int32_t)(y*(g->native_height-1)/dy);
    if (c->mirror_x) nx = g->native_width-1-nx;
    if (c->mirror_y) ny = g->native_height-1-ny;
    return orcsdr_native_to_view(g,(orcsdr_point_t){nx,ny},out);
}
