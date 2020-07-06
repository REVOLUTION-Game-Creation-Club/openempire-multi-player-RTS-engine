#include "Mode.h"

#include "Util.h"
#include "Surface.h"

#include <stdlib.h>

static SDL_Surface* ConvertByteMask(const Mode mode, Outline* const outline_table, const int32_t which_tile, const bool demo)
{
    const uint8_t* const data = &mode.tile_byte_mask[which_tile * mode.tile_size];
    SDL_Surface* const surface = Surface_Make(mode.frame.width, mode.frame.height);
    int32_t index = 0;
    for(int32_t y = 0; y < mode.frame.height; y++)
    {
        const Outline outline = outline_table[y];
        const int32_t avail = mode.frame.width - Outline_GetWidth(outline);
        const int32_t a = outline.left_padding;
        const int32_t b = a + avail;
        for(int32_t x = a; x < b; x++)
        {
            const int8_t amount = data[index++];
            const uint8_t mask = (UINT8_MAX * amount) / INT8_MAX;
            const uint32_t grey_scale = (mask << 16) | (mask << 8) | mask;
            const uint32_t color = demo ? grey_scale : mask;
            Surface_PutPixel(surface, x, y, color);
        }
    }
    return surface;
}

static Mode ConvertAllByteMasks(Mode mode, Outline* const outline_table, const int32_t nr_tiles)
{
    mode.mask_real = UTIL_ALLOC(SDL_Surface*, nr_tiles);
    mode.mask_demo = UTIL_ALLOC(SDL_Surface*, nr_tiles);
    // BOTH A DEMO AND REAL MASK ARE GENERATED. THE REAL MASK IS A PURE ALPHA CHANNEL USED FOR ACTUAL BLENDING.
    // THE DEMO MASK CONVERTS THE ALPHA VALUE TO A GREY SCALE SO THE ALPHA CHANNEL CAN BE VISUALIZED IN THE DEMO RENDERER.
    for(int32_t which_tile = 0; which_tile < nr_tiles; which_tile++)
    {
        mode.mask_demo[which_tile] = ConvertByteMask(mode, outline_table, which_tile, true);
        mode.mask_real[which_tile] = ConvertByteMask(mode, outline_table, which_tile, false);
    }
    return mode;
}

static Mode LoadHeader(FILE* const fp, const int32_t nr_tiles)
{
    static Mode zero;
    Mode mode = zero;
    UTIL_FREAD(&mode.tile_size, 1, fp);
    mode.tile_flags = UTIL_ALLOC(uint8_t, nr_tiles);
    UTIL_FREAD(mode.tile_flags, nr_tiles, fp);
    return mode;
}

static Mode LoadBitMasks(Mode mode, FILE* const fp, const int32_t nr_tiles)
{
    const int32_t size = (nr_tiles + 1) * mode.tile_size / 8;
    mode.tile_bit_mask = UTIL_ALLOC(uint8_t, size);
    UTIL_FREAD(mode.tile_bit_mask, size, fp);
    return mode;
}

static Mode LoadByteMasks(Mode mode, FILE* const fp, const int32_t nr_tiles)
{
    const int32_t size = nr_tiles * mode.tile_size;
    mode.tile_byte_mask =  UTIL_ALLOC(uint8_t, size);
    UTIL_FREAD(mode.tile_byte_mask, size, fp);
    return mode;
}

Mode Mode_Load(FILE* const fp, const int32_t nr_tiles, const Frame reference, Outline* const outline_table)
{
    Mode mode = LoadHeader(fp, nr_tiles);
    mode.frame = reference;
    mode = LoadBitMasks(mode, fp, nr_tiles);
    mode = LoadByteMasks(mode, fp, nr_tiles);
    mode = ConvertAllByteMasks(mode, outline_table, nr_tiles);
    return mode;
}

void Mode_Free(const Mode mode, const int32_t nr_tiles)
{
    free(mode.tile_flags);
    free(mode.tile_bit_mask);
    free(mode.tile_byte_mask);
    for(int32_t i = 0; i < nr_tiles; i++)
    {
        SDL_FreeSurface(mode.mask_real[i]);
        SDL_FreeSurface(mode.mask_demo[i]);
    }
    free(mode.mask_real);
    free(mode.mask_demo);
}

/* SEE: HTTPS://GITHUB.COM/SFTTECH/OPENAGE/DOCS/MEDIA
 *
 * FOR EACH MODE THERE ARE 31 BLEND IDS.
 * THE FIRST 16 BLEND IDS ARE VARIATIONS IN PAIRS OF 4. THIS WAS DONE TO PREVENT REPEATING BLENDS.
 * THE LAST 16 BLEND IDS ARE UNIQUE, PROVIDING THINNING LAYOUTS FOR SOMETHING LIKE A PATH.
 *
 *    [0 : 3]        [4 : 7]        [8 : 11]      [12 : 15]         [16]           [17]           [18]           [19]           [20]
 *
 *       -              %              -              %              -              -              %              -              %
 *     -----          -%%%%          -----          %%%%-          -----          -----          %%%%%          -----          -%%%%
 *   ---------      -----%%%%      ---------      %%%%-----      --------%      ---------      ---%%%---      %--------      -----%%%%
 * ---------%%%%  ---------%%%%  %%%%---------  %%%%---------  ---------%%%%  -------------  -------------  %%%%---------  %%%%-----%%%%
 *   -----%%%%      ---------      %%%%-----      ---------      --------%      ---%%%---      ---------      %--------      %%%%-----
 *     -%%%%          -----          %%%%-          -----          -----          %%%%%          -----          -----          %%%%-
 *       %              -              %              -              -              %              -              -              %
 *
 *      [21]           [22]           [23]           [24]           [25]           [26]           [27]           [28]           [29]           [30]
 *
 *       %              %              %              -              %              %              %              %              %              %
 *     %%%%-          %%%%-          %%%%%          -----          -%%%%          -%%%%          %%%%-          %%%%%          %%%%%          %%%%%
 *   %%%%-----      %%%%%%---      %%%%%%%%%      ---------      ---%%%%%%      -----%%%%      %%%%-----      %%%%-%%%%      %%%%-%%%%      %%%%-%%%%
 * %%%%-----%%%%  %%%%%%%%-----  %%%%%%%%%%%%%  %%%%%%%%%%%%%  -----%%%%%%%%  %%%%-----%%%%  %%%%-----%%%%  %%%%-----%%%%  %%%%-----%%%%  %%%%-----%%%%
 *   -----%%%%      %%%%%%---      ---------      %%%%%%%%%      ---%%%%%%      %%%%-%%%%      %%%%-%%%%      %%%%-----      -----%%%%      %%%%-%%%%
 *     -%%%%          %%%%-          -----          %%%%%          -%%%%          %%%%%          %%%%%          %%%%-          -%%%%          %%%%%
 *       %              %              -              %              %              %              %              %              %              %
 */

int32_t Mode_GetBlendIndex(const Point a, const Point b)
{
    const Point diff = Point_Sub(a, b);
    const Point delta = { a.x % 4, a.y % 4 };
    if(diff.x ==  0 && diff.y ==  1) return  0 + delta.x;
    if(diff.x ==  1 && diff.y ==  0) return  4 + delta.y;
    if(diff.x == -1 && diff.y ==  0) return  8 + delta.y;
    if(diff.x ==  0 && diff.y == -1) return 12 + delta.x;
    if(diff.x ==  1 && diff.y ==  1) return 16;
    if(diff.x == -1 && diff.y ==  1) return 17;
    if(diff.x ==  1 && diff.y == -1) return 18;
    if(diff.x == -1 && diff.y == -1) return 19;
    return 30;
}
