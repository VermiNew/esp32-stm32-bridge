#pragma once
/**
 * cmd_irq.h — GPIO interrupt event capture for stm32_slave
 *
 * Attaches hardware interrupts to GPIO pins. Events are counted in ISRs
 * and reported back to the master on demand (poll-based to avoid
 * unsolicited frames that would confuse the protocol state machine).
 *
 * Commands (DATA field after SEND:NNN:CCCC:):
 *   IRQ:ATTACH:PIN:MODE    attach interrupt  MODE = RISE | FALL | CHANGE
 *   IRQ:DETACH:PIN         detach interrupt from pin
 *   IRQ:DETACH:ALL         detach all
 *   IRQ:POLL               get and clear all pending events
 *                           → "A0:3,B5:1" (pin:count pairs) or NONE
 *   IRQ:LIST               list attached pins and their modes → or NONE
 *
 * Up to MAX_IRQ_SLOTS pins can be monitored simultaneously.
 *
 * IMPORTANT: ISR callbacks on STM32duino must be extremely short (just
 * increment a counter). No String operations inside ISRs.
 */

void sendDone(const String& seq, const String& result);
void sendErr (const String& seq, const String& reason);

static const int MAX_IRQ_SLOTS = 8;

// ---- ISR-safe state (volatile) ----
static volatile int32_t irqCounts[MAX_IRQ_SLOTS] = {};
static int              irqPinNums[MAX_IRQ_SLOTS];   // flat pin number, -1 = free
static uint8_t          irqModes[MAX_IRQ_SLOTS];     // RISING/FALLING/CHANGE
static char             irqPinNames[MAX_IRQ_SLOTS][4]; // "A0".."C15"

// Pre-defined ISR functions for each slot (C++ lambdas can't be used with
// attachInterrupt on STM32duino without std::function overhead).
static void irqISR0() { irqCounts[0]++; }
static void irqISR1() { irqCounts[1]++; }
static void irqISR2() { irqCounts[2]++; }
static void irqISR3() { irqCounts[3]++; }
static void irqISR4() { irqCounts[4]++; }
static void irqISR5() { irqCounts[5]++; }
static void irqISR6() { irqCounts[6]++; }
static void irqISR7() { irqCounts[7]++; }

typedef void (*IrqFn)();
static const IrqFn irqFns[MAX_IRQ_SLOTS] = {
    irqISR0, irqISR1, irqISR2, irqISR3,
    irqISR4, irqISR5, irqISR6, irqISR7
};

static void irqInit() {
    for (int i = 0; i < MAX_IRQ_SLOTS; i++) {
        irqPinNums[i] = -1;
        irqCounts[i]  = 0;
        irqModes[i]   = 0;
        irqPinNames[i][0] = '\0';
    }
}

// Find slot holding pin, or -1 if not found.
static int irqFindPin(int pin) {
    for (int i = 0; i < MAX_IRQ_SLOTS; i++)
        if (irqPinNums[i] == pin) return i;
    return -1;
}

// Find a free slot, or -1.
static int irqFreeSlot() {
    for (int i = 0; i < MAX_IRQ_SLOTS; i++)
        if (irqPinNums[i] < 0) return i;
    return -1;
}

static void handleIrq(const String& seq, const String& args) {
    String toks[4];
    int n = splitTokens(args, ':', toks, 4);
    if (n < 1) { sendErr(seq, "IRQ:BAD_ARGS"); return; }

    const String& sub = toks[0];

    // ----- ATTACH -----
    if (sub == "ATTACH") {
        if (n < 3) { sendErr(seq, "IRQ:MISSING_ARGS"); return; }
        toks[1].toUpperCase();
        int pin = parsePin(toks[1]);
        if (pin < 0) { sendErr(seq, "IRQ:BAD_PIN"); return; }

        String modeStr = toks[2]; modeStr.toUpperCase();
        int mode;
        if      (modeStr == "RISE"   || modeStr == "RISING")  mode = RISING;
        else if (modeStr == "FALL"   || modeStr == "FALLING") mode = FALLING;
        else if (modeStr == "CHANGE")                         mode = CHANGE;
        else { sendErr(seq, "IRQ:BAD_MODE"); return; }

        // Re-use existing slot or grab a free one
        int slot = irqFindPin(pin);
        if (slot < 0) {
            slot = irqFreeSlot();
            if (slot < 0) { sendErr(seq, "IRQ:NO_SLOTS"); return; }
        } else {
            detachInterrupt(digitalPinToInterrupt(pin));
        }

        irqPinNums[slot] = pin;
        irqModes[slot]   = (uint8_t)mode;
        irqCounts[slot]  = 0;
        // Store the pin name for reporting
        const char ports[] = "ABC";
        snprintf(irqPinNames[slot], sizeof(irqPinNames[slot]), "%c%d",
                 ports[pin / 16], pin % 16);

        pinMode(pin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(pin), irqFns[slot], mode);

        sendDone(seq, "ATTACHED:" + toks[1] + ":" + modeStr);
        return;
    }

    // ----- DETACH -----
    if (sub == "DETACH") {
        if (n < 2) { sendErr(seq, "IRQ:MISSING_PIN"); return; }

        if (toks[1].equalsIgnoreCase("ALL")) {
            for (int i = 0; i < MAX_IRQ_SLOTS; i++) {
                if (irqPinNums[i] >= 0) {
                    detachInterrupt(digitalPinToInterrupt(irqPinNums[i]));
                    irqPinNums[i] = -1;
                    irqCounts[i]  = 0;
                }
            }
            sendDone(seq, "ALL_DETACHED");
            return;
        }

        toks[1].toUpperCase();
        int pin = parsePin(toks[1]);
        if (pin < 0) { sendErr(seq, "IRQ:BAD_PIN"); return; }

        int slot = irqFindPin(pin);
        if (slot < 0) { sendErr(seq, "IRQ:NOT_ATTACHED"); return; }

        detachInterrupt(digitalPinToInterrupt(pin));
        irqPinNums[slot] = -1;
        irqCounts[slot]  = 0;
        sendDone(seq, "DETACHED:" + toks[1]);
        return;
    }

    // ----- POLL (get and clear pending event counts) -----
    if (sub == "POLL") {
        String result;
        for (int i = 0; i < MAX_IRQ_SLOTS; i++) {
            if (irqPinNums[i] < 0) continue;
            int32_t cnt = irqCounts[i];
            if (cnt == 0) continue;
            // Atomic clear (disable IRQ briefly)
            noInterrupts();
            irqCounts[i] -= cnt;
            interrupts();

            if (result.length()) result += ',';
            result += String(irqPinNames[i]) + ':' + String(cnt);
        }
        sendDone(seq, result.length() ? result : "NONE");
        return;
    }

    // ----- LIST -----
    if (sub == "LIST") {
        const char* modeNames[] = {"LOW", "CHANGE", "RISING", "FALLING", "HIGH"};
        String result;
        for (int i = 0; i < MAX_IRQ_SLOTS; i++) {
            if (irqPinNums[i] < 0) continue;
            if (result.length()) result += ',';
            // mode: RISING=2, FALLING=3, CHANGE=1 in Arduino
            const char* m = "?";
            if      (irqModes[i] == RISING)  m = "RISE";
            else if (irqModes[i] == FALLING) m = "FALL";
            else if (irqModes[i] == CHANGE)  m = "CHANGE";
            result += String(irqPinNames[i]) + ':' + m;
        }
        sendDone(seq, result.length() ? result : "NONE");
        return;
    }

    sendErr(seq, "IRQ:UNKNOWN_SUB");
}
