// =============================================================================
// comm_manager.cpp — I2C protobuf communication with ESP32 player stations.
// =============================================================================

#include "comm_manager.h"
#include "config.h"
#include "game_state.h"
#include <Arduino.h>
#include <Wire.h>
#include <pb_encode.h>
#include <pb_decode.h>

namespace {

uint8_t tx_buf[I2C_BUF_SIZE];
uint8_t rx_buf[I2C_BUF_SIZE];

// Map game phase enum to proto enum
catan_GamePhase phaseToProto(GamePhase p) {
    switch (p) {
        case GamePhase::WAITING_FOR_PLAYERS: return catan_GamePhase_PHASE_WAITING_FOR_PLAYERS;
        case GamePhase::BOARD_SETUP:         return catan_GamePhase_PHASE_BOARD_SETUP;
        case GamePhase::NUMBER_REVEAL:       return catan_GamePhase_PHASE_NUMBER_REVEAL;
        case GamePhase::INITIAL_PLACEMENT:   return catan_GamePhase_PHASE_INITIAL_PLACEMENT;
        case GamePhase::PLAYING:             return catan_GamePhase_PHASE_PLAYING;
        case GamePhase::ROBBER:              return catan_GamePhase_PHASE_ROBBER;
        case GamePhase::TRADE:               return catan_GamePhase_PHASE_TRADE;
        case GamePhase::GAME_OVER:           return catan_GamePhase_PHASE_GAME_OVER;
        default:                             return catan_GamePhase_PHASE_WAITING_FOR_PLAYERS;
    }
}

}  // anonymous namespace

namespace comm {

void init() {
    // Wire.begin() is called in main setup (shared I2C bus with expanders)
}

uint8_t detectPlayers() {
    uint8_t mask = 0;
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        uint8_t addr = PLAYER_I2C_BASE + i;
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
            mask |= (1 << i);
        }
    }
    return mask;
}

bool sendToPlayer(uint8_t player_id, const catan_BoardToPlayer& msg) {
    if (player_id >= MAX_PLAYERS) return false;

    pb_ostream_t stream = pb_ostream_from_buffer(tx_buf, sizeof(tx_buf));
    if (!pb_encode(&stream, catan_BoardToPlayer_fields, &msg)) {
        Serial.print(F("[COMM] Encode fail: "));
        Serial.println(PB_GET_ERROR(&stream));
        return false;
    }

    uint8_t addr = PLAYER_I2C_BASE + player_id;
    Wire.beginTransmission(addr);
    Wire.write(tx_buf, stream.bytes_written);
    uint8_t err = Wire.endTransmission();

    return (err == 0);
}

bool readFromPlayer(uint8_t player_id, catan_PlayerToBoard& msg) {
    if (player_id >= MAX_PLAYERS) return false;

    uint8_t addr = PLAYER_I2C_BASE + player_id;
    uint8_t len = Wire.requestFrom(addr, (uint8_t)catan_PlayerToBoard_size);
    if (len == 0) return false;

    uint8_t count = 0;
    while (Wire.available() && count < sizeof(rx_buf)) {
        rx_buf[count++] = Wire.read();
    }
    if (count == 0) return false;

    msg = catan_PlayerToBoard_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(rx_buf, count);
    if (!pb_decode(&stream, catan_PlayerToBoard_fields, &msg)) {
        return false;
    }

    return true;
}

void broadcastToAll(uint8_t connected_mask, const catan_BoardToPlayer& base_msg) {
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (!(connected_mask & (1 << i))) continue;
        sendToPlayer(i, base_msg);
    }
}

void syncStateToAll(uint8_t connected_mask) {
    GamePhase gp = game::phase();

    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (!(connected_mask & (1 << i))) continue;

        catan_BoardToPlayer msg = catan_BoardToPlayer_init_zero;
        msg.type           = catan_MsgType_MSG_GAME_STATE;
        msg.phase          = phaseToProto(gp);
        msg.current_player = game::currentPlayer();
        msg.num_players    = game::numPlayers();
        msg.your_player_id = i;

        // Dice
        msg.die1       = game::lastDie1();
        msg.die2       = game::lastDie2();
        msg.dice_total = game::lastDiceTotal();
        msg.has_rolled = game::hasRolled();

        // Reveal
        msg.reveal_number = game::currentRevealNumber();

        // Setup round
        msg.setup_round = game::setupRound();

        // Victory points for all players
        msg.vp0 = game::victoryPoints(0);
        msg.vp1 = game::victoryPoints(1);
        msg.vp2 = game::victoryPoints(2);
        msg.vp3 = game::victoryPoints(3);

        // Resources for THIS player only
        const PlayerData& pd = game::playerData(i);
        msg.res_lumber = pd.resources[0];
        msg.res_wool   = pd.resources[1];
        msg.res_grain  = pd.resources[2];
        msg.res_brick  = pd.resources[3];
        msg.res_ore    = pd.resources[4];

        // Winner
        uint8_t winner = game::checkWinner();
        msg.winner_id = (winner != NO_PLAYER) ? winner : 0xFF;

        // Display strings based on phase and whose turn it is
        bool my_turn = (game::currentPlayer() == i);

        switch (gp) {
            case GamePhase::WAITING_FOR_PLAYERS:
                snprintf(msg.line1, sizeof(msg.line1), "Waiting %u/%u",
                         game::numPlayers(), (uint8_t)MAX_PLAYERS);
                snprintf(msg.line2, sizeof(msg.line2), "Player %u", i + 1);
                break;

            case GamePhase::BOARD_SETUP:
                strncpy(msg.line1, "Board Setup...", sizeof(msg.line1));
                break;

            case GamePhase::NUMBER_REVEAL:
                snprintf(msg.line1, sizeof(msg.line1), "Number: %u",
                         game::currentRevealNumber());
                strncpy(msg.btn_center, "Next", sizeof(msg.btn_center));
                break;

            case GamePhase::INITIAL_PLACEMENT:
                snprintf(msg.line1, sizeof(msg.line1), "Setup R%u",
                         game::setupRound());
                if (my_turn) {
                    strncpy(msg.line2, "Place piece!", sizeof(msg.line2));
                    strncpy(msg.btn_center, "Done", sizeof(msg.btn_center));
                } else {
                    snprintf(msg.line2, sizeof(msg.line2), "P%u placing",
                             game::currentPlayer() + 1);
                }
                break;

            case GamePhase::PLAYING:
                snprintf(msg.line1, sizeof(msg.line1), "P%u Turn",
                         game::currentPlayer() + 1);
                if (my_turn) {
                    if (!game::hasRolled()) {
                        strncpy(msg.line2, "Roll dice!", sizeof(msg.line2));
                        strncpy(msg.btn_left, "Roll", sizeof(msg.btn_left));
                    } else {
                        snprintf(msg.line2, sizeof(msg.line2), "Dice:%u+%u=%u",
                                 game::lastDie1(), game::lastDie2(),
                                 game::lastDiceTotal());
                        strncpy(msg.btn_left, "Trade", sizeof(msg.btn_left));
                        strncpy(msg.btn_right, "End", sizeof(msg.btn_right));
                    }
                } else {
                    if (game::hasRolled()) {
                        snprintf(msg.line2, sizeof(msg.line2), "Dice:%u+%u=%u",
                                 game::lastDie1(), game::lastDie2(),
                                 game::lastDiceTotal());
                    } else {
                        strncpy(msg.line2, "Waiting...", sizeof(msg.line2));
                    }
                }
                break;

            case GamePhase::ROBBER:
                if (my_turn) {
                    strncpy(msg.line1, "Move Robber!", sizeof(msg.line1));
                    strncpy(msg.line2, "Place on tile", sizeof(msg.line2));
                } else {
                    snprintf(msg.line1, sizeof(msg.line1), "P%u robber",
                             game::currentPlayer() + 1);
                }
                break;

            case GamePhase::TRADE:
                if (my_turn) {
                    strncpy(msg.line1, "Trade", sizeof(msg.line1));
                    strncpy(msg.btn_left, "<Res", sizeof(msg.btn_left));
                    strncpy(msg.btn_center, "Trade", sizeof(msg.btn_center));
                    strncpy(msg.btn_right, "Res>", sizeof(msg.btn_right));
                } else {
                    snprintf(msg.line1, sizeof(msg.line1), "P%u trading",
                             game::currentPlayer() + 1);
                }
                break;

            case GamePhase::GAME_OVER:
                if (winner != NO_PLAYER) {
                    snprintf(msg.line1, sizeof(msg.line1), "P%u Wins!", winner + 1);
                }
                strncpy(msg.line2, "Game Over", sizeof(msg.line2));
                break;
        }

        sendToPlayer(i, msg);
    }
}

}  // namespace comm
