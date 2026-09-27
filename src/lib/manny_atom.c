#include "manny_atom.h"
#include "platform.h"

#include <string.h>

#define MANNY_ATOM_INITIAL_CAPACITY 16
#define MANNY_ATOM_LOAD_NUMERATOR   3
#define MANNY_ATOM_LOAD_DENOMINATOR 4

typedef struct Manny_Atom_Entry
{
	String value;
	u64    hash;
}
Manny_Atom_Entry;

typedef struct Manny_Atom_Slot
{
	u64 hash;
	u32 id;
}
Manny_Atom_Slot;

typedef struct Manny_Interner_Table
{
	Manny_Atom_Entry *entries;
	u32             entry_count;
	u32             entry_capacity;
	Manny_Atom_Slot  *slots;
	u32             slot_count;
	u32             slot_capacity;
}
Manny_Interner_Table;

struct Manny_Interner
{
	Arena              *arena;
	Platform_Mutex      mutex;
	Manny_Interner_Table  table;
};

static u64 manny_atom_hash(String value)
{
	u64 hash = 14695981039346656037ULL;
	for (u64 i = 0; i < value.size; ++i) {
		hash ^= (u8)value.data[i];
		hash *= 1099511628211ULL;
	}
	return hash;
}

static Manny_Atom_Slot *manny_atom_empty_slot(Manny_Atom_Slot *slots, u32 capacity, u64 hash)
{
	u32 mask = capacity - 1;
	u32 index = (u32)hash & mask;
	while (slots[index].id != 0) index = (index + 1) & mask;
	return slots + index;
}

static b32 manny_interner_reserve_entries(Manny_Interner *interner, u32 needed)
{
	Manny_Interner_Table *table = &interner->table;
	if (table->entry_capacity >= needed) return true;
	u32 capacity = table->entry_capacity ? table->entry_capacity : MANNY_ATOM_INITIAL_CAPACITY;
	while (capacity < needed) {
		if (capacity > UINT32_MAX / 2) return false;
		capacity *= 2;
	}
	Manny_Atom_Entry *entries = arena_push_zero_aligned(interner->arena, (u64)capacity * sizeof(*entries), _Alignof(Manny_Atom_Entry));
	if (!entries) return false;
	if (table->entry_count) memcpy(entries, table->entries, (u64)table->entry_count * sizeof(*entries));
	table->entries = entries;
	table->entry_capacity = capacity;
	return true;
}

static b32 manny_interner_rehash(Manny_Interner *interner, u32 capacity)
{
	Manny_Interner_Table *table = &interner->table;
	if (capacity < MANNY_ATOM_INITIAL_CAPACITY || (capacity & (capacity - 1)) != 0) return false;
	Manny_Atom_Slot *slots = arena_push_zero_aligned(interner->arena, (u64)capacity * sizeof(*slots), _Alignof(Manny_Atom_Slot));
	if (!slots) return false;
	for (u32 i = 0; i < table->entry_count; ++i) {
		Manny_Atom_Slot *slot = manny_atom_empty_slot(slots, capacity, table->entries[i].hash);
		slot->hash = table->entries[i].hash;
		slot->id = i + 1;
	}
	table->slots = slots;
	table->slot_count = table->entry_count;
	table->slot_capacity = capacity;
	return true;
}

static b32 manny_interner_reserve_slot(Manny_Interner *interner)
{
	Manny_Interner_Table *table = &interner->table;
	u32 capacity = table->slot_capacity;
	if (capacity == 0) return manny_interner_rehash(interner, MANNY_ATOM_INITIAL_CAPACITY);
	if ((u64)(table->slot_count + 1) * MANNY_ATOM_LOAD_DENOMINATOR <= (u64)capacity * MANNY_ATOM_LOAD_NUMERATOR) return true;
	if (capacity > UINT32_MAX / 2) return false;
	return manny_interner_rehash(interner, capacity * 2);
}

static Manny_Atom manny_interner_find(const Manny_Interner_Table *table, String value, u64 hash)
{
	if (!table->slots || table->slot_capacity == 0) return (Manny_Atom){0};
	u32 mask = table->slot_capacity - 1;
	u32 index = (u32)hash & mask;
	for (u32 probe = 0; probe < table->slot_capacity; ++probe) {
		const Manny_Atom_Slot *slot = table->slots + index;
		if (slot->id == 0) return (Manny_Atom){0};
		if (slot->hash == hash && slot->id <= table->entry_count && string_equal(table->entries[slot->id - 1].value, value)) return (Manny_Atom){ slot->id };
		index = (index + 1) & mask;
	}
	return (Manny_Atom){0};
}

Manny_Interner *manny_interner_create(Arena *arena)
{
	if (!arena) return NULL;
	Manny_Interner *interner = arena_push_zero_aligned(arena, sizeof(*interner), _Alignof(Manny_Interner));
	if (!interner) return NULL;
	interner->arena = arena;
	platform_init_mutex(&interner->mutex);
	return interner;
}

void manny_interner_destroy(Manny_Interner *interner)
{
	if (interner) platform_destroy_mutex(&interner->mutex);
}

Manny_Atom manny_interner_intern(Manny_Interner *interner, String value)
{
	Manny_Atom atom = {0};
	if (!interner || !value.data) return atom;
	platform_lock_mutex(&interner->mutex);
	Manny_Interner_Table *table = &interner->table;
	if (table->entry_count == UINT32_MAX) goto done;
	u64 hash = manny_atom_hash(value);
	atom = manny_interner_find(table, value, hash);
	if (atom.id == 0) {
		Manny_Interner_Table original = *table;
		u64 mark = arena_mark(interner->arena);
		String copy = str_push_copy(interner->arena, value);
		if (!copy.data || !manny_interner_reserve_entries(interner, table->entry_count + 1) || !manny_interner_reserve_slot(interner)) {
			*table = original;
			arena_restore(interner->arena, mark);
		}
		else {
			atom.id = ++table->entry_count;
			table->entries[atom.id - 1] = (Manny_Atom_Entry){ copy, hash };
			Manny_Atom_Slot *slot = manny_atom_empty_slot(table->slots, table->slot_capacity, hash);
			slot->hash = hash;
			slot->id = atom.id;
			++table->slot_count;
		}
	}

done:
	platform_unlock_mutex(&interner->mutex);
	return atom;
}

String manny_interner_string(const Manny_Interner *interner, Manny_Atom atom)
{
	String result = {0};
	if (!interner || atom.id == 0) return result;
	platform_lock_mutex((Platform_Mutex *)&interner->mutex);
	if (atom.id <= interner->table.entry_count) result = interner->table.entries[atom.id - 1].value;
	platform_unlock_mutex((Platform_Mutex *)&interner->mutex);
	return result;
}

b32 manny_atom_is_valid(Manny_Atom atom)
{
	return atom.id != 0;
}
