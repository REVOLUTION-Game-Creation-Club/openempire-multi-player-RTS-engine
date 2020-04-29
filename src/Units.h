#pragma once

#include "Unit.h"
#include "Overview.h"
#include "Color.h"
#include "Trigger.h"
#include "Restore.h"
#include "Parts.h"
#include "Grid.h"
#include "Grid.h"
#include "Motive.h"
#include "Field.h"
#include "Map.h"
#include "Points.h"
#include "Packet.h"
#include "Registrar.h"
#include "Stack.h"
#include "Share.h"

typedef struct
{
    Unit* unit;
    Stack* stack;
    Color color;
    Share share[COLOR_COUNT];
    int32_t count;
    int32_t max;
    int32_t size;
    int32_t cpu_count;
    int32_t repath_index;
}
Units;

Units Units_Make(const int32_t size, const int32_t cpu_count, const int32_t max, const Color);

void Units_Free(const Units);

Stack Units_GetStackCart(const Units, const Point);

void Units_Field(const Units, const Map, const Field);

void Units_ResetTiled(const Units);

Units Units_Generate(Units, const Map, const Grid, const Registrar, const int32_t users, const Color spectator);

Units Units_Caretake(Units, const Registrar, const Grid, const Map, const Field, const bool must_randomize_mouse);

Units Units_Float(Units, const Units, const Registrar, const Overview, const Grid, const Map, const Motive);

Units Units_PacketService(Units, const Registrar, const Packet, const Grid, const Map, const Field);

uint64_t Units_Xor(const Units);

Point Units_GetFirstTownCenterPan(const Units, const Grid);

Units Units_ApplyRestore(Units, const Restore, const Grid, const Field);

Restore Units_PackRestore(const Units, const int32_t cycles);

void Units_FreeAllPathsForRecovery(const Units);
