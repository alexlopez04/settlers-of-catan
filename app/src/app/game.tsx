import { router } from 'expo-router';
import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  Alert,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import Animated, {
  useSharedValue,
  useAnimatedStyle,
  withTiming,
  withSpring,
  withSequence,
  withRepeat,
} from 'react-native-reanimated';
import { SafeAreaView } from 'react-native-safe-area-context';

import { useBle, ResourceCounts } from '@/context/ble-context';
import { Spacing } from '@/constants/theme';
import { useTheme } from '@/hooks/use-theme';
import { BoardState, GamePhase, NO_WINNER, PlayerAction } from '@/services/proto';
import { SFSymbolIcon } from '@/components/ui/symbol';
import { BoardOverview } from '@/components/ui/board-overview';

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

const REVEAL_ORDER = [2, 3, 4, 5, 6, 8, 9, 10, 11, 12];

// ── Button specs ──────────────────────────────────────────────────────────

interface ButtonSpec {
  label: string;
  sfSymbol?: string;
  action: PlayerAction;
  enabled: boolean;
  primary?: boolean;
}

/**
 * Returns only the buttons relevant for the current state.
 * Buttons always expand to fill the bar (no placeholders).
 */
function buttonsForPhase(
  phase: GamePhase,
  myTurn: boolean,
  hasRolled: boolean,
  connectedCount: number,
): ButtonSpec[] {
  switch (phase) {
    case GamePhase.LOBBY: {
      const canStart = connectedCount >= 1;
      const btns: ButtonSpec[] = [
        { label: 'Ready', sfSymbol: 'checkmark.circle', action: PlayerAction.READY, enabled: true },
      ];
      if (canStart) {
        btns.push({ label: 'Start Game', sfSymbol: 'play.fill', action: PlayerAction.START_GAME, enabled: true, primary: true });
      }
      return btns;
    }
    case GamePhase.BOARD_SETUP:
      return [{ label: 'Start Reveal', sfSymbol: 'arrow.right.circle.fill', action: PlayerAction.NEXT_NUMBER, enabled: true, primary: true }];
    case GamePhase.NUMBER_REVEAL:
      return [{ label: 'Next Number', sfSymbol: 'arrow.right.circle.fill', action: PlayerAction.NEXT_NUMBER, enabled: true, primary: true }];
    case GamePhase.INITIAL_PLACEMENT:
      return [{ label: 'Placement Done', sfSymbol: 'checkmark.circle.fill', action: PlayerAction.PLACE_DONE, enabled: myTurn, primary: myTurn }];
    case GamePhase.PLAYING: {
      if (!hasRolled) {
        return [{ label: 'Roll Dice', sfSymbol: 'die.face.5.fill', action: PlayerAction.ROLL_DICE, enabled: myTurn, primary: myTurn }];
      }
      return [{ label: 'End Turn', sfSymbol: 'arrow.trianglehead.clockwise', action: PlayerAction.END_TURN, enabled: myTurn, primary: myTurn }];
    }
    case GamePhase.ROBBER:
      return [{ label: 'Skip Robber', sfSymbol: 'forward.fill', action: PlayerAction.SKIP_ROBBER, enabled: myTurn, primary: myTurn }];
    default:
      return [];
  }
}

// ── Snake draft display helper ─────────────────────────────────────────────

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

// ── Animation helpers ─────────────────────────────────────────────────────

/** Plays a fade + slide-up entrance whenever `triggerKey` changes. */
function FadeSlideIn({ children, triggerKey }: {
  children: React.ReactNode;
  triggerKey: string | number;
}) {
  const opacity = useSharedValue(0);
  const translateY = useSharedValue(18);

  useEffect(() => {
    opacity.value = 0;
    translateY.value = 18;
    opacity.value = withTiming(1, { duration: 280 });
    translateY.value = withSpring(0, { damping: 18, stiffness: 180 });
  }, [triggerKey]);

  const animStyle = useAnimatedStyle(() => ({
    opacity: opacity.value,
    transform: [{ translateY: translateY.value }],
  }));

  return <Animated.View style={animStyle}>{children}</Animated.View>;
}

/** Turn chip that pulses when it becomes the player's turn. */
function TurnChip({ myTurn, currentPlayer, theme }: {
  myTurn: boolean;
  currentPlayer: number;
  theme: ReturnType<typeof import('@/hooks/use-theme').useTheme>;
}) {
  const scale = useSharedValue(1);
  const prevMyTurn = useRef(false);

  useEffect(() => {
    if (myTurn && !prevMyTurn.current) {
      scale.value = withSequence(
        withSpring(1.1, { damping: 5, stiffness: 300 }),
        withSpring(1, { damping: 12, stiffness: 200 }),
      );
    }
    prevMyTurn.current = myTurn;
  }, [myTurn]);

  const chipStyle = useAnimatedStyle(() => ({
    transform: [{ scale: scale.value }],
  }));

  return (
    <Animated.View style={[s.turnChip, { backgroundColor: myTurn ? theme.primary : theme.backgroundElement }, chipStyle]}>
      <Text style={[s.turnChipText, { color: myTurn ? '#fff' : theme.text }]}>
        {myTurn ? '⬤  Your turn' : `Player ${currentPlayer + 1}'s turn`}
      </Text>
    </Animated.View>
  );
}

/** Shuffles through random die faces then springs to the real result when hasRolled becomes true. */
function AnimatedDiceDisplay({ die1, die2, total, hasRolled, theme }: {
  die1: number;
  die2: number;
  total: number;
  hasRolled: boolean;
  theme: ReturnType<typeof import('@/hooks/use-theme').useTheme>;
}) {
  const [d1, setD1] = useState(die1);
  const [d2, setD2] = useState(die2);
  const scale = useSharedValue(1);
  const isFirst = useRef(true);
  const prevHasRolled = useRef(hasRolled);

  useEffect(() => {
    if (isFirst.current) {
      isFirst.current = false;
      prevHasRolled.current = hasRolled;
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
          scale.value = 0.7;
          scale.value = withSpring(1, { damping: 8, stiffness: 100 });
        }
      }, 75);
      return () => clearInterval(interval);
    }
    if (!hasRolled) {
      setD1(die1);
      setD2(die2);
    }
  }, [hasRolled, die1, die2]);

  const diceStyle = useAnimatedStyle(() => ({
    transform: [{ scale: scale.value }],
  }));

  return (
    <View style={[s.heroCard, { backgroundColor: theme.backgroundElement }]}>
      <Animated.Text style={[s.diceDisplay, { color: theme.text }, diceStyle]}>
        {DIE_FACES[d1]}  {DIE_FACES[d2]}
      </Animated.Text>
      <Animated.Text style={[s.diceTotal, { color: theme.primary }, diceStyle]}>
        {d1 + d2}
      </Animated.Text>
    </View>
  );
}

/** Big number token that bounces in each time the reveal number changes. */
function RevealHero({ gameState, theme }: {
  gameState: import('@/services/proto').BoardState | null;
  theme: ReturnType<typeof import('@/hooks/use-theme').useTheme>;
}) {
  const revealNum = gameState?.revealNumber ?? 0;
  const idx       = REVEAL_ORDER.indexOf(revealNum);
  const placed    = idx >= 0 ? idx : REVEAL_ORDER.length;
  const scale     = useSharedValue(1);
  const prevNum   = useRef(revealNum);

  useEffect(() => {
    if (revealNum !== prevNum.current && revealNum > 0) {
      scale.value = withSequence(
        withSpring(1.3, { damping: 5, stiffness: 300 }),
        withSpring(1, { damping: 10, stiffness: 200 }),
      );
    }
    prevNum.current = revealNum;
  }, [revealNum]);

  const numStyle = useAnimatedStyle(() => ({
    transform: [{ scale: scale.value }],
  }));

  return (
    <View style={[s.heroCard, { backgroundColor: theme.backgroundElement }]}>
      <Text style={[s.heroSub, { color: theme.textSecondary }]}>Placing number token</Text>
      <Animated.Text style={[s.revealNumber, { color: theme.primary }, numStyle]}>
        {revealNum > 0 ? revealNum : '—'}
      </Animated.Text>
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

/** Player slot that pops when that player connects. */
function LobbySlot({ index, connected, isMe, theme }: {
  index: number;
  connected: boolean;
  isMe: boolean;
  theme: ReturnType<typeof import('@/hooks/use-theme').useTheme>;
}) {
  const scale = useSharedValue(1);
  const prevConnected = useRef(connected);

  useEffect(() => {
    if (connected && !prevConnected.current) {
      scale.value = withSequence(
        withSpring(1.12, { damping: 5, stiffness: 300 }),
        withSpring(1, { damping: 12, stiffness: 200 }),
      );
    }
    prevConnected.current = connected;
  }, [connected]);

  const slotStyle = useAnimatedStyle(() => ({
    transform: [{ scale: scale.value }],
  }));

  return (
    <Animated.View
      style={[
        s.lobbySlot,
        {
          backgroundColor: connected ? theme.primary : theme.background,
          borderColor: connected ? theme.primary : theme.textSecondary,
        },
        slotStyle,
      ]}>
      <Text style={[s.lobbySlotText, { color: connected ? '#fff' : theme.textSecondary }]}>
        P{index + 1}
      </Text>
      <Text style={[s.lobbySlotSub, { color: connected ? 'rgba(255,255,255,0.8)' : theme.textSecondary }]}>
        {isMe ? 'you' : connected ? '●' : '○'}
      </Text>
    </Animated.View>
  );
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

    case GamePhase.LOBBY: {
      const mask = gameState?.connectedMask ?? 0;
      const connected = [0, 1, 2, 3].map(i => Boolean(mask & (1 << i)));
      const needMore  = connectedCount < 2;
      return (
        <View style={[s.heroCard, { backgroundColor: theme.backgroundElement }]}>
          <Text style={[s.heroTitle, { color: theme.text }]}>Waiting for players</Text>
          <View style={s.lobbyGrid}>
            {[0, 1, 2, 3].map(i => (
              <LobbySlot
                key={i}
                index={i}
                connected={connected[i]}
                isMe={i === myId}
                theme={theme}
              />
            ))}
          </View>
          <Text style={[s.heroSub, { color: theme.textSecondary }]}>
            {connectedCount} of 4 connected
          </Text>
          {needMore && (
            <Text style={[s.lobbyHint, { color: theme.textSecondary }]}>
              Need at least 2 players to start
            </Text>
          )}
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
      const round     = gameState?.setupRound ?? 1;
      const isForward = round <= 1;
      const direction = isForward ? '→ forward' : '← reverse';
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
            <AnimatedDiceDisplay die1={die1} die2={die2} total={total} hasRolled={hasRolled} theme={theme} />
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

// ── Action bar ────────────────────────────────────────────────────────────

interface ActionBarProps {
  buttons: ButtonSpec[];
  onAction: (a: PlayerAction) => void;
  pendingAction: PlayerAction | null;
  primaryColor: string;
  surfaceColor: string;
  textColor: string;
  mutedColor: string;
}

function ActionBar({ buttons, onAction, pendingAction, primaryColor, surfaceColor, textColor, mutedColor }: ActionBarProps) {
  if (buttons.length === 0) return null;
  return (
    <SafeAreaView edges={['bottom']} style={[s.actionBar, { borderTopColor: surfaceColor }]}>
      <View style={s.actionBarInner}>
        {buttons.map(spec => {
          const isPending = pendingAction === spec.action;
          const disabled  = !spec.enabled || pendingAction !== null;
          const bg        = spec.primary ? primaryColor : surfaceColor;
          const fg        = spec.primary ? '#fff' : disabled ? mutedColor : textColor;
          return (
            <Pressable
              key={spec.action}
              onPress={() => onAction(spec.action)}
              disabled={disabled}
              style={({ pressed }) => [
                s.actionBtn,
                { backgroundColor: bg, opacity: disabled ? 0.35 : pressed ? 0.75 : 1, transform: [{ scale: pressed ? 0.97 : 1 }] },
              ]}>
              <View style={s.actionBtnInner}>
                {isPending ? (
                  <Text style={[s.actionBtnText, { color: fg }]}>…</Text>
                ) : (
                  <>
                    {spec.sfSymbol && (
                      <SFSymbolIcon name={spec.sfSymbol as any} size={18} color={fg} weight="semibold" />
                    )}
                    <Text style={[s.actionBtnText, { color: fg }]} numberOfLines={1}>
                      {spec.label}
                    </Text>
                  </>
                )}
              </View>
            </Pressable>
          );
        })}
      </View>
    </SafeAreaView>
  );
}

// ── Stepper ───────────────────────────────────────────────────────────────

function Stepper({ onPress, decrement, bg, fg }: { onPress: () => void; decrement?: boolean; bg: string; fg: string }) {
  return (
    <Pressable
      onPress={onPress}
      hitSlop={10}
      style={({ pressed }) => [s.stepper, { backgroundColor: bg, opacity: pressed ? 0.6 : 1 }]}>
      <SFSymbolIcon
        name={decrement ? 'minus' : 'plus'}
        size={16}
        color={fg}
        weight="semibold"
        fallback={decrement ? '−' : '+'}
      />
    </Pressable>
  );
}

// ── Sync indicator ────────────────────────────────────────────────────────

function SyncIndicator({ syncing, lastSynced, theme }: {
  syncing: boolean;
  lastSynced: number | null;
  theme: ReturnType<typeof import('@/hooks/use-theme').useTheme>;
}) {
  const opacity = useSharedValue(0);

  useEffect(() => {
    if (syncing) {
      opacity.value = withRepeat(
        withSequence(
          withTiming(1, { duration: 400 }),
          withTiming(0.3, { duration: 400 }),
        ),
        -1,
        false,
      );
    } else {
      opacity.value = withTiming(0.7, { duration: 200 });
    }
  }, [syncing]);

  const animStyle = useAnimatedStyle(() => ({ opacity: opacity.value }));

  if (!syncing && !lastSynced) return null;

  return (
    <Animated.View style={[s.syncRow, animStyle]}>
      <SFSymbolIcon name="arrow.triangle.2.circlepath" size={13} color={theme.textSecondary} fallback="↻" />
      <Text style={[s.syncText, { color: theme.textSecondary }]}>
        {syncing ? 'Syncing…' : 'Synced'}
      </Text>
    </Animated.View>
  );
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
  const [pendingAction, setPendingAction] = useState<PlayerAction | null>(null);
  const [showBoard, setShowBoard] = useState(false);
  const [syncing, setSyncing] = useState(false);
  const [lastSynced, setLastSynced] = useState<number | null>(null);
  const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const vpScale = useSharedValue(1);
  const prevVpRef = useRef(0);

  // Navigate away when disconnected.
  useEffect(() => {
    if (connectionState === 'idle') router.replace('/');
  }, [connectionState]);

  // Restore VP and resources from boardState when first received (app re-open / reconnect).
  const restoredRef = useRef(false);
  useEffect(() => {
    if (!restoredRef.current && gameState && playerId != null) {
      const savedVp = gameState.vp[playerId] ?? 0;
      if (savedVp > 0) setLocalVp(savedVp);
      setResources({
        lumber: gameState.resLumber[playerId] ?? 0,
        wool:   gameState.resWool[playerId]   ?? 0,
        grain:  gameState.resGrain[playerId]  ?? 0,
        brick:  gameState.resBrick[playerId]  ?? 0,
        ore:    gameState.resOre[playerId]    ?? 0,
      });
      restoredRef.current = true;
    }
  }, [gameState, playerId]);

  // Debounced auto-sync when resources or VP change.
  const scheduleSync = useCallback(
    (vp: number, res: ResourceCounts) => {
      if (debounceRef.current) clearTimeout(debounceRef.current);
      debounceRef.current = setTimeout(async () => {
        setSyncing(true);
        try {
          await sendReport(vp, res);
          setLastSynced(Date.now());
        } catch {
          /* silent – next change will retry */
        } finally {
          setSyncing(false);
        }
      }, 1200);
    },
    [sendReport],
  );

  const adjustVp = useCallback(
    (delta: number) => {
      setLocalVp(v => {
        const next = Math.max(0, Math.min(20, v + delta));
        scheduleSync(next, resources);
        return next;
      });
    },
    [resources, scheduleSync],
  );

  const adjustResource = useCallback(
    (key: ResKey, delta: number) => {
      setResources(r => {
        const next = { ...r, [key]: Math.max(0, r[key] + delta) };
        scheduleSync(localVp, next);
        return next;
      });
    },
    [localVp, scheduleSync],
  );

  const handleAction = useCallback(
    async (action: PlayerAction) => {
      setPendingAction(action);
      try { await sendAction(action); } catch { /* ignore */ }
      finally { setPendingAction(null); }
    },
    [sendAction],
  );

  // Bounce the VP display when the count changes.
  useEffect(() => {
    if (localVp !== prevVpRef.current) {
      prevVpRef.current = localVp;
      vpScale.value = withSequence(
        withSpring(1.35, { damping: 5, stiffness: 400 }),
        withSpring(1, { damping: 10, stiffness: 200 }),
      );
    }
  }, [localVp]);

  const vpAnimStyle = useAnimatedStyle(() => ({
    transform: [{ scale: vpScale.value }],
  }));

  // ── Derived values ─────────────────────────────────────────────────────

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
    phase === GamePhase.PLAYING ||
    phase === GamePhase.ROBBER;

  const buttons = buttonsForPhase(phase, myTurn, hasRolled, connectedCount);
  const boardAvailable = (gameState?.tiles?.length ?? 0) > 0;

  const handleDisconnect = useCallback(() => {
    const activePhases: GamePhase[] = [
      GamePhase.BOARD_SETUP,
      GamePhase.NUMBER_REVEAL,
      GamePhase.INITIAL_PLACEMENT,
      GamePhase.PLAYING,
      GamePhase.ROBBER,
    ];
    if (activePhases.includes(phase)) {
      Alert.alert(
        'Leave Game?',
        'The game is in progress. Are you sure you want to disconnect?',
        [
          { text: 'Cancel', style: 'cancel' },
          { text: 'Leave', style: 'destructive', onPress: () => disconnect() },
        ],
      );
    } else {
      disconnect();
    }
  }, [disconnect, phase]);

  // ── Render ─────────────────────────────────────────────────────────────

  return (
    <View style={[s.root, { backgroundColor: theme.background }]}>
      <SafeAreaView style={s.safeArea} edges={['top', 'left', 'right']}>

        {/* Header */}
        <View style={[s.header, { borderBottomColor: theme.backgroundElement }]}>
          <View style={s.headerLeft}>
            <Text style={[s.playerName, { color: theme.text }]}>Player {myId + 1}</Text>
            <View style={[s.phaseBadge, { backgroundColor: theme.backgroundElement }]}>
              <Text style={[s.phaseText, { color: theme.textSecondary }]}>{PHASE_LABEL[phase]}</Text>
            </View>
          </View>
          <View style={s.headerRight}>
            {boardAvailable && (
              <Pressable
                onPress={() => setShowBoard(true)}
                hitSlop={12}
                style={({ pressed }) => [s.headerBtn, { opacity: pressed ? 0.5 : 1 }]}>
                <SFSymbolIcon name="map" size={22} color={theme.text} fallback="🗺" />
              </Pressable>
            )}
            <Pressable
              onPress={() => handleDisconnect()}
              hitSlop={12}
              style={({ pressed }) => [s.headerBtn, { opacity: pressed ? 0.5 : 1 }]}>
              <SFSymbolIcon name="xmark.circle" size={22} color={theme.textSecondary} fallback="✕" />
            </Pressable>
          </View>
        </View>

        {/* Scrollable content */}
        <ScrollView
          style={s.scroll}
          contentContainerStyle={s.scrollContent}
          showsVerticalScrollIndicator={false}>

          <FadeSlideIn triggerKey={phase}>
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
          </FadeSlideIn>

          {/* Resources */}
          {showResources && (
            <View style={s.section}>
              <Text style={[s.sectionLabel, { color: theme.textSecondary }]}>Resources</Text>
              <View style={[s.resourcesCard, { backgroundColor: theme.backgroundElement }]}>
                {RESOURCES.map(res => (
                  <View key={res.key} style={s.resourceRow}>
                    <Text style={s.resourceEmoji}>{res.emoji}</Text>
                    <Text style={[s.resourceLabel, { color: theme.text }]}>{res.label}</Text>
                    <View style={s.resourceControls}>
                      <Stepper decrement onPress={() => adjustResource(res.key, -1)} bg={theme.background} fg={theme.text} />
                      <Text style={[s.resourceCount, { color: theme.text }]}>{resources[res.key]}</Text>
                      <Stepper onPress={() => adjustResource(res.key, 1)} bg={theme.background} fg={theme.text} />
                    </View>
                  </View>
                ))}
              </View>
            </View>
          )}

          {/* Victory points */}
          {(showResources || isGameOver) && (
            <View style={s.section}>
              <View style={s.sectionHeader}>
                <Text style={[s.sectionLabel, { color: theme.textSecondary }]}>Victory Points</Text>
                <SyncIndicator syncing={syncing} lastSynced={lastSynced} theme={theme} />
              </View>
              <View style={[s.vpEditor, { backgroundColor: theme.backgroundElement }]}>
                <Stepper decrement onPress={() => adjustVp(-1)} bg={theme.background} fg={theme.text} />
                <View style={s.vpEditorCenter}>
                  <Text style={[s.vpEditorLabel, { color: theme.textSecondary }]}>My VP</Text>
                  <Animated.Text style={[s.vpEditorValue, { color: theme.primary }, vpAnimStyle]}>{localVp}</Animated.Text>
                </View>
                <Stepper onPress={() => adjustVp(1)} bg={theme.background} fg={theme.text} />
              </View>

              {/* Scoreboard — full only at game over; during game show only own VP */}
              {numPlayers > 0 && gameState && isGameOver && (
                <View style={[s.vpRow, { backgroundColor: theme.backgroundElement }]}>
                  {Array.from({ length: numPlayers }, (_, i) => {
                    const isMe     = i === myId;
                    const isWinner = gameState.winnerId === i;
                    return (
                      <View
                        key={i}
                        style={[
                          s.vpCell,
                          isMe && { backgroundColor: theme.backgroundSelected, borderRadius: 10 },
                        ]}>
                        {isWinner && <Text style={s.trophyIcon}>🏆</Text>}
                        <Text style={[s.vpCellLabel, { color: theme.textSecondary }]}>
                          {isMe ? 'You' : `P${i + 1}`}
                        </Text>
                        <Text style={[s.vpCellValue, { color: theme.text }]}>
                          {isMe ? localVp : (gameState.vp[i] ?? 0)}
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
          buttons={buttons}
          onAction={handleAction}
          pendingAction={pendingAction}
          primaryColor={theme.primary}
          surfaceColor={theme.backgroundElement}
          textColor={theme.text}
          mutedColor={theme.textSecondary}
        />
      </SafeAreaView>

      {/* Board overview modal */}
      <BoardOverview
        visible={showBoard}
        onClose={() => setShowBoard(false)}
        boardState={gameState}
      />
    </View>
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
  headerLeft:  { gap: Spacing.one },
  headerRight: { flexDirection: 'row', alignItems: 'center', gap: Spacing.two },
  headerBtn:   { padding: Spacing.one },
  playerName:  { fontSize: 20, fontWeight: '700' },
  phaseBadge:  { alignSelf: 'flex-start', paddingHorizontal: Spacing.two, paddingVertical: 3, borderRadius: 6 },
  phaseText:   { fontSize: 12, fontWeight: '600' },

  // Scroll
  scroll:        { flex: 1 },
  scrollContent: { padding: Spacing.four, gap: Spacing.three, paddingBottom: Spacing.six },

  // Generic section
  section:       { gap: Spacing.two },
  sectionLabel:  { fontSize: 12, fontWeight: '700', textTransform: 'uppercase', letterSpacing: 0.8 },
  sectionHeader: { flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' },

  // Sync indicator
  syncRow: { flexDirection: 'row', alignItems: 'center', gap: 4 },
  syncText: { fontSize: 12 },

  // Hero card
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
  lobbyGrid: { flexDirection: 'row', gap: Spacing.two, marginVertical: Spacing.two },
  lobbySlot: {
    flex: 1, borderRadius: 14, borderWidth: 2,
    paddingVertical: Spacing.three, alignItems: 'center', gap: 4,
  },
  lobbySlotText: { fontSize: 15, fontWeight: '700' },
  lobbySlotSub:  { fontSize: 12 },
  lobbyHint:     { fontSize: 13, textAlign: 'center', marginTop: 4 },

  // Snake draft strip
  snakeRow:      { flexDirection: 'row', gap: 6, marginTop: Spacing.two },
  snakeSlot:     { borderWidth: 2, borderRadius: 10, paddingHorizontal: 10, paddingVertical: 6 },
  snakeSlotText: { fontSize: 13, fontWeight: '700' },

  // Number reveal
  revealNumber:   { fontSize: 80, fontWeight: '900', lineHeight: 90 },
  revealProgress: { flexDirection: 'row', gap: 5, marginVertical: Spacing.two },
  revealPip:      { width: 10, height: 10, borderRadius: 5, borderWidth: 1.5 },

  // Playing phase
  playingHero: { gap: Spacing.two },
  turnChip: {
    alignSelf: 'center', borderRadius: 20,
    paddingHorizontal: Spacing.three, paddingVertical: Spacing.one + 2,
  },
  turnChipText: { fontSize: 15, fontWeight: '700' },
  diceDisplay:  { fontSize: 60, lineHeight: 70 },
  diceTotal:    { fontSize: 44, fontWeight: '900' },

  // Resources card
  resourcesCard: { borderRadius: 16, padding: Spacing.three, gap: Spacing.two },
  resourceRow: {
    flexDirection: 'row', alignItems: 'center',
    gap: Spacing.three, paddingVertical: Spacing.one,
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
    flexDirection: 'row', alignItems: 'center',
    justifyContent: 'space-around', borderRadius: 16, padding: Spacing.three,
  },
  vpEditorCenter: { alignItems: 'center' },
  vpEditorLabel:  { fontSize: 12, fontWeight: '600', textTransform: 'uppercase', letterSpacing: 0.5 },
  vpEditorValue:  { fontSize: 52, fontWeight: '900', minWidth: 64, textAlign: 'center' },

  // VP row (scoreboard — game over only)
  vpRow:       { flexDirection: 'row', borderRadius: 16, padding: Spacing.three, justifyContent: 'space-around' },
  vpCell:      { alignItems: 'center', paddingVertical: Spacing.one, paddingHorizontal: Spacing.two, gap: 2 },
  trophyIcon:  { fontSize: 16 },
  vpCellLabel: { fontSize: 12, fontWeight: '600' },
  vpCellValue: { fontSize: 24, fontWeight: '800' },

  connectedLabel: { fontSize: 11, textAlign: 'center', marginTop: Spacing.two },

  // Action bar
  actionBar:      { borderTopWidth: StyleSheet.hairlineWidth },
  actionBarInner: { flexDirection: 'row', padding: Spacing.three, gap: Spacing.two },
  actionBtn: {
    flex: 1, borderRadius: 14,
    paddingVertical: Spacing.three,
    alignItems: 'center', justifyContent: 'center',
    minHeight: 54,
  },
  actionBtnInner: {
    flexDirection: 'row', alignItems: 'center',
    justifyContent: 'center', gap: 6,
  },
  actionBtnText: { fontSize: 15, fontWeight: '700', textAlign: 'center' },
});
