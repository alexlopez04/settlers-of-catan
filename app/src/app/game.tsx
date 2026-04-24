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
  withSpring,
  withSequence,
} from 'react-native-reanimated';
import { SafeAreaView } from 'react-native-safe-area-context';

import * as Haptics from 'expo-haptics';

import { useBle } from '@/context/ble-context';
import type { ResourceCounts } from '@/context/ble-context';
import { Spacing } from '@/constants/theme';
import { PHASE_LABEL, RESOURCES, buttonsForPhase } from '@/constants/game';
import type { ResKey } from '@/constants/game';
import { useTheme } from '@/hooks/use-theme';
import { GamePhase, PlayerAction, RejectReason, REJECT_MESSAGES } from '@/services/proto';
import { SFSymbolIcon } from '@/components/ui/symbol';
import { BoardOverview } from '@/components/ui/board-overview';
import { PlacementToast } from '@/components/game/placement-toast';
import { Stepper } from '@/components/game/stepper';
import { SyncIndicator } from '@/components/game/sync-indicator';
import { PhaseHero, FadeSlideIn } from '@/components/game/phase-hero';
import { ActionBar } from '@/components/game/action-bar';

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
  const [rejectMessage, setRejectMessage] = useState<string | null>(null);
  const rejectTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
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

  // Show a brief toast when the board rejects this player's placement.
  useEffect(() => {
    const reason = gameState?.lastRejectReason ?? RejectReason.NONE;
    if (reason === RejectReason.NONE) return;
    // Only show the rejection to the player whose turn it was (current player).
    // The board sets lastRejectReason for exactly one broadcast, so by the time
    // we receive it the current_player field still identifies the offending player.
    if (gameState?.currentPlayer !== myId) return;
    const msg = REJECT_MESSAGES[reason] ?? 'Invalid placement';
    if (rejectTimerRef.current) clearTimeout(rejectTimerRef.current);
    setRejectMessage(msg);
    Haptics.notificationAsync(Haptics.NotificationFeedbackType.Error);
    rejectTimerRef.current = setTimeout(() => setRejectMessage(null), 3000);
  }, [gameState?.lastRejectReason]);

  // Clean up the dismiss timer on unmount.
  useEffect(() => () => {
    if (rejectTimerRef.current) clearTimeout(rejectTimerRef.current);
  }, []);
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

        {/* Placement rejection toast (overlays everything) */}
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
});
