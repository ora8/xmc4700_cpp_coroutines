#include "xmc_regs.hpp"
#include <cstdint>
#include <initializer_list>

// ------------------------------------------------------------
// Externe Funktionen
// ------------------------------------------------------------

// main() darf in C++ nicht aufgerufen werden (auch -Wpedantic meldet das). Gleiches
// Symbol "main" unter anderem Namen deklarieren und darüber starten.
extern "C" int app_main() __asm__("main");

namespace {

using xmc::reg;

// Läuft vor der .data/.bss-Initialisierung: nur lokale Variablen verwenden.
void busy_wait(std::uint32_t loops) noexcept {
  for (std::uint32_t i = 0; i < loops; ++i) {
    __asm volatile("nop");
  }
}

// ------------------------------------------------------------
// Systemtakt 144 MHz aus dem 12-MHz-Quarz des Relax Kits
// (Ablauf wie Infineons SystemInit; am Board getestet im Projekt embassy-xmc-2)
//
// Quarz 12 MHz -> OSC_HP -> P=2 -> VCO x48 = 288 MHz -> K2=2 -> fPLL = 144 MHz
// fSYS = fCPU = fPERIPH = fCCU = 144 MHz
//
// Ohne diese Einstellung läuft der Chip auf dem internen Oszillator fOFI, der
// auf dem Test-Board nicht 24, sondern ca. 36 MHz hatte: SysTick und
// UART-Baudrate stimmen dann nicht.
// ------------------------------------------------------------

constexpr std::uintptr_t SCU_CLK_SYSCLKCR = 0x5000460C;
constexpr std::uintptr_t SCU_CLK_CPUCLKCR = 0x50004610;
constexpr std::uintptr_t SCU_CLK_PBCLKCR = 0x50004614;
constexpr std::uintptr_t SCU_CLK_CCUCLKCR = 0x50004620;
constexpr std::uintptr_t SCU_CLK_SLEEPCR = 0x50004630; // SYSSEL = Bit 0
constexpr std::uintptr_t SCU_OSC_OSCHPCTRL = 0x50004704;
constexpr std::uintptr_t SCU_PLL_PLLSTAT = 0x50004710;
constexpr std::uintptr_t SCU_PLL_PLLCON0 = 0x50004714;
constexpr std::uintptr_t SCU_PLL_PLLCON1 = 0x50004718;
constexpr std::uintptr_t SCU_PLL_PLLCON2 = 0x5000471C;
constexpr std::uintptr_t FLASH0_FCON = 0x58002014;

constexpr std::uint32_t SYSSEL_PLL = 1u << 16;
constexpr std::uint32_t VCOBYP = 1u << 0;
constexpr std::uint32_t VCOPWD = 1u << 1;
constexpr std::uint32_t FINDIS = 1u << 4;
constexpr std::uint32_t OSCDISCDIS = 1u << 6;
constexpr std::uint32_t PLLPWD = 1u << 16;
constexpr std::uint32_t OSCRES = 1u << 17;
constexpr std::uint32_t RESLD = 1u << 18;
constexpr std::uint32_t VCOBYST = 1u << 0;
constexpr std::uint32_t VCOLOCK = 1u << 2;
constexpr std::uint32_t OSC_USABLE = 0b111u << 7; // PLLLV | PLLHV | PLLSP

constexpr std::uint32_t pllcon1(std::uint32_t k2) noexcept {
  constexpr std::uint32_t p = 2, n = 48;
  return ((p - 1) << 24) | ((k2 - 1) << 16) | ((n - 1) << 8);
}

bool wait_for(std::uintptr_t addr, std::uint32_t mask,
              std::uint32_t want) noexcept {
  for (std::uint32_t i = 0; i < 1000000u; ++i) {
    if ((reg(addr) & mask) == want) {
      return true;
    }
  }
  return false;
}

void clock_init_144mhz() noexcept {
  // Läuft das System schon auf der PLL (Warmstart), erst auf fOFI zurück
  if (reg(SCU_CLK_SYSCLKCR) & SYSSEL_PLL) {
    reg(SCU_CLK_SYSCLKCR) = 0;
    busy_wait(1000);
  }

  // Flash-Wartezyklen vor dem Hochtakten (22 ns Zugriffszeit -> 4 Takte bei 144
  // MHz)
  reg(FLASH0_FCON) = (reg(FLASH0_FCON) & ~0xFu) | 4u;

  // PLL einschalten, OSC_HP im Quarzbetrieb (OSCVAL = 12 MHz / 2,5 MHz - 1 = 3)
  reg(SCU_PLL_PLLCON0) &= ~(VCOPWD | PLLPWD);
  reg(SCU_OSC_OSCHPCTRL) =
      (reg(SCU_OSC_OSCHPCTRL) & ~((3u << 4) | (0xFu << 16))) | (3u << 16);
  reg(SCU_PLL_PLLCON2) &= ~((1u << 0) | (1u << 8)); // PINSEL, K1INSEL: OSC_HP
  reg(SCU_PLL_PLLCON0) &= ~OSCRES;
  if (!wait_for(SCU_PLL_PLLSTAT, OSC_USABLE, OSC_USABLE)) {
    return; // Quarz schwingt nicht: auf fOFI bleiben
  }

  // PLL im Bypass auf 24 MHz konfigurieren und einrasten lassen
  reg(SCU_PLL_PLLCON0) |= VCOBYP;
  reg(SCU_PLL_PLLCON0) |= FINDIS;
  reg(SCU_PLL_PLLCON1) = pllcon1(12);
  reg(SCU_PLL_PLLCON0) |= OSCDISCDIS;
  reg(SCU_PLL_PLLCON0) &= ~FINDIS;
  reg(SCU_PLL_PLLCON0) |= RESLD;
  if (!wait_for(SCU_PLL_PLLSTAT, VCOLOCK, VCOLOCK)) {
    return;
  }
  reg(SCU_PLL_PLLCON0) &= ~VCOBYP;
  if (!wait_for(SCU_PLL_PLLSTAT, VCOBYST, 0)) {
    return;
  }

  // Teiler 1:1, System auf die PLL (24 MHz), dann in Stufen auf 144 MHz
  reg(SCU_CLK_CPUCLKCR) = 0;
  reg(SCU_CLK_PBCLKCR) = 0;
  reg(SCU_CLK_CCUCLKCR) = 0;
  reg(SCU_CLK_SYSCLKCR) = SYSSEL_PLL;
  // Auch im Sleep-Modus (WFI im Executor) auf der PLL bleiben. Nach dem Reset
  // wählt SLEEPCR.SYSSEL fOFI: Dann wechselt der Takt bei jedem WFI, und der
  // UART verstümmelt die letzten Bytes einer Ausgabe, SysTick zählt im Schlaf
  // zu langsam.
  reg(SCU_CLK_SLEEPCR) |= 1u;
  for (std::uint32_t k2 : {6u, 4u, 3u, 2u}) { // 48, 72, 96, 144 MHz
    busy_wait(10000);
    reg(SCU_PLL_PLLCON1) = pllcon1(k2);
  }
  busy_wait(10000);
}

} // namespace

extern "C" void SystemInit() {
  // FPU einschalten (CP10/CP11 voller Zugriff). Der Code ist mit
  // -mfloat-abi=hard übersetzt; ohne das endet die erste Gleitkomma-Operation
  // in einem UsageFault.
  reg(xmc::core::SCB_CPACR) |= (0xFu << 20);

  // Das Boot-ROM des XMC4700 setzt SCB.CCR.UNALIGN_TRP; der Compiler darf für
  // Cortex-M4 aber unausgerichtete Zugriffe erzeugen. Bit löschen.
  reg(xmc::core::SCB_CCR) &= ~(1u << 3);

  xmc::core::dsb_isb();

  clock_init_144mhz();
}

// ------------------------------------------------------------
// Linker-Symbole
// ------------------------------------------------------------

extern "C" {
extern std::uint32_t _estack;

extern std::uint32_t _sidata;
extern std::uint32_t _sdata;
extern std::uint32_t _edata;

extern std::uint32_t _sbss;
extern std::uint32_t _ebss;
}

// Konstruktoren globaler C++-Objekte: Tabellen aus dem Linker-Skript. Selbst aufrufen
// statt __libc_init_array(): Das bräuchte _init() aus den Newlib-Startdateien, die wegen
// -nostartfiles fehlen (sonst zieht crt0.o exit() und stdio-Platzhalter mit).
using InitFunc = void (*)();
extern "C" {
extern InitFunc __preinit_array_start[], __preinit_array_end[];
extern InitFunc __init_array_start[], __init_array_end[];
}

// ------------------------------------------------------------
// Interrupt-Handler
// ------------------------------------------------------------

extern "C" void Reset_Handler();
extern "C" void SysTick_Handler();

extern "C" void Default_Handler() {
  for (;;) {
    __asm volatile("nop");
  }
}

// ------------------------------------------------------------
// Interrupt-Vektortabelle
// ------------------------------------------------------------

using Handler = void (*)();

__attribute__((section(".isr_vector"), used)) Handler const vectors[] = {

    // Cortex-M4 Core Exceptions
    reinterpret_cast<Handler>(&_estack), //  0: Initial Stack Pointer
    Reset_Handler,                       //  1: Reset
    Default_Handler,                     //  2: NMI
    Default_Handler,                     //  3: HardFault
    Default_Handler,                     //  4: MemManage
    Default_Handler,                     //  5: BusFault
    Default_Handler,                     //  6: UsageFault

    nullptr, //  7: Reserved
    nullptr, //  8: Reserved
    nullptr, //  9: Reserved
    nullptr, // 10: Reserved

    Default_Handler, // 11: SVCall
    Default_Handler, // 12: Debug Monitor

    nullptr, // 13: Reserved

    Default_Handler, // 14: PendSV
    SysTick_Handler  // 15: SysTick
};

// ------------------------------------------------------------
// Reset Handler
// ------------------------------------------------------------

extern "C" void Reset_Handler() {
  // 1. Systeminitialisierung: FPU, Ausrichtung, Takt
  SystemInit();

  // 2. .data aus Flash in RAM kopieren
  auto *src = &_sidata;

  for (auto *dst = &_sdata; dst < &_edata;) {
    *dst++ = *src++;
  }

  // 3. .bss mit null initialisieren
  for (auto *dst = &_sbss; dst < &_ebss;) {
    *dst++ = 0;
  }

  // 4. Globale C++-Objekte initialisieren
  for (auto *f = __preinit_array_start; f < __preinit_array_end; ++f) (*f)();
  for (auto *f = __init_array_start; f < __init_array_end; ++f) (*f)();

  // 5. Hauptprogramm starten
  app_main();

  // 6. Falls main() zurückkehrt
  for (;;) {
    __asm volatile("nop");
  }
}
