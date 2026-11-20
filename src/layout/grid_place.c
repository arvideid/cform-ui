#include <string.h>

#include "layout/internal.h"

#define BITS_PER_BYTE 8u
#define BITS_PER_WORD 64u
#define BYTES_PER_WORD (BITS_PER_WORD / BITS_PER_BYTE)
#define AUTO_LINE UINT32_MAX

static bool cell_taken(const uint8_t *cells, uint32_t columns, uint32_t row, uint32_t column) {
    const uint32_t index = row * columns + column;

    return (cells[index / BITS_PER_BYTE] & (uint8_t)(1u << (index % BITS_PER_BYTE))) != 0u;
}

static void mark_cell(uint8_t *cells, uint32_t columns, uint32_t row, uint32_t column) {
    const uint32_t index = row * columns + column;

    cells[index / BITS_PER_BYTE] |= (uint8_t)(1u << (index % BITS_PER_BYTE));
}

/* The line an item asks to start at, 0-based, or AUTO_LINE for `auto`. A line
 * past the last track of the axis starts in that last track instead. */
static uint32_t first_line(uint32_t declared, uint32_t count) {
    if (declared == 0u) {
        return AUTO_LINE;
    }

    const uint32_t line = declared - 1u;

    return line < count ? line : count - 1u;
}

/* The tracks an item fills, never past the last line of the axis. */
static uint32_t filled_tracks(uint32_t declared, uint32_t start, uint32_t count) {
    const uint32_t want = declared == 0u ? 1u : declared;
    const uint32_t room = count - start;

    return want < room ? want : room;
}

static bool area_free(const uint8_t *cells, uint32_t columns, uint32_t row, uint32_t column,
                      uint32_t row_span, uint32_t column_span) {
    for (uint32_t r = row; r < row + row_span; ++r) {
        for (uint32_t c = column; c < column + column_span; ++c) {
            if (cell_taken(cells, columns, r, c)) {
                return false;
            }
        }
    }

    return true;
}

/*
 * The lowest cell nobody holds, found a word at a time. An all-ones word cannot
 * contain a candidate, so skipping it answers the same question the row-major
 * scan asks and reads 10,000 cells in 157 words instead of 10,000 tests.
 */
static bool first_free_cell(const uint8_t *cells, size_t cell_count, uint32_t columns,
                            uint32_t *out_row, uint32_t *out_column) {
    const size_t word_count = (cell_count + BITS_PER_WORD - 1u) / BITS_PER_WORD;

    for (size_t word_index = 0u; word_index < word_count; ++word_index) {
        uint64_t occupied = 0u;

        for (uint32_t byte = 0u; byte < BYTES_PER_WORD; ++byte) {
            occupied |= (uint64_t)cells[word_index * BYTES_PER_WORD + byte]
                        << (byte * BITS_PER_BYTE);
        }

        uint64_t unoccupied = ~occupied;
        if (unoccupied == 0u) {
            continue;
        }

        uint32_t offset = 0u;
        while ((unoccupied & 1u) == 0u) {
            unoccupied >>= 1;
            ++offset;
        }

        const size_t cell = word_index * BITS_PER_WORD + offset;
        if (cell >= cell_count) {
            return false;
        }

        *out_row = (uint32_t)(cell / columns);
        *out_column = (uint32_t)(cell % columns);

        return true;
    }

    return false;
}

/*
 * The first cell an item can take, scanning row-major and dense. A line the
 * item fixed is honoured; an item whose fixed row is full moves on to the next
 * free cell instead of overlapping what is already there.
 */
static bool find_cell(const uint8_t *cells, uint32_t columns, uint32_t rows, const grid_item *item,
                      uint32_t *out_row, uint32_t *out_column) {
    /* A one-cell item that names no lines wants exactly the lowest free cell,
     * which is what every implicit placement asks for. */
    if (item->row_start == AUTO_LINE && item->column_start == AUTO_LINE && item->row_span == 1u &&
        item->column_span == 1u) {
        return first_free_cell(cells, (size_t)rows * columns, columns, out_row, out_column);
    }

    for (uint32_t row = 0u; row < rows; ++row) {
        if (item->row_start != AUTO_LINE && row != item->row_start) {
            continue;
        }
        if (row + item->row_span > rows) {
            break;
        }

        for (uint32_t column = 0u; column < columns; ++column) {
            if (item->column_start != AUTO_LINE && column != item->column_start) {
                continue;
            }
            if (column + item->column_span > columns) {
                break;
            }
            if (area_free(cells, columns, row, column, item->row_span, item->column_span)) {
                *out_row = row;
                *out_column = column;

                return true;
            }
        }
    }

    return false;
}

static void mark_area(uint8_t *cells, uint32_t columns, const grid_item *item) {
    for (uint32_t r = item->row_start; r < item->row_start + item->row_span; ++r) {
        for (uint32_t c = item->column_start; c < item->column_start + item->column_span; ++c) {
            mark_cell(cells, columns, r, c);
        }
    }
}

uint32_t grid_place(const layout_ctx *ctx, uint32_t slot, uint32_t column_count, grid_item *items,
                    uint32_t *rows_out) {
    const uint32_t first = ctx->tree->child_start[slot];
    const uint32_t count = child_count(ctx, slot);
    const cf_style *self = &ctx->tree->styles[slot];

    /* A child that names a row makes that row exist. Nothing else grows the
     * grid by declaration, so a runaway span cannot ask for tracks nobody
     * placed into; only the rows an auto item still needs are added on top. */
    uint32_t rows = self->row_count;

    for (uint32_t i = 0u; i < count; ++i) {
        const cf_style *child = &ctx->tree->styles[ctx->tree->child_slots[first + i]];

        if (child->position != CF_POSITION_ABSOLUTE && child->row_start > rows) {
            rows = child->row_start;
        }
    }

    /* Every child could be placed, and each one still to be looked for can be
     * pushed a row past the last, so this bound is never too small. */
    const uint32_t bound = rows + count;
    const size_t mark = cf_arena_mark(ctx->scratch);
    /* Whole words, so the placement scan may read eight bytes at a time without
     * running past the map; the padding bits stay clear and read as no cell. */
    const size_t bytes =
        (((size_t)bound * column_count + BITS_PER_WORD - 1u) / BITS_PER_WORD) * BYTES_PER_WORD;
    uint8_t *cells = cf_alloc(ctx->scratch, bytes);

    if (cells == NULL) {
        cf_logf(CF_LOG_LAYOUT, "grid: scratch refused a %u by %u occupancy map", bound,
                column_count);
        *rows_out = rows;

        return 0u;
    }

    memset(cells, 0, bytes);

    uint32_t placed = 0u;

    for (uint32_t i = 0u; i < count; ++i) {
        const uint32_t child_slot = ctx->tree->child_slots[first + i];
        const cf_style *child = &ctx->tree->styles[child_slot];

        if (child->position == CF_POSITION_ABSOLUTE) {
            continue;
        }

        grid_item *item = &items[placed++];
        item->slot = child_slot;
        item->column_start = first_line(child->column_start, column_count);
        item->row_start = first_line(child->row_start, rows);
        item->column_span =
            filled_tracks(child->column_span,
                          item->column_start == AUTO_LINE ? 0u : item->column_start, column_count);
        item->row_span = filled_tracks(child->row_span,
                                       item->row_start == AUTO_LINE ? 0u : item->row_start, bound);

        if (item->row_start != AUTO_LINE && item->column_start != AUTO_LINE) {
            mark_area(cells, column_count, item);
        }
    }

    for (uint32_t i = 0u; i < placed; ++i) {
        grid_item *item = &items[i];

        if (item->row_start != AUTO_LINE && item->column_start != AUTO_LINE) {
            continue;
        }

        uint32_t row = 0u;
        uint32_t column = 0u;

        if (find_cell(cells, column_count, bound, item, &row, &column)) {
            item->row_start = row;
            item->column_start = column;

            if (row + item->row_span > rows) {
                rows = row + item->row_span;
            }
            mark_area(cells, column_count, item);

            continue;
        }

        cf_logf(CF_LOG_LAYOUT, "grid: %u rows of %u columns could not place every item", bound,
                column_count);

        /* The last row still in the map takes the item: overlapping draws one
         * cell twice, which is a visible bug, while vanishing is not a plan
         * anyone can debug. */
        item->row_start = bound - item->row_span;
        item->column_start = 0u;
        mark_area(cells, column_count, item);
    }

    cf_arena_rewind(ctx->scratch, mark);

    *rows_out = rows;

    return placed;
}
