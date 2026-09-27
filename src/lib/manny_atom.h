#ifndef MANNY_ATOM_H
#define MANNY_ATOM_H

// NOTE(RJ)
// Atoms are just interned strings

#include "base.h"


typedef struct Manny_Atom
{
	u32 id;
}
Manny_Atom;

typedef struct Manny_Interner Manny_Interner;

b32 manny_atom_is_valid(Manny_Atom atom);

Manny_Interner *manny_interner_create(Arena *arena);
void manny_interner_destroy(Manny_Interner *interner);
Manny_Atom manny_interner_intern(Manny_Interner *interner, String value);
String manny_interner_string(const Manny_Interner *interner, Manny_Atom atom);

#endif
