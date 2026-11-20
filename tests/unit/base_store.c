#include "cf/base.h"
#include "cform_test.h"

typedef struct layout_slot {
    float width;
    float height;
    uint32_t dirty;
} layout_slot;

static void store_hands_back_a_zeroed_element(void) {
    cf_store store;
    CF_CHECK(cf_store_init(&store, sizeof(layout_slot), 4), "store initializes");

    cf_id id = CF_NO_ID;
    layout_slot *slot = cf_store_add(&store, &id);

    CF_CHECK(slot != NULL && !cf_id_is_none(id), "an element and its id come back");
    CF_CHECK(slot->width == 0.0f && slot->dirty == 0u,
             "the element starts in its valid zero state");

    cf_store_free(&store);
}

static void store_keeps_every_id_valid_across_growth(void) {
    cf_store store;
    CF_CHECK(cf_store_init(&store, sizeof(layout_slot), 2), "store starts at two slots");

    cf_id ids[64];
    for (size_t i = 0; i < 64; ++i) {
        layout_slot *slot = cf_store_add(&store, &ids[i]);
        CF_CHECK(slot != NULL, "each add lands");
        slot->width = (float)i;
    }

    for (size_t i = 0; i < 64; ++i) {
        const layout_slot *slot = cf_store_get(&store, ids[i]);
        CF_CHECK(slot != NULL, "the id still resolves after every doubling");
        if (slot != NULL) {
            CF_NEAR(slot->width, (float)i);
        }
    }

    CF_CHECK(store.capacity >= 64, "capacity doubled rather than growing per element");

    cf_store_free(&store);
}

static void store_rejects_a_stale_id_after_the_slot_is_recycled(void) {
    cf_store store;
    CF_CHECK(cf_store_init(&store, sizeof(layout_slot), 4), "store initializes");

    cf_id first = CF_NO_ID;
    layout_slot *slot = cf_store_add(&store, &first);
    slot->height = 12.0f;

    cf_store_remove(&store, first);

    cf_id second = CF_NO_ID;
    layout_slot *recycled = cf_store_add(&store, &second);
    CF_CHECK(second.index == first.index, "the free slot is handed out again");
    CF_CHECK(second.generation != first.generation, "the generation moved on");
    CF_CHECK(cf_store_get(&store, first) == NULL, "the old id no longer reads the element");
    CF_CHECK(recycled->height == 0.0f, "the recycled element is empty, not the previous tenant's");

    cf_store_free(&store);
}

static void store_recycles_a_free_slot_before_extending(void) {
    cf_store store;
    CF_CHECK(cf_store_init(&store, sizeof(layout_slot), 8), "store initializes");

    cf_id ids[3] = {CF_NO_ID, CF_NO_ID, CF_NO_ID};
    for (size_t i = 0; i < 3; ++i) {
        cf_store_add(&store, &ids[i]);
    }
    cf_store_remove(&store, ids[1]);

    cf_id reused = CF_NO_ID;
    CF_CHECK(cf_store_add(&store, &reused) != NULL, "the add succeeds");
    CF_CHECK(reused.index == ids[1].index, "the hole is filled before the extent grows");
    CF_CHECK(store.extent == 3, "no slot was appended");
    CF_CHECK(store.alive == 3, "every slot is live again");

    cf_store_free(&store);
}

static void store_iterates_live_slots_in_index_order(void) {
    cf_store store;
    CF_CHECK(cf_store_init(&store, sizeof(layout_slot), 4), "store initializes");

    cf_id ids[4] = {CF_NO_ID, CF_NO_ID, CF_NO_ID, CF_NO_ID};
    for (size_t i = 0; i < 4; ++i) {
        cf_store_add(&store, &ids[i]);
    }
    cf_store_remove(&store, ids[2]);

    size_t visited = 0;
    for (size_t slot = 0; slot < store.extent; ++slot) {
        if (!store.live[slot]) {
            continue;
        }
        layout_slot *element = cf_store_at(&store, slot);
        CF_CHECK(element != NULL, "a live slot has an element");
        CF_CHECK(cf_store_id(&store, slot).index == (uint32_t)slot, "the id agrees with the index");
        visited++;
    }

    CF_CHECK(visited == 3, "the dead slot was skipped");
    CF_CHECK(cf_store_at(&store, store.extent) == NULL, "one past the extent is not addressable");

    cf_store_free(&store);
}

static void store_remove_ignores_an_id_from_a_previous_generation(void) {
    cf_store store;
    CF_CHECK(cf_store_init(&store, sizeof(layout_slot), 4), "store initializes");

    cf_id first = CF_NO_ID;
    cf_store_add(&store, &first);
    cf_store_remove(&store, first);

    cf_id second = CF_NO_ID;
    cf_store_add(&store, &second);
    cf_store_remove(&store, first);

    CF_CHECK(store.alive == 1, "the stale id removed nothing");
    CF_CHECK(cf_store_get(&store, second) != NULL, "the live element is untouched");

    cf_store_free(&store);
}

int main(void) {
    printf("store\n");
    CF_RUN(store_hands_back_a_zeroed_element);
    CF_RUN(store_keeps_every_id_valid_across_growth);
    CF_RUN(store_rejects_a_stale_id_after_the_slot_is_recycled);
    CF_RUN(store_recycles_a_free_slot_before_extending);
    CF_RUN(store_iterates_live_slots_in_index_order);
    CF_RUN(store_remove_ignores_an_id_from_a_previous_generation);
    return CF_REPORT();
}
