#pragma once
#define CMD_CAN_H_INCLUDED
/**
 * cmd_can.h — CAN bus (bxCAN) for stm32_slave
 *
 * Uses STM32F103 built-in CAN1 controller via HAL.
 * NO external library required — pure STM32duino HAL.
 *
 * Pins (with AFIO remap to avoid USB conflict):
 *   PB8 = CAN1_RX,  PB9 = CAN1_TX
 *   Requires a CAN transceiver (e.g. TJA1050, SN65HVD230).
 *   Connect TJA1050: CANH/CANL to bus, VCC=5V, GND=GND,
 *   TXD→PB9, RXD→PB8, RS pin → GND (always-on high-speed mode).
 *
 * ⚠ CONFLICT: PB8/PB9 overlap with TIM4 CH3/CH4 (PWM).
 *   After CAN:BEGIN, do NOT use PWM on PB8/PB9.
 *
 * Commands (DATA field after SEND:NNN:CCCC:):
 *   CAN:BEGIN:SPEED_KBPS              init CAN in normal mode (needs transceiver)
 *   CAN:BEGIN:SPEED_KBPS:LOOPBACK     init in loopback mode   (NO transceiver needed)
 *   CAN:BEGIN:SPEED_KBPS:SILENT       init in silent/listen-only mode (sniff only)
 *   CAN:TX:ID:HEX               send standard frame (11-bit ID, max 8 bytes)
 *   CAN:TXE:ID:HEX              send extended frame (29-bit ID)
 *   CAN:RX                      read next RX frame → "ID:HEX:EXT:RTR" or NONE
 *   CAN:FILTER:ID:MASK          set acceptance filter (pass frames matching ID & MASK)
 *   CAN:FILTER:OFF              disable filter (accept all)
 *   CAN:STATUS                  bus status, TX/RX error counters
 *   CAN:END                     de-initialize CAN
 *
 * RX result format: "ID:HEX_DATA:EXT:RTR"
 *   ID    = decimal frame ID
 *   HEX   = data bytes (0–16 hex chars = 0–8 bytes)
 *   EXT   = 0 (standard 11-bit) or 1 (extended 29-bit)
 *   RTR   = 0 (data frame) or 1 (remote frame)
 *
 * Bit timing for PCLK1=36 MHz:
 *   All speeds use Seg1=15, Seg2=2, SJW=1 → sample point ~88.9%
 *   125 kbps: PSC=16,  500 kbps: PSC=4
 *   250 kbps: PSC=8,  1000 kbps: PSC=2
 */

#include <stm32f1xx_hal.h>

void sendDone(const String& seq, const String& result);
void sendErr (const String& seq, const String& reason);

// ---------------------------------------------------------------------------
// HAL handle and RX ring buffer
// ---------------------------------------------------------------------------

static CAN_HandleTypeDef hcan1;
static bool canActive       = false;
static bool canSuppressWarn = false;   // set via CAN:NOWARN, cleared via CAN:WARN

static const int CAN_RX_BUF = 16;
struct CanFrame {
    uint32_t id;
    uint8_t  data[8];
    uint8_t  len;
    bool     ext;
    bool     rtr;
};
static CanFrame canRxBuf[CAN_RX_BUF];
static volatile int canRxHead = 0, canRxTail = 0;

// CAN RX interrupt — FIFO0
extern "C" void USB_LP_CAN1_RX0_IRQHandler() {
    HAL_CAN_IRQHandler(&hcan1);
}
extern "C" void CAN1_RX1_IRQHandler() {
    HAL_CAN_IRQHandler(&hcan1);
}

// HAL callback: message received in FIFO0
extern "C" void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef* hcan) {
    CAN_RxHeaderTypeDef hdr;
    uint8_t data[8] = {};
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &hdr, data) == HAL_OK) {
        int next = (canRxHead + 1) % CAN_RX_BUF;
        if (next != canRxTail) {  // not full
            CanFrame& f = canRxBuf[canRxHead];
            f.id  = (hdr.IDE == CAN_ID_EXT) ? hdr.ExtId : hdr.StdId;
            f.len = (uint8_t)hdr.DLC;
            f.ext = (hdr.IDE == CAN_ID_EXT);
            f.rtr = (hdr.RTR == CAN_RTR_REMOTE);
            for (int i = 0; i < (int)hdr.DLC && i < 8; i++) f.data[i] = data[i];
            canRxHead = next;
        }
    }
}

// ---------------------------------------------------------------------------
// Init helpers
// ---------------------------------------------------------------------------

struct CanBitTiming { uint32_t psc; uint32_t seg1; uint32_t seg2; };

// Pre-computed for PCLK1=36 MHz, sample point ~88%
static const CanBitTiming CAN_TIMINGS[] = {
    // speed_kbps=125: 36M / (16*(1+15+2)) = 125000
    { 16, 15, 2 },
    // speed_kbps=250: 36M / (8*(1+15+2))  = 250000
    {  8, 15, 2 },
    // speed_kbps=500: 36M / (4*(1+15+2))  = 500000
    {  4, 15, 2 },
    // speed_kbps=1000: 36M / (2*(1+15+2)) = 1000000
    {  2, 15, 2 },
};
static const uint32_t CAN_SPEEDS[] = { 125, 250, 500, 1000 };

static bool canBegin(uint32_t speedKbps, uint32_t canMode = CAN_MODE_NORMAL) {
    // Find timing entry
    const CanBitTiming* bt = nullptr;
    for (int i = 0; i < 4; i++) {
        if (CAN_SPEEDS[i] == speedKbps) { bt = &CAN_TIMINGS[i]; break; }
    }
    if (!bt) return false;

    // Enable clocks
    __HAL_RCC_CAN1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    // Remap CAN1 to PB8(RX)/PB9(TX)
    __HAL_AFIO_REMAP_CAN1_2();

    // Configure GPIO
    GPIO_InitTypeDef gpio = {};
    gpio.Pin   = GPIO_PIN_9;
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin  = GPIO_PIN_8;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gpio);

    // CAN init
    hcan1.Instance                  = CAN1;
    hcan1.Init.Prescaler            = bt->psc;
    hcan1.Init.Mode                 = canMode;
    hcan1.Init.SyncJumpWidth        = CAN_SJW_1TQ;
    hcan1.Init.TimeSeg1             = (bt->seg1 - 1) << 16;  // HAL uses CAN_BS1_xTQ
    hcan1.Init.TimeSeg2             = (bt->seg2 - 1) << 20;
    hcan1.Init.TimeTriggeredMode    = DISABLE;
    hcan1.Init.AutoBusOff           = ENABLE;
    hcan1.Init.AutoWakeUp           = DISABLE;
    hcan1.Init.AutoRetransmission   = ENABLE;
    hcan1.Init.ReceiveFifoLocked    = DISABLE;
    hcan1.Init.TransmitFifoPriority = DISABLE;

    // HAL BS1/BS2 enums: CAN_BS1_1TQ=0 .. CAN_BS1_16TQ=15
    hcan1.Init.TimeSeg1 = (CAN_BTR_TS1_Msk & ((bt->seg1 - 1) << CAN_BTR_TS1_Pos));
    hcan1.Init.TimeSeg2 = (CAN_BTR_TS2_Msk & ((bt->seg2 - 1) << CAN_BTR_TS2_Pos));

    if (HAL_CAN_Init(&hcan1) != HAL_OK) return false;

    // Accept-all filter (bank 0, mask=0)
    CAN_FilterTypeDef flt = {};
    flt.FilterBank           = 0;
    flt.FilterMode           = CAN_FILTERMODE_IDMASK;
    flt.FilterScale          = CAN_FILTERSCALE_32BIT;
    flt.FilterIdHigh         = 0x0000;
    flt.FilterIdLow          = 0x0000;
    flt.FilterMaskIdHigh     = 0x0000;
    flt.FilterMaskIdLow      = 0x0000;
    flt.FilterFIFOAssignment = CAN_RX_FIFO0;
    flt.FilterActivation     = ENABLE;
    if (HAL_CAN_ConfigFilter(&hcan1, &flt) != HAL_OK) return false;

    // Enable FIFO0 interrupt
    HAL_NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
    HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING);

    return HAL_CAN_Start(&hcan1) == HAL_OK;
}

// ---------------------------------------------------------------------------
// Handler
// ---------------------------------------------------------------------------

static void handleCan(const String& seq, const String& args) {
    String toks[4];
    int n = splitTokens(args, ':', toks, 4);
    if (n < 1) { sendErr(seq, "CAN:BAD_ARGS"); return; }

    const String& sub = toks[0];

    // ----- NOWARN / WARN (suppress / restore transceiver advisory) -----
    if (sub == "NOWARN") {
        canSuppressWarn = true;
        sendDone(seq, "CAN:WARN_SUPPRESSED");
        return;
    }
    if (sub == "WARN") {
        canSuppressWarn = false;
        sendDone(seq, "CAN:WARN_RESTORED");
        return;
    }

    // ----- BEGIN -----
    if (sub == "BEGIN") {
        if (n < 2) { sendErr(seq, "CAN:MISSING_SPEED"); return; }
        uint32_t spd = (uint32_t)toks[1].toInt();

        // Optional 3rd token: LOOPBACK | SILENT | NORMAL (default)
        uint32_t hwMode = CAN_MODE_NORMAL;
        String modeStr  = "NORMAL";
        if (n >= 3) {
            String m = toks[2]; m.toUpperCase();
            if      (m == "LOOPBACK") { hwMode = CAN_MODE_LOOPBACK; modeStr = "LOOPBACK"; }
            else if (m == "SILENT")   { hwMode = CAN_MODE_SILENT;   modeStr = "SILENT";   }
            else if (m != "NORMAL")   { sendErr(seq, "CAN:BAD_MODE"); return; }
        }

        bool ok = canBegin(spd, hwMode);
        if (!ok) { sendErr(seq, "CAN:INIT_FAIL"); return; }
        canActive = true;
        canRxHead = canRxTail = 0;

        String result = "CAN:OK:" + String(spd) + "kbps:" + modeStr + ":PB8/PB9";
        // Append a short advisory for normal mode — visible in any client,
        // not just the Serial Monitor. Suppress with CAN:NOWARN on the slave.
        if (hwMode == CAN_MODE_NORMAL && !canSuppressWarn) {
            result += " [WARN:TRANSCEIVER_REQUIRED:TJA1050_or_SN65HVD230]";
        }
        sendDone(seq, result);
        return;
    }

    if (!canActive) { sendErr(seq, "CAN:NOT_INIT"); return; }

    // ----- TX (standard 11-bit) -----
    if (sub == "TX" || sub == "TXE") {
        if (n < 3) { sendErr(seq, "CAN:MISSING_ARGS"); return; }
        uint32_t id = (uint32_t)strtoul(toks[1].c_str(), nullptr, 0);
        uint8_t  data[8];
        int      len = hexToBytes(toks[2], data, 8);
        if (len < 0) { sendErr(seq, "CAN:BAD_HEX"); return; }
        if (len > 8) { sendErr(seq, "CAN:TOO_LONG"); return; }

        CAN_TxHeaderTypeDef hdr = {};
        bool ext = (sub == "TXE");
        if (ext) {
            hdr.ExtId = id & 0x1FFFFFFFUL;
            hdr.IDE   = CAN_ID_EXT;
        } else {
            hdr.StdId = id & 0x7FFU;
            hdr.IDE   = CAN_ID_STD;
        }
        hdr.RTR = CAN_RTR_DATA;
        hdr.DLC = (uint32_t)len;

        uint32_t txMailbox;
        if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) == 0) {
            sendErr(seq, "CAN:TX_FULL"); return;
        }
        if (HAL_CAN_AddTxMessage(&hcan1, &hdr, data, &txMailbox) != HAL_OK) {
            sendErr(seq, "CAN:TX_FAIL"); return;
        }
        sendDone(seq, "TX:" + String(id) + ":" + String(len) + "B");
        return;
    }

    // ----- RX -----
    if (sub == "RX") {
        if (canRxHead == canRxTail) { sendDone(seq, "NONE"); return; }

        CanFrame& f = canRxBuf[canRxTail];
        canRxTail = (canRxTail + 1) % CAN_RX_BUF;

        String hex = bytesToHex(f.data, f.len);
        String out = String(f.id) + ":" + hex + ":"
                   + String(f.ext ? 1 : 0) + ":"
                   + String(f.rtr ? 1 : 0);
        sendDone(seq, out);
        return;
    }

    // ----- FILTER -----
    if (sub == "FILTER") {
        if (n >= 2 && toks[1].equalsIgnoreCase("OFF")) {
            // Re-apply accept-all
            CAN_FilterTypeDef flt = {};
            flt.FilterBank           = 0;
            flt.FilterMode           = CAN_FILTERMODE_IDMASK;
            flt.FilterScale          = CAN_FILTERSCALE_32BIT;
            flt.FilterFIFOAssignment = CAN_RX_FIFO0;
            flt.FilterActivation     = ENABLE;
            HAL_CAN_ConfigFilter(&hcan1, &flt);
            sendDone(seq, "FILTER:OFF");
            return;
        }
        if (n < 3) { sendErr(seq, "CAN:FILTER:MISSING_ARGS"); return; }
        uint32_t id   = (uint32_t)strtoul(toks[1].c_str(), nullptr, 0);
        uint32_t mask = (uint32_t)strtoul(toks[2].c_str(), nullptr, 0);

        CAN_FilterTypeDef flt = {};
        flt.FilterBank           = 0;
        flt.FilterMode           = CAN_FILTERMODE_IDMASK;
        flt.FilterScale          = CAN_FILTERSCALE_32BIT;
        // Standard ID filter: ID in bits [15:5] of IdHigh
        flt.FilterIdHigh         = (uint16_t)((id  << 5) & 0xFFE0);
        flt.FilterMaskIdHigh     = (uint16_t)((mask << 5) & 0xFFE0);
        flt.FilterFIFOAssignment = CAN_RX_FIFO0;
        flt.FilterActivation     = ENABLE;
        HAL_CAN_ConfigFilter(&hcan1, &flt);

        char buf[24];
        snprintf(buf, sizeof(buf), "FILTER:0x%03lX:0x%03lX",
                 (unsigned long)id, (unsigned long)mask);
        sendDone(seq, String(buf));
        return;
    }

    // ----- STATUS -----
    if (sub == "STATUS") {
        uint32_t err = HAL_CAN_GetError(&hcan1);
        uint32_t state = HAL_CAN_GetState(&hcan1);
        // Read error counters from CAN_ESR register
        uint8_t tec = (uint8_t)((CAN1->ESR >> 16) & 0xFF);
        uint8_t rec = (uint8_t)((CAN1->ESR >> 24) & 0xFF);
        char buf[48];
        snprintf(buf, sizeof(buf), "STATE:%lu:ERR:0x%04lX:TEC:%d:REC:%d",
                 (unsigned long)state, (unsigned long)err, tec, rec);
        sendDone(seq, String(buf));
        return;
    }

    // ----- END -----
    if (sub == "END") {
        HAL_CAN_Stop(&hcan1);
        HAL_CAN_DeInit(&hcan1);
        canActive = false;
        sendDone(seq, "CAN:OFF");
        return;
    }

    sendErr(seq, "CAN:UNKNOWN_SUB");
}
