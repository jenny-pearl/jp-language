#pragma once

#include <stdint.h>
#include <sys/mman.h>
#include <assert.h>

#define KIB(x) ((uint64_t)(x) << 10)
#define MIB(x) ((uint64_t)(x) << 20)
#define GIB(x) ((uint64_t)(x) << 30)

typedef struct {
	int64_t reserve;
	int64_t commit;
	int64_t pointer;
	int64_t step;
} Arena;

Arena *arena_create(int64_t reserve, int64_t commit, int64_t step)
{
	Arena *arena = mmap(0, reserve, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);

	mprotect(arena, commit + sizeof(Arena), PROT_READ | PROT_WRITE);

	arena->reserve = reserve;
	arena->commit = commit;
	arena->pointer = sizeof(Arena);
	arena->step = step;

	return arena;
}

void *arena_push(Arena *arena, int64_t size, int word_aligned)
{
	if (word_aligned) {
		size = (size + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
	}

	if (arena->pointer + size > arena->commit) {
		int64_t increase = ((arena->pointer + size - arena->commit) / arena->step + 1) * arena->step;
		assert(arena->reserve > arena->commit + increase);
		mprotect((char *)arena + arena->commit, increase, PROT_READ | PROT_WRITE);
		arena->commit += increase;
	}

	void *ret = (char *)arena + arena->pointer;
	arena->pointer += size;

	return ret;
}

void arena_wind_back(Arena *arena)
{
	arena->pointer = sizeof(Arena);
}

void arena_destroy(Arena *arena)
{
	if (arena) munmap(arena, arena->reserve);
}
