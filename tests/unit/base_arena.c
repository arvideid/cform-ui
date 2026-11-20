#include <stdalign.h>
#include <stdint.h>

#include "cf/base.h"
#include "cform_test.h"

static void arena_aligns_every_block_for_any_type(void) {
    cf_arena arena;
    CF_CHECK(cf_arena_init(&arena, 4096), "arena of 4096 bytes initializes");

    uint8_t *bytes = cf_alloc(&arena, 3);
    double *number = cf_alloc(&arena, sizeof(double));
    void **pointer = cf_alloc(&arena, sizeof(void *));

    CF_CHECK(bytes != NULL && number != NULL && pointer != NULL, "three blocks come back");
    CF_CHECK(((uintptr_t)number % alignof(max_align_t)) == 0, "a block follows an odd-sized one");
    CF_CHECK(((uintptr_t)pointer % alignof(void *)) == 0, "a pointer block is pointer-aligned");

    *number = 2.5;
    CF_NEAR(*number, 2.5);

    cf_arena_free(&arena);
}

static void arena_clear_reuses_the_same_capacity(void) {
    cf_arena arena;
    CF_CHECK(cf_arena_init(&arena, 128), "arena initializes");

    void *first = cf_alloc(&arena, 64);
    CF_CHECK(cf_alloc(&arena, 64) != NULL, "the arena fills to capacity");
    CF_CHECK(cf_alloc(&arena, 16) == NULL, "nothing is left before the clear");

    cf_arena_clear(&arena);

    CF_CHECK(cf_alloc(&arena, 64) == first, "the next allocation lands where the first did");

    cf_arena_free(&arena);
}

static void arena_returns_null_when_the_request_does_not_fit(void) {
    cf_arena arena;
    CF_CHECK(cf_arena_init(&arena, 64), "arena initializes");

    CF_CHECK(cf_alloc(&arena, 65) == NULL, "a block larger than the arena is refused");
    CF_CHECK(arena.used == 0, "a refused block leaves the cursor alone");
    CF_CHECK(cf_alloc(&arena, 0) == NULL, "an empty request is refused too");

    cf_arena_free(&arena);
}

static void arena_records_its_high_water_mark(void) {
    cf_arena arena;
    CF_CHECK(cf_arena_init(&arena, 1024), "arena initializes");

    cf_alloc(&arena, 100);
    cf_alloc(&arena, 300);
    const size_t reached = arena.peak;

    cf_arena_clear(&arena);
    cf_alloc(&arena, 50);

    CF_CHECK(arena.used < reached, "the cursor rewinds below the mark");
    CF_CHECK(arena.peak == reached, "the mark keeps the frame's real cost");

    cf_arena_free(&arena);
}

static void a_cleared_arena_is_a_valid_empty_arena(void) {
    cf_arena arena = {0};

    CF_CHECK(cf_alloc(&arena, 16) == NULL, "an uninitialized arena allocates nothing");
    cf_arena_clear(&arena);
    cf_arena_free(&arena);
    CF_CHECK(cf_alloc(&arena, 16) == NULL, "freeing an arena that never allocated is a no-op");
}

static void arena_hands_a_finished_call_its_memory_back(void) {
    cf_arena arena;
    CF_CHECK(cf_arena_init(&arena, 1024), "arena initializes");

    uint8_t *outer = cf_alloc(&arena, 64);
    const size_t mark = cf_arena_mark(&arena);

    CF_CHECK(cf_alloc(&arena, 256) != NULL, "the inner call allocates");
    CF_CHECK(cf_arena_mark(&arena) == mark + 256, "the mark is where the inner call began");

    cf_arena_rewind(&arena, mark);

    CF_CHECK(cf_alloc(&arena, 64) == outer + 64, "the freed block is handed out again");
    CF_CHECK(arena.peak == mark + 256, "the high-water mark keeps the call's real cost");

    cf_arena_free(&arena);
}

static void arena_ignores_a_mark_that_is_not_the_latest_call(void) {
    cf_arena arena;
    CF_CHECK(cf_arena_init(&arena, 256), "arena initializes");

    cf_alloc(&arena, 32);
    const size_t mark = cf_arena_mark(&arena);
    cf_alloc(&arena, 128);

    cf_arena_rewind(&arena, mark + 200);
    CF_CHECK(arena.used == mark + 128, "a mark above the cursor rewinds nothing");

    cf_arena_rewind(&arena, mark);
    CF_CHECK(arena.used == mark, "the live mark still rewinds after the refused one");

    cf_arena_free(&arena);
}

static void marking_an_unusable_arena_reports_an_empty_one(void) {
    cf_arena arena = {0};

    CF_CHECK(cf_arena_mark(NULL) == 0, "a missing arena has no cursor");
    CF_CHECK(cf_arena_mark(&arena) == 0, "an uninitialized arena has nothing to reclaim");
    cf_arena_rewind(&arena, 4096);
    CF_CHECK(cf_arena_mark(&arena) == 0, "rewinding past the end changes nothing");
    cf_arena_rewind(NULL, 0);
}

int main(void) {
    printf("arena\n");
    CF_RUN(arena_aligns_every_block_for_any_type);
    CF_RUN(arena_clear_reuses_the_same_capacity);
    CF_RUN(arena_returns_null_when_the_request_does_not_fit);
    CF_RUN(arena_records_its_high_water_mark);
    CF_RUN(a_cleared_arena_is_a_valid_empty_arena);
    CF_RUN(arena_hands_a_finished_call_its_memory_back);
    CF_RUN(arena_ignores_a_mark_that_is_not_the_latest_call);
    CF_RUN(marking_an_unusable_arena_reports_an_empty_one);
    return CF_REPORT();
}
