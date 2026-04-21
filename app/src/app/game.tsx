import { router } from 'expo-router';
import React, { useCallback, useEffect } from 'react';
import {
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { useBle } from '@/context/ble-context';
import { Spacing } from '@/constants/theme';
import { useTheme } from '@/hooks/use-theme';
import { GamePhase, PlayerAction } from '@/services/proto';

// ── Phase labels ───────────────────────────────────────────────────────────

const PHASE_LABEL: Record<GamePhase, string> = {
  [GamePhase.PHASE_WAITING_FOR_PLAYERS]: 'Waiting for Players',
  [GamePhase.PHASE_BOARD_SETUP]: 'Board Setup',
  [GamePhase.PHASE_NUMBER_REVEAL]: 'Number Reveal',
  [GamePhase.PHASE_INITIAL_PLACEMENT]: 'Initial Placement',
  [GamePhase.PHASE_PLAYING]: 'Main Game',
  [GamePhase.PHASE_ROBBER]: 'Robber',
  [GamePhase.PHASE_TRADE]: 'Trading',
  [GamePhase.PHASE_GAME_OVER]: 'Game Over',
};

// ── Resource data ─────────────────────────────────────────────────────────

const RESOURCES = [
  { key: 'resLumber' as const, emoji: '🪵', label: 'Lumber', color: '#6B3A1F' },
  { key: 'resWool'   as const, emoji: '🐑', label: 'Wool',   color: '#5A8C35' },
  { key: 'resGrain'  as const, emoji: '🌾', label: 'Grain',  color: '#C8991A' },
  { key: 'resBrick'  as const, emoji: '🧱', label: 'Brick',  color: '#B84B2A' },
  { key: 'resOre'    as const, emoji: '⛏',  label: 'Ore',    color: '#7A7A8C' },
];

const NO_WINNER = 0xff;

// ── Die face ──────────────────────────────────────────────────────────────

const DIE_FACES = ['', '⚀', '⚁', '⚂', '⚃', '⚄', '⚅'];

export default function GameScreen() {
  const theme = useTheme();
  const { connectionState, connectedName, gameState, sendAction, disconnect } = useBle();

  // Return to scan screen when disconnected
  useEffect(() => {
    if (connectionState === 'idle') {
      router.replace('/');
    }
  }, [connectionState]);

  const handleAction = useCallback(
    async (action: PlayerAction) => {
      try {
        await sendAction(action);
      } catch {
        // Silently ignore; the user can retry
      }
    },
    [sendAction],
  );

  const handleDisconnect = useCallback(async () => {
    await disconnect();
  }, [disconnect]);

  // ── Derived state ──────────────────────────────────────────────────────

  const phase = gameState?.phase ?? GamePhase.PHASE_WAITING_FOR_PLAYERS;
  const yourId = gameState?.yourPlayerId ?? 0;
  const numPlayers = gameState?.numPlayers ?? 0;
  const isPlaying =
    phase === GamePhase.PHASE_PLAYING ||
    phase === GamePhase.PHASE_ROBBER ||
    phase === GamePhase.PHASE_TRADE;
  const showDice = isPlaying && (gameState?.diceTotal ?? 0) > 0;
  const showResources = isPlaying || phase === GamePhase.PHASE_INITIAL_PLACEMENT;
  const isGameOver = phase === GamePhase.PHASE_GAME_OVER;

  // ── UI ─────────────────────────────────────────────────────────────────

  return (
    <View style={[styles.root, { backgroundColor: theme.background }]}>
      <SafeAreaView style={styles.safeArea} edges={['top', 'left', 'right']}>

        {/* ── Header ── */}
        <View style={[styles.header, { borderBottomColor: theme.backgroundElement }]}>
          <View style={styles.headerLeft}>
            <Text style={[styles.playerName, { color: theme.text }]}>
              Player {yourId + 1}
            </Text>
            <View style={[styles.phaseBadge, { backgroundColor: theme.backgroundElement }]}>
              <Text style={[styles.phaseText, { color: theme.textSecondary }]}>
                {PHASE_LABEL[phase]}
              </Text>
            </View>
          </View>
          <Pressable
            onPress={handleDisconnect}
            hitSlop={12}
            style={({ pressed }) => [styles.disconnectBtn, { opacity: pressed ? 0.6 : 1 }]}>
            <Text style={[styles.disconnectText, { color: theme.textSecondary }]}>✕</Text>
          </Pressable>
        </View>

        {/* ── Scrollable content ── */}
        <ScrollView
          style={styles.scroll}
          contentContainerStyle={styles.scrollContent}
          showsVerticalScrollIndicator={false}>

          {/* Dice */}
          {showDice && (
            <View style={[styles.card, { backgroundColor: theme.backgroundElement }]}>
              <Text style={[styles.diceDisplay, { color: theme.text }]}>
                {DIE_FACES[gameState!.die1]}  {DIE_FACES[gameState!.die2]}
              </Text>
              <Text style={[styles.diceTotal, { color: theme.primary }]}>
                {gameState!.diceTotal}
              </Text>
            </View>
          )}

          {/* Number reveal */}
          {phase === GamePhase.PHASE_NUMBER_REVEAL && (gameState?.revealNumber ?? 0) > 0 && (
            <View style={[styles.card, { backgroundColor: theme.backgroundElement }]}>
              <Text style={[styles.revealLabel, { color: theme.textSecondary }]}>Revealing</Text>
              <Text style={[styles.revealNumber, { color: theme.primary }]}>
                {gameState!.revealNumber}
              </Text>
            </View>
          )}

          {/* Status text */}
          {(gameState?.line1 || gameState?.line2) && (
            <View style={styles.statusSection}>
              {gameState?.line1 ? (
                <Text style={[styles.statusLine1, { color: theme.text }]}>{gameState.line1}</Text>
              ) : null}
              {gameState?.line2 ? (
                <Text style={[styles.statusLine2, { color: theme.textSecondary }]}>
                  {gameState.line2}
                </Text>
              ) : null}
            </View>
          )}

          {/* Resources */}
          {showResources && gameState && (
            <View style={styles.section}>
              <Text style={[styles.sectionLabel, { color: theme.textSecondary }]}>Resources</Text>
              <View style={[styles.resourcesRow, { backgroundColor: theme.backgroundElement }]}>
                {RESOURCES.map(res => (
                  <View key={res.key} style={styles.resourceCell}>
                    <Text style={styles.resourceEmoji}>{res.emoji}</Text>
                    <Text style={[styles.resourceCount, { color: theme.text }]}>
                      {gameState[res.key]}
                    </Text>
                  </View>
                ))}
              </View>
            </View>
          )}

          {/* Victory Points */}
          {numPlayers > 0 && gameState && (
            <View style={styles.section}>
              <Text style={[styles.sectionLabel, { color: theme.textSecondary }]}>
                Victory Points
              </Text>
              <View style={[styles.vpRow, { backgroundColor: theme.backgroundElement }]}>
                {Array.from({ length: numPlayers }, (_, i) => {
                  const isYou = i === yourId;
                  const isWinner = isGameOver && gameState.winnerId === i;
                  return (
                    <View
                      key={i}
                      style={[
                        styles.vpCell,
                        isYou && { backgroundColor: theme.backgroundSelected, borderRadius: 10 },
                      ]}>
                      {isWinner && <Text style={styles.trophyIcon}>🏆</Text>}
                      <Text style={[styles.vpLabel, { color: theme.textSecondary }]}>
                        P{i + 1}{isYou ? ' ●' : ''}
                      </Text>
                      <Text style={[styles.vpCount, { color: theme.text }]}>
                        {gameState.vp[i as 0 | 1 | 2 | 3]}
                      </Text>
                    </View>
                  );
                })}
              </View>
            </View>
          )}

          {/* Game over banner */}
          {isGameOver && gameState && gameState.winnerId !== NO_WINNER && (
            <View style={[styles.winnerBanner, { backgroundColor: theme.primary }]}>
              <Text style={styles.winnerText}>
                🏆  Player {gameState.winnerId + 1} wins!
              </Text>
            </View>
          )}

          {/* Connection name */}
          <Text style={[styles.connectedLabel, { color: theme.textSecondary }]}>
            {connectedName ?? ''}
          </Text>
        </ScrollView>

        {/* ── Action buttons ── */}
        {gameState && (
          <ActionBar
            btnLeft={gameState.btnLeft}
            btnCenter={gameState.btnCenter}
            btnRight={gameState.btnRight}
            onLeft={() => handleAction(PlayerAction.ACTION_BTN_LEFT)}
            onCenter={() => handleAction(PlayerAction.ACTION_BTN_CENTER)}
            onRight={() => handleAction(PlayerAction.ACTION_BTN_RIGHT)}
            primaryColor={theme.primary}
            surfaceColor={theme.backgroundElement}
            textColor={theme.text}
          />
        )}
      </SafeAreaView>
    </View>
  );
}

// ── ActionBar component ───────────────────────────────────────────────────

interface ActionBarProps {
  btnLeft: string;
  btnCenter: string;
  btnRight: string;
  onLeft: () => void;
  onCenter: () => void;
  onRight: () => void;
  primaryColor: string;
  surfaceColor: string;
  textColor: string;
}

function ActionBar({
  btnLeft, btnCenter, btnRight,
  onLeft, onCenter, onRight,
  primaryColor, surfaceColor, textColor,
}: ActionBarProps) {
  const hasAny = btnLeft || btnCenter || btnRight;
  if (!hasAny) return null;

  return (
    <SafeAreaView edges={['bottom']} style={[styles.actionBar, { borderTopColor: surfaceColor }]}>
      <View style={styles.actionBarInner}>
        <ActionButton label={btnLeft}   onPress={onLeft}   primary={primaryColor} surface={surfaceColor} text={textColor} />
        <ActionButton label={btnCenter} onPress={onCenter} primary={primaryColor} surface={surfaceColor} text={textColor} />
        <ActionButton label={btnRight}  onPress={onRight}  primary={primaryColor} surface={surfaceColor} text={textColor} />
      </View>
    </SafeAreaView>
  );
}

interface ActionButtonProps {
  label: string;
  onPress: () => void;
  primary: string;
  surface: string;
  text: string;
}

function ActionButton({ label, onPress, primary, surface, text }: ActionButtonProps) {
  if (!label) {
    return <View style={styles.actionButtonPlaceholder} />;
  }
  return (
    <Pressable
      onPress={onPress}
      style={({ pressed }) => [
        styles.actionButton,
        { backgroundColor: surface, opacity: pressed ? 0.7 : 1 },
      ]}>
      <Text style={[styles.actionButtonText, { color: text }]}>{label}</Text>
    </Pressable>
  );
}

// ── Styles ────────────────────────────────────────────────────────────────

const styles = StyleSheet.create({
  root: { flex: 1 },
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
  headerLeft: { gap: Spacing.one },
  playerName: { fontSize: 20, fontWeight: '700' },
  phaseBadge: {
    alignSelf: 'flex-start',
    paddingHorizontal: Spacing.two,
    paddingVertical: 3,
    borderRadius: 6,
  },
  phaseText: { fontSize: 12, fontWeight: '600' },
  disconnectBtn: { padding: Spacing.two },
  disconnectText: { fontSize: 20, fontWeight: '300' },

  // Scroll
  scroll: { flex: 1 },
  scrollContent: {
    padding: Spacing.four,
    gap: Spacing.three,
    paddingBottom: Spacing.six,
  },

  // Card
  card: {
    borderRadius: 16,
    padding: Spacing.four,
    alignItems: 'center',
  },
  diceDisplay: { fontSize: 56, lineHeight: 64 },
  diceTotal: { fontSize: 40, fontWeight: '800', marginTop: Spacing.one },
  revealLabel: { fontSize: 13, fontWeight: '600', marginBottom: Spacing.one },
  revealNumber: { fontSize: 64, fontWeight: '800' },

  // Status
  statusSection: { gap: Spacing.one },
  statusLine1: { fontSize: 22, fontWeight: '700', textAlign: 'center' },
  statusLine2: { fontSize: 16, textAlign: 'center' },

  // Section
  section: { gap: Spacing.two },
  sectionLabel: { fontSize: 12, fontWeight: '700', textTransform: 'uppercase', letterSpacing: 0.8 },

  // Resources
  resourcesRow: {
    flexDirection: 'row',
    borderRadius: 16,
    padding: Spacing.three,
    justifyContent: 'space-around',
  },
  resourceCell: { alignItems: 'center', gap: Spacing.one },
  resourceEmoji: { fontSize: 26 },
  resourceCount: { fontSize: 18, fontWeight: '700' },

  // VP
  vpRow: {
    flexDirection: 'row',
    borderRadius: 16,
    padding: Spacing.three,
    justifyContent: 'space-around',
  },
  vpCell: { alignItems: 'center', paddingVertical: Spacing.one, paddingHorizontal: Spacing.two, gap: 2 },
  trophyIcon: { fontSize: 16 },
  vpLabel: { fontSize: 12, fontWeight: '600' },
  vpCount: { fontSize: 24, fontWeight: '800' },

  // Winner
  winnerBanner: { borderRadius: 14, padding: Spacing.three, alignItems: 'center' },
  winnerText: { color: '#fff', fontSize: 20, fontWeight: '700' },

  // Footer labels
  connectedLabel: { fontSize: 12, textAlign: 'center', marginTop: Spacing.two },

  // Action bar
  actionBar: { borderTopWidth: StyleSheet.hairlineWidth },
  actionBarInner: {
    flexDirection: 'row',
    padding: Spacing.three,
    gap: Spacing.two,
  },
  actionButton: {
    flex: 1,
    borderRadius: 14,
    paddingVertical: Spacing.three,
    alignItems: 'center',
    justifyContent: 'center',
    minHeight: 52,
  },
  actionButtonPlaceholder: { flex: 1 },
  actionButtonText: { fontSize: 15, fontWeight: '600' },
});
