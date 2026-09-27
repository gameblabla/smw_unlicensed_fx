/* Bare-metal abort().
 *
 * The V810 compiler answers a dereference it can still see as null -- or a
 * switch index it can still see as out of range -- by isolating that path
 * into a call to abort().  Newlib's abort() reaches raise(), and raise()
 * reaches kill() and getpid(), which nothing else in this link provides, so
 * the unresolved pair fails the whole link with no build error from the
 * compiler.  Whether the isolated path is emitted at all comes down to
 * inlining decisions, so it appears and disappears with unrelated edits.
 *
 * Newlib's own abort() halts in a loop once raise() cannot deliver the
 * signal, which is the only thing it can do on a machine with no process
 * table, so this is the same behaviour reached directly.  Defining it here
 * also keeps errno, the reent structure and the nano malloc chain from being
 * linked in behind it, which is worth about 2 KiB of the 2,004,992-byte
 * program ceiling. */
void abort(void)
{
    for (;;) {
    }
}
