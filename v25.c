// A solution to the Parker 5x5 Unique Word Problem
//
// Author: Stew Forster (stew675@gmail.com)	Date: Aug 2022
//

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include <stdatomic.h>
#include <immintrin.h>

// NUM_POISON must be defined before include utilities.h
#define NUM_POISON 16
#define USE_AVX2_SCAN

#include "utilities.h"

// ********************* SOLVER ALGORITHM ********************

static void
add_solution(uint32_t *solution)
{
	int i, pos = atomic_fetch_add(&num_sol, 1);
	char *so = solutions + pos * 30;
	const char *wd;

	for (i = 1; i < 6; i++) {
		wd = hash_lookup(solution[i], words);
		assert(wd != NULL);

		*so++ = *wd++; *so++ = *wd++; *so++ = *wd++; *so++ = *wd++;
		*so++ = *wd; *so++ = (i < 5) ? '\t' : '\n';
	}
} // add_solution


static inline uint64_t
vscan(uint32_t mask, uint32_t *set, uint32_t *n)
{
#define vzero _mm256_setzero_si256()

	// Find all valid keys
	__m256i vmask = _mm256_set1_epi32(mask);
	__m256i vkeys1 = _mm256_loadu_si256((__m256i *)set);
	__m256i vkeys2 = _mm256_loadu_si256((__m256i *)(set + 8));
	__m256i vres = _mm256_cmpeq_epi32(_mm256_and_si256(vmask, vkeys1), vzero);
	uint32_t mask1 = _mm256_movemask_epi8(vres);
	vres = _mm256_cmpeq_epi32(_mm256_and_si256(vmask, vkeys2), vzero);
	uint64_t mask64 = _mm256_movemask_epi8(vres);
	mask64 = (mask64 << 32) | mask1;
	*n = __builtin_popcountll(mask64) >> 2;

	// Return packed positions of valid matches
	return _pext_u64(0xfedcba9876543210, mask64);
} // vscan

// find_solutions() which is the busiest loop is kept
// as small and tight as possible for the most speed
void
find_solutions(int depth, struct frequency *f, uint32_t mask,
		int skipped, uint32_t *solution, uint32_t key)
{
	solution[depth] = key;
	if (depth == 5)
		return add_solution(solution);
	mask |= key;

	struct frequency *e = frq + (min_search_depth + depth);
	for (uint32_t n, *set, *end; f < e; f++) {
		if (mask & f->m)
			continue;

		CALCULATE_SET_AND_END;

		// Find all matching keys
		for (; set < end; set += 16)
			for (uint64_t vresmask = vscan(mask, set, &n); n--; vresmask >>= 4)
				find_solutions(depth + 1, f + 1, mask, skipped, solution, set[vresmask & 0xFULL]);

		if (skipped)
			break;
		skipped = 1;
	}
} // find_solutions

// Thread driver
static void
solve_work()
{
	uint32_t solution[6] __attribute__((aligned(64)));
	struct frequency *f = frq;
	struct tier *t;
	int32_t pos;

	// Solve starting with least frequent set
	t = f->sets;
	while ((pos = atomic_fetch_add(&set0pos, 1)) < t->l)
		find_solutions(1, f + 1, 0, 0, solution, t->s[pos]);

	// Solve after skipping least frequent set
	f++;
	t = f->sets;
	while ((pos = atomic_fetch_add(&set1pos, 1)) < t->l)
		find_solutions(1, f + 1, 0, 1, solution, t->s[pos]);

	atomic_fetch_add(&solvers_done, 1);
} // solve_work

void
solve()
{
	// Instruct any waiting worker-threads to start solving
	start_solvers();

	// The main thread also participates in finding solutions
	solve_work();

	// Wait for all solver threads to finish up
	while(solvers_done < nthreads)
		asm("nop");
} // solve
