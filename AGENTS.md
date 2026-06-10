# Supermikrokontroler — Contributor / Agent Guide

ESP32 master controls an STM32 Blue Pill slave over UART.
Three Arduino sketches + flashing tooling.

---

## Iterative workflow

Never do a "waterfall" — never write large amounts of code in a single step.

### Cycle

1. **Plan** — Define a small, concrete mini-step.
2. **Pick the simplest part** — Build on verified ground.
3. **Write code** — Implement only that small piece.
4. **Read your own code** — Review before running anything.
5. **Verify:**
   - **Compile** — `arduino-cli compile` for the affected sketch (FQBNs below).
     If unavailable, do a careful manual review and state compilation must be
     confirmed in Arduino IDE.
   - **Hardware check** — the human flashes and observes real behavior
     (LED blinks, Serial Monitor output, `ping` → `PONG`). The agent cannot
     do this step; it must ask the human and wait for the result.
6. **Fix or commit:**
   - Broken → fix → back to step 5.
   - Working → commit with a Conventional Commit message.
7. **Repeat** — pick the next small piece.

### FQBNs

```bash
# STM32 Blue Pill
arduino-cli compile --fqbn STMicroelectronics:stm32:GenF1:pnum=BLUEPILL_F103C8 stm32_slave

# ESP32
arduino-cli compile --fqbn esp32:esp32:esp32 esp32_master
arduino-cli compile --fqbn esp32:esp32:esp32 esp32_flasher
```

Required cores: `esp32:esp32` (Espressif) and `STMicroelectronics:stm32` (stm32duino).

---

## Conventional Commits

```
<type>(<scope>): <description>
```

Scopes: `stm32-slave`, `esp32-master`, `esp32-flasher`, `tooling`, `docs`.

Types: `feat`, `fix`, `style`, `refactor`, `docs`, `chore`, `test`.

A commit is a **reward for working (compilable) code**, not for written code.

---

## Protocol reference

Frame format: `TYPE:SEQ:DATA\n`  
SEQ = 3-digit zero-padded (001..999, wraps to 001). Both sides must agree byte-for-byte.

| Frame | Direction | Meaning |
|-------|-----------|---------|
| `SEND:NNN:CMD` | M→S | Execute CMD |
| `RECV:NNN` | S→M | Fast ACK |
| `DONE:NNN:RESULT` | S→M | Result |
| `BUSY:NNN` | S→M | Reply to POLL |
| `POLL:NNN` | M→S | Query status |
| `FREE:NNN` | M→S | Release slot |
| `ERR:NNN:REASON` | either | Error |
| `PING` / `PONG` | M→S / S→M | Link test |
| `RESET` | either | Resync |

Master timings: `TIMEOUT_RECV=300ms`, `TIMEOUT_DONE=2000ms`,
`TIMEOUT_POLL=400ms`, `MAX_RETRY=3`, `MAX_POLLS=15`.

---

## Pin naming (slave)

Token format: `A<n>`, `B<n>`, `C<n>` where n=0..15.  
Maps to STM32duino flat pin index: `pin = (port - 'A') * 16 + n`.  
So PA0=0, PB0=16, PC13=45.

---

## Hardware safety

- **NEVER connect 5V to any STM32 pin.** 3.3V only.
- Bootloader UART: USART1 (PA9/PA10), 115200 **8E1** (even parity).
- Application UART: same pins, 115200 **8N1**. Do NOT mix them.
- BOOT0=1 → bootloader (PC13 does NOT blink).
- BOOT0=0 → run application (PC13 blinks 3× on boot).
- ESP32 bridge uses GPIO16 (RX2) / GPIO17 (TX2) — NOT the "TX/RX" silkscreen pins.

---

## Code hygiene

- **Everything in code is English**: identifiers, comments, Serial output, README.
  The only Polish is in the live chat with the human.
- All source files are UTF-8, no BOM.
- No `delay()` in main loops — use `millis()`.
- No new libraries without asking the human.
- Comments explain WHY, not what.
- Never `git add .` or `git add -A` — stage only intentionally changed files.
- Never run destructive git commands without explicit approval.
- Read existing code before modifying it.

---

## Safety checklist before every commit

- [ ] Sketch compiles cleanly for its target FQBN.
- [ ] No warnings introduced.
- [ ] No `delay()` in `loop()`.
- [ ] Protocol frame format unchanged (unless this is a protocol change commit).
- [ ] Hardware safety advice is accurate (3.3V, 8E1 vs 8N1, correct GPIO pins).
