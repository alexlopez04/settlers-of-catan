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
import { SafeAreaView } from 'react-native-safe-area-context';

import * as Haptics from 'expo-haptics';

import { useBle } from '@/context/ble-context';
import { Spacing } from '@/constants/theme';
import { PHASE_LABEL, buttonsForPhase } from '@/constants/game';
import { useTheme } from '@/hooks/use-theme';
import { GamePhase, PlayerAction, PlayerInput, RejectReason, REJECT_MESSAGES } from '@/services/proto';
import { SFSymbolIcon } from '@/components/ui/symbol';
import { BoardOverview } from '@/components/ui/board-overview';
import { PlacementToast } from '@/components/game/placement-toast';
import { PhaseHero, FadeSlideIn } from '@/components/game/phase-hero';
import { ActionBar } from '@/components/game/action-bar';
import { LobbyOrientationPicker } from '@/components/game/lobby-orientation';
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
} from '@/components/game/game-actions';

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
  const rejectTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  // Navigate away when disconnected.
  useEffect(() => {
    if (connectionState === 'idle') router.replace('/');
  }, [connectionState]);

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

  // ── Derived values ─────────────────────────────────────────────────────

  const phase         = gameState?.phase ?? GamePhase.LOBBY;
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

  const buttons = buttonsForPhase(phase, myTurn, hasRolled, connectedCount);
  const boardAvailable = (gameState?.tiles?.length ?? 0) > 0;

  const sendInputTyped = useCallback(
    (input: { action: PlayerAction; [k: string]: unknown }) =>
      sendInput(input as Partial<PlayerInput> & { action: PlayerAction }),
    [sendInput],
  );

  const handleDisconnect = useCallback(() => {
    const activePhases: GamePhase[] = [
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

  const sharedProps = gameState ? {
    state: gameState,
    myId,
    myTurn,
    sendInput: sendInputTyped,
    theme,
  } : null;

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
                <DevCardsPanel {...sharedProps} />
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
