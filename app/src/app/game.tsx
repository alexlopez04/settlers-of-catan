import { router } from 'expo-router';
import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  Alert,
  Animated,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  useWindowDimensions,
  View,
} from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import * as Haptics from 'expo-haptics';

import { useBle } from '@/context/ble-context';
import { useSettings } from '@/context/settings-context';
import { Spacing } from '@/constants/theme';
import { DIE_FACES, PHASE_LABEL, buttonsForPhase } from '@/constants/game';
import { useTheme } from '@/hooks/use-theme';
import {
  GamePhase,
  NO_PLAYER,
  PlayerAction,
  PlayerInput,
  RejectReason,
  REJECT_MESSAGES,
  DevCard,
  DEV_CARD_COUNT,
  Difficulty,
} from '@/services/proto';
import type { BoardState } from '@/services/proto';
import type { BoardRotation } from '@/utils/board-rotation';
import { SFSymbolIcon } from '@/components/ui/symbol';
import { BoardMap, PLAYER_FILL } from '@/components/ui/board-map';
import { PinchPanMap } from '@/components/ui/pinch-pan-map';
import { PlacementToast } from '@/components/game/placement-toast';
import { PhaseHero, FadeSlideIn } from '@/components/game/phase-hero';
import { ActionBar } from '@/components/game/action-bar';
import { LobbyOrientationPicker } from '@/components/game/lobby-orientation';
import { LobbyDifficultyPicker } from '@/components/game/lobby-difficulty-picker';
import { TutorialOverlay } from '@/components/game/tutorial-overlay';
import { useTutorial, type TutorialStepId } from '@/context/tutorial-context';
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
  DiceRollPopup,
} from '@/components/game/game-actions';

// ── Player avatar text colours (must stay in sync with PLAYER_FILL in board-map) ──
const PLAYER_TEXT_COLOR = ['#ffffff', '#ffffff', '#ffffff', '#1a1a1a'] as const;

// ── Expanded board overlay ────────────────────────────────────────────────────
// Rendered as an absolute-fill View (not a Modal) so that PlacementToast and
// other overlays rendered later in the tree still appear on top.

const ANIM_DURATION = 220;

interface BoardExpandedOverlayProps {
  visible: boolean;
  onClose: () => void;
  boardState: BoardState | null;
  boardRotation: BoardRotation;
  debug: { numberOverlay: boolean };
  theme: ReturnType<typeof useTheme>;
}

function BoardExpandedOverlay({
  visible, onClose, boardState, boardRotation, debug, theme,
}: BoardExpandedOverlayProps) {
  const { width, height } = useWindowDimensions();
  const opacity = useRef(new Animated.Value(0)).current;
  const scale   = useRef(new Animated.Value(0.96)).current;
  const [mounted, setMounted] = useState(false);

  const mapSize = Math.min(width, height * 0.82);

  useEffect(() => {
    if (visible) {
      setMounted(true);
      // Enter: fade in + scale up
      Animated.parallel([
        Animated.timing(opacity, { toValue: 1, duration: ANIM_DURATION, useNativeDriver: true }),
        Animated.timing(scale,   { toValue: 1, duration: ANIM_DURATION, useNativeDriver: true }),
      ]).start();
    } else {
      // Exit: fade out + scale down, then unmount
      Animated.parallel([
        Animated.timing(opacity, { toValue: 0, duration: ANIM_DURATION, useNativeDriver: true }),
        Animated.timing(scale,   { toValue: 0.96, duration: ANIM_DURATION, useNativeDriver: true }),
      ]).start(({ finished }) => {
        if (finished) setMounted(false);
      });
    }
  }, [visible]);

  if (!mounted) return null;

  return (
    <Animated.View style={[em.overlay, { backgroundColor: theme.background, opacity, transform: [{ scale }] }]}>
      <SafeAreaView style={em.safe} edges={['top', 'bottom', 'left', 'right']}>
        {/* Header */}
        <View style={[em.header, { borderBottomColor: theme.backgroundElement }]}>
          <Text style={[em.title, { color: theme.text }]}>Board</Text>
          <Pressable
            onPress={onClose}
            hitSlop={12}
            style={em.closeBtn}
            accessibilityLabel="Close board"
            accessibilityRole="button">
            <SFSymbolIcon name="xmark.circle.fill" size={28} color={theme.textSecondary} fallback="✕" />
          </Pressable>
        </View>

        {/* Zoomable/pannable map — gesture area fills all available space */}
        <View style={em.mapArea}>
          <PinchPanMap
            size={mapSize}
            containerStyle={em.pinchContainer}>
            <BoardMap
              tiles={boardState?.tiles ?? null}
              vertices={boardState?.vertices}
              edges={boardState?.edges}
              robberTile={boardState?.robberTile}
              rotation={boardRotation}
              size={mapSize}
              showPorts
              showVertexIndices={debug.numberOverlay}
              showEdgeIndices={debug.numberOverlay}
            />
          </PinchPanMap>
        </View>
      </SafeAreaView>
    </Animated.View>
  );
}

const em = StyleSheet.create({
  overlay: {
    ...StyleSheet.absoluteFillObject,
    zIndex: 50,
  },
  safe:    { flex: 1 },
  header: {
    flexDirection:     'row',
    alignItems:        'center',
    justifyContent:    'space-between',
    paddingHorizontal: Spacing.four,
    paddingVertical:   Spacing.three,
    borderBottomWidth: StyleSheet.hairlineWidth,
  },
  title:    { fontSize: 20, fontWeight: '700' },
  closeBtn: { padding: Spacing.one },
  mapArea: {
    flex:           1,
    alignItems:     'center',
    justifyContent: 'center',
  },
  pinchContainer: {
    flex:       1,
    alignSelf:  'stretch',
    overflow:   'hidden',
    width:      undefined,
    height:     undefined,
  },
});

// ── Compact status row (PLAYING phase) ────────────────────────────────────────

type Theme = ReturnType<typeof useTheme>;

function CompactPlayingStatus({
  myTurn, currentPlayer, hasRolled, die1, die2, theme,
}: {
  myTurn: boolean;
  currentPlayer: number;
  hasRolled: boolean;
  die1: number;
  die2: number;
  theme: Theme;
}) {
  return (
    <View style={[cs.row, { backgroundColor: theme.backgroundElement }]}>
      <View style={[
        cs.turnPill,
        {
          backgroundColor: myTurn ? theme.primary : 'transparent',
          borderColor:      myTurn ? theme.primary : theme.textSecondary,
        },
      ]}>
        <Text style={[cs.turnText, { color: myTurn ? '#fff' : theme.text }]}>
          {myTurn ? 'Your turn' : `Player ${currentPlayer + 1}'s turn`}
        </Text>
      </View>

      {hasRolled && die1 > 0 && die2 > 0 ? (
        <View style={cs.diceRow}>
          <Text style={[cs.diceFaces, { color: theme.text }]}>{DIE_FACES[die1]}  {DIE_FACES[die2]}</Text>
          <View style={[cs.totalBadge, { backgroundColor: theme.primary }]}>
            <Text style={cs.totalText}>{die1 + die2}</Text>
          </View>
        </View>
      ) : (
        <Text style={[cs.rollHint, { color: theme.textSecondary }]}>
          {myTurn ? 'Tap Roll Dice ↓' : 'Waiting for roll…'}
        </Text>
      )}
    </View>
  );
}

const cs = StyleSheet.create({
  row: {
    flexDirection:     'row',
    alignItems:        'center',
    justifyContent:    'space-between',
    borderRadius:      14,
    paddingHorizontal: Spacing.three,
    paddingVertical:   Spacing.two,
  },
  turnPill: {
    borderRadius:      20,
    borderWidth:       1.5,
    paddingHorizontal: Spacing.three,
    paddingVertical:   6,
  },
  turnText:   { fontSize: 14, fontWeight: '700' },
  diceRow:    { flexDirection: 'row', alignItems: 'center', gap: Spacing.two },
  diceFaces:  { fontSize: 22, lineHeight: 28 },
  totalBadge: {
    borderRadius:      10,
    paddingHorizontal: Spacing.two,
    paddingVertical:   3,
    minWidth:          32,
    alignItems:        'center',
  },
  totalText: { fontSize: 15, fontWeight: '800', color: '#fff' },
  rollHint:  { fontSize: 13, fontWeight: '500' },
});

// ── Module-level resume-dialog guard ─────────────────────────────────────────
let resumeAlertShown = false;

// ── Main screen ───────────────────────────────────────────────────────────────

export default function GameScreen() {
  const theme                         = useTheme();
  const { boardRotation, debug }      = useSettings();
  const { width: screenWidth }        = useWindowDimensions();
  const {
    connectionState,
    connectedName,
    playerId,
    gameState,
    sendAction,
    sendInput,
    disconnect,
  } = useBle();

  const [pendingAction,     setPendingAction]     = useState<PlayerAction | null>(null);
  const [rejectMessage,     setRejectMessage]     = useState<string | null>(null);
  const [drawnCard,         setDrawnCard]         = useState<DevCard | null>(null);
  const [showBoardExpanded, setShowBoardExpanded] = useState(false);
  const [showDicePopup,     setShowDicePopup]     = useState(false);
  const [turnStartCards,    setTurnStartCards]    = useState<number[] | null>(null);

  // ── Tutorial ──────────────────────────────────────────────────────────────
  const { shouldShowStep, markStepSeen, skipAll: skipTutorial } = useTutorial();
  const [activeTutorialStep, setActiveTutorialStep] = useState<TutorialStepId | null>(null);
  const prevTutorialPhaseRef = useRef<GamePhase | null>(null);
  const tutorialFirstRollRef = useRef(false);

  const showTutorialStep = useCallback((id: TutorialStepId) => {
    if (shouldShowStep(id)) setActiveTutorialStep(id);
  }, [shouldShowStep]);

  const handleTutorialDismiss = useCallback(() => {
    setActiveTutorialStep(prev => {
      if (prev) markStepSeen(prev);
      return null;
    });
  }, [markStepSeen]);

  const handleTutorialSkip = useCallback(() => {
    skipTutorial();
    setActiveTutorialStep(null);
  }, [skipTutorial]);
  const prevDevCardsRef   = useRef<number[]>([]);
  const prevHasRolledRef  = useRef(false);

  useEffect(() => {
    if (connectionState === 'idle') {
      resumeAlertShown = false;
      router.replace('/');
    }
  }, [connectionState]);

  const phase = gameState?.phase ?? GamePhase.LOBBY;

  useEffect(() => {
    if (
      !gameState?.hasSavedGame ||
      phase !== GamePhase.LOBBY ||
      playerId !== 0 ||
      resumeAlertShown
    ) return;
    resumeAlertShown = true;
    Alert.alert(
      'Resume Previous Game?',
      'A saved game was found from a previous session. Would you like to continue where you left off?',
      [
        { text: 'Start New Game', style: 'destructive', onPress: () => sendAction(PlayerAction.RESUME_NO) },
        { text: 'Resume',         style: 'default',     onPress: () => sendAction(PlayerAction.RESUME_YES) },
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

  // Show the dice popup whenever a new roll lands (for all players)
  useEffect(() => {
    const rolled = gameState?.hasRolled ?? false;
    if (rolled && !prevHasRolledRef.current && (gameState?.die1 ?? 0) > 0) {
      setShowDicePopup(true);
    }
    prevHasRolledRef.current = rolled;
  }, [gameState?.hasRolled, gameState?.die1]);

  useEffect(() => {
    const reason = gameState?.lastRejectReason ?? RejectReason.NONE;
    if (reason === RejectReason.NONE) return;
    if (gameState?.currentPlayer !== myId) return;
    const msg = REJECT_MESSAGES[reason] ?? 'Action rejected';
    setRejectMessage(msg);
    Haptics.notificationAsync(Haptics.NotificationFeedbackType.Error);
  }, [gameState?.lastRejectReason, gameState?.currentPlayer, myId]);

  useEffect(() => {
    if (!gameState) return;
    if (gameState.currentPlayer !== myId) {
      setTurnStartCards(null);
      return;
    }
    // It's my turn. Refresh the snapshot only at turn-start (before rolling)
    // so cards bought after rolling are detected as "new this turn". This
    // also handles the 1-player simulator where currentPlayer never changes —
    // END_TURN resets hasRolled to false, which triggers a fresh snapshot.
    if (!gameState.hasRolled) {
      setTurnStartCards(
        gameState.devCards.slice(myId * DEV_CARD_COUNT, myId * DEV_CARD_COUNT + DEV_CARD_COUNT),
      );
    }
  }, [gameState?.currentPlayer, gameState?.hasRolled, gameState?.devCards, myId]);

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

  const boughtThisTurn = useMemo(() => {
    if (!gameState || !turnStartCards) return Array(DEV_CARD_COUNT).fill(0) as number[];
    return Array.from({ length: DEV_CARD_COUNT }, (_, d) => {
      const curr  = gameState.devCards[myId * DEV_CARD_COUNT + d] ?? 0;
      const start = turnStartCards[d] ?? 0;
      return Math.max(0, curr - start);
    });
  }, [gameState, myId, turnStartCards]);

  // ── Derived values ──────────────────────────────────────────────────────

  const numPlayers    = gameState?.numPlayers ?? 0;
  const currentPlayer = gameState?.currentPlayer ?? 0;
  const myTurn        = gameState != null && currentPlayer === myId;
  const hasRolled     = gameState?.hasRolled ?? false;
  const isPlaying     = phase === GamePhase.PLAYING;

  // ── Tutorial effects (placed after phase + hasRolled are declared) ────────

  // Trigger a tutorial step on each phase transition.
  useEffect(() => {
    const prev = prevTutorialPhaseRef.current;
    prevTutorialPhaseRef.current = phase;
    if (prev === null) return; // skip initial mount
    if (prev === phase) return;

    // Reset first-roll tracker whenever we leave PLAYING.
    if (phase !== GamePhase.PLAYING) tutorialFirstRollRef.current = false;

    const phaseStepMap: Partial<Record<GamePhase, TutorialStepId>> = {
      [GamePhase.LOBBY]:             'welcome',
      [GamePhase.BOARD_SETUP]:       'board_setup',
      [GamePhase.NUMBER_REVEAL]:     'number_reveal',
      [GamePhase.INITIAL_PLACEMENT]: 'initial_placement',
      [GamePhase.PLAYING]:           'first_roll',
      [GamePhase.ROBBER]:            'robber',
    };

    const stepId = phaseStepMap[phase];
    if (stepId) showTutorialStep(stepId);
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [phase]);

  // "Build & Trade" step fires on the very first dice roll in a PLAYING phase.
  useEffect(() => {
    if (phase !== GamePhase.PLAYING) return;
    if (!hasRolled) return;
    if (tutorialFirstRollRef.current) return;
    tutorialFirstRollRef.current = true;
    // Delay slightly so the dice-roll popup can clear first.
    const t = setTimeout(() => {
      setActiveTutorialStep(prev => {
        if (prev !== null) return prev;
        return shouldShowStep('use_resources') ? 'use_resources' : null;
      });
    }, 300);
    return () => clearTimeout(t);
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [hasRolled, phase]);

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
    myId,
    (gameState?.trade?.fromPlayer ?? NO_PLAYER) !== NO_PLAYER,
  );
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

  const sharedProps = useMemo(() => gameState ? {
    state: gameState,
    myId,
    myTurn,
    sendInput: sendInputTyped,
    theme,
  } : null, [gameState, myId, myTurn, sendInputTyped, theme]);

  const isLobby         = phase === GamePhase.LOBBY;
  const showBoardInline = boardAvailable && !isLobby;

  // Inline map: fills the card (scroll horizontal padding is Spacing.four each side)
  const inlineMapSize = screenWidth - Spacing.four * 2;

  // ── Render ──────────────────────────────────────────────────────────────

  return (
    <View style={[s.root, { backgroundColor: theme.background }]}>
      <SafeAreaView style={s.safeArea} edges={['top', 'left', 'right']}>

        {/* ── Compact header ──────────────────────────────────────────── */}
        <View style={[s.header, { borderBottomColor: theme.backgroundElement }]}>
          <View style={s.headerLeft}>
            <View style={[s.playerAvatar, { backgroundColor: PLAYER_FILL[myId] }]}>
              <Text style={[s.playerAvatarText, { color: PLAYER_TEXT_COLOR[myId] }]}>
                P{myId + 1}
              </Text>
            </View>
            <View style={[s.phaseBadge, { backgroundColor: theme.backgroundElement }]}>
              <Text style={[s.phaseText, { color: theme.textSecondary }]}>
                {PHASE_LABEL[phase]}
              </Text>
            </View>
          </View>
          <View style={s.headerRight}>
            <Pressable
            onPress={() => router.push('/rules')}
            hitSlop={12}
            accessibilityLabel="View rules"
            accessibilityRole="button"
            style={({ pressed }) => [s.headerBtn, { opacity: pressed ? 0.5 : 1 }]}>
            <SFSymbolIcon name="book.closed" size={22} color={theme.textSecondary} fallback="📖" />
          </Pressable>
          <Pressable
            onPress={handleDisconnect}
            hitSlop={12}
            accessibilityLabel="Leave game"
            accessibilityRole="button"
            style={({ pressed }) => [s.headerBtn, { opacity: pressed ? 0.5 : 1 }]}>
            <SFSymbolIcon name="xmark.circle" size={22} color={theme.textSecondary} fallback="✕" />
          </Pressable>
          </View>
        </View>

        {/* ── Scrollable content ───────────────────────────────────────── */}
        <ScrollView
          style={s.scroll}
          contentContainerStyle={s.scrollContent}
          showsVerticalScrollIndicator={false}>

          {/* ── Board map card — compact inline, tap to expand ────────── */}
          {showBoardInline && (
            <Pressable
              onPress={() => setShowBoardExpanded(true)}
              accessibilityLabel="View full board map"
              accessibilityRole="button"
              style={({ pressed }) => [
                s.boardCard,
                { backgroundColor: theme.backgroundElement, opacity: pressed ? 0.88 : 1 },
              ]}>
              <BoardMap
                tiles={gameState?.tiles ?? null}
                vertices={gameState?.vertices}
                edges={gameState?.edges}
                robberTile={gameState?.robberTile}
                rotation={boardRotation}
                size={inlineMapSize}
                showPorts
                showVertexIndices={debug.numberOverlay}
                showEdgeIndices={debug.numberOverlay}
              />
              {/* Expand affordance badge */}
              <View style={s.expandBadge}>
                <SFSymbolIcon
                  name="arrow.up.left.and.arrow.down.right"
                  size={12}
                  color="#fff"
                  fallback="⤢"
                />
              </View>
            </Pressable>
          )}

          {/* ── Phase status ─────────────────────────────────────────── */}
          {showBoardInline && isPlaying ? (
            <CompactPlayingStatus
              myTurn={myTurn}
              currentPlayer={currentPlayer}
              hasRolled={hasRolled}
              die1={gameState?.die1 ?? 0}
              die2={gameState?.die2 ?? 0}
              theme={theme}
            />
          ) : (
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
          )}

          {/* ── Lobby pickers ────────────────────────────────────────── */}
          {isLobby && (
            <>
              <LobbyDifficultyPicker
                currentDifficulty={gameState?.difficulty ?? Difficulty.NORMAL}
                myId={myId}
              />
              <LobbyOrientationPicker />
            </>
          )}

          {/* ── Resource / store / trade / dev-card panels ───────────── */}
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

        {/* ── Action bar ──────────────────────────────────────────────── */}
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

      {/* ── Expanded board overlay (below alerts/toasts in render order) ── */}
      <BoardExpandedOverlay
        visible={showBoardExpanded}
        onClose={() => setShowBoardExpanded(false)}
        boardState={gameState}
        boardRotation={boardRotation}
        debug={debug}
        theme={theme}
      />

      {/* ── Full-screen overlays ─────────────────────────────────────── */}
      {sharedProps && !showDicePopup && <RobberOverlay {...sharedProps} />}
      {sharedProps && <StealOverlay       {...sharedProps} />}
      {sharedProps && <DiscardOverlay     {...sharedProps} />}
      {sharedProps && <IncomingTradeDialog {...sharedProps} />}
      <DrawnDevCardModal
        card={drawnCard}
        onDismiss={() => setDrawnCard(null)}
        theme={theme}
      />
      <PlacementToast message={rejectMessage} onClose={() => setRejectMessage(null)} theme={theme} />
      <DiceRollPopup
        visible={showDicePopup}
        die1={gameState?.die1 ?? 1}
        die2={gameState?.die2 ?? 1}
        resources={gameState?.lastDistribution.slice(myId * 5, myId * 5 + 5)}
        onDismiss={() => setShowDicePopup(false)}
        theme={theme}
      />
      <TutorialOverlay
        stepId={activeTutorialStep}
        onDismiss={handleTutorialDismiss}
        onSkip={handleTutorialSkip}
        theme={theme}
      />
    </View>
  );
}

// ── Styles ────────────────────────────────────────────────────────────────

const s = StyleSheet.create({
  root:     { flex: 1 },
  safeArea: { flex: 1 },

  header: {
    flexDirection:     'row',
    alignItems:        'center',
    justifyContent:    'space-between',
    paddingHorizontal: Spacing.four,
    paddingVertical:   Spacing.two,
    borderBottomWidth: StyleSheet.hairlineWidth,
  },
  headerLeft: { flexDirection: 'row', alignItems: 'center', gap: Spacing.two },
  headerRight: { flexDirection: 'row', alignItems: 'center', gap: Spacing.two },
  playerAvatar: {
    width:          34,
    height:         34,
    borderRadius:   17,
    alignItems:     'center',
    justifyContent: 'center',
  },
  playerAvatarText: { fontSize: 13, fontWeight: '800' },
  phaseBadge: {
    alignSelf:         'flex-start',
    paddingHorizontal: Spacing.two,
    paddingVertical:   3,
    borderRadius:      6,
  },
  phaseText: { fontSize: 12, fontWeight: '600' },
  headerBtn: { padding: Spacing.one },

  scroll:        { flex: 1 },
  scrollContent: { padding: Spacing.four, gap: Spacing.three, paddingBottom: Spacing.six },

  boardCard: {
    borderRadius: 16,
    overflow:     'hidden',
    // Subtle depth
    shadowColor:   '#000',
    shadowOffset:  { width: 0, height: 1 },
    shadowOpacity: 0.08,
    shadowRadius:  4,
    elevation:     2,
  },
  expandBadge: {
    position:     'absolute',
    top:          Spacing.two,
    right:        Spacing.two,
    borderRadius: 8,
    padding:      6,
    backgroundColor: 'rgba(0,0,0,0.32)',
  },

  connectedLabel: { fontSize: 11, textAlign: 'center', marginTop: Spacing.two },
});
