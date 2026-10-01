#ifndef MANNY_BUILD_INTERNAL_H
#define MANNY_BUILD_INTERNAL_H

#include "manny_build.h"
#include "manny_atom.h"

typedef struct Manny_Path
{
	Manny_Atom atom;
}
Manny_Path;

typedef struct Manny_Path_Array
{
	Manny_Path *items;
	u32       count;
}
Manny_Path_Array;

b32 manny_path_resolve(Manny_Build *build, Manny_Path directory, String source, Manny_Path *result);
String manny_path_string(const Manny_Build *build, Manny_Path path);
b32 manny_path_is_valid(Manny_Path path);
Manny_Path manny_build_root(const Manny_Build *build);

#endif

