#ifdef __cplusplus
#include <cstdint>
#include <cstddef>
extern "C" {
#else
#include <stddef.h>
#include <stdint.h>
#endif

struct AppEvent {
  enum Type {
    kQuit,
    kMouseMove,
    kMouseButton,
    kTimerTimeout,
  } type;

  union {
    struct {
      int x, y;
      int dx, dy;
      uint8_t buttons;
    } mouse_move;

    struct {
      int x, y;
      int press;
      int button;
    } mouse_button;

    struct {
      unsigned long timeout;
      int value;
    } timer;
  }arg;
};

#ifdef __cplusplus
}
#endif
