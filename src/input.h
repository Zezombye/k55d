#ifndef K55D_INPUT_H
#define K55D_INPUT_H

#define G_KEY_COUNT 6

// Installs the low-level keyboard hook. Call from the thread that runs the message loop.
void input_init(void);

// Fn key changed state. Fn acts as the left Windows key.
void input_fn(int pressed);

// Other key changed state (key index from the event channel). While Fn is held, the keyboard does not send the
// other keys to Windows, thus k55d injects them.
void input_fn_combo(int key, int pressed);

// G key (0 to 5) changed state. G1 to G6 act as F13 to F18.
void input_g(int g, int pressed);

// Releases all keys that k55d holds down. Call when the keyboard disconnects, because the release events will not come.
void input_release_all(void);

#endif
