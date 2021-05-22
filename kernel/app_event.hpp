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
  } type;

  union {
    struct {
      int x, y;
      int dx, dy;
      uint8_t buttons;
    } mouse_move;
  } arg;
};

#ifdef __cplusplus
}
#endif
