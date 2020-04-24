#include "Vram.h"

#include "Surface.h"
#include "Channels.h"
#include "Lines.h"
#include "Config.h"
#include "Util.h"
#include "Rect.h"
#include "Quad.h"
#include "Interfac.h"
#include "Buttons.h"

static inline Rects GetChannelRects(const int32_t xres, const int32_t yres, const int32_t cpu_count)
{
    Rects rects = Rects_Make(cpu_count);
    const int32_t width = xres / rects.count;
    const int32_t remainder = xres % rects.count;
    for(int32_t i = 0; i < rects.count; i++)
    {
        Rect rect = {
            { (i + 0) * width,    0 },
            { (i + 1) * width, yres },
        };
        if(i == rects.count - 1)
            rect.b.x += remainder;
        rects.rect[i] = rect;
    }
    return rects;
}

Vram Vram_Lock(SDL_Texture* const texture, const int32_t xres, const int32_t yres, const int32_t cpu_count)
{
    void* raw;
    int32_t pitch;
    SDL_LockTexture(texture, NULL, &raw, &pitch);
    static Vram zero;
    Vram vram = zero;
    vram.pixels = (uint32_t*) raw;
    vram.width = (int32_t) (pitch / sizeof(*vram.pixels));
    vram.xres = xres;
    vram.yres = yres;
    vram.cpu_count = cpu_count;
    vram.channel_rects = GetChannelRects(xres, yres, cpu_count);
    return vram;
}

void Vram_Unlock(SDL_Texture* const texture)
{
    SDL_UnlockTexture(texture);
}

static inline void Put(const Vram vram, const int32_t x, const int32_t y, const uint32_t pixel)
{
    vram.pixels[x + y * vram.width] = pixel;
}

static inline uint32_t Get(const Vram vram, const int32_t x, const int32_t y)
{
    return vram.pixels[x + y * vram.width];
}

void Vram_DrawCross(const Vram vram, const Point point, const int32_t len, const uint32_t color)
{
    for(int32_t x = point.x - len; x <= point.x + len; x++) Put(vram, x, point.y, color);
    for(int32_t y = point.y - len; y <= point.y + len; y++) Put(vram, point.x, y, color);
}

void Vram_Clear(const Vram vram, const uint32_t color)
{
    for(int32_t y = 0; y < vram.yres; y++)
    for(int32_t x = 0; x < vram.xres; x++)
        Put(vram, x, y, color);
}

static inline bool OutOfBounds(const Vram vram, const int32_t x, const int32_t y)
{
    return x < 0 || y < 0 || x >= vram.xres || y >= vram.yres;
}

// SEE: HTTPS://GIST.GITHUB.COM/XPROGER/96253E93BACCFBF338DE
static inline uint32_t Blend(const uint32_t bot_pixel, const uint32_t top_pixel, const uint8_t alpha)
{
    uint32_t rb = top_pixel & SURFACE_RB_MASK;
    uint32_t g  = top_pixel & SURFACE_G_MASK;
    rb += ((bot_pixel & SURFACE_RB_MASK) - rb) * alpha >> 8;
    g  += ((bot_pixel & SURFACE_G_MASK) - g) * alpha >> 8;
    return (rb & SURFACE_RB_MASK) | (g & SURFACE_G_MASK);
}

static inline void TransferTilePixel(const Vram vram, const Tile tile, Point coords, const int32_t x, const int32_t y)
{
    const uint32_t vram_pixel = Get(vram, coords.x, coords.y);
    const uint32_t height = vram_pixel >> SURFACE_A_SHIFT;
    if(tile.height > height)
    {
        uint32_t surface_pixel = Surface_GetPixel(tile.surface, x, y);
        if(surface_pixel != SURFACE_COLOR_KEY)
        {
            if(height == FILE_PRIO_BUILDING
            || height == FILE_PRIO_SHADOW
            || tile.is_floating)
                surface_pixel = Blend(surface_pixel, vram_pixel, 0xFF / 2);
            const uint32_t pixel = (tile.height << SURFACE_A_SHIFT) | surface_pixel;
            Put(vram, coords.x, coords.y, pixel);
        }
    }
}

static inline void DrawTileNoClip(const Vram vram, const Tile tile)
{
    for(int32_t y = 0; y < tile.frame.height; y++)
    for(int32_t x = 0; x < tile.frame.width; x++)
    {
        const Point coords = Tile_GetTopLeftOffsetCoords(tile, x, y);
        TransferTilePixel(vram, tile, coords, x, y);
    }
}

static inline void DrawTileClip(const Vram vram, const Tile tile)
{
    for(int32_t y = 0; y < tile.frame.height; y++)
    for(int32_t x = 0; x < tile.frame.width; x++)
    {
        const Point coords = Tile_GetTopLeftOffsetCoords(tile, x, y);
        if(Rect_ContainsPoint(tile.bound, coords))
            TransferTilePixel(vram, tile, coords, x, y);
    }
}

void Vram_DrawTile(const Vram vram, const Tile tile)
{
    if(!tile.totally_offscreen)
        tile.needs_clipping
            ? DrawTileClip(vram, tile)
            : DrawTileNoClip(vram, tile);
}

typedef struct
{
    Vram vram;
    Tiles tiles;
    int32_t a;
    int32_t b;
}
BatchNeedle;

static inline int32_t DrawBatchNeedle(void* data)
{
    BatchNeedle* needle = (BatchNeedle*) data;
    for(int32_t i = needle->a; i < needle->b; i++)
        Vram_DrawTile(needle->vram, needle->tiles.tile[i]);
    return 0;
}

static inline void RenderTerrainTiles(const Vram vram, const Tiles terrain_tiles)
{
    BatchNeedle* const needles = UTIL_ALLOC(BatchNeedle, vram.cpu_count);
    const int32_t width = terrain_tiles.count / vram.cpu_count;
    const int32_t remainder = terrain_tiles.count % vram.cpu_count;
    for(int32_t i = 0; i < vram.cpu_count; i++)
    {
        needles[i].vram = vram;
        needles[i].tiles = terrain_tiles;
        needles[i].a = (i + 0) * width;
        needles[i].b = (i + 1) * width;
    }
    needles[vram.cpu_count - 1].b += remainder;
    SDL_Thread** const threads = UTIL_ALLOC(SDL_Thread*, vram.cpu_count);
    for(int32_t i = 0; i < vram.cpu_count; i++) threads[i] = SDL_CreateThread(DrawBatchNeedle, "N/A", &needles[i]);
    for(int32_t i = 0; i < vram.cpu_count; i++) SDL_WaitThread(threads[i], NULL);
    free(needles);
    free(threads);
}

static inline uint32_t BlendMaskWithBuffer(const Vram vram, const int32_t xx, const int32_t yy, SDL_Surface* const mask, const int32_t x, const int32_t y, const uint32_t top_pixel)
{
    const uint32_t mask_pixel = Surface_GetPixel(mask, x, y);
    const uint32_t bot_pixel = Get(vram, xx, yy);
    const uint32_t blend_pixel = Blend(bot_pixel, top_pixel, mask_pixel);
    return blend_pixel;
}

static inline void BlendTilePixel(const Vram vram, const Tile tile, const Point coords, SDL_Surface* const mask, const int32_t x, const int32_t y)
{
    const uint32_t height = Get(vram, coords.x, coords.y) >> SURFACE_A_SHIFT;
    if(tile.height >= height) // NOTE: GREATER THAN OR EQUAL TO SO THAT TERRAIN TILES CAN BLEND.
    {
        const uint32_t top_pixel = Surface_GetPixel(tile.surface, x, y);
        if(top_pixel != SURFACE_COLOR_KEY)
        {
            const uint32_t blend_pixel = BlendMaskWithBuffer(vram, coords.x, coords.y, mask, x, y, top_pixel);
            const uint32_t pixel = blend_pixel | (tile.height << SURFACE_A_SHIFT);
            Put(vram, coords.x, coords.y, pixel);
        }
    }
}

static inline void DrawTileMaskClip(const Vram vram, const Tile tile, SDL_Surface* const mask)
{
    for(int32_t y = 0; y < tile.frame.height; y++)
    for(int32_t x = 0; x < tile.frame.width; x++)
    {
        const Point coords = Tile_GetTopLeftOffsetCoords(tile, x, y);
        if(Rect_ContainsPoint(tile.bound, coords))
            BlendTilePixel(vram, tile, coords, mask, x, y);
    }
}

static inline void DrawTileMaskNoClip(const Vram vram, const Tile tile, SDL_Surface* const mask)
{
    for(int32_t y = 0; y < tile.frame.height; y++)
    for(int32_t x = 0; x < tile.frame.width; x++)
    {
        const Point coords = Tile_GetTopLeftOffsetCoords(tile, x, y);
        BlendTilePixel(vram, tile, coords, mask, x, y);
    }
}

static inline void DrawTileMask(const Vram vram, const Tile tile, SDL_Surface* const mask)
{
    tile.needs_clipping
        ? DrawTileMaskClip(vram, tile, mask)
        : DrawTileMaskNoClip(vram, tile, mask);
}

static inline void DrawBlendLine(const Vram vram, const Line line, const Registrar terrain, const Map map, const Overview overview, const Grid grid, const Blendomatic blendomatic)
{
    const Point inner = line.inner;
    const Point outer = line.outer;
    const Terrain file = Map_GetTerrainFile(map, inner);
    // THE OUTER TILE USES THE OUTER TILE ANIMATION,
    // BUT WITH THE INNER FILE SO THAT THE CORRECT SURFACE CAN BE LOOKED UP.
    const Animation outer_animation = terrain.animation[COLOR_GAIA][file];
    const Tile outer_tile = Tile_GetTerrain(overview, grid, outer, outer_animation, file);
    const int32_t index = (file == FILE_TERRAIN_FARM_READY) ? 3 : 0; // XXX: NEEDS A TERRAIN HARD CORNER BLEND BOOLEAN
    const Mode blend_mode = blendomatic.mode[index];
    const int32_t blend_id = Mode_GetBlendIndex(inner, outer);
    DrawTileMask(vram, outer_tile, blend_mode.mask_real[blend_id]);
}

typedef struct
{
    Vram vram;
    Lines lines;
    Registrar terrain;
    Map map;
    Overview overview;
    Blendomatic blendomatic;
    Grid grid;
    int32_t a;
    int32_t b;
}
BlendNeedle;

static inline int32_t DrawBlendNeedle(void* const data)
{
    BlendNeedle* const needle = (BlendNeedle*) data;
    for(int32_t i = needle->a; i < needle->b; i++)
    {
        const Line line = needle->lines.line[i];
        DrawBlendLine(needle->vram, line, needle->terrain, needle->map, needle->overview, needle->grid, needle->blendomatic);
    }
    return 0;
}

static inline int32_t GetNextBestBlendTile(const Lines lines, const int32_t slice, const int32_t slices)
{
    if(slice == 0)
        return 0;
    const int32_t width = (slice * lines.count) / slices;
    int32_t index = width;
    while(index < lines.count)
    {
        // SINCE LINES ARE SORTED BY OUTER TILES, AN OUTER TILE MAY GET SHARED
        // ACROSS THREADS. TO COUNTERACT THAT, ADVANCE THE INDEX OF THE END OF THE SLICE
        // TO A NEW OUTER BLEND TILE.
        const Point prev = lines.line[index - 1].outer;
        const Point curr = lines.line[index - 0].outer;
        if(Point_Equal(prev, curr))
        {
            index++;
            continue;
        }
        else break;
    }
    return index;
}

static inline void BlendTerrainTiles(const Vram vram, const Registrar terrain, const Map map, const Overview overview, const Grid grid, const Lines blend_lines, const Blendomatic blendomatic)
{
    BlendNeedle* const needles = UTIL_ALLOC(BlendNeedle, vram.cpu_count);
    for(int32_t i = 0; i < vram.cpu_count; i++)
    {
        needles[i].vram = vram;
        needles[i].lines = blend_lines;
        needles[i].terrain = terrain;
        needles[i].map = map;
        needles[i].overview = overview;
        needles[i].blendomatic = blendomatic;
        needles[i].grid = grid;
        needles[i].a = GetNextBestBlendTile(blend_lines, i + 0, vram.cpu_count);
        needles[i].b = GetNextBestBlendTile(blend_lines, i + 1, vram.cpu_count);
    }
    SDL_Thread** const threads = UTIL_ALLOC(SDL_Thread*, vram.cpu_count);
    for(int32_t i = 0; i < vram.cpu_count; i++) threads[i] = SDL_CreateThread(DrawBlendNeedle, "N/A", &needles[i]);
    for(int32_t i = 0; i < vram.cpu_count; i++) SDL_WaitThread(threads[i], NULL);
    free(needles);
    free(threads);
}

void Vram_DrawMap(const Vram vram, const Registrar terrain, const Map map, const Overview overview, const Grid grid, const Blendomatic blendomatic, const Lines blend_lines, const Tiles terrain_tiles)
{
    RenderTerrainTiles(vram, terrain_tiles);
    if(!overview.event.key_left_shift)
        BlendTerrainTiles(vram, terrain, map, overview, grid, blend_lines, blendomatic);
}

typedef struct
{
    Vram vram;
    Tiles tiles;
}
ChannelNeedle;

static inline int32_t DrawChannelNeedle(void* data)
{
    ChannelNeedle* const needle = (ChannelNeedle*) data;
    for(int32_t i = 0; i < needle->tiles.count; i++)
        Vram_DrawTile(needle->vram, needle->tiles.tile[i]);
    return 0;
}

void Vram_DrawUnits(const Vram vram, const Tiles tiles)
{
    const Channels channels = Channels_Make(tiles, vram);
    ChannelNeedle* const needles = UTIL_ALLOC(ChannelNeedle, channels.count);
    for(int32_t i = 0; i < channels.count; i++)
    {
        needles[i].vram = vram;
        needles[i].tiles = channels.tiles[i];
    }
    SDL_Thread** const threads = UTIL_ALLOC(SDL_Thread*, channels.count);
    for(int32_t i = 0; i < channels.count; i++) threads[i] = SDL_CreateThread(DrawChannelNeedle, "N/A", &needles[i]);
    for(int32_t i = 0; i < channels.count; i++) SDL_WaitThread(threads[i], NULL);
    free(needles);
    free(threads);
    Channels_Free(channels);
}

static inline void DrawSelectionPixel(const Vram vram, const Point point, const uint32_t color)
{
    if(!OutOfBounds(vram, point.x, point.y))
        if((Get(vram, point.x, point.y) >> SURFACE_A_SHIFT) < FILE_PRIO_DECAY)
            Put(vram, point.x, point.y, color);
}

// SEE: HTTPS://GIST.GITHUB.COM/BERT/1085538
static inline void DrawEllipse(const Vram vram, Rect rect, const uint32_t color)
{
    int32_t a = abs(rect.b.x - rect.a.x);
    int32_t b = abs(rect.b.y - rect.a.y);
    int32_t c = b & 1;
    int32_t dx = 4 * (1 - a) * b * b;
    int32_t dy = 4 * (1 + c) * a * a;
    int32_t err = dx + dy + c * a * a;
    if(rect.a.x > rect.b.x)
    {
        rect.a.x = rect.b.x;
        rect.b.x += a;
    }
    if(rect.a.y > rect.b.y)
        rect.a.y = rect.b.y;
    rect.a.y += (b + 1) / 2;
    rect.b.y = rect.a.y - c;
    a *= 8 * a;
    c = 8 * b * b;
    do
    {
        const Point point[] = {
            { rect.b.x, rect.a.y },
            { rect.a.x, rect.a.y },
            { rect.a.x, rect.b.y },
            { rect.b.x, rect.b.y },
        };
        for(int32_t i = 0; i < UTIL_LEN(point); i++)
            DrawSelectionPixel(vram, point[i], color);
        const int32_t e2 = 2 * err;
        if(e2 >= dx)
        {
            rect.a.x++;
            rect.b.x--;
            err += dx += c;
        }
        if(e2 <= dy)
        {
            rect.a.y++;
            rect.b.y--;
            err += dy += a;
        }
    }
    while(rect.a.x <= rect.b.x);
    while(rect.a.y - rect.b.y < b)
    {
        const Point point[] = {
            { rect.a.x - 1, rect.a.y },
            { rect.b.x + 1, rect.a.y },
            { rect.a.x - 1, rect.b.y },
            { rect.b.x + 1, rect.b.y },
        };
        for(int32_t i = 0; i < UTIL_LEN(point); i++)
            DrawSelectionPixel(vram, point[i], color);
        rect.a.y++;
        rect.b.y--;
    }
}

void Vram_DrawSelectionBox(const Vram vram, const Overview overview, const uint32_t color, const bool enabled)
{
    if(enabled && Overview_IsSelectionBoxBigEnough(overview))
    {
        const Rect box = Rect_CorrectOrientation(overview.selection_box);
        for(int32_t x = box.a.x; x < box.b.x; x++) Put(vram, x, box.a.y, color);
        for(int32_t x = box.a.x; x < box.b.x; x++) Put(vram, x, box.b.y, color);
        for(int32_t y = box.a.y; y < box.b.y; y++) Put(vram, box.a.x, y, color);
        for(int32_t y = box.a.y; y < box.b.y; y++) Put(vram, box.b.x, y, color);
    }
}

void Vram_DrawUnitSelections(const Vram vram, const Tiles tiles, const Color color)
{
    for(int32_t i = 0; i < tiles.count; i++)
    {
        const Tile tile = tiles.tile[i];
        const Point center = Tile_GetHotSpotCoords(tile);
        const Rect rect = Rect_GetEllipse(center, tile.reference->trait.width / CONFIG_GRID_CELL_SIZE);
        if(!Unit_IsExempt(tile.reference))
        {
            if(Unit_IsSelectedByColor(tile.reference, color))
            {
                if(tile.reference->is_engaged_in_melee)
                    DrawEllipse(vram, rect, 0xFF0000);
                else
                if(tile.reference->using_attack_move)
                    DrawEllipse(vram, rect, 0xFFFF00);
                else
                if(tile.reference->has_direct)
                    DrawEllipse(vram, rect, 0x00FF00);
                else
                    DrawEllipse(vram, rect, 0xFFFFFF);
            }
            else
            if(tile.reference->has_parent_lock)
                DrawEllipse(vram, rect, 0xFFFFFF);
        }
    }
}

void Vram_FlashUnits(const Vram vram, const Tiles tiles)
{
    for(int32_t i = 0; i < tiles.count; i++)
    {
        const Tile tile = tiles.tile[i];
        const Point center = Tile_GetHotSpotCoords(tile);
        const Rect rect = Rect_GetEllipse(center, tile.reference->trait.width / CONFIG_GRID_CELL_SIZE);
        if(!Unit_IsExempt(tile.reference)
        && tile.reference->trait.is_inanimate == false
        && tile.reference->grid_flash_timer < CONFIG_VRAM_FLASH_TIMER_MAX
        && tile.reference->is_flash_on)
            DrawEllipse(vram, rect, 0xFFFFFF);
    }
}

static inline void DrawHealthBar(const Vram vram, const Point top, Unit* const unit)
{
    const int32_t w = unit->trait.is_inanimate ? 90 : 30;
    const int32_t h = 2;
    const int32_t hh =  h / 2;
    const int32_t hw =  w / 2;
    const int32_t max_health = unit->trait.max_health;
    const int32_t percent = (w * unit->health) / (max_health == 0 ? 1 : max_health);
    const int32_t y0 = top.y - hh;
    const int32_t y1 = top.y + hh;
    const int32_t x0 = top.x - hw;
    const int32_t x1 = top.x + hw;
    for(int32_t y = y0; y < y1; y++)
    for(int32_t x = x0; x < x1; x++)
        if(!OutOfBounds(vram, x, y))
        {
            const uint32_t base = x - x0 > percent ? 0xFF0000 : 0x00FF00;
            const uint32_t color = base | (FILE_PRIO_HIGHEST << SURFACE_A_SHIFT);
            Put(vram, x, y, color);
        }
}

void Vram_DrawUnitHealthBars(const Vram vram, const Tiles tiles, const Color color)
{
    for(int32_t i = 0; i < tiles.count; i++)
    {
        const Tile tile = tiles.tile[i];
        const Point center = Tile_GetHotSpotCoords(tile);
        const Point top = {
            center.x,
            center.y - CONFIG_VRAM_UNIT_HEALTH_HEIGHT,
        };
        Unit* const unit = tile.reference;
        if(Unit_IsSelectedByColor(unit, color)
        || unit->is_being_built)
            DrawHealthBar(vram, top, unit);
    }
}

void DrawTileOutline(const Vram vram, const Registrar terrain, const Point iso, const uint32_t color)
{
    const Animation animation = terrain.animation[COLOR_GAIA][FILE_TERRAIN_DIRT];
    const Image image = animation.image[0];
    const Frame frame = animation.frame[0];
    for(int32_t i = 0; i < frame.height; i++)
    {
        const Outline outline = image.outline_table[i];
        const int32_t left  = iso.x + outline.left_padding;
        const int32_t right = iso.x + frame.width - outline.right_padding;
        const int32_t y = i + iso.y;
        const Point a = { left , y };
        const Point b = { right, y };
        for(int32_t xx = 0; xx < 2; xx++)
        for(int32_t yy = 0; yy < 2; yy++)
        {
            const Point shift = { xx, yy };
            const Point aa = Point_Add(a, shift);
            const Point bb = Point_Add(b, shift);
            DrawSelectionPixel(vram, aa, color);
            DrawSelectionPixel(vram, bb, color);
        }
    }
}

void DrawDimensionGrid(const Vram vram, const Registrar terrain, const Overview overview, const Grid grid, Unit* const unit)
{
    for(int32_t x = 0; x < unit->trait.dimensions.x; x++)
    for(int32_t y = 0; y < unit->trait.dimensions.y; y++)
    {
        const Point shift = { x, y };
        const Point cart = Point_Add(shift, unit->cart);
        const Point iso = Overview_CartToIso(overview, grid, cart);
        DrawTileOutline(vram, terrain, iso, 0xFFFFFF);
    }
}

void Vram_FlashDimensionGrids(const Vram vram, const Registrar terrain, const Overview overview, const Grid grid, const Tiles tiles)
{
    for(int32_t i = 0; i < tiles.count; i++)
    {
        Unit* const unit = tiles.tile[i].reference;
        if(!Unit_IsExempt(unit)
        && unit->grid_flash_timer < CONFIG_VRAM_FLASH_TIMER_MAX
        && unit->is_flash_on
        && unit->trait.is_inanimate)
            DrawDimensionGrid(vram, terrain, overview, grid, unit);
    }
}

void Vram_DrawSelectedDimensionGrids(const Vram vram, const Registrar terrain, const Overview overview, const Grid grid, const Tiles tiles)
{
    for(int32_t i = 0; i < tiles.count; i++)
    {
        Unit* const unit = tiles.tile[i].reference;
        if(Unit_IsSelectedByColor(unit, overview.color)
        && unit->trait.is_inanimate)
            DrawDimensionGrid(vram, terrain, overview, grid, unit);
    }
}

void Vram_DrawMouseTileSelect(const Vram vram, const Registrar terrain, const Overview overview, const Grid grid)
{
    const Point snap = Overview_IsoSnapTo(overview, grid, overview.mouse_cursor);
    DrawTileOutline(vram, terrain, snap, 0xFF0000);
}

static inline void DrawWithBounds(const Vram vram, SDL_Surface* surface, const Point offset, const Rect rect, const bool red_out)
{
    for(int32_t y = rect.a.y; y < rect.b.y; y++)
    for(int32_t x = rect.a.x; x < rect.b.x; x++)
    {
        uint32_t pixel = Surface_GetPixel(surface, x, y);
        const int32_t xx = offset.x + x;
        const int32_t yy = offset.y + y;
        if(pixel != SURFACE_COLOR_KEY)
        {
            if(red_out)
                pixel &= 0xFF0000;
            Put(vram, xx, yy, pixel);
        }
    }
}

typedef struct
{
    Animation animation[ICONTYPE_COUNT];
    Buttons buttons;
}
Pack;

static inline Pack GetPackFromMotive(const Registrar interfac, const Motive motive, const Color color, const Age age)
{
    const Animation* const base = interfac.animation[color];
    static Pack zero;
    Pack pack = zero;
    pack.buttons = Buttons_FromMotive(motive, age);
    pack.animation[ICONTYPE_BUILD]   = base[FILE_INTERFAC_BUILDING_ICONS];
    pack.animation[ICONTYPE_UNIT]    = base[FILE_INTERFAC_UNIT_ICONS];
    pack.animation[ICONTYPE_TECH]    = base[FILE_INTERFAC_TECH_ICONS];
    pack.animation[ICONTYPE_COMMAND] = base[FILE_INTERFAC_COMMAND_ICONS];
    return pack;
}

static inline void DrawPack(const Vram vram, const Pack pack, const Share share)
{
    for(int32_t index = 0; index < pack.buttons.count; index++)
    {
        const Button button = Button_Upgrade(pack.buttons.button[index], share.bits);
        SDL_Surface* const surface = pack.animation[button.icon_type].surface[button.index];
        const Point offset = Point_Layout(index, vram.xres, vram.yres);
        const Rect rect = {
            { 0, 0 },
            { surface->w, surface->h }
        };
        const bool red_out = Bits_Get(share.bits, button.trigger)
                          || Bits_Get(share.busy, button.trigger);
        DrawWithBounds(vram, surface, offset, rect, red_out);
    }
}

void Vram_DrawMotiveRow(const Vram vram, const Registrar interfac, const Share share, const Color color)
{
    const Pack pack = GetPackFromMotive(interfac, share.motive, color, share.status.age);
    DrawPack(vram, pack, share);
}

void Vram_DrawHud(const Vram vram, const Registrar interfac)
{
    const Rect rect = {
        {   0,  0 },
        { 393, 50 },
    };
    const Point corner = { 0, 0 };
    const Animation animation = interfac.animation[COLOR_GAIA][FILE_INTERFAC_HUD_0];
    DrawWithBounds(vram, animation.surface[0], corner, rect, false);
}

void Vram_Free(const Vram vram)
{
    Rects_Free(vram.channel_rects);
}

static inline void DrawDot(const Vram vram, const Point point, const int32_t size, const uint32_t in, const uint32_t out)
{
    for(int32_t y = -size; y <= size; y++)
    for(int32_t x = -size; x <= size; x++)
    {
        const int xx = x + point.x;
        const int yy = y + point.y;
        if(!OutOfBounds(vram, xx, yy))
        {
            const bool is_outline =
                x == -size ||
                x ==  size ||
                y == -size ||
                y ==  size;
            const uint32_t pixel = is_outline ? out : in;
            Put(vram, xx, yy, pixel);
        }
    }
}

static inline Point ToIsoMiniMap(const Vram vram, const Point cart)
{
    Point iso = Point_ToIso(cart);
    iso.y += vram.yres / 2;
    return iso;
}

static inline void DrawMiniMapTerrain(const Vram vram, const Map map)
{
    for(int32_t y = 0; y < map.size; y++)
    for(int32_t x = 0; x < map.size; x++)
    {
        const Point cart = { x, y };
        const Point iso = ToIsoMiniMap(vram, cart);
        const Terrain terrain = Map_GetTerrainFile(map, cart);
        const uint32_t pixel = (0xFFU << SURFACE_A_SHIFT) | map.color[terrain];
        Put(vram, iso.x, iso.y, pixel);
    }
}

static inline void DrawMiniMapUnits(const Vram vram, const Units units)
{
    for(int32_t i = 0; i < units.count; i++)
    {
        Unit* const unit = &units.unit[i];
        if(!Unit_IsExempt(unit))
        {
            const Point iso = ToIsoMiniMap(vram, unit->cart);
            const uint32_t pixel = (0xFFU << SURFACE_A_SHIFT) | Color_ToInt(unit->color);
            const int32_t size = unit->color == COLOR_GAIA ? 1 : unit->trait.is_inanimate ? 2 : 1;
            DrawDot(vram, iso, size, pixel, 0xFF000000);
        }
    }
}

void Vram_DrawMiniMap(const Vram vram, const Units units, const Map map)
{
    DrawMiniMapTerrain(vram, map);
    DrawMiniMapUnits(vram, units);
}
