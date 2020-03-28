#include "Overview.h"

#include "Config.h"
#include "Util.h"

#include <SDL2/SDL.h>
#include <stdbool.h>

Overview Overview_Make(const int32_t xres, const int32_t yres)
{
    static Overview zero;
    Overview overview = zero;
    overview.xres = xres;
    overview.yres = yres;
    return overview;
}

static Overview UpdateMouse(Overview overview, const Input input)
{
    overview.mouse_cursor = input.cursor;
    overview.event.mouse_l = input.l;
    overview.event.mouse_r = input.r;
    overview.event.mouse_ld = input.ld;
    overview.event.mouse_lu = input.lu;
    overview.event.mouse_rd = input.rd;
    overview.event.mouse_ru = input.ru;
    return overview;
}

static Overview UpdateKeys(Overview overview, const Input input)
{
    overview.event.key_q = input.key[SDL_SCANCODE_Q];
    overview.event.key_w = input.key[SDL_SCANCODE_W];
    overview.event.key_e = input.key[SDL_SCANCODE_E];
    overview.event.key_r = input.key[SDL_SCANCODE_R];
    overview.event.key_t = input.key[SDL_SCANCODE_T];
    overview.event.key_a = input.key[SDL_SCANCODE_A];
    overview.event.key_s = input.key[SDL_SCANCODE_S];
    overview.event.key_d = input.key[SDL_SCANCODE_D];
    overview.event.key_f = input.key[SDL_SCANCODE_F];
    overview.event.key_g = input.key[SDL_SCANCODE_G];
    overview.event.key_z = input.key[SDL_SCANCODE_Z];
    overview.event.key_x = input.key[SDL_SCANCODE_X];
    overview.event.key_c = input.key[SDL_SCANCODE_C];
    overview.event.key_v = input.key[SDL_SCANCODE_V];
    overview.event.key_b = input.key[SDL_SCANCODE_B];
    overview.event.key_1 = input.key[SDL_SCANCODE_1];
    overview.event.key_2 = input.key[SDL_SCANCODE_2];
    overview.event.key_3 = input.key[SDL_SCANCODE_3];
    overview.event.key_left_shift = input.key[SDL_SCANCODE_LSHIFT];
    overview.event.key_left_alt   = input.key[SDL_SCANCODE_LALT];
    overview.event.tab = input.key[SDL_SCANCODE_TAB];
    return overview;
}

static Overview UpdateSelectionBox(Overview overview)
{
    if(overview.event.mouse_ld)
        overview.selection_box.a = overview.mouse_cursor;
    overview.selection_box.b = overview.mouse_cursor;
    return overview;
}

static Overview UpdatePan(Overview overview)
{
    if(!overview.event.key_left_alt)
    {
        if(overview.event.key_w) overview.pan.y -= CONFIG_OVERVIEW_SCROLL_SPEED;
        if(overview.event.key_s) overview.pan.y += CONFIG_OVERVIEW_SCROLL_SPEED;
        if(overview.event.key_d) overview.pan.x += CONFIG_OVERVIEW_SCROLL_SPEED;
        if(overview.event.key_a) overview.pan.x -= CONFIG_OVERVIEW_SCROLL_SPEED;
    }
    return overview;
}

Overview Overview_Update(Overview overview, const Input input, const uint64_t parity, const int32_t cycles, const int32_t queue_size, const Share share, const int32_t ping)
{
    overview = UpdateMouse(overview, input);
    overview = UpdateKeys(overview, input);
    overview = UpdatePan(overview);
    overview = UpdateSelectionBox(overview);
    overview.parity = parity;
    overview.cycles = cycles;
    overview.queue_size = queue_size;
    overview.share = share;
    overview.ping = ping == -1 ? overview.ping : ping;
    return overview;
}

bool Overview_IsSelectionBoxBigEnough(const Overview overview)
{
    return Rect_GetArea(overview.selection_box) > CONFIG_OVERVIEW_SELECTION_BOX_MIN_SIZE;
}

/*      +
 *     /D\
 *    /C H\      +--------+
 *   /B G L\     |A B C D |
 *  +A F K P+ -> |E F G H |
 *   \E J O/     |I J K L |
 *    \I N/      |M N O P |
 *     \M/       +--------+
 *      +
 */
Point Overview_IsoToCart(const Overview overview, const Grid grid, const Point iso, const bool raw)
{
    const int32_t x = +iso.x - overview.xres / 2;
    const int32_t y = -iso.y + overview.yres / 2;
    const int32_t xx = x + overview.pan.x;
    const int32_t yy = y - overview.pan.y;
    const int32_t w = grid.tile_iso_width - 1;
    const int32_t h = grid.tile_iso_height - 1;
    const int32_t rx = (xx * h + yy * w);
    const int32_t ry = (yy * w - xx * h);
    const int32_t cx = (+2 * rx + w * h * grid.size) / (2 * w);
    const int32_t cy = (-2 * ry + w * h * grid.size) / (4 * h);
    const Point cart_raw = { cx, cy };
    const Point cart = {
        1 * cx / h,
        2 * cy / w,
    };
    return raw ? cart_raw : cart;
}

/*                     +
 *                    /D\
 *  +--------+       /C H\
 *  |A B C D |      /B G L\
 *  |E F G H | --> +A F K P+
 *  |I J K L |      \E J O/
 *  |M N O P |       \I N/
 *  +--------+        \M/
 *                     +
 */
Point Overview_CartToIso(const Overview overview, const Grid grid, const Point cart)
{
    const int32_t w = grid.tile_iso_width - 1;
    const int32_t h = grid.tile_iso_height - 1;
    const int32_t xx = (cart.y + cart.x) * (w / 2);
    const int32_t yy = (cart.y - cart.x) * (h / 2);
    const int32_t mx = xx + overview.xres / 2;
    const int32_t my = yy + overview.yres / 2;
    const int32_t cx = mx - (w / 2) * grid.size;
    const int32_t cy = my - (h / 2);
    const Point iso = {
        cx - overview.pan.x,
        cy - overview.pan.y,
    };
    return iso;
}

Quad Overview_GetRenderBox(const Overview overview, const Grid grid, const int32_t border)
{
    const Point p0 = { border, border};
    const Point p1 = { overview.xres - border, border};
    const Point p2 = { border, overview.yres - border };
    const Point p3 = { overview.xres - border, overview.yres - border};
    const Point a = Overview_IsoToCart(overview, grid, p0, false);
    const Point b = Overview_IsoToCart(overview, grid, p1, false);
    const Point c = Overview_IsoToCart(overview, grid, p2, false);
    const Point d = Overview_IsoToCart(overview, grid, p3, false);
    const Quad quad = { a, b, c, d};
    return quad;
}

Point Overview_IsoSnapTo(const Overview overview, const Grid grid, const Point iso)
{
    const Point cart = Overview_IsoToCart(overview, grid, iso, false);
    const Point snap = Overview_CartToIso(overview, grid, cart);
    return snap;
}

bool Overview_UsedAction(const Overview overview)
{
    return overview.event.mouse_lu || overview.event.mouse_ru;
}

bool Overview_IsSpectator(const Overview overview)
{
    return overview.color == overview.spectator;
}
