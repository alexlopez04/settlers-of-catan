// =============================================================================
// comm_manager.cpp — v4 Envelope-based UART link to the bridge (Serial1).
//
// Wire framing: [0xCA magic][len : uint8][nanopb Envelope : len bytes]
//
// Receive state machine:
//   HUNT  — discarding bytes until 0xCA is seen
//   LEN   — next byte is the payload length
//   BODY  — accumulating `len` payload bytes, then decoding Envelope.
//
// Reliability: a small per-sender sequence table deduplicates retries. Any
// reliable envelope carrying a PlayerInput is auto-Acked before being handed
// to the caller.
// =============================================================================

#include "comm_manager.h"
#include "config.h"
#include "catan_wire.h"
#include "catan_log.h"
#include <Arduino.h>

namespace {

// ── TX ───────────────────────────────────────────────────────────────────────
uint8_t  tx_buf[CATAN_MAX_FRAME];
uint32_t tx_seq = 0;   // board's own outgoing sequence counter

// ── RX state machine ─────────────────────────────────────────────────────────
enum class RxState : uint8_t { HUNT, LEN, BODY };
uint8_t  rx_buf[CATAN_MAX_FRAME];
uint8_t  rx_pos    = 0;
uint8_t  rx_expect = 0;   // total frame bytes expected (HEADER + payload)
RxState  rx_state  = RxState::HUNT;

// ── Dedup table: last seen seq per player slot (0..3) ────────────────────────
uint32_t last_seq_from[4] = {0, 0, 0, 0};

// ── Public diagnostic counters ───────────────────────────────────────────────
comm::Stats g_stats = {0, 0, 0, 0, 0, 0, 0};

static inline uint32_t nextTxSeq() {
    if (++tx_seq == 0) tx_seq = 1;  // seq 0 reserved for "unset"
    return tx_seq;
}

static bool writeFramedEnvelope(const catan_Envelope& env) {
    const size_t n = catan_wire_encode(&env, tx_buf, sizeof(tx_buf));
    if (n == 0) {
        LOGE("COMM", "envelope encode failed (body_tag=%u)", (unsigned)env.which_body);
        return false;
    }
    Serial1.write(tx_buf, n);
    g_stats.tx_bytes += (uint32_t)n;
    return true;
}

}  // namespace

namespace comm {

const Stats& stats() { return g_stats; }

void init() {
    Serial1.begin(BRIDGE_SERIAL_BAUD);
    LOGI("COMM", "Serial1 bridge link up @%lu baud (proto v%u)",
         (unsigned long)BRIDGE_SERIAL_BAUD, (unsigned)CATAN_PROTO_VERSION);
}

bool sendEnvelopeBody(catan_Envelope& env, bool reliable) {
    env.proto_version   = CATAN_PROTO_VERSION;
    env.sender_id       = CATAN_NODE_BOARD;
    env.sequence_number = nextTxSeq();
    env.timestamp_ms    = millis();
    env.reliable        = reliable;
    env.message_type    = (catan_MessageType)env.which_body;
    return writeFramedEnvelope(env);
}

bool sendBoardState(const catan_BoardState& state) {
    catan_Envelope env = catan_Envelope_init_zero;
    env.which_body = catan_Envelope_board_state_tag;
    env.body.board_state = state;
    if (!sendEnvelopeBody(env, /*reliable=*/false)) return false;
    g_stats.tx_boardstate++;
    LOGD("COMM", "tx BoardState #%lu seq=%lu phase=%u cur=%u",
         (unsigned long)g_stats.tx_boardstate,
         (unsigned long)env.sequence_number,
         (unsigned)state.phase, (unsigned)state.current_player);
    return true;
}

bool sendAck(uint32_t to_sender, uint32_t seq) {
    catan_Envelope env = catan_Envelope_init_zero;
    env.which_body = catan_Envelope_ack_tag;
    env.body.ack.ack_sender = to_sender;
    env.body.ack.ack_seq    = seq;
    if (!sendEnvelopeBody(env, /*reliable=*/false)) return false;
    g_stats.tx_ack++;
    LOGD("COMM", "tx Ack -> %lu seq=%lu", (unsigned long)to_sender, (unsigned long)seq);
    return true;
}

bool pollPlayerInput(catan_PlayerInput& out, uint32_t& out_sender) {
    while (Serial1.available()) {
        uint8_t b = (uint8_t)Serial1.read();
        g_stats.rx_bytes++;

        switch (rx_state) {
            case RxState::HUNT:
                if (b == CATAN_WIRE_MAGIC) {
                    rx_buf[0] = b;
                    rx_pos    = 1;
                    rx_state  = RxState::LEN;
                }
                break;

            case RxState::LEN:
                if (b == 0 || b > CATAN_MAX_PAYLOAD) {
                    LOGW("COMM", "bad len=%u, resyncing", (unsigned)b);
                    g_stats.rx_frames_bad++;
                    rx_state = RxState::HUNT;
                } else {
                    rx_buf[1]  = b;
                    rx_expect  = (uint8_t)(CATAN_FRAME_HEADER + b);
                    rx_pos     = CATAN_FRAME_HEADER;
                    rx_state   = RxState::BODY;
                }
                break;

            case RxState::BODY:
                rx_buf[rx_pos++] = b;
                if (rx_pos >= rx_expect) {
                    rx_state = RxState::HUNT;
                    catan_Envelope env;
                    if (!catan_wire_decode(rx_buf, rx_pos, &env)) {
                        LOGW("COMM", "decode failed (len=%u)", (unsigned)rx_buf[1]);
                        g_stats.rx_frames_bad++;
                        break;
                    }
                    if (!catan_wire_envelope_valid(&env)) {
                        LOGW("COMM", "envelope rejected (proto_v=%u sender=%lu body=%u)",
                             (unsigned)env.proto_version,
                             (unsigned long)env.sender_id,
                             (unsigned)env.which_body);
                        g_stats.rx_frames_bad++;
                        break;
                    }
                    g_stats.rx_frames_ok++;

                    uint8_t pidx = catan_node_player_index(env.sender_id);
                    if (env.reliable && pidx < 4) {
                        if (env.sequence_number <= last_seq_from[pidx]) {
                            LOGD("COMM", "dup seq=%lu from=%lu, re-acking",
                                 (unsigned long)env.sequence_number,
                                 (unsigned long)env.sender_id);
                            g_stats.rx_dups++;
                            sendAck(env.sender_id, env.sequence_number);
                            break;
                        }
                        last_seq_from[pidx] = env.sequence_number;
                        sendAck(env.sender_id, env.sequence_number);
                    }

                    if (env.which_body == catan_Envelope_player_input_tag) {
                        out        = env.body.player_input;
                        out_sender = env.sender_id;
                        return true;
                    }
                    // Sync requests / acks silently consumed for now.
                    break;
                }
                break;
        }
    }
    return false;
}

}  // namespace comm
