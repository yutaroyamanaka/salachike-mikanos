#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <numeric>
#include <vector>
#include <deque>
#include <limits>
#include <array>

#include "frame_buffer_config.hpp"
#include "graphics.hpp"
#include "interrupt.hpp"
#include "mouse.hpp"
#include "font.hpp"
#include "console.hpp"
#include "pci.hpp"
#include "logger.hpp"
#include "usb/xhci/xhci.hpp"
#include "asmfunc.h"
#include "memory_map.hpp"
#include "segment.hpp"
#include "memory_manager.hpp"
#include "paging.hpp"
#include "window.hpp"
#include "layer.hpp"
#include "timer.hpp"
#include "frame_buffer.hpp"
#include "message.hpp"
#include "acpi.hpp"
#include "keyboard.hpp"
#include "task.hpp"
#include "terminal.hpp"
#include "fat.hpp"
#include "syscall.hpp"

int printk(const char* format, ...) {
  va_list ap;
  int result;
  char s[1024];

  va_start(ap, format);
  result = vsprintf(s, format, ap);
  va_end(ap);

  console->PutString(s);

  return result;
}

std::shared_ptr<ToplevelWindow> text_window;
unsigned int text_window_layer_id;
void InitializeTextWindow() {
  const int win_w = 160;
  const int win_h = 52;

  text_window = std::make_shared<ToplevelWindow>(
      win_w, win_h, screen_config.pixel_format, "Text Box Test"
  );
  DrawTextbox(*text_window->InnerWriter(), {0, 0}, text_window->InnerSize());
  
  text_window_layer_id = layer_manager->NewLayer()
    .SetWindow(text_window)
    .SetDraggable(true)
    .Move({350, 200})
    .ID();

  layer_manager->UpDown(text_window_layer_id, std::numeric_limits<int>::max());
}

int text_window_index;
void DrawTextCursor(bool visible) {
  const auto color = visible ? ToColor(0) : ToColor(0xffffff);
  const auto pos = Vector2D<int>{4 + 8 * text_window_index, 5};
  FillRectangle(*text_window->InnerWriter(), pos, {7, 15}, color);
}

void InputTextWindow(char c) {
  if(c == 0) return;

  auto pos = []() {
    return Vector2D<int>{4 + 8*text_window_index, 6};
  };

  const int max_chars = (text_window->InnerSize().x - 8) / 8 - 1;
  if(c == '\b' && text_window_index > 0) {
    DrawTextCursor(false);
    text_window_index--;
    FillRectangle(*text_window->InnerWriter(), pos(), {8, 16}, ToColor(0xffffff));
    DrawTextCursor(true);
  } else if(c >= ' ' && text_window_index < max_chars) {
    DrawTextCursor(false);
    WriteAscii(*text_window->InnerWriter(), pos(), c, ToColor(0));
    text_window_index++;
    DrawTextCursor(true);
  }

  layer_manager->Draw(text_window_layer_id);
}

std::shared_ptr<ToplevelWindow> main_window;
unsigned int main_window_layer_id;
void InitializeMainWindow() {
  main_window = std::make_shared<ToplevelWindow>(160, 52, screen_config.pixel_format, "Hello Window");

  main_window_layer_id = layer_manager->NewLayer()
    .SetWindow(main_window)
    .SetDraggable(true)
    .Move({300, 100})
    .ID();
 
  layer_manager->UpDown(main_window_layer_id, std::numeric_limits<int>::max());
}

void SwitchEhci2Xhci(const pci::Device& xhc_dev) {
  bool intel_ehc_exist = false;
  for(int i = 0; i < pci::num_device; i++) {
    if(pci::devices[i].class_code.Match(0x0cu, 0x03u, 0x20u) && 0x8086 == pci::ReadVendorId(pci::devices[i])) {
      intel_ehc_exist = true;
      break;
    }
  }

  if(!intel_ehc_exist) return;

  uint32_t superspeed_ports = pci::ReadConfReg(xhc_dev, 0xdc);
  pci::WriteConfReg(xhc_dev, 0xd8, superspeed_ports);
  uint32_t ehci2xhci_ports = pci::ReadConfReg(xhc_dev, 0xd4);
  pci::WriteConfReg(xhc_dev, 0xd0, ehci2xhci_ports);
  Log(kDebug, "SwitchEhci2Xhci: SS = %02, xHCI = %02x\n", superspeed_ports, ehci2xhci_ports);
}

usb::xhci::Controller* xhc;

alignas(16) uint8_t kernel_main_stack[1024 * 1024];

extern "C" void KernelMainNewStack(const FrameBufferConfig& frame_buffer_config_ref, const MemoryMap& memory_map_ref, 
    const acpi::RSDP& acpi_table, void* volume_image) {

  MemoryMap memory_map{memory_map_ref};

  InitializeGraphics(frame_buffer_config_ref);
  InitializeConsole();

  printk("Welcome to Mikan OS!\n");
  SetLogLevel(kWarn);

  InitializeSegmentation();
  InitializePaging();
  InitializeMemoryManager(memory_map);
  InitializeTSS();
  InitializeInterrupt();

  fat::Initialize(volume_image);
  InitializePCI();

  InitializeLayer();
  InitializeMainWindow();
  InitializeTextWindow();
  layer_manager->Draw({{0, 0}, ScreenSize()});

  acpi::Initialize(acpi_table);
  InitializeLAPICTimer();

  const int kTextboxCursorTimer = 1;
  const int kTimer05Sec = static_cast<int>(kTimerFreq * 0.5);
  time_manager->AddTimer(Timer{kTimer05Sec, kTextboxCursorTimer, 1});
  bool textbox_cursor_visible = false;

  InitializeSyscall();
  InitializeTask();
  Task& main_task = task_manager->CurrentTask();
  terminals = new std::map<uint64_t, Terminal*>;
  const uint64_t task_terminal_id = task_manager->NewTask()
    .InitContext(TaskTerminal, 0)
    .Wakeup()
    .ID();

  usb::xhci::Initialize();
  InitializeKeyboard();
  InitializeMouse();

  char str[128];

  while (true) {
    __asm__("cli");
    const auto tick = time_manager->CurrentTick();
    __asm__("sti");

    sprintf(str, "%010lu", tick);
    FillRectangle(*main_window->InnerWriter(), {20, 4}, {8 * 10, 16}, {0xc6, 0xc6, 0xc6});
    WriteString(*main_window->InnerWriter(), {20, 4}, str, {0, 0, 0});
    layer_manager->Draw(main_window_layer_id);

    __asm__("cli");
    auto msg = main_task.ReceiveMessage();
    if(!msg) {
      main_task.Sleep();
      __asm__("sti");
      continue;
    }

    __asm__("sti");

    switch(msg->type) {
      case Message::kInterruptXHCI:
        usb::xhci::ProcessEvents();
        break;
      case Message::kTimerTimeout:
        if(msg->arg.timer.value == kTextboxCursorTimer) {
          __asm__("cli");
          time_manager->AddTimer(
              Timer{msg->arg.timer.timeout + kTimer05Sec, kTextboxCursorTimer, 1}
              );
          __asm__("sti");
          textbox_cursor_visible = !textbox_cursor_visible;
          DrawTextCursor(textbox_cursor_visible);
          layer_manager->Draw(text_window_layer_id);

          __asm__("cli");
          task_manager->SendMessage(task_terminal_id, *msg);
          __asm__("sti");
        }
        break;
      case Message::kKeyPush:
        if(msg->arg.keyboard.ascii == '\t') {
          if(msg->arg.keyboard.press) {
            active_layer->ActivateNextIDLayer();
          }
        } else {
          if(auto act = active_layer->GetActive(); act == text_window_layer_id) {
            if(msg->arg.keyboard.press) {
              InputTextWindow(msg->arg.keyboard.ascii);
            }
          } else {
            __asm__("cli");
            auto task_it = layer_task_map->find(act);
            __asm__("sti");
            if(task_it != layer_task_map->end()) {
              __asm__("cli");
              task_manager->SendMessage(task_it->second, *msg);
              __asm__("sti");
            } else {
              printk("Key push not handled : keycode %02x, ascii %02x\n", msg->arg.keyboard.keycode, msg->arg.keyboard.ascii);
            }
          }
        }
        break;
      case Message::kLayer:
        ProcessLayerMessage(*msg);
        __asm__("cli");
        task_manager->SendMessage(msg->src_task, Message{Message::kLayerFinish});
        __asm__("sti");
        break;
      default:
        Log(kError, "Unknown message type: %d\n", msg->type);
    }
  }
}

extern "C" void __cxa_pure_virtual() {
  while (1) __asm__("hlt");
}
