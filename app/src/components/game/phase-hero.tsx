import React, { useEffect, useRef, useState } from 'react';
import { StyleSheet, Text, View } from 'react-native';
import Animated, {
  useSharedValue,
  useAnimatedStyle,
  withTiming,
} from 'react-native-reanimated';

import { Spacing } from '@/constants/theme';
import { DIE_FACES, REVEAL_ORDER } from '@/constants/game';
import { GamePhase, NO_WINNER } from '@/services/proto';
import { PLAYER_FILL } from '@/components/ui/board-map';
import type { BoardState } from '@/services/proto';
import type { useTheme } from '@/hooks/use-theme';

// ── Types ─────────────────────────────────────────────────────────────────

export interface HeroProps {
  gameState: BoardState | null;
  phase: GamePhase;
  myId: number;
  myTurn: boolean;
  numPlayers: number;
  currentPlayer: number;
  hasRolled: boolean;
  connectedCount: number;
  theme: ReturnType<typeof useTheme>;
}

// ── Helper ────────────────────────────────────────────────────────────────

function buildSnakeDisplay(
  numPlayers: number,
  currentPlayer: number,
  isForward: boolean,
): { playerId: number; isCurrent: boolean }[] {
  return Array.from({ length: numPlayers }, (_, i) => {
    const id = isForward
      ? (currentPlayer + i) % numPlayers
      : (currentPlayer + numPlayers - i) % numPlayers;
    return { playerId: id, isCurrent: i === 0 };
  });
}

// ── FadeSlideIn ───────────────────────────────────────────────────────────

/** Plays a fade + slide-up entrance whenever `triggerKey` changes. */
export function FadeSlideIn({ children, triggerKey }: {
  children: React.ReactNode;
  triggerKey: string | number;
}) {
  const opacity = useSharedValue(0);

  useEffect(() => {
    opacity.value = 0;
    opacity.value = withTiming(1, { duration: 280 });
  }, [triggerKey]);

  const animStyle = useAnimatedStyle(() => ({
    opacity: opacity.value,
  }));

  return <Animated.View style={animStyle}>{children}</Animated.View>;
}

// ── LobbySlot ─────────────────────────────────────────────────────────────

const PLAYER_TEXT: readonly string[] = ['#ffffff', '#ffffff', '#ffffff', '#1a1a1a'];

function LobbySlot({ index, connected, isMe, theme }: {
  index: number;
  connected: boolean;
  isMe: boolean;
  theme: ReturnType<typeof useTheme>;
}) {
  const fillColor = PLAYER_FILL[index];
  const textColor = connected ? PLAYER_TEXT[index] : theme.textSecondary;
  return (
    <View
      style={[
        s.lobbySlot,
        {
          backgroundColor: connected ? fillColor : 'transparent',
          borderColor: connected ? fillColor : theme.textSecondary,
        },
      ]}>
      <Text style={[s.lobbySlotText, { color: textColor }]}>
        P{index + 1}
      </Text>
      {isMe && (
        <Text style={[s.lobbySlotSub, { color: textColor }]}>you</Text>
      )}
    </View>
  );
}

// ── TurnChip ──────────────────────────────────────────────────────────────

/** Turn chip that pulses when it becomes the player's turn. */
export function TurnChip({ myTurn, currentPlayer, theme }: {
  myTurn: boolean;
  currentPlayer: number;
  theme: ReturnType<typeof useTheme>;
}) {
  return (
    <View
      style={[s.turnChip, { backgroundColor: myTurn ? theme.primary : theme.backgroundElement }]}>
      <Text style={[s.turnChipText, { color: myTurn ? '#fff' : theme.text }]}>
        {myTurn ? 'Your turn' : `Player ${currentPlayer + 1}'s turn`}
      </Text>
    </View>
  );
}

// ── AnimatedDiceDisplay ───────────────────────────────────────────────────

/** Shuffles through random die faces then springs to the real result when hasRolled becomes true. */
function AnimatedDiceDisplay({ die1, die2, hasRolled, theme }: {
  die1: number;
  die2: number;
  hasRolled: boolean;
  theme: ReturnType<typeof useTheme>;
}) {
  const [d1, setD1]     = useState(die1);
  const [d2, setD2]     = useState(die2);
  const isFirst         = useRef(true);
  const prevHasRolled   = useRef(hasRolled);

  useEffect(() => {
    if (isFirst.current) {
      isFirst.current        = false;
      prevHasRolled.current  = hasRolled;
      setD1(die1);
      setD2(die2);
      return;
    }
    const wasRolled = prevHasRolled.current;
    prevHasRolled.current = hasRolled;

    if (hasRolled && !wasRolled && die1 > 0 && die2 > 0) {
      let count = 0;
      const interval = setInterval(() => {
        setD1(Math.ceil(Math.random() * 6));
        setD2(Math.ceil(Math.random() * 6));
        count++;
        if (count >= 10) {
          clearInterval(interval);
          setD1(die1);
          setD2(die2);
        }
      }, 75);
      return () => clearInterval(interval);
    }
    if (!hasRolled) {
      setD1(die1);
      setD2(die2);
    }
  }, [hasRolled, die1, die2]);

  return (
    <View style={[s.heroCard, { backgroundColor: theme.backgroundElement }]}>
      <Text style={[s.diceDisplay, { color: theme.text }]}>
        {DIE_FACES[d1]}  {DIE_FACES[d2]}
      </Text>
      <Text style={[s.diceTotal, { color: theme.primary }]}>
        {d1 + d2}
      </Text>
    </View>
  );
}

// ── RevealHero ────────────────────────────────────────────────────────────

/** Big number token that bounces in each time the reveal number changes. */
function RevealHero({ gameState, theme }: {
  gameState: BoardState | null;
  theme: ReturnType<typeof useTheme>;
}) {
  const revealNum = gameState?.revealNumber ?? 0;
  const idx       = REVEAL_ORDER.indexOf(revealNum);
  const placed    = idx >= 0 ? idx : REVEAL_ORDER.length;
  return (
    <View style={[s.heroCard, { backgroundColor: theme.backgroundElement }]}>
      <Text style={[s.heroSub, { color: theme.textSecondary }]}>Placing number token</Text>
      <Text style={[s.revealNumber, { color: theme.primary }]}>
        {revealNum > 0 ? revealNum : '—'}
      </Text>
      <View style={s.revealProgress}>
        {REVEAL_ORDER.map((n, i) => (
          <View
            key={n}
            style={[
              s.revealPip,
              {
                backgroundColor: i < placed ? theme.primary : i === placed ? theme.text : theme.background,
                borderColor: i <= placed ? theme.primary : theme.textSecondary,
              },
            ]}
          />
        ))}
      </View>
      <Text style={[s.heroSub, { color: theme.textSecondary }]}>
        {placed} of {REVEAL_ORDER.length} placed
      </Text>
    </View>
  );
}

// ── PhaseHero ─────────────────────────────────────────────────────────────

export function PhaseHero(p: HeroProps) {
  const { gameState, phase, myId, myTurn, numPlayers, currentPlayer, hasRolled, connectedCount, theme } = p;

  switch (phase) {

    case GamePhase.LOBBY: {
      const mask      = gameState?.connectedMask ?? 0;
      const connected = [0, 1, 2, 3].map(i => Boolean(mask & (1 << i)));
      return (
        <View style={[s.heroCard, { backgroundColor: theme.backgroundElement }]}>
          <Text style={[s.heroTitle, { color: theme.text }]}>Waiting for players</Text>
          <View style={s.lobbyGrid}>
            {[0, 1, 2, 3].map(i => (
              <LobbySlot key={i} index={i} connected={connected[i]} isMe={i === myId} theme={theme} />
            ))}
          </View>
          <Text style={[s.heroSub, { color: theme.textSecondary }]}>
            {connectedCount} of 4 connected
          </Text>
        </View>
      );
    }

    case GamePhase.BOARD_SETUP:
      return (
        <View style={[s.heroCard, { backgroundColor: theme.backgroundElement }]}>
          <Text style={s.heroEmoji}>🗺</Text>
          <Text style={[s.heroTitle, { color: theme.text }]}>Board Ready</Text>
          <Text style={[s.heroSub, { color: theme.textSecondary }]}>
            Tap "Start Reveal" when everyone is ready to step through the number tokens.
          </Text>
        </View>
      );

    case GamePhase.NUMBER_REVEAL:
      return <RevealHero gameState={gameState} theme={theme} />;

    case GamePhase.INITIAL_PLACEMENT: {
      const round      = gameState?.setupRound ?? 1;
      const isForward  = round <= 1;
      const direction  = isForward ? '→ forward' : '← reverse';
      return (
        <View style={[s.heroCard, { backgroundColor: theme.backgroundElement }]}>
          <Text style={[s.heroSub, { color: theme.textSecondary }]}>{`Round ${round} of 2  ·  ${direction}`}</Text>
          {myTurn ? (
            <>
              <Text style={[s.heroTitle, { color: theme.primary }]}>Your turn!</Text>
              <Text style={[s.heroSub, { color: theme.textSecondary }]}>
                Place a settlement and a road, then tap Done
              </Text>
            </>
          ) : (
            <>
              <Text style={[s.heroTitle, { color: theme.text }]}>
                Player {currentPlayer + 1}'s turn
              </Text>
              <Text style={[s.heroSub, { color: theme.textSecondary }]}>
                Waiting for them to place…
              </Text>
            </>
          )}
          {numPlayers >= 2 && (
            <View style={s.snakeRow}>
              {buildSnakeDisplay(numPlayers, currentPlayer, isForward).map((slot, i) => (
                <View key={i} style={[s.snakeSlot, { borderColor: slot.isCurrent ? theme.primary : theme.textSecondary }]}>
                  <Text style={[s.snakeSlotText, { color: slot.isCurrent ? theme.primary : theme.textSecondary }]}>
                    P{slot.playerId + 1}
                  </Text>
                </View>
              ))}
            </View>
          )}
        </View>
      );
    }

    case GamePhase.PLAYING: {
      const die1  = gameState?.die1 ?? 0;
      const die2  = gameState?.die2 ?? 0;
      const total = die1 + die2;
      return (
        <View style={s.playingHero}>
          <TurnChip myTurn={myTurn} currentPlayer={currentPlayer} theme={theme} />
          {hasRolled && total > 0 ? (
            <AnimatedDiceDisplay die1={die1} die2={die2} hasRolled={hasRolled} theme={theme} />
          ) : myTurn ? (
            <View style={[s.heroCard, { backgroundColor: theme.backgroundElement }]}>
              <Text style={s.heroEmoji}>🎲</Text>
              <Text style={[s.heroSub, { color: theme.textSecondary }]}>Tap Roll Dice to start your turn</Text>
            </View>
          ) : (
            <View style={[s.heroCard, { backgroundColor: theme.backgroundElement }]}>
              <Text style={[s.heroSub, { color: theme.textSecondary }]}>
                Waiting for Player {currentPlayer + 1} to roll…
              </Text>
            </View>
          )}
        </View>
      );
    }

    case GamePhase.ROBBER:
      return (
        <View style={[s.heroCard, { backgroundColor: theme.backgroundElement }]}>
          <Text style={s.heroEmoji}>🏴‍☠️</Text>
          <Text style={[s.heroTitle, { color: theme.text }]}>Robber!</Text>
          <Text style={[s.heroSub, { color: theme.textSecondary }]}>
            {myTurn
              ? 'Move the robber, then tap Skip Robber when done'
              : `Player ${currentPlayer + 1} is moving the robber…`}
          </Text>
        </View>
      );

    case GamePhase.GAME_OVER: {
      const winner = gameState?.winnerId;
      return (
        <View style={[s.heroCard, { backgroundColor: theme.primary }]}>
          <Text style={s.heroEmoji}>🏆</Text>
          <Text style={[s.heroTitle, { color: '#fff' }]}>
            {winner !== undefined && winner !== NO_WINNER
              ? `Player ${winner + 1} wins!`
              : 'Game Over'}
          </Text>
          {winner === myId && (
            <Text style={[s.heroSub, { color: 'rgba(255,255,255,0.85)' }]}>
              Congratulations!
            </Text>
          )}
        </View>
      );
    }

    default:
      return null;
  }
}

// ── Styles ────────────────────────────────────────────────────────────────

const s = StyleSheet.create({
  heroCard: {
    borderRadius: 20,
    padding: Spacing.four,
    alignItems: 'center',
    gap: Spacing.two,
  },
  heroEmoji: { fontSize: 48, lineHeight: 56 },
  heroTitle: { fontSize: 26, fontWeight: '800', textAlign: 'center' },
  heroSub:   { fontSize: 15, textAlign: 'center', lineHeight: 22 },

  lobbyGrid: { flexDirection: 'row', gap: Spacing.two, marginVertical: Spacing.two },
  lobbySlot: {
    flex: 1,
    borderRadius: 14,
    borderWidth: 2,
    paddingVertical: Spacing.three,
    alignItems: 'center',
    gap: 4,
  },
  lobbySlotText: { fontSize: 15, fontWeight: '700' },
  lobbySlotSub:  { fontSize: 12 },

  snakeRow:      { flexDirection: 'row', gap: 6, marginTop: Spacing.two },
  snakeSlot:     { borderWidth: 2, borderRadius: 10, paddingHorizontal: 10, paddingVertical: 6 },
  snakeSlotText: { fontSize: 13, fontWeight: '700' },

  revealNumber:   { fontSize: 80, fontWeight: '900', lineHeight: 90 },
  revealProgress: { flexDirection: 'row', gap: 5, marginVertical: Spacing.two },
  revealPip:      { width: 10, height: 10, borderRadius: 5, borderWidth: 1.5 },

  playingHero: { gap: Spacing.two },
  turnChip: {
    alignSelf: 'center',
    borderRadius: 20,
    paddingHorizontal: Spacing.three,
    paddingVertical: Spacing.one + 2,
  },
  turnChipText: { fontSize: 15, fontWeight: '700' },
  diceDisplay:  { fontSize: 60, lineHeight: 70 },
  diceTotal:    { fontSize: 44, fontWeight: '900' },
});
