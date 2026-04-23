import { router } from 'expo-router';
import React, { useCallback, useEffect, useState } from 'react';
import {
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { useBle, ResourceCounts } from '@/context/ble-context';
import { Spacing } from '@/constants/theme';
import { useTheme } from '@/hooks/use-theme';
import { BoardState, GamePhase, NO_WINNER, PlayerAction } from '@/services/proto';

// ── Constants ─────────────────────────────────────────────────────────────

const PHASE_LABEL: Record<GamePhase, string> = {
  [GamePhase.LOBBY]: 'Lobby',
  [GamePhase.BOARD_SETUP]: 'Board Setup',
  [GamePhase.NUMBER_REVEAL]: 'Number Reveal',
  [GamePhase.INITIAL_PLACEMENT]: 'Initial Placement',
  [GamePhase.PLAYING]: 'Playing',
  [GamePhase.ROBBER]: 'Robber',
  [GamePhase.GAME_OVER]: 'Game Over',
};

type ResKey = keyof ResourceCounts;
const RESOURCES: { key: ResKey; emoji: string; label: string }[] = [
  { key: 'lumber', emoji: '🪵', label: 'Lumber' },
  { key: 'wool',   emoji: '🐑', label: 'Wool'   },
  { key: 'grain',  emoji: '🌾', label: 'Grain'  },
  { key: 'brick',  emoji: '🧱', label: 'Brick'  },
  { key: 'ore',    emoji: '⛏',  label: 'Ore'    },
];

const DIE_FACES = ['', '⚀', '⚁', '⚂', '⚃', '⚄', '⚅'];

// The number-reveal order matches the firmware kRevealOrder array.
const REVEAL_ORDER = [2, 3, 4, 5, 6, 8, 9, 10, 11, 12];

// ── Button specs ──────────────────────────────────────────────────────────

interface ButtonSpec {
  label: string;
  action: PlayerAction;
  enabled: boolean;
  primary?: boolean; // highlight with primary colour
}

type ButtonRow = [ButtonSpec | null, ButtonSpec | null, ButtonSpec | null];

function buttonsForPhase(
  phase: GamePhase,
  myTurn: boolean,
  hasRolled: boolean,
  connectedCount: number,
): ButtonRow {
  switch (phase) {
    case GamePhase.LOBBY: {
      const canStart = connectedCount >= 1;
      return [
        { label: 'Ready',      action: PlayerAction.READY,      enabled: true },
        { label: 'Start Game', action: PlayerAction.START_GAME, enabled: canStart, primary: canStart },
        null,
      ];
    }
    case GamePhase.BOARD_SETUP:
      return [
        null,
        { label: 'Start Reveal →', action: PlayerAction.NEXT_NUMBER, enabled: true, primary: true },
        null,
      ];
    case GamePhase.NUMBER_REVEAL:
      return [
        null,
        { label: 'Next Number →', action: PlayerAction.NEXT_NUMBER, enabled: true, primary: true },
        null,
      ];
    case GamePhase.INITIAL_PLACEMENT:
      return [
        null,
        {
          label: 'Placement Done',
          action: PlayerAction.PLACE_DONE,
          enabled: myTurn,
          primary: myTurn,
        },
        null,
      ];
    case GamePhase.PLAYING:
      return [
        {
          label: 'Roll Dice',
          action: PlayerAction.ROLL_DICE,
          enabled: myTurn && !hasRolled,
          primary: myTurn && !hasRolled,
        },
        null,
        {
          label: 'End Turn',
          action: PlayerAction.END_TURN,
          enabled: myTurn && hasRolled,
          primary: myTurn && hasRolled,
        },
      ];
    case GamePhase.ROBBER:
      return [
        null,
        {
          label: 'Skip Robber',
          action: PlayerAction.SKIP_ROBBER,
          enabled: myTurn,
          primary: myTurn,
        },
        null,
      ];
    default:
      return [null, null, null];
  }
}

// ── Main screen ───────────────────────────────────────────────────────────

export default function GameScreen() {
  const theme = useTheme();
  const {
    connectionState,
    connectedName,
    playerId,
    gameState,
    sendAction,
    sendReport,
    disconnect,
  } = useBle();

  const [localVp, setLocalVp] = useState(0);
  const [resources, setResources] = useState<ResourceCounts>({
    lumber: 0, wool: 0, grain: 0, brick: 0, ore: 0,
  });
  const [reporting, setReporting] = useState(false);

  useEffect(() => {
    if (connectionState === 'idle') router.replace('/');
  }, [connectionState]);

  const handleAction = useCallback(
    async (action: PlayerAction) => {
      try { await sendAction(action); } catch { /* ignore */ }
    },
    [sendAction],
  );

  const handleReport = useCallback(async () => {
    setReporting(true);
    try { await sendReport(localVp, resources); } catch { /* ignore */ }
    finally { setReporting(false); }
  }, [localVp, resources, sendReport]);

  // ── Derived values ───────────────────────────────────────────────────────

  const phase         = gameState?.phase ?? GamePhase.LOBBY;
  const myId          = playerId ?? 0;
  const numPlayers    = gameState?.numPlayers ?? 0;
  const currentPlayer = gameState?.currentPlayer ?? 0;
  const myTurn        = gameState != null && currentPlayer === myId;
  const hasRolled     = gameState?.hasRolled ?? false;
  const isGameOver    = phase === GamePhase.GAME_OVER;

  const connectedMask  = gameState?.connectedMask ?? 0;
  const connectedCount = [0, 1, 2, 3].filter(i => Boolean(connectedMask & (1 << i))).length;

  const showResources =
    phase === GamePhase.INITIAL_PLACEMENT ||
    phase === GamePhase.PLAYING ||
    phase === GamePhase.ROBBER;

  const [btnL, btnC, btnR] = buttonsForPhase(phase, myTurn, hasRolled, connectedCount);

  // ── Render ───────────────────────────────────────────────────────────────

  return (
    <View style={[s.root, { backgroundColor: theme.background }]}>
      <SafeAreaView style={s.safeArea} edges={['top', 'left', 'right']}>

        {/* Header */}
        <View style={[s.header, { borderBottomColor: theme.backgroundElement }]}>
          <View style={s.headerLeft}>
            <Text style={[s.playerName, { color: theme.text }]}>
              Player {myId + 1}
            </Text>
            <View style={[s.phaseBadge, { backgroundColor: theme.backgroundElement }]}>
              <Text style={[s.phaseText, { color: theme.textSecondary }]}>
                {PHASE_LABEL[phase]}
              </Text>
            </View>
          </View>
          <Pressable
            onPress={() => disconnect()}
            hitSlop={12}
            style={({ pressed }) => [s.disconnectBtn, { opacity: pressed ? 0.6 : 1 }]}>
            <Text style={[s.disconnectText, { color: theme.textSecondary }]}>✕</Text>
          </Pressable>
        </View>

        {/* Scrollable content */}
        <ScrollView
          style={s.scroll}
          contentContainerStyle={s.scrollContent}
          showsVerticalScrollIndicator={false}>

          {/* Phase-specific hero */}
          <PhaseHero
            gameState={gameState}
            phase={phase}
            myId={myId}
            myTurn={myTurn}
            numPlayers={numPlayers}
            currentPlayer={currentPlayer}
            hasRolled={hasRolled}
            connectedCount={connectedCount}
            theme={theme}
          />

          {/* Resources (placement + playing + robber only) */}
          {showResources && (
            <View style={s.section}>
              <Text style={[s.sectionLabel, { color: theme.textSecondary }]}>Resources</Text>
              <View style={[s.resourcesCard, { backgroundColor: theme.backgroundElement }]}>
                {RESOURCES.map(res => (
                  <View key={res.key} style={s.resourceRow}>
                    <Text style={s.resourceEmoji}>{res.emoji}</Text>
                    <Text style={[s.resourceLabel, { color: theme.text }]}>{res.label}</Text>
                    <View style={s.resourceControls}>
                      <Stepper onPress={() => setResources(r => ({ ...r, [res.key]: Math.max(0, r[res.key] - 1) }))} label="−" bg={theme.background} fg={theme.text} />
                      <Text style={[s.resourceCount, { color: theme.text }]}>{resources[res.key]}</Text>
                      <Stepper onPress={() => setResources(r => ({ ...r, [res.key]: r[res.key] + 1 }))}           label="+" bg={theme.background} fg={theme.text} />
                    </View>
                  </View>
                ))}
              </View>
            </View>
          )}

          {/* Victory points (playing / robber / game-over) */}
          {(showResources || isGameOver) && (
            <View style={s.section}>
              <Text style={[s.sectionLabel, { color: theme.textSecondary }]}>Victory Points</Text>
              {/* My VP stepper */}
              <View style={[s.vpEditor, { backgroundColor: theme.backgroundElement }]}>
                <Stepper onPress={() => setLocalVp(v => Math.max(0, v - 1))}      label="−" bg={theme.background} fg={theme.text} />
                <View style={s.vpEditorCenter}>
                  <Text style={[s.vpEditorLabel, { color: theme.textSecondary }]}>My VP</Text>
                  <Text style={[s.vpEditorValue, { color: theme.primary }]}>{localVp}</Text>
                </View>
                <Stepper onPress={() => setLocalVp(v => Math.min(20, v + 1))}     label="+" bg={theme.background} fg={theme.text} />
              </View>
              <Pressable
                onPress={handleReport}
                disabled={reporting}
                style={({ pressed }) => [
                  s.reportBtn,
                  { backgroundColor: theme.primary, opacity: pressed || reporting ? 0.6 : 1 },
                ]}>
                <Text style={s.reportBtnText}>{reporting ? 'Sending…' : 'Report to Board'}</Text>
              </Pressable>

              {/* Scoreboard from board */}
              {numPlayers > 0 && gameState && (
                <View style={[s.vpRow, { backgroundColor: theme.backgroundElement }]}>
                  {Array.from({ length: numPlayers }, (_, i) => {
                    const isMe     = i === myId;
                    const isWinner = isGameOver && gameState.winnerId === i;
                    return (
                      <View
                        key={i}
                        style={[
                          s.vpCell,
                          isMe && { backgroundColor: theme.backgroundSelected, borderRadius: 10 },
                        ]}>
                        {isWinner && <Text style={s.trophyIcon}>🏆</Text>}
                        <Text style={[s.vpCellLabel, { color: theme.textSecondary }]}>
                          P{i + 1}{isMe ? ' ●' : ''}
                        </Text>
                        <Text style={[s.vpCellValue, { color: theme.text }]}>
                          {gameState.vp[i] ?? 0}
                        </Text>
                      </View>
                    );
                  })}
                </View>
              )}
            </View>
          )}

          <Text style={[s.connectedLabel, { color: theme.textSecondary }]}>
            {connectedName ?? ''}
          </Text>
        </ScrollView>

        {/* Action bar */}
        <ActionBar
          btnLeft={btnL}
          btnCenter={btnC}
          btnRight={btnR}
          onAction={handleAction}
          primaryColor={theme.primary}
          surfaceColor={theme.backgroundElement}
          textColor={theme.text}
          mutedColor={theme.textSecondary}
        />
      </SafeAreaView>
    </View>
  );
}

// ── Snake draft display helper ────────────────────────────────────────────

/** Returns ordered slots for the current round's snake order strip.
 *  We don't know the first player from the proto alone, so we reconstruct a
 *  plausible order: in the forward round, the current player is first in the
 *  remaining sequence; in the reverse round, the current player is the head
 *  of the reverse sequence.  The strip shows all N players in turn order with
 *  the current slot highlighted.
 *  numPlayers: 2..4, currentPlayer: 0-based, isForward: round 1 vs round 2. */
function buildSnakeDisplay(
  numPlayers: number,
  currentPlayer: number,
  isForward: boolean,
): { playerId: number; isCurrent: boolean }[] {
  const slots: { playerId: number; isCurrent: boolean }[] = [];
  for (let i = 0; i < numPlayers; i++) {
    const id = isForward
      ? (currentPlayer + i) % numPlayers
      : (currentPlayer + numPlayers - i) % numPlayers;
    slots.push({ playerId: id, isCurrent: i === 0 });
  }
  return slots;
}

// ── Phase hero cards ──────────────────────────────────────────────────────

interface HeroProps {
  gameState: BoardState | null;
  phase: GamePhase;
  myId: number;
  myTurn: boolean;
  numPlayers: number;
  currentPlayer: number;
  hasRolled: boolean;
  connectedCount: number;
  theme: ReturnType<typeof import('@/hooks/use-theme').useTheme>;
}

function PhaseHero(p: HeroProps) {
  const { gameState, phase, myId, myTurn, numPlayers, currentPlayer, hasRolled, connectedCount, theme } = p;

  switch (phase) {

    // ── Lobby ──────────────────────────────────────────────────────────────
    case GamePhase.LOBBY: {
      const mask = gameState?.connectedMask ?? 0;
      const connected = [0, 1, 2, 3].map(i => Boolean(mask & (1 << i)));
      const count     = connectedCount;
      const needMore  = count < 2;
      return (
        <View style={[s.heroCard, { backgroundColor: theme.backgroundElement }]}>
          <Text style={[s.heroTitle, { color: theme.text }]}>Waiting for players</Text>
          <View style={s.lobbyGrid}>
            {[0, 1, 2, 3].map(i => (
              <View
                key={i}
                style={[
                  s.lobbySlot,
                  {
                    backgroundColor: connected[i] ? theme.primary : theme.background,
                    borderColor: connected[i] ? theme.primary : theme.textSecondary,
                  },
                ]}>
                <Text style={[s.lobbySlotText, { color: connected[i] ? '#fff' : theme.textSecondary }]}>
                  P{i + 1}
                </Text>
                <Text style={[s.lobbySlotSub, { color: connected[i] ? 'rgba(255,255,255,0.8)' : theme.textSecondary }]}>
                  {i === myId ? 'you' : connected[i] ? '●' : '○'}
                </Text>
              </View>
            ))}
          </View>
          <Text style={[s.heroSub, { color: theme.textSecondary }]}>
            {count} of 4 connected
          </Text>
          {needMore && (
            <Text style={[s.lobbyHint, { color: theme.textSecondary }]}>
              Need at least 2 players to start
            </Text>
          )}
        </View>
      );
    }

    // ── Board Setup ────────────────────────────────────────────────────────
    case GamePhase.BOARD_SETUP:
      return (
        <View style={[s.heroCard, { backgroundColor: theme.backgroundElement }]}>
          <Text style={s.heroEmoji}>🗺</Text>
          <Text style={[s.heroTitle, { color: theme.text }]}>Board Ready</Text>
          <Text style={[s.heroSub, { color: theme.textSecondary }]}>
            The board has been generated.
          </Text>
          <Text style={[s.heroSub, { color: theme.textSecondary }]}>
            Tap “Start Reveal” when everyone is ready to step through the number tokens.
          </Text>
        </View>
      );

    // ── Number Reveal ──────────────────────────────────────────────────────
    case GamePhase.NUMBER_REVEAL: {
      const revealNum = gameState?.revealNumber ?? 0;
      const idx       = REVEAL_ORDER.indexOf(revealNum);
      const placed    = idx >= 0 ? idx : REVEAL_ORDER.length;
      return (
        <View style={[s.heroCard, { backgroundColor: theme.backgroundElement }]}>
          <Text style={[s.heroSub, { color: theme.textSecondary }]}>Placing number token</Text>
          <Text style={[s.revealNumber, { color: theme.primary }]}>
            {revealNum > 0 ? revealNum : '—'}
          </Text>
          {/* Progress pip row */}
          <View style={s.revealProgress}>
            {REVEAL_ORDER.map((n, i) => (
              <View
                key={n}
                style={[
                  s.revealPip,
                  {
                    backgroundColor:
                      i < placed
                        ? theme.primary
                        : i === placed
                        ? theme.text
                        : theme.background,
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

    // ── Initial Placement ──────────────────────────────────────────────────
    case GamePhase.INITIAL_PLACEMENT: {
      const round       = gameState?.setupRound ?? 1;
      const isForward   = round <= 1;
      const direction   = isForward ? '→ forward' : '← reverse';
      const turnLabel   = `Round ${round} of 2  ·  ${direction}`;
      return (
        <View style={[s.heroCard, { backgroundColor: theme.backgroundElement }]}>
          <Text style={[s.heroSub, { color: theme.textSecondary }]}>{turnLabel}</Text>
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
                <View
                  key={i}
                  style={[
                    s.snakeSlot,
                    { borderColor: slot.isCurrent ? theme.primary : theme.textSecondary },
                  ]}>
                  <Text style={[
                    s.snakeSlotText,
                    { color: slot.isCurrent ? theme.primary : theme.textSecondary },
                  ]}>
                    P{slot.playerId + 1}
                  </Text>
                </View>
              ))}
            </View>
          )}
        </View>
      );
    }

    // ── Playing ────────────────────────────────────────────────────────────
    case GamePhase.PLAYING: {
      const die1  = gameState?.die1 ?? 0;
      const die2  = gameState?.die2 ?? 0;
      const total = die1 + die2;
      return (
        <View style={s.playingHero}>
          {/* Turn indicator chip */}
          <View
            style={[
              s.turnChip,
              { backgroundColor: myTurn ? theme.primary : theme.backgroundElement },
            ]}>
            <Text style={[s.turnChipText, { color: myTurn ? '#fff' : theme.text }]}>
              {myTurn ? '⬤  Your turn' : `Player ${currentPlayer + 1}'s turn`}
            </Text>
          </View>

          {/* Dice display */}
          {hasRolled && total > 0 ? (
            <View style={[s.heroCard, { backgroundColor: theme.backgroundElement }]}>
              <Text style={[s.diceDisplay, { color: theme.text }]}>
                {DIE_FACES[die1]}  {DIE_FACES[die2]}
              </Text>
              <Text style={[s.diceTotal, { color: theme.primary }]}>{total}</Text>
            </View>
          ) : myTurn ? (
            <View style={[s.heroCard, { backgroundColor: theme.backgroundElement }]}>
              <Text style={s.heroEmoji}>🎲</Text>
              <Text style={[s.heroSub, { color: theme.textSecondary }]}>
                Tap Roll Dice to start your turn
              </Text>
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

    // ── Robber ─────────────────────────────────────────────────────────────
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

    // ── Game Over ──────────────────────────────────────────────────────────
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

// ── Action bar ────────────────────────────────────────────────────────────

interface ActionBarProps {
  btnLeft:   ButtonSpec | null;
  btnCenter: ButtonSpec | null;
  btnRight:  ButtonSpec | null;
  onAction: (a: PlayerAction) => void;
  primaryColor: string;
  surfaceColor: string;
  textColor: string;
  mutedColor: string;
}

function ActionBar({ btnLeft, btnCenter, btnRight, onAction, primaryColor, surfaceColor, textColor, mutedColor }: ActionBarProps) {
  if (!btnLeft && !btnCenter && !btnRight) return null;
  return (
    <SafeAreaView edges={['bottom']} style={[s.actionBar, { borderTopColor: surfaceColor }]}>
      <View style={s.actionBarInner}>
        <ActionBtn spec={btnLeft}   onAction={onAction} primary={primaryColor} surface={surfaceColor} text={textColor} muted={mutedColor} />
        <ActionBtn spec={btnCenter} onAction={onAction} primary={primaryColor} surface={surfaceColor} text={textColor} muted={mutedColor} />
        <ActionBtn spec={btnRight}  onAction={onAction} primary={primaryColor} surface={surfaceColor} text={textColor} muted={mutedColor} />
      </View>
    </SafeAreaView>
  );
}

interface ActionBtnProps {
  spec: ButtonSpec | null;
  onAction: (a: PlayerAction) => void;
  primary: string;
  surface: string;
  text: string;
  muted: string;
}

function ActionBtn({ spec, onAction, primary, surface, text, muted }: ActionBtnProps) {
  if (!spec) return <View style={s.actionBtnPlaceholder} />;
  const disabled = !spec.enabled;
  const bg       = spec.primary ? primary : surface;
  const fg       = spec.primary ? '#fff'  : disabled ? muted : text;
  return (
    <Pressable
      onPress={() => onAction(spec.action)}
      disabled={disabled}
      style={({ pressed }) => [
        s.actionBtn,
        { backgroundColor: bg, opacity: disabled ? 0.3 : pressed ? 0.75 : 1 },
      ]}>
      <Text style={[s.actionBtnText, { color: fg }]}>{spec.label}</Text>
    </Pressable>
  );
}

// ── Stepper ───────────────────────────────────────────────────────────────

function Stepper({ onPress, label, bg, fg }: { onPress: () => void; label: string; bg: string; fg: string }) {
  return (
    <Pressable
      onPress={onPress}
      hitSlop={10}
      style={({ pressed }) => [s.stepper, { backgroundColor: bg, opacity: pressed ? 0.6 : 1 }]}>
      <Text style={[s.stepperText, { color: fg }]}>{label}</Text>
    </Pressable>
  );
}

// ── Styles ────────────────────────────────────────────────────────────────

const s = StyleSheet.create({
  root:     { flex: 1 },
  safeArea: { flex: 1 },

  // Header
  header: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingHorizontal: Spacing.four,
    paddingVertical: Spacing.three,
    borderBottomWidth: StyleSheet.hairlineWidth,
  },
  headerLeft:     { gap: Spacing.one },
  playerName:     { fontSize: 20, fontWeight: '700' },
  phaseBadge:     { alignSelf: 'flex-start', paddingHorizontal: Spacing.two, paddingVertical: 3, borderRadius: 6 },
  phaseText:      { fontSize: 12, fontWeight: '600' },
  disconnectBtn:  { padding: Spacing.two },
  disconnectText: { fontSize: 20, fontWeight: '300' },

  // Scroll
  scroll:        { flex: 1 },
  scrollContent: { padding: Spacing.four, gap: Spacing.three, paddingBottom: Spacing.six },

  // Generic section
  section:      { gap: Spacing.two },
  sectionLabel: { fontSize: 12, fontWeight: '700', textTransform: 'uppercase', letterSpacing: 0.8 },

  // Hero card (used by most phases)
  heroCard: {
    borderRadius: 20,
    padding: Spacing.four,
    alignItems: 'center',
    gap: Spacing.two,
  },
  heroEmoji: { fontSize: 48, lineHeight: 56 },
  heroTitle: { fontSize: 26, fontWeight: '800', textAlign: 'center' },
  heroSub:   { fontSize: 15, textAlign: 'center', lineHeight: 22 },

  // Lobby
  lobbyGrid: {
    flexDirection: 'row',
    gap: Spacing.two,
    marginVertical: Spacing.two,
  },
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
  lobbyHint:     { fontSize: 13, textAlign: 'center', marginTop: 4 },

  // Snake draft strip (setup phase)
  snakeRow:      { flexDirection: 'row', gap: 6, marginTop: Spacing.two },
  snakeSlot: {
    borderWidth: 2,
    borderRadius: 10,
    paddingHorizontal: 10,
    paddingVertical: 6,
  },
  snakeSlotText: { fontSize: 13, fontWeight: '700' },

  // Number reveal
  revealNumber:   { fontSize: 80, fontWeight: '900', lineHeight: 90 },
  revealProgress: { flexDirection: 'row', gap: 5, marginVertical: Spacing.two },
  revealPip:      { width: 10, height: 10, borderRadius: 5, borderWidth: 1.5 },

  // Playing phase
  playingHero: { gap: Spacing.two },
  turnChip: {
    alignSelf: 'center',
    borderRadius: 20,
    paddingHorizontal: Spacing.three,
    paddingVertical: Spacing.one + 2,
  },
  turnChipText: { fontSize: 15, fontWeight: '700' },
  diceDisplay: { fontSize: 60, lineHeight: 70 },
  diceTotal:   { fontSize: 44, fontWeight: '900' },

  // Resources card
  resourcesCard: { borderRadius: 16, padding: Spacing.three, gap: Spacing.two },
  resourceRow: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: Spacing.three,
    paddingVertical: Spacing.one,
  },
  resourceEmoji:    { fontSize: 22, width: 28, textAlign: 'center' },
  resourceLabel:    { fontSize: 15, fontWeight: '600', flex: 1 },
  resourceControls: { flexDirection: 'row', alignItems: 'center', gap: Spacing.two },
  resourceCount:    { fontSize: 18, fontWeight: '700', minWidth: 28, textAlign: 'center' },

  // Stepper
  stepper:     { width: 36, height: 36, borderRadius: 18, alignItems: 'center', justifyContent: 'center' },
  stepperText: { fontSize: 22, fontWeight: '700', lineHeight: 24 },

  // VP editor
  vpEditor: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-around',
    borderRadius: 16,
    padding: Spacing.three,
  },
  vpEditorCenter: { alignItems: 'center' },
  vpEditorLabel:  { fontSize: 12, fontWeight: '600', textTransform: 'uppercase', letterSpacing: 0.5 },
  vpEditorValue:  { fontSize: 52, fontWeight: '900', minWidth: 64, textAlign: 'center' },
  reportBtn:      { borderRadius: 14, paddingVertical: Spacing.three, alignItems: 'center' },
  reportBtnText:  { color: '#fff', fontSize: 16, fontWeight: '700' },

  // VP row (scoreboard)
  vpRow:      { flexDirection: 'row', borderRadius: 16, padding: Spacing.three, justifyContent: 'space-around' },
  vpCell:     { alignItems: 'center', paddingVertical: Spacing.one, paddingHorizontal: Spacing.two, gap: 2 },
  trophyIcon: { fontSize: 16 },
  vpCellLabel:{ fontSize: 12, fontWeight: '600' },
  vpCellValue:{ fontSize: 24, fontWeight: '800' },

  connectedLabel: { fontSize: 11, textAlign: 'center', marginTop: Spacing.two },

  // Action bar
  actionBar:      { borderTopWidth: StyleSheet.hairlineWidth },
  actionBarInner: { flexDirection: 'row', padding: Spacing.three, gap: Spacing.two },
  actionBtn: {
    flex: 1,
    borderRadius: 14,
    paddingVertical: Spacing.three,
    alignItems: 'center',
    justifyContent: 'center',
    minHeight: 54,
  },
  actionBtnPlaceholder: { flex: 1 },
  actionBtnText:        { fontSize: 15, fontWeight: '700' },
});
