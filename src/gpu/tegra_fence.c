/*
 * Copyright (c) GRATE-DRIVER project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "driver.h"
#include "tegra_fence.h"

#ifdef FENCE_DEBUG
static struct tegra_fence poisoned_debug_fence = {
    .bug0 = 1,
    .bug1 = 0,
};

struct tegra_fence *poisoned_fence = &poisoned_debug_fence;

unsigned tegra_fences_created;
unsigned tegra_fences_destroyed;

drmMMListHead tegra_live_fences = {
    .next = &tegra_live_fences,
    .prev = &tegra_live_fences,
};

void tegra_fences_debug_dump(unsigned int max)
{
    struct tegra_fence *f;

    DRMLISTFOREACHENTRY(f, &tegra_live_fences, dbg_entry) {
        TEGRA_FENCE_DEBUG_MSG(f, "debug dump");

        if (!max--)
            break;
    }
}
#endif
