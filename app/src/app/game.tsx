import { router } from 'expo-router';
import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  Alert,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import * as Haptics from 'expo-haptics';

import { useBle } from '@/context/ble-context';
import { Spacing } from '@/constants/theme';
import { PHASE_LABEL, buttonsForPhase } from '@/constants/game';
import { useTheme } from '@/hooks/use-theme';
import { GamePhase, NO_PLAYER, PlayerAction, PlayerInput, RejectReason, REJECT_MESSAGES, DevCard, DEV_CARD_COUNT, Difficulty } from '@/services/proto';
import { SFSymbolIcon } from '@/components/ui/symbol';
import { BoardOverview } from '@/components/ui/board-overview';
import { PlacementToast } from '@/components/game/placement-toast';
import { PhaseHero, FadeSlideIn } from '@/components/game/phase-hero';
import { ActionBar } from '@/components/game/action-bar';
import { LobbyOrientationPicker } from '@/components/game/lobby-orientation';
import { LobbyDifficultyPicker } from '@/components/game/lobby-difficulty-picker';
import {
  ResourcesPanel,
  StorePanel,
  PendingPurchasesBanner,
  DevCardsPanel,
  TradePanel,
  IncomingTradeDialog,
  RobberOverlay,
  StealOverlay,
  DiscardOverlay,
  DistributionToast,
  Scoreboard,
  DrawnDevCardModal,
} from '@/components/game/game-actions';

// ── Module-level resume-dialog guard ────────────────────────────────────
// Kept outside the component so it survives remounts that can occur during
// BLE reconnection or navigation transitions.  Reset to false whenever the
// session ends (connectionState → idle).
let resumeAlertShown = false;

// ── Main screen ───────────────────────────────────────────────────────────

export default function GameScreen() {
  const theme = useTheme();
  const {
    connectionState,
    connectedName,
    playerId,
    gameState,
    sendAction,
    sendInput,
    disconnect,
  } = useBle();

  const [pendingAction, setPendingAction] = useState<PlayerAction | null>(null);
  const [showBoard, setShowBoard]         = useState(false);
  const [rejectMessage, setRejectMessage] = useState<string | null>(null);
  const [drawnCard, setDrawnCard]           = useState<DevCard | null>(null);
  // Snapshot of my dev-card counts at the start of my current turn; null when not my turn.
  const [turnStartCards, setTurnStartCards] = useState<number[] | null>(null);
  const rejectTimerRef     = useRef<ReturnType<typeof setTimeout> | null>(null);
  const prevDevCardsRef    = useRef<number[]>([]);

  // Navigate away when disconnected; also reset the module-level resume guard
  // so a future session can show the dialog again.
  useEffect(() => {
    if (connectionState === 'idle') {
      resumeAlertShown = false;
      router.replace('/');
    }
  }, [connectionState]);

  // Derive phase early so it can be used in hooks below.
  const phase = gameState?.phase ?? GamePhase.LOBBY;

  // ── Resume-game dialog ──────────────────────────────────────────────────
  // Only player 0 (slot 0) is prompted; others simply wait. Once the player
  // answers, the board clears hasSavedGame and stops broadcasting it.
  // resumeAlertShown is module-level so it survives component remounts that
  // can occur during navigation transitions or brief BLE reconnections.
  useEffect(() => {
    if (
      !gameState?.hasSavedGame ||
      phase !== GamePhase.LOBBY ||
      playerId !== 0 ||
      resumeAlertShown
    ) {
      return;
    }
    resumeAlertShown = true;
    Alert.alert(
      'Resume Previous Game?',
      'A saved game was found from a previous session. Would you like to continue where you left off?',
      [
        {
          text: 'Start New Game',
          style: 'destructive',
          onPress: () => sendAction(PlayerAction.RESUME_NO),
        },
        {
          text: 'Resume',
          style: 'default',
          onPress: () => sendAction(PlayerAction.RESUME_YES),
        },
      ],
      { cancelable: false },
    );
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [gameState?.hasSavedGame, phase, playerId]);


  const handleAction = useCallback(
    async (action: PlayerAction) => {
      setPendingAction(action);
      try { await sendAction(action); } catch { /* ignore */ }
      finally { setPendingAction(null); }
    },
    [sendAction],
  );

  const myId = playerId ?? 0;

  // Show a brief toast when the board rejects this player's action.
  useEffect(() => {
    const reason = gameState?.lastRejectReason ?? RejectReason.NONE;
    if (reason === RejectReason.NONE) return;
    if (gameState?.currentPlayer !== myId) return;
    const msg = REJECT_MESSAGES[reason] ?? 'Action rejected';
    if (rejectTimerRef.current) clearTimeout(rejectTimerRef.current);
    setRejectMessage(msg);
    Haptics.notificationAsync(Haptics.NotificationFeedbackType.Error);
    rejectTimerRef.current = setTimeout(() => setRejectMessage(null), 3000);
  }, [gameState?.lastRejectReason, gameState?.currentPlayer, myId]);

  useEffect(() => () => {
    if (rejectTimerRef.current) clearTimeout(rejectTimerRef.current);
  }, []);

  // Capture a snapshot of my dev-card counts at the very start of each of my turns.
  // Clear it when my turn ends. This lets us derive which cards were bought THIS turn.
  useEffect(() => {
    if (!gameState) return;
    if (gameState.currentPlayer === myId) {
      setTurnStartCards(prev => {
        // Only take the snapshot once — when my turn first begins.
        if (prev !== null) return prev;
        return gameState.devCards.slice(
          myId * DEV_CARD_COUNT,
          myId * DEV_CARD_COUNT + DEV_CARD_COUNT,
        );
      });
    } else {
      setTurnStartCards(null);
    }
  }, [gameState?.currentPlayer, gameState?.devCards, myId]);

  // Detect a newly drawn dev card and show the popup.
  useEffect(() => {
    if (!gameState) return;
    const curr = gameState.devCards;
    const prev = prevDevCardsRef.current;
    if (prev.length > 0) {
      for (let d = 0; d < DEV_CARD_COUNT; d++) {
        const prevCount = prev[myId * DEV_CARD_COUNT + d] ?? 0;
        const currCount = curr[myId * DEV_CARD_COUNT + d] ?? 0;
        if (currCount > prevCount) setDrawnCard(d as DevCard);
      }
    }
    prevDevCardsRef.current = curr.slice();
  }, [gameState?.devCards, myId]);

  // Derived: which of my cards were bought during this turn (play-locked).
  const boughtThisTurn = useMemo(() => {
    if (!gameState || !turnStartCards) return Array(DEV_CARD_COUNT).fill(0) as number[];
    return Array.from({ length: DEV_CARD_COUNT }, (_, d) => {
      const curr  = gameState.devCards[myId * DEV_CARD_COUNT + d] ?? 0;
      const start = turnStartCards[d] ?? 0;
      return Math.max(0, curr - start);
    });
  }, [gameState, myId, turnStartCards]);

  // ── Derived values ─────────────────────────────────────────────────────

  const numPlayers    = gameState?.numPlayers ?? 0;
  const currentPlayer = gameState?.currentPlayer ?? 0;
  const myTurn        = gameState != null && currentPlayer === myId;
  const hasRolled     = gameState?.hasRolled ?? false;
  const isPlaying     = phase === GamePhase.PLAYING;

  const connectedMask  = gameState?.connectedMask ?? 0;
  const connectedCount = [0, 1, 2, 3].filter(i => Boolean(connectedMask & (1 << i))).length;

  const showResources =
    phase === GamePhase.PLAYING ||
    phase === GamePhase.ROBBER  ||
    phase === GamePhase.DISCARD ||
    phase === GamePhase.GAME_OVER;

  const buttons = buttonsForPhase(
    phase,
    myTurn,
    hasRolled,
    connectedCount,
    (gameState?.trade?.fromPlayer ?? NO_PLAYER) !== NO_PLAYER,
  );
  const boardAvailable = (gameState?.tiles?.length ?? 0) > 0;

  const sendInputTyped = useCallback(
    (input: { action: PlayerAction; [k: string]: unknown }) =>
      sendInput(input as Partial<PlayerInput> & { action: PlayerAction }),
    [sendInput],
  );

  const handleDisconnect = useCallback(() => {    const activePhases: GamePhase[] = [
      GamePhase.BOARD_SETUP,
      GamePhase.NUMBER_REVEAL,
      GamePhase.INITIAL_PLACEMENT,
      GamePhase.PLAYING,
      GamePhase.ROBBER,
      GamePhase.DISCARD,
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

  const sharedProps = useMemo(() => gameState ? {
    state: gameState,
    myId,
    myTurn,
    sendInput: sendInputTyped,
    theme,
  } : null, [gameState, myId, myTurn, sendInputTyped, theme]);

  // ── Render ─────────────────────────────────────────────────────────────

  return (
    <View style={[s.root, { backgroundColor: theme.background }]}>
      <SafeAreaView style={s.safeArea} edges={['top', 'left', 'right']}>

        <PlacementToast message={rejectMessage} theme={theme} />

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

          {/* Difficulty selection in the lobby */}
          {phase === GamePhase.LOBBY && (
            <LobbyDifficultyPicker
              currentDifficulty={gameState?.difficulty ?? Difficulty.NORMAL}
              myId={myId}
            />
          )}

          {/* Orientation calibration in the lobby */}
          {phase === GamePhase.LOBBY && (
            <LobbyOrientationPicker />
          )}

          {sharedProps && showResources && (
            <>
              <DistributionToast {...sharedProps} />
              <ResourcesPanel {...sharedProps} />
              <PendingPurchasesBanner {...sharedProps} />
              {isPlaying && hasRolled && myTurn && (
                <>
                  <StorePanel {...sharedProps} />
                  <TradePanel {...sharedProps} />
                </>
              )}
              {isPlaying && (
                <DevCardsPanel {...sharedProps} boughtThisTurn={boughtThisTurn} />
              )}
              <Scoreboard {...sharedProps} />
            </>
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

      {/* Modal overlays */}
      {sharedProps && <RobberOverlay   {...sharedProps} />}
      {sharedProps && <StealOverlay    {...sharedProps} />}
      {sharedProps && <DiscardOverlay  {...sharedProps} />}
      {sharedProps && <IncomingTradeDialog {...sharedProps} />}
      <DrawnDevCardModal
        card={drawnCard}
        onDismiss={() => setDrawnCard(null)}
        theme={theme}
      />

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

  scroll:        { flex: 1 },
  scrollContent: { padding: Spacing.four, gap: Spacing.three, paddingBottom: Spacing.six },

  connectedLabel: { fontSize: 11, textAlign: 'center', marginTop: Spacing.two },
});
