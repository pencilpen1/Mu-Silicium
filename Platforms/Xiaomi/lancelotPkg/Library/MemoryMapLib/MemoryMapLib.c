#include <Library/MemoryMapLib.h>

STATIC
EFI_MEMORY_REGION_DESCRIPTOR
gMemoryDescriptor[] = {
  // Name, Address, Length, HobOption, ResourceType, ResourceAttribute, MemoryType, ArmAttribute

  // DDR: captured 4 GB lancelot DT and conservative /proc/iomem holes.
  {"UEFI Stack", 0x40000000, 0x00040000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"UEFI FD", 0x40080000, 0x00200000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"DXE Heap", 0x40280000, 0x03C00000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv, WRITE_BACK},
  {"UEFI Boot Log", 0x512C0000, 0x00030000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"WOA Handoff", 0x512F0000, 0x00001000, AddMem, SYS_MEM, SYS_MEM_CAP, EfiACPIMemoryNVS, WRITE_BACK},
  {"Display Reserved", 0x7CDB0000, 0x02250000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"Runtime Reserved 00", 0x4CE00000ULL, 0x60000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"Runtime Reserved 01", 0x4D000000ULL, 0x100000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"Runtime Reserved 02", 0x6C000000ULL, 0x900000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"Runtime Reserved 03", 0x70000000ULL, 0x7000000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"Runtime Reserved 04", 0x77FF0000ULL, 0x10000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"Runtime Reserved 05", 0x7A000000ULL, 0xC00000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"Runtime Reserved 06", 0x7F000000ULL, 0xE00000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"Runtime Reserved 07", 0x7FF00000ULL, 0x100000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"Runtime Reserved 08", 0x8F000000ULL, 0xD00000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"Runtime Reserved 09", 0x9F3F0000ULL, 0xB10000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"Runtime Reserved 10", 0x9FFF0000ULL, 0x10000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"Runtime Reserved 11", 0xAE000000ULL, 0x10300000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"Runtime Reserved 12", 0xBF000000ULL, 0x400000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"Runtime Reserved 13", 0xBFC00000ULL, 0x20000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"Runtime Reserved 14", 0xBFE00000ULL, 0x40000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"Runtime Reserved 15", 0xBFF80000ULL, 0x80000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"Runtime Reserved 16", 0x13FFF6000ULL, 0xA000ULL, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},

  // Register Regions
  {"UART0", 0x11002000, 0x1000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"GIC Distributor",    0x0C000000, 0x00040000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"GIC Redistributors", 0x0C040000, 0x00200000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"Top Ck Gen",         0x10000000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"Infra Cfg AO",       0x10001000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"MUSB MAC",           0x11200000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  // Windows board drivers require these MMIO mappings during preflight.
  {"IO Config", 0x10002000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"Keypad", 0x10010000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"SPI0", 0x1100A000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"Pinctrl",            0x10005000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"WatchDog Timer",     0x10007000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"AP Mixed",           0x1000C000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"PMIC Wrapper",       0x1000D000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"Charger EINT", 0x1000B000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  // Device mappings only. OS boot alone does not start or touch the GPU.
  {"SPM Control",        0x10006000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"MFG Configuration",  0x13000000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"Mali Registers",     0x13040000, 0x00004000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"I2C DMA",            0x11000000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"I2C7",               0x11004000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"EMI",                0x10219000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"MSDC-0",             0x11230000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"MSDC-1",             0x11240000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"MSDC Top-0",         0x11CD0000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"MSDC Top-1",         0x11C90000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"USB2 PHY",           0x11CC0000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"Display OVL 2L",     0x1400C000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"Display OVL",        0x1400B000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE}
};

VOID
GetMemoryMap (
  OUT EFI_MEMORY_REGION_DESCRIPTOR **MemoryDescriptor,
  OUT UINT8                         *MemoryDescriptorCount)
{
  // Pass Data
  *MemoryDescriptor      = gMemoryDescriptor;
  *MemoryDescriptorCount = ARRAY_SIZE (gMemoryDescriptor);
}
