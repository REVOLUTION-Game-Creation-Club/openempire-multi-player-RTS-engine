#pragma once

#include "Point.h"

#include <stdint.h>

typedef struct
{
    int32_t size;
    int32_t tile_iso_width;
    int32_t tile_iso_height;
    int32_t tile_cart_width;
    int32_t tile_cart_height;
    Point tile_cart_mid;
}
Grid;

Grid Grid_Make(const int32_t size, const int32_t tile_iso_width, const int32_t tile_iso_height);

Point Grid_GetGridPoint(const Grid, const Point);

Point Grid_GetGridPointWithOffset(const Grid, const Point cart, const Point offset);

Point Grid_GetOffsetFromGridPoint(const Grid, const Point);

Point Grid_CellToOffset(const Grid, const Point);

Point Grid_CellToCart(const Grid, const Point);

Point Grid_CartToCell(const Grid, const Point);

Point Grid_GetCornerOffset(const Grid, const Point);

Point Grid_OffsetToCell(const Point);

Point Grid_PanToCart(const Grid, const Point);

Point Grid_CartToPan(const Grid, const Point);

Point Grid_GetRubbleShift(const Grid);
