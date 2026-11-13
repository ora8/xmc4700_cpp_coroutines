#include "spi_hw.hpp"
#include "xmc_regs.hpp"

namespace {
using xmc::reg;

constexpr std::uintptr_t U = 0x48024000u; // USIC2_CH0

// SCU: Takt-Gating und Reset von USIC2 (Register 1, jeweils Bit 8)
constexpr std::uintptr_t SCU_CLK_CGATSTAT1 = 0x5000464Cu;
constexpr std::uintptr_t SCU_CLK_CGATCLR1 = 0x50004654u;
constexpr std::uintptr_t SCU_RESET_PRSTAT1 = 0x50004418u;
constexpr std::uintptr_t SCU_RESET_PRCLR1 = 0x50004420u;
constexpr std::uint32_t SCU_USIC2 = 1u << 8;

// Kanal-Register
constexpr std::uintptr_t KSCFG = 0x0C, FDR = 0x10, BRG = 0x14, DX0CR = 0x1C,
                         SCTR = 0x34, TCSR = 0x38, PCR = 0x3C, CCR = 0x40,
                         PSCR = 0x4C, RBUFSR = 0x50, RBUF = 0x54, TBUF0 = 0x80;

constexpr std::uint32_t OVERSAMPLING = 2; // SSC: 2 Zeitquanten pro Bit
constexpr std::uint32_t TCSR_TDV = 1u << 7;
constexpr std::uint32_t RBUFSR_RDV = (1u << 13) | (1u << 14);

// SCTR: MSB zuerst, Ruhepegel 1, TRM = 1, Rahmen unbegrenzt, 8-Bit-Wörter
constexpr std::uint32_t SCTR_SPI = (1u << 0) | (1u << 1) | (1u << 8) | (0x3Fu << 16) | (7u << 24);
// TCSR: HPCMD, TDSSM (Single-Shot), TDEN = 01
constexpr std::uint32_t TCSR_SPI = (1u << 4) | (1u << 8) | (1u << 10);
// PCR (SSC): MSLSEN, SELCTR, SELINV, FEM. Das interne Slave-Select-Signal ist auf keinen
// Pin geführt; Chip-Select übernimmt ein GPIO.
constexpr std::uint32_t PCR_SPI = 0xFu;
// DX0CR: INSW = 1 (Eingang direkt auf die Schiebeeinheit; ohne das liest der Empfänger
// nur Nullen), DSEL = 2 (DX0C = P3.7) bzw. 6 (DX0G = eigener DOUT0, Rückschleife)
constexpr std::uint32_t DX0CR_INSW = 1u << 4;
constexpr std::uint32_t DX0_DSEL_P3_7 = 2;
constexpr std::uint32_t DX0_DSEL_LOOPBACK = 6;

/// BRG.SCLKCFG (Bits 31:30) je SPI-Modus (Zuordnung wie XMC-for-Arduino)
constexpr std::uint32_t sclkcfg(SpiHw::Mode mode) noexcept {
  switch (mode) {
  case SpiHw::Mode::Mode0: return 0b10; // Ruhepegel 0, Verzögerung an
  case SpiHw::Mode::Mode1: return 0b00; // Ruhepegel 0, ohne Verzögerung
  case SpiHw::Mode::Mode2: return 0b11; // Ruhepegel 1, Verzögerung an
  case SpiHw::Mode::Mode3: return 0b01; // Ruhepegel 1, ohne Verzögerung
  }
  return 0b10;
}

void wait_clear(std::uintptr_t addr, std::uint32_t mask) noexcept {
  for (unsigned i = 0; i < 10000u && (reg(addr) & mask); ++i) {
    // warten, bis die Bits gelöscht sind
  }
}
} // namespace

namespace {
struct Divider {
  std::uint32_t step = 0, pdiv = 0, actual = 0;
};

/// Teiler für f = fPERIPH * STEP / 1024 / (PDIV + 1) / 2. Nur Werte <= Wunschfrequenz,
/// damit das angeschlossene Gerät nicht übertaktet wird. step == 0: nicht erreichbar.
Divider calc_divider(std::uint32_t peripheral_hz, std::uint32_t frequency) noexcept {
  Divider best;
  if (frequency == 0) return best;
  const std::uint64_t f = peripheral_hz;
  const std::uint64_t den = 1024ull * OVERSAMPLING * frequency;
  for (std::uint64_t step = 1; step <= 1023; ++step) {
    const std::uint64_t p = (f * step + den - 1) / den; // aufrunden
    if (p < 1 || p > 1024) continue;
    const std::uint64_t actual = f * step / (1024ull * OVERSAMPLING * p);
    if (actual <= frequency && actual > best.actual) {
      best = {std::uint32_t(step), std::uint32_t(p), std::uint32_t(actual)};
    }
  }
  return best;
}
} // namespace

bool SpiHw::set_frequency(std::uint32_t frequency) noexcept {
  const Divider div = calc_divider(peripheral_hz_, frequency);
  if (div.step == 0) return false;
  // Nur zwischen Übertragungen aufrufen; alle Funktionen kehren erst zurück, wenn der
  // Bus frei ist. SCLKCFG (Modus) bleibt erhalten.
  reg(U + FDR) = (2u << 14) | div.step;
  reg(U + BRG) = (sclkcfg(mode_) << 30) | ((div.pdiv - 1u) << 16) | ((OVERSAMPLING - 1u) << 10);
  frequency_ = div.actual;
  return true;
}

bool SpiHw::init(std::uint32_t peripheral_hz, const Config &config) noexcept {
  const Divider div = calc_divider(peripheral_hz, config.frequency);
  if (div.step == 0) return false;
  const std::uint32_t best_step = div.step, best_pdiv = div.pdiv;
  peripheral_hz_ = peripheral_hz;
  mode_ = config.mode;

  // 1. USIC2 takten und aus dem Reset holen
  reg(SCU_CLK_CGATCLR1) = SCU_USIC2;
  wait_clear(SCU_CLK_CGATSTAT1, SCU_USIC2);
  reg(SCU_RESET_PRCLR1) = SCU_USIC2;
  wait_clear(SCU_RESET_PRSTAT1, SCU_USIC2);

  // 2. Kanal einschalten, für die Konfiguration anhalten
  reg(U + KSCFG) = 3u;
  for (unsigned i = 0; i < 10000u && !(reg(U + KSCFG) & 1u); ++i) {
    // warten, bis MODEN gesetzt ist
  }
  reg(U + CCR) = 0;

  // 3. Takt und Modus
  reg(U + FDR) = (2u << 14) | best_step;
  reg(U + BRG) = (sclkcfg(config.mode) << 30) | ((best_pdiv - 1u) << 16) |
                 ((OVERSAMPLING - 1u) << 10);

  // 4. Rahmen, Senden, Protokoll, Eingang
  reg(U + SCTR) = SCTR_SPI;
  reg(U + TCSR) = TCSR_SPI;
  reg(U + PCR) = PCR_SPI;
  reg(U + DX0CR) = DX0CR_INSW | (config.loopback ? DX0_DSEL_LOOPBACK : DX0_DSEL_P3_7);

  // 5. Status löschen, SSC-Modus starten
  reg(U + PSCR) = 0x1FFFFu;
  reg(U + CCR) = 1u;

  // 6. Pins: MISO Eingang mit Pull-up, MOSI und SCK ALT1
  xmc::port::set_pin_mode(xmc::port::P3, 7, xmc::port::INPUT_PULL_UP);
  xmc::port::set_pin_mode(xmc::port::P3, 8, xmc::port::output_alt(1));
  xmc::port::set_pin_mode(xmc::port::P3, 9, xmc::port::output_alt(1));

  frequency_ = div.actual;
  return true;
}

void SpiHw::transfer(const std::uint8_t *tx, std::uint8_t *rx, std::size_t n) noexcept {
  // Reste aus früheren Übertragungen verwerfen
  while (reg(U + RBUFSR) & RBUFSR_RDV) {
    (void)xmc::read(U + RBUF);
  }
  // Höchstens zwei Bytes unterwegs (Sendepuffer + Schieberegister): Der Bus läuft ohne
  // Pause, und der doppelte Empfangspuffer (RBUF0/RBUF1) läuft nie über.
  std::size_t sent = 0, received = 0;
  while (received < n) {
    if (sent < n && sent - received < 2 && !(reg(U + TCSR) & TCSR_TDV)) {
      reg(U + TBUF0) = tx ? tx[sent] : 0u;
      ++sent;
    }
    if (reg(U + RBUFSR) & RBUFSR_RDV) {
      const auto b = static_cast<std::uint8_t>(reg(U + RBUF));
      if (rx) rx[received] = b;
      ++received;
    }
  }
}

void SpiHw::write_repeated(std::uint8_t byte, std::size_t n) noexcept {
  while (reg(U + RBUFSR) & RBUFSR_RDV) {
    (void)xmc::read(U + RBUF);
  }
  std::size_t sent = 0, received = 0;
  while (received < n) {
    if (sent < n && sent - received < 2 && !(reg(U + TCSR) & TCSR_TDV)) {
      reg(U + TBUF0) = byte;
      ++sent;
    }
    if (reg(U + RBUFSR) & RBUFSR_RDV) {
      (void)xmc::read(U + RBUF);
      ++received;
    }
  }
}
