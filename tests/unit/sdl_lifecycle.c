#include "cf/backend.h"
#include "cform_test.h"

/*
 * The resource half of the backend contract, run against the one backend that
 * owns a device: an upload answers a number, a release takes the bytes back and
 * hands that number to the next upload, and a stream still naming a retired
 * number draws nothing instead of whatever the number came to mean. An ask the
 * caller cannot describe - no pixels, no side, a side no device carries - is
 * refused before it reaches the hardware. What the backend takes for the window it
 * also gives back, and only what it took - a second window does not shut the
 * platform the first one is standing on.
 *
 * The window is opened on SDL's dummy video driver, which the test property sets
 * in the environment: no display, no desktop automation, and the same slots a host
 * calls on a real device. The driver makes no pointer shape, so every cursor ask
 * here is a refusal - which is the half of that contract a headless run can prove,
 * that a desk with no cursor survives being asked. Which shape lands under the
 * hand on a real desk is a person's call, in the gallery window.
 *
 * The written-text service is the other half of the same agreement and answers the
 * other way: SDL keeps that state for the window itself, so both asks are taken here
 * whatever the driver can show. What this tier proves about it is that the pair is
 * safe in every order and around a teardown; whether a keyboard rises on a device
 * that has one to show is the platform's, and this run cannot see it.
 *
 * Where even that cannot open a window the case says so and leaves, the way the
 * font case does.
 */

/* A shape cf_cursor does not name: what a host built against a wider list would
 * hand to a port that indexes a table by the number it was given. */
#define UNNAMED_SHAPE 9u

/* Two RGBA8888 pixels, the layout create_texture promises. */
static const uint8_t pixels[4u * 2u * 2u] = {255u, 0u,   0u, 255u, 0u, 0u,
                                             255u, 255u, 0u, 0u,   0u, 255u};

static cf_backend open_window(const char *title) {
    return cf_backend_sdl(&(cf_sdl_config){.title = title, .width = 64u, .height = 64u});
}

/* One image command naming the texture, which is the whole ask to draw it. */
static void draw_texture(const cf_backend *backend, uint32_t texture) {
    cf_command command = {0};
    command.kind = CF_COMMAND_IMAGE;
    command.rect = (cf_rect){0.0f, 0.0f, 16.0f, 16.0f};
    command.texture = texture;

    backend->submit(backend->user_data, &command, 1u);
    backend->present(backend->user_data);
}

static uint32_t upload(const cf_backend *backend) {
    return backend->create_texture(backend->user_data, 2u, 2u, pixels);
}

static void a_released_upload_leaves_its_number_behind(cf_backend *backend) {
    const uint32_t first = upload(backend);
    CF_CHECK(first != 0u, "an upload answers a number");
    draw_texture(backend, first);

    uint32_t second = upload(backend);
    CF_CHECK(second != 0u && second != first, "two live uploads are two numbers");

    /* The retired number comes back, so the live capacity - and not the number of
     * images a page has shown over its lifetime - is what the table costs. */
    backend->release_texture(backend->user_data, first);
    draw_texture(backend, first);
    second = upload(backend);
    CF_CHECK(second == first, "the retired number is the next one handed out");
    backend->release_texture(backend->user_data, second);

    /* A hundred create-draw-release cycles on one slot: the peak number is the
     * plateau the acceptance asks for, and it answers one rather than a hundred. */
    uint32_t peak = 0u;
    for (uint32_t cycle = 0u; cycle < 100u; ++cycle) {
        const uint32_t id = upload(backend);
        if (id == 0u) {
            break;
        }
        draw_texture(backend, id);
        if (id > peak) {
            peak = id;
        }
        backend->release_texture(backend->user_data, id);
    }
    CF_CHECK(peak == 1u, "a hundred cycles never needed more than one slot");
}

/* An upload is a promise about a buffer, and an ask that cannot be described is
 * refused before it reaches the device: a width whose row stride wraps would have
 * the copy read rows of bytes the caller never had. */
static void an_upload_that_cannot_be_described_is_refused(const cf_backend *backend) {
    const uint32_t live = upload(backend);
    CF_CHECK(live != 0u, "a described upload is taken");

    CF_CHECK(backend->create_texture(backend->user_data, 0u, 2u, pixels) == 0u,
             "no width is no upload");
    CF_CHECK(backend->create_texture(backend->user_data, 2u, 0u, pixels) == 0u,
             "no height is no upload");
    CF_CHECK(backend->create_texture(backend->user_data, 2u, 2u, NULL) == 0u,
             "no pixels is no upload");
    CF_CHECK(backend->create_texture(backend->user_data, 1u << 31, 1u, pixels) == 0u,
             "a width whose stride would wrap is refused");
    CF_CHECK(backend->create_texture(backend->user_data, 20000u, 1u, pixels) == 0u,
             "a side past any display is refused");

    /* The refusals took a number and gave it back, so the table is where it was:
     * the live upload still draws, and retiring it frees the one slot rather than
     * leaving a row of empties behind it. */
    draw_texture(backend, live);
    backend->release_texture(backend->user_data, live);
    const uint32_t again = upload(backend);
    CF_CHECK(again == live, "a refused upload leaves the table where it was");
    backend->release_texture(backend->user_data, again);
}

/*
 * Asking for a pointer shape on a desk that has none. The slot must be there - a
 * window is exactly the output that can answer - and every ask, repeated, out of
 * range, or through a state the caller never opened, has to leave the device able
 * to draw its next frame. What the driver refuses here is the case that matters:
 * a port that treated a refused cursor as a lost window would stop painting over
 * a cosmetic preference.
 */
static void asking_for_a_shape_survives_a_desk_that_has_none(const cf_backend *backend) {
    CF_CHECK(backend->set_cursor != NULL, "a window offers the shape slot");

    for (uint32_t round = 0u; round < 3u; ++round) {
        backend->set_cursor(backend->user_data, CF_CURSOR_DEFAULT);
        backend->set_cursor(backend->user_data, CF_CURSOR_POINTER);
        backend->set_cursor(backend->user_data, CF_CURSOR_TEXT);
        backend->set_cursor(backend->user_data, (cf_cursor)UNNAMED_SHAPE);
    }

    /* A refusal handed the ask back rather than acting on it, so a null state is
     * the same case again from the other side: nothing to show the shape on.
     */
    backend->set_cursor(NULL, CF_CURSOR_POINTER);

    const uint32_t live = upload(backend);
    CF_CHECK(live != 0u, "the frame after the asks still uploads");
    draw_texture(backend, live);
    backend->release_texture(backend->user_data, live);
}

/*
 * Opening and shutting the service that turns keys into written text, both directions
 * and in the orders a page produces them: a field focused, a field left, the same
 * answer twice from a host that drives the pipeline from its own loop, and an ask
 * through no state at all. A frame that still uploads afterwards is the point - a
 * port that treated the keyboard as essential would stop drawing over a service the
 * desk has no way to show.
 */
static void the_keyboard_takes_both_asks(const cf_backend *backend) {
    CF_CHECK(backend->set_text_input != NULL, "a window offers the keyboard slot");

    for (uint32_t round = 0u; round < 3u; ++round) {
        backend->set_text_input(backend->user_data, true);
        backend->set_text_input(backend->user_data, false);
    }

    backend->set_text_input(backend->user_data, false);
    backend->set_text_input(backend->user_data, false);
    backend->set_text_input(NULL, true);

    const uint32_t live = upload(backend);
    CF_CHECK(live != 0u, "the frame after the asks still uploads");
    draw_texture(backend, live);
    backend->release_texture(backend->user_data, live);

    /* Left open on the way out, so the teardown below is the one that has to shut it:
     * a window that closes with its service running is a window a second one inherits
     * the state from. */
    backend->set_text_input(backend->user_data, true);
}

/* Every ask that names nothing the backend holds: a host should not have to know
 * which of these it is, and none of them may reach a texture to destroy. */
static void a_release_naming_nothing_changes_nothing(cf_backend *backend) {
    const uint32_t live = upload(backend);

    backend->release_texture(backend->user_data, 0u);
    backend->release_texture(backend->user_data, 1000000u);
    draw_texture(backend, 0u);
    draw_texture(backend, 1000000u);

    backend->release_texture(backend->user_data, live);
    backend->release_texture(backend->user_data, live);
    draw_texture(backend, live);

    CF_CHECK(upload(backend) == live, "a double release leaves the slot reusable");
    backend->release_texture(backend->user_data, live);
}

/* The recorder takes no uploads and keeps no device state, so the two slots the
 * host calls are NULL there - which is why a caller asks before it calls. */
static void a_backend_that_reclaims_nothing_says_so(void) {
    const cf_backend empty = {0};
    CF_CHECK(empty.release_texture == NULL && empty.destroy == NULL,
             "a zero backend promises no resources");

    cf_recorder recorder = {0};
    const cf_backend recorder_backend = cf_backend_recorder(&recorder);
    CF_CHECK(recorder_backend.release_texture == NULL && recorder_backend.destroy == NULL,
             "the recorder retires nothing it never took");
    CF_CHECK(recorder_backend.size == sizeof(cf_backend),
             "and it states the struct the shell will read it as");
}

/* A host that already had the platform up keeps it: a window borrowed from it
 * must not quit it on the way out, or the other window still drawing through the
 * same subsystem loses its renderer and its uploads with it. */
static void a_second_window_leaves_the_first_one_running(void) {
    const cf_backend first = open_window("cform lifecycle one");

    if (first.submit == NULL) {
        printf("      skipped: no window this backend can open\n");
        return;
    }

    const cf_backend second = open_window("cform lifecycle two");

    if (second.submit == NULL) {
        first.destroy(first.user_data);
        printf("      skipped: no second window\n");
        return;
    }

    const uint32_t held = upload(&first);
    second.destroy(second.user_data);

    const uint32_t again = upload(&first);
    CF_CHECK(again != 0u, "the first window uploads after the second closes");
    CF_CHECK(again != held, "and an upload there is not the retired one over there");
    first.release_texture(first.user_data, again);
    first.release_texture(first.user_data, held);
    first.destroy(first.user_data);
}

int main(void) {
    printf("sdl lifecycle\n");
    a_backend_that_reclaims_nothing_says_so();

    cf_backend backend = open_window("cform lifecycle");

    if (backend.submit == NULL || backend.create_texture == NULL ||
        backend.release_texture == NULL || backend.destroy == NULL) {
        printf("      skipped: no window this backend can open\n");
        return CF_REPORT();
    }

    CF_CHECK(backend.abi_version == CF_BACKEND_ABI_VERSION, "the backend states the contract");

    a_released_upload_leaves_its_number_behind(&backend);
    an_upload_that_cannot_be_described_is_refused(&backend);
    a_release_naming_nothing_changes_nothing(&backend);
    asking_for_a_shape_survives_a_desk_that_has_none(&backend);
    the_keyboard_takes_both_asks(&backend);

    /* Last, because nothing may be submitted through a destroyed backend: what a
     * teardown that skips this leaves behind is bytes the host has no way to reach
     * again.
     */
    backend.destroy(backend.user_data);

    a_second_window_leaves_the_first_one_running();

    return CF_REPORT();
}
