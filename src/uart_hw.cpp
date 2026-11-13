#include "uart_hw.hpp"
#include "xmc_regs.hpp"
#include <cstdint>

// UART auf USIC0 Kanal 0, ASC-Modus, 8N1, 115200 Baud.
// Beim XMC4700 Relax Kit hängt dieser Kanal am virtuellen COM-Port des
// On-Board-J-Link (Linux: /dev/ttyACM0): TX = P1.5 (ALT2, U0C0.DOUT0), RX =
// P1.4 (Eingang DX0B). Konfiguration wie im am Board getesteten Rust-Treiber
// (embassy-xmc-2, uart.rs).

namespace {
using xmc::reg;

constexpr std::uintptr_t U = 0x40030000u; // USIC0_CH0

// SCU: Takt-Gating und Reset von USIC0 (jeweils Bit 11)
constexpr std::uintptr_t SCU_CLK_CGATSTAT0 = 0x50004640u;
constexpr std::uintptr_t SCU_CLK_CGATCLR0 = 0x50004648u;
constexpr std::uintptr_t SCU_RESET_PRSTAT0 = 0x5000440Cu;
constexpr std::uintptr_t SCU_RESET_PRCLR0 = 0x50004414u;
constexpr std::uint32_t SCU_USIC0 = 1u << 11;

// Kanal-Register
constexpr std::uintptr_t KSCFG = 0x0C, FDR = 0x10, BRG = 0x14, DX0CR = 0x1C,
                         SCTR = 0x34, TCSR = 0x38, PCR = 0x3C, CCR = 0x40,
                         PSCR = 0x4C, TBUF0 = 0x80;
// Sendepuffer enthält noch ungesendete Daten
constexpr std::uint32_t TCSR_TDV = 1u << 7;
constexpr std::uint32_t OVERSAMPLING = 16;
constexpr std::uint32_t BAUD = 115200;

void wait_clear(std::uintptr_t addr, std::uint32_t mask) noexcept {
  for (unsigned i = 0; i < 10000u && (reg(addr) & mask); ++i) {
    // warten, bis die Bits gelöscht sind
  }
}
} // namespace

bool UartHw::init(std::uint32_t peripheral_hz) noexcept {
  // Teiler: f = fPERIPH * STEP / 1024 / (PDIV + 1) / 16, beste Kombination
  // suchen
  std::uint32_t best_step = 0, best_pdiv = 0;
  std::uint64_t best_err = ~0ull;
  const std::uint64_t den = 1024ull * OVERSAMPLING * BAUD;
  for (std::uint64_t step = 1; step <= 1023; ++step) {
    const std::uint64_t p = (peripheral_hz * step + den / 2) / den;
    if (p < 1 || p > 1024)
      continue;
    const std::uint64_t actual =
        peripheral_hz * step / (1024ull * OVERSAMPLING * p);
    const std::uint64_t err = actual > BAUD ? actual - BAUD : BAUD - actual;
    if (err < best_err) {
      best_err = err;
      best_step = std::uint32_t(step);
      best_pdiv = std::uint32_t(p);
    }
  }
  if (best_step == 0 || best_err * 50 > BAUD)
    return false; // > 2 % Abweichung

  // 1. USIC0 takten und aus dem Reset holen (nach dem Reset ist beides aus!)
  reg(SCU_CLK_CGATCLR0) = SCU_USIC0;
  wait_clear(SCU_CLK_CGATSTAT0, SCU_USIC0);
  reg(SCU_RESET_PRCLR0) = SCU_USIC0;
  wait_clear(SCU_RESET_PRSTAT0, SCU_USIC0);

  // 2. Kanal einschalten (MODEN mit Bit-Schutz), für die Konfiguration anhalten
  reg(U + KSCFG) = 3u;
  for (unsigned i = 0; i < 10000u && !(reg(U + KSCFG) & 1u); ++i) {
    // warten, bis MODEN gesetzt ist
  }
  reg(U + CCR) = 0;

  // 3. Baudrate: Fractional Divider, 16 Zeitquanten pro Bit
  reg(U + FDR) = (2u << 14) | best_step;
  reg(U + BRG) = ((best_pdiv - 1u) << 16) | ((OVERSAMPLING - 1u) << 10);

  // 4. Rahmen: PDL = 1 (Ruhepegel High), TRM = 1, 8-Bit-Rahmen und -Wörter,
  //    LSB zuerst
  reg(U + SCTR) = (1u << 1) | (1u << 8) | (7u << 16) | (7u << 24);
  // TDSSM: TDV wird nach dem Übernehmen gelöscht (sonst Dauersenden); TDEN = 01
  reg(U + TCSR) = (1u << 8) | (1u << 10);
  // SMD = 1 (3 Abtastungen), Abtastpunkt 9 von 16, 1 Stoppbit
  reg(U + PCR) = 1u | (9u << 8);
  reg(U + DX0CR) = 1u;      // RX: DX0B = P1.4
  reg(U + PSCR) = 0x1FFFFu; // Statusflags löschen
  reg(U + CCR) = 2u;        // ASC-Modus

  // 5. Pins: P1.4 Eingang mit Pull-up (RX), P1.5 ALT2 Push-Pull (TX)
  xmc::port::set_pin_mode(xmc::port::P1, 4, xmc::port::INPUT_PULL_UP);
  xmc::port::set_pin_mode(xmc::port::P1, 5, xmc::port::output_alt(2));
  return true;
}

bool UartHw::put(char c) noexcept {
  // Warten, bis der Sendepuffer frei ist (mit Abbruch, falls der Kanal nicht
  // läuft)
  for (unsigned t = 0; t < 1000000u; ++t) {
    if ((reg(U + TCSR) & TCSR_TDV) == 0u) {
      reg(U + TBUF0) = static_cast<std::uint8_t>(c);
      return true;
    }
  }
  return false;
}

void UartHw::print(const char *s) noexcept {
  while (*s) {
    if (*s == '\n' && !put('\r'))
      return;
    if (!put(*s++))
      return;
  }
}

void UartHw::print_int(std::int32_t n) noexcept {
  char buffer[12];
  unsigned pos = 0;
  std::uint32_t value;
  if (n < 0) {
    put('-');
    value = std::uint32_t(-(n + 1)) + 1u;
  } else
    value = static_cast<std::uint32_t>(n);
  do {
    buffer[pos++] = char('0' + value % 10u);
    value /= 10u;
  } while (value);
  while (pos)
    put(buffer[--pos]);
}

void UartHw::print_fixed(float value, unsigned decimals) noexcept {
  if (!(value == value)) {
    print("NaN");
    return;
  }
  if (value < 0) {
    put('-');
    value = -value;
  }
  if (value > 100000000.0f) {
    print("OVERFLOW");
    return;
  }
  if (decimals > 4)
    decimals = 4;
  std::uint32_t scale = 1;
  for (unsigned i = 0; i < decimals; ++i)
    scale *= 10;
  const auto rounded = static_cast<std::uint32_t>(value * float(scale) + 0.5f);
  print_int(static_cast<std::int32_t>(rounded / scale));
  if (decimals) {
    put('.');
    auto fraction = rounded % scale;
    for (unsigned i = 0; i < decimals; ++i) {
      scale /= 10;
      put(char('0' + fraction / scale));
      fraction %= scale;
    }
  }
}
