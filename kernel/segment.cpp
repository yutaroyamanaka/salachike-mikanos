#include "segment.hpp"
#include "asmfunc.h"
#include "x86_descriptor.hpp"
#include "logger.hpp"
#include "interrupt.hpp"
#include "memory_manager.hpp"

namespace {
  std::array<SegmentDescriptor, 7> gdt;
  std::array<uint32_t, 26> tss;

  static_assert((kTSS >> 3) + 1 < gdt.size());
}


void SetCodeSegment(SegmentDescriptor& desc,
    DescriptorType type,
    unsigned int descriptor_privilege_level,
    uint32_t base,
    uint32_t limit) {
  desc.data = 0;

  desc.bits.base_low = base & 0xffffu;
  desc.bits.base_middle = (base >> 16) & 0xffu;
  desc.bits.base_high = (base >> 24) & 0xffu;
  
  desc.bits.limit_low = limit & 0xffffu;
  desc.bits.limit_high = (limit >> 16) & 0xfu;

  desc.bits.type = type;
  desc.bits.system_segment = 1;
  desc.bits.descriptor_privilege_level = descriptor_privilege_level;
  desc.bits.present = 1;
  desc.bits.available = 0;
  desc.bits.long_mode = 1;
  desc.bits.default_operation_size = 0;
  desc.bits.granularity = 1;
}

void SetDataSegment(SegmentDescriptor& desc,
    DescriptorType type,
    unsigned int descriptor_privilege_level,
    uint32_t base,
    uint32_t limit) {
  SetCodeSegment(desc, type, descriptor_privilege_level, base, limit);

  desc.bits.long_mode = 0;
  desc.bits.default_operation_size = 1;
}

void SetupSegments() {
  gdt[0].data = 0;
  SetCodeSegment(gdt[1], DescriptorType::kExecuteRead, 0, 0, 0xfffff);
  SetDataSegment(gdt[2], DescriptorType::kReadWrite, 0, 0, 0xfffff);
  SetDataSegment(gdt[3], DescriptorType::kReadWrite, 3, 0, 0xfffff);
  SetCodeSegment(gdt[4], DescriptorType::kExecuteRead, 3, 0, 0xfffff);
  LoadGDT(sizeof(gdt) - 1, reinterpret_cast<uintptr_t>(&gdt[0]));
}

void InitializeSegmentation() {
  SetupSegments();
  SetDSAll(kKernelDS);
  SetCSSS(kKernelCS, kKernelSS);
}

void SetTSS(int index, uint64_t value) {
  tss[index] = value & 0xffffffff;
  tss[index + 1] = value >> 32;
}

uint64_t AllocateStackArea(int num_4kframes) {
  auto [ stk, err ] = memory_manager->Allocate(num_4kframes);
  if(err) {
    Log(kError, "failed to allocate stack area %s\n", err.Name());
    exit(1);
  }
  return reinterpret_cast<uint64_t>(stk.Frame()) + num_4kframes * 4096;
}

void InitializeTSS(){
  SetTSS(1, AllocateStackArea(8));
  SetTSS(7 + 2 * kISForTimer, AllocateStackArea(8));

  /* When the interruption in the user mode happens, CPU will refer the GDT entry specified by the TR Register and get the TSS
   * So, GDT entry specifies TSS header address and TSS RSP0 specifies the stack end address.
   * */
  uint64_t tss_addr = reinterpret_cast<uint64_t>(&tss[0]);
  SetSystemSegment(gdt[kTSS >> 3], DescriptorType::kTSSAvailable, 0,
      tss_addr & 0xffffffff, sizeof(tss)-1); /* GDT[5] */
  gdt[(kTSS >> 3) + 1].data = tss_addr >> 32; /* GDT[6] */

  LoadTR(kTSS);
}

void SetSystemSegment(SegmentDescriptor& desc, DescriptorType type, unsigned int descriptor_privilege_level, uint32_t base, uint32_t limit) {
  SetCodeSegment(desc, type, descriptor_privilege_level, base, limit);
  desc.bits.system_segment = 0;
  desc.bits.long_mode = 0;
}
