#include "../common.h"

extern dbuf_t *dbuf;
static const unsigned long init_capacity = (sizeof(dbuf->internal_buf) /
                                            sizeof(dbuf->internal_buf[0]));

static int check_init_state(dbuf_t *dbuf)
{
        return (dbuf->base == dbuf->internal_buf &&
                dbuf->pos  == dbuf->internal_buf &&
                dbuf->capacity == init_capacity);
}

static int check_dbuf_printf(dbuf_t *dbuf)
{
        char scratch_mem[] = { '\0', '0', '0', '0', '\0' };
        char *const str = scratch_mem + 1;
        const unsigned long last_idx = sizeof(scratch_mem) - 2UL;
        unsigned long idx, count;

        for (idx = last_idx, count = 0UL;
             idx > 0UL;
             count++) {
                int rv;

                rv = dbuf_printf(dbuf, "%s", str);
                if (rv < 0 || (unsigned long) rv != last_idx) {
                        printf("ERROR: Unexpected return value\n"
                               "Expected: %lu\n"
                               "  Actual: %d\n",
                               last_idx,
                               rv);
                        return 0;
                }

                for (; idx > 0UL && scratch_mem[idx] == '9'; idx--) ;

                if (idx > 0UL) {
                        scratch_mem[idx]++;
                        while (idx < last_idx)
                                scratch_mem[++idx] = '0';
                }
        }

        if ((unsigned long) (dbuf->pos - dbuf->base) !=
            last_idx * count) {
                printf("ERROR: The number of filled bytes &"
                       "expected amount mismatch\n"
                       "Expected: %lu\n"
                       "  Actual: %lu\n",
                       last_idx * count,
                       (unsigned long) (dbuf->pos - dbuf->base));
                return 0;
        }

        return 1;
}

int main(void)
{
        dbuf_t dbuf_mem, *const dbuf = &dbuf_mem;
        char *saved_ptr, *ptr;

        dbuf_init(dbuf);

        if (!check_init_state(dbuf)) {
                printf("ERROR: Insane init state\n"
                       "    &dbuf->internal_buf == %p\n"
                       "     dbuf->base         == %p\n"
                       "     dbuf->pos          == %p\n"
                       "     dbuf->capacity     == %lu\n",
                       dbuf->internal_buf, dbuf->base, dbuf->pos, dbuf->capacity);
                return 1;
        }

        if ((ptr = dbuf_alloc(dbuf, init_capacity + 1UL)) == NULL) {
                printf("ERROR: Failed to allocate %lu bytes\n",
                       init_capacity + 1UL);
                return 1;
        }

        if (dbuf->pos != ptr ||
            dbuf->pos != dbuf->base ||
            dbuf->base == dbuf->internal_buf ||
            dbuf->capacity == init_capacity) {
                printf("ERROR: Wrong state after allocation of %lu bytes\n"
                       "     ptr                == %p\n"
                       "    &dbuf->internal_buf == %p\n"
                       "     dbuf->base         == %p\n"
                       "     dbuf->pos          == %p\n"
                       "     dbuf->capacity     == %lu\n",
                       init_capacity + 1UL, ptr,
                       dbuf->internal_buf, dbuf->base, dbuf->pos, dbuf->capacity);
                return 1;
        }

        /* UL overflows should be caught by dbuf API */
        saved_ptr = ptr;
        if ((ptr = dbuf_alloc(dbuf, ULONG_MAX)) != NULL) {
                printf("ERROR: Unexpected ptr != NULL "
                       "in the test for UL overflow\n"
                       "Tried to allocate %lu bytes\n",
                       ULONG_MAX);
                return 1;
        }
        ptr = saved_ptr;

        /* Ensure no state change occured */
        if (dbuf->pos != ptr ||
            dbuf->pos != dbuf->base ||
            dbuf->base == dbuf->internal_buf ||
            dbuf->capacity == init_capacity) {
                printf("ERROR: Wrong state after failed allocation "
                       "of %lu bytes\n"
                       "     ptr                == %p\n"
                       "    &dbuf->internal_buf == %p\n"
                       "     dbuf->base         == %p\n"
                       "     dbuf->pos          == %p\n"
                       "     dbuf->capacity     == %lu\n",
                       ULONG_MAX, ptr,
                       dbuf->internal_buf, dbuf->base, dbuf->pos, dbuf->capacity);
                return 1;
        }

        if (!check_dbuf_printf(dbuf))
                return 1;
        
        dbuf_free(dbuf);

        if (!check_init_state(dbuf)) {
                printf("ERROR: Insane init state for freed buffer\n"
                       "    &dbuf->internal_buf == %p\n"
                       "     dbuf->base         == %p\n"
                       "     dbuf->pos          == %p\n"
                       "     dbuf->capacity     == %lu\n",
                       dbuf->internal_buf, dbuf->base, dbuf->pos, dbuf->capacity);
                return 1;
        }

        printf("DBUF API appears to function correctly\n");

        return 0;
}
