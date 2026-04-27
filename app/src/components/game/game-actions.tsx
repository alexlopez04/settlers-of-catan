// =============================================================================
// game-actions.tsx — v8 store / dev cards / trade / robber / discard panels.
//
// The ESP32-C6 is authoritative: every interaction here just sends a
// PlayerInput over BLE (or to the simulator). The UI displays the current
// BoardState values read-only; no local mirroring of VP or resources.
// =============================================================================

import React, { useEffect, useMemo, useRef, useState } from 'react';
import {
  Animated,
  Modal,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  useWindowDimensions,
  View,
} from 'react-native';

import { useSettings } from '@/context/settings-context';
import { BoardMap } from '@/components/ui/board-map';
import { PinchPanMap } from '@/components/ui/pinch-pan-map';
import type { useTheme } from '@/hooks/use-theme';

import { Spacing } from '@/constants/theme';
import { DEV_CARD_COST, CITY_COST, DIE_FACES, ROAD_COST, RESOURCES, SETTLEMENT_COST } from '@/constants/game';
import { PORTS, PortType } from '@/constants/board-topology';
import {
  BoardState,
  DEV_CARD_COUNT,
  DevCard,
  GamePhase,
  NO_PLAYER,
  PlayerAction,
  Resource,
  devCardCount,
  playerResources,
  playerTotalCards,
} from '@/services/proto';

// ── Helpers ─────────────────────────────────────────────────────────────────

/**
 * Compute the best bank trade rate for each resource (mirrors firmware
 * bankTradeRate). Returns a length-5 array indexed by Resource.
 */
function bankTradeRates(state: BoardState, player: number): number[] {
  const rates = [4, 4, 4, 4, 4];
  for (const port of PORTS) {
    const owns = port.vertices.some(v => {
      const vo = state.vertices[v];
      return vo != null && vo.owner === player;
    });
    if (!owns) continue;
    if (port.type === PortType.GENERIC_3_1) {
      for (let r = 0; r < 5; r++) rates[r] = Math.min(rates[r], 3);
    } else {
      const res = (port.type as number) - 1; // LUMBER_2_1=1→0, WOOL_2_1=2→1, …
      rates[res] = Math.min(rates[res], 2);
    }
  }
  return rates;
}

// ── Types ───────────────────────────────────────────────────────────────────

type Theme = ReturnType<typeof useTheme>;
type SendInput = (input: { action: PlayerAction; [k: string]: unknown }) => Promise<void>;

interface CommonProps {
  state: BoardState;
  myId: number;
  myTurn: boolean;
  sendInput: SendInput;
  theme: Theme;
}

// ── Helpers ─────────────────────────────────────────────────────────────────

function ResourceBadgeRow({
  values,
  highlight,
  theme,
}: {
  values: number[];
  highlight?: number[];
  theme: Theme;
}) {
  return (
    <View style={s.resourceRow}>
      {RESOURCES.map((r, i) => (
        <View
          key={r.key}
          style={[
            s.resBadge,
            { backgroundColor: theme.background },
            highlight?.[i] ? { borderColor: theme.primary, borderWidth: 1 } : null,
          ]}>
          <Text style={s.resBadgeEmoji}>{r.emoji}</Text>
          <Text style={[s.resBadgeCount, { color: theme.text }]}>{values[i] ?? 0}</Text>
        </View>
      ))}
    </View>
  );
}

function btnStyle(enabled: boolean, theme: Theme) {
  return [
    s.btn,
    { backgroundColor: enabled ? theme.primary : theme.backgroundElement },
    !enabled && { opacity: 0.4 },
  ];
}

// ── Resources panel ─────────────────────────────────────────────────────────

export function ResourcesPanel({ state, myId, theme }: CommonProps) {
  const my = playerResources(state, myId);
  return (
    <View style={s.section}>
      <View style={s.sectionHeaderRow}>
        <Text style={[s.sectionLabel, { color: theme.textSecondary }]}>Your resources</Text>
        <Text style={[s.totalLabel, { color: theme.textSecondary }]}>
          {my.reduce((a, b) => a + b, 0)} cards
        </Text>
      </View>
      <View style={[s.card, { backgroundColor: theme.backgroundElement }]}>
        <ResourceBadgeRow values={my} theme={theme} />
      </View>
    </View>
  );
}

// ── Distribution toast ──────────────────────────────────────────────────────

export function DistributionToast({ state, myId, theme }: CommonProps) {
  const slice = useMemo(
    () => state.lastDistribution.slice(myId * 5, myId * 5 + 5),
    [state.lastDistribution, myId],
  );
  const total = slice.reduce((a, b) => a + b, 0);
  if (total === 0) return null;
  return (
    <View style={[s.distToast, { backgroundColor: theme.backgroundElement }]}>
      <Text style={[s.distLabel, { color: theme.textSecondary }]}>You collected</Text>
      <View style={s.distChips}>
        {RESOURCES.map((r, i) => slice[i] > 0 && (
          <View key={r.key} style={[s.distChip, { backgroundColor: theme.background }]}>
            <Text style={s.distChipEmoji}>{r.emoji}</Text>
            <Text style={[s.distChipCount, { color: theme.text }]}>+{slice[i]}</Text>
          </View>
        ))}
      </View>
    </View>
  );
}

// ── Store (Buy panel) ───────────────────────────────────────────────────────

function canAfford(my: number[], cost: readonly number[]): boolean {
  for (let i = 0; i < 5; i++) if ((my[i] ?? 0) < cost[i]) return false;
  return true;
}

function CostLine({ cost, theme }: { cost: readonly number[]; theme: Theme }) {
  return (
    <View style={s.costLine}>
      {RESOURCES.map((r, i) => cost[i] > 0 && (
        <Text key={r.key} style={[s.costEmoji, { color: theme.text }]}>
          {r.emoji}×{cost[i]}
        </Text>
      ))}
    </View>
  );
}

export function StorePanel({ state, myId, myTurn, sendInput, theme }: CommonProps) {
  const my = playerResources(state, myId);
  const phase = state.phase;
  const canBuy = myTurn && phase === GamePhase.PLAYING && state.hasRolled;
  const items: { label: string; cost: readonly number[]; action: PlayerAction; available: boolean }[] = [
    { label: 'Road',       cost: ROAD_COST,       action: PlayerAction.BUY_ROAD,       available: true },
    { label: 'Settlement', cost: SETTLEMENT_COST, action: PlayerAction.BUY_SETTLEMENT, available: true },
    { label: 'City',       cost: CITY_COST,       action: PlayerAction.BUY_CITY,       available: true },
    { label: 'Dev Card',   cost: DEV_CARD_COST,   action: PlayerAction.BUY_DEV_CARD,   available: state.devDeckRemaining > 0 },
  ];
  return (
    <View style={s.section}>
      <Text style={[s.sectionLabel, { color: theme.textSecondary }]}>Build / Buy</Text>
      <View style={[s.card, { backgroundColor: theme.backgroundElement }]}>
        {items.map(item => {
          const enabled = canBuy && item.available && canAfford(my, item.cost);
          return (
            <Pressable
              key={item.label}
              disabled={!enabled}
              onPress={() => sendInput({ action: item.action })}
              style={({ pressed }) => [
                s.storeRow,
                { backgroundColor: theme.background, opacity: pressed ? 0.7 : enabled ? 1 : 0.4 },
              ]}>
              <Text style={[s.storeLabel, { color: theme.text }]}>{item.label}</Text>
              <CostLine cost={item.cost} theme={theme} />
            </Pressable>
          );
        })}
        <Text style={[s.storeHint, { color: theme.textSecondary }]}>
          {state.devDeckRemaining} dev cards left
        </Text>
      </View>
    </View>
  );
}

// ── Pending purchases banner ────────────────────────────────────────────────

export function PendingPurchasesBanner({ state, myId, theme }: CommonProps) {
  const r = state.pendingRoadBuy[myId] ?? 0;
  const t = state.pendingSettlementBuy[myId] ?? 0;
  const c = state.pendingCityBuy[myId] ?? 0;
  const f = state.freeRoadsRemaining[myId] ?? 0;
  if (r === 0 && t === 0 && c === 0 && f === 0) return null;
  const parts: string[] = [];
  if (f > 0) parts.push(`${f} free road${f === 1 ? '' : 's'}`);
  if (r > 0) parts.push(`${r} road${r === 1 ? '' : 's'} ready`);
  if (t > 0) parts.push(`${t} settlement${t === 1 ? '' : 's'} ready`);
  if (c > 0) parts.push(`${c} ${c === 1 ? 'city' : 'cities'} ready`);
  return (
    <View style={[s.banner, { backgroundColor: theme.backgroundElement, borderColor: theme.primary }]}>
      <Text style={[s.bannerText, { color: theme.text }]}>
        Place: {parts.join(' • ')}
      </Text>
    </View>
  );
}

// ── Dev cards panel ─────────────────────────────────────────────────────────

const DEV_LABEL: Record<DevCard, string> = {
  [DevCard.KNIGHT]:         'Knight',
  [DevCard.VP]:             'VP',
  [DevCard.ROAD_BUILDING]:  'Road Building',
  [DevCard.YEAR_OF_PLENTY]: 'Year of Plenty',
  [DevCard.MONOPOLY]:       'Monopoly',
};

export function DevCardsPanel({
  state, myId, myTurn, sendInput, theme, boughtThisTurn = [],
}: CommonProps & { boughtThisTurn?: number[] }) {
  const [yopOpen, setYopOpen] = useState(false);
  const [monoOpen, setMonoOpen] = useState(false);

  const counts = useMemo(() => {
    const out: Record<DevCard, number> = {
      [DevCard.KNIGHT]: 0, [DevCard.VP]: 0, [DevCard.ROAD_BUILDING]: 0,
      [DevCard.YEAR_OF_PLENTY]: 0, [DevCard.MONOPOLY]: 0,
    };
    for (let d = 0; d < DEV_CARD_COUNT; d++) {
      out[d as DevCard] = devCardCount(state, myId, d as DevCard);
    }
    return out;
  }, [state, myId]);

  const hasAny = (Object.values(counts) as number[]).reduce((a, b) => a + b, 0) > 0;
  if (!hasAny) return null;

  const canPlay =
    myTurn &&
    state.phase === GamePhase.PLAYING &&
    !state.hasRolled &&
    !state.cardPlayedThisTurn;

  const onPlay = (action: PlayerAction) => sendInput({ action });

  return (
    <View style={s.section}>
      <Text style={[s.sectionLabel, { color: theme.textSecondary }]}>Development cards</Text>
      <View style={[s.card, { backgroundColor: theme.backgroundElement }]}>
        {([
          DevCard.KNIGHT,
          DevCard.ROAD_BUILDING,
          DevCard.YEAR_OF_PLENTY,
          DevCard.MONOPOLY,
          DevCard.VP,
        ] as DevCard[]).map(card => {
          const n = counts[card];
          if (n === 0) return null;
          const isVp = card === DevCard.VP;
          const isNewlyBought = (boughtThisTurn[card] ?? 0) > 0;
          const enabled = canPlay && !isVp && !isNewlyBought;
          return (
            <View key={card} style={[s.devRow, { backgroundColor: theme.background }]}>
              <Text style={[s.devLabel, { color: theme.text }]}>
                {DEV_LABEL[card]} ×{n}
              </Text>
              {!isVp && (
                <View style={s.devPlayCol}>
                  <Pressable
                    disabled={!enabled}
                    onPress={() => {
                      if (card === DevCard.KNIGHT)              onPlay(PlayerAction.PLAY_KNIGHT);
                      else if (card === DevCard.ROAD_BUILDING)  onPlay(PlayerAction.PLAY_ROAD_BUILDING);
                      else if (card === DevCard.YEAR_OF_PLENTY) setYopOpen(true);
                      else if (card === DevCard.MONOPOLY)       setMonoOpen(true);
                    }}
                    style={btnStyle(enabled, theme)}>
                    <Text style={[s.btnText, { color: enabled ? '#fff' : theme.textSecondary }]}>
                      Play
                    </Text>
                  </Pressable>
                  {isNewlyBought && (
                    <Text style={[s.devNewHint, { color: theme.textSecondary }]}>
                      next turn
                    </Text>
                  )}
                </View>
              )}
            </View>
          );
        })}
      </View>

      <YearOfPlentyModal
        visible={yopOpen}
        onClose={() => setYopOpen(false)}
        onPick={(r1, r2) => {
          setYopOpen(false);
          sendInput({
            action: PlayerAction.PLAY_YEAR_OF_PLENTY,
            cardRes1: r1,
            cardRes2: r2,
          });
        }}
        theme={theme}
      />
      <MonopolyModal
        visible={monoOpen}
        onClose={() => setMonoOpen(false)}
        onPick={(res) => {
          setMonoOpen(false);
          sendInput({ action: PlayerAction.PLAY_MONOPOLY, monopolyRes: res });
        }}
        theme={theme}
      />
    </View>
  );
}

// ── Drawn dev card popup ────────────────────────────────────────────────────

const DEV_EMOJI: Record<DevCard, string> = {
  [DevCard.KNIGHT]:         '⚔️',
  [DevCard.VP]:             '🏆',
  [DevCard.ROAD_BUILDING]:  '🛤️',
  [DevCard.YEAR_OF_PLENTY]: '🌾',
  [DevCard.MONOPOLY]:       '💰',
};

const DEV_DESCRIPTION: Record<DevCard, string> = {
  [DevCard.KNIGHT]:         'Move the robber to any tile and optionally steal a resource from an adjacent player.',
  [DevCard.VP]:             'Worth 1 Victory Point — kept hidden until the game ends.',
  [DevCard.ROAD_BUILDING]:  'Place 2 roads anywhere for free.',
  [DevCard.YEAR_OF_PLENTY]: 'Take any 2 resources from the bank.',
  [DevCard.MONOPOLY]:       'Name a resource — every opponent gives you all of theirs.',
};

export function DrawnDevCardModal({
  card, onDismiss, theme,
}: {
  card: DevCard | null;
  onDismiss: () => void;
  theme: Theme;
}) {
  if (card === null) return null;
  return (
    <Modal visible transparent animationType="fade" onRequestClose={onDismiss}>
      <View style={s.modalBg}>
        <View style={[s.modalCard, { backgroundColor: theme.backgroundElement }]}>
          <Text style={[s.modalTitle, { color: theme.text }]}>You drew a Dev Card!</Text>
          <View style={[s.drawnCardBadge, { backgroundColor: theme.background }]}>
            <Text style={s.drawnCardEmoji}>{DEV_EMOJI[card]}</Text>
            <Text style={[s.drawnCardName, { color: theme.text }]}>{DEV_LABEL[card]}</Text>
          </View>
          <Text style={[s.modalBody, { color: theme.textSecondary }]}>
            {DEV_DESCRIPTION[card]}
          </Text>
          <Text style={[s.drawnCardHint, { color: theme.textSecondary }]}>
            You cannot play this card until your next turn.
          </Text>
          <Pressable onPress={onDismiss} style={btnStyle(true, theme)}>
            <Text style={[s.btnText, { color: '#fff' }]}>Got it</Text>
          </Pressable>
        </View>
      </View>
    </Modal>
  );
}

// ── Robber tile picker ──────────────────────────────────────────────────────

export function RobberOverlay({
  state, myId, myTurn, sendInput, theme,
}: CommonProps) {
  if (!myTurn || state.phase !== GamePhase.ROBBER) return null;
  // If steal-eligible mask is non-zero we are past PLACE_ROBBER and choosing a target.
  const isStealing = state.stealEligibleMask !== 0;
  if (isStealing) return null;

  return (
    <RobberMapModal state={state} sendInput={sendInput} theme={theme} />
  );
}

/** Inner component so hooks can run unconditionally. */
function RobberMapModal({
  state,
  sendInput,
  theme,
}: Pick<CommonProps, 'state' | 'sendInput' | 'theme'>) {
  const { boardRotation } = useSettings();
  const { width }         = useWindowDimensions();
  // Visible area in the modal: leave room for title + body text + safe area.
  const mapSize = Math.min(width - Spacing.four * 2, 340);

  return (
    <Modal visible transparent animationType="fade">
      <View style={s.modalBg}>
        <View style={[s.robberCard, { backgroundColor: theme.backgroundElement }]}>
          <Text style={[s.modalTitle, { color: theme.text }]}>Move the Robber</Text>
          <Text style={[s.modalBody, { color: theme.textSecondary }]}>
            Tap a tile to place the robber there.
          </Text>

          {/* Pinch-to-zoom / pan hex map — no UIScrollView */}
          <PinchPanMap size={mapSize}>
            <BoardMap
              tiles={state.tiles}
              vertices={state.vertices}
              edges={state.edges}
              robberTile={state.robberTile}
              rotation={boardRotation}
              size={mapSize}
              disabledTile={state.robberTile}
              onTilePress={(tileIdx) =>
                sendInput({ action: PlayerAction.PLACE_ROBBER, robberTile: tileIdx })
              }
              showPorts={false}
            />
          </PinchPanMap>

          <Text style={[s.robberHint, { color: theme.textSecondary }]}>
            Pinch to zoom · drag to pan
          </Text>
        </View>
      </View>
    </Modal>
  );
}

// ── Steal target picker ─────────────────────────────────────────────────────

export function StealOverlay({
  state, myId, myTurn, sendInput, theme,
}: CommonProps) {
  if (!myTurn || state.phase !== GamePhase.ROBBER) return null;
  if (state.stealEligibleMask === 0) return null;

  return (
    <Modal visible transparent animationType="fade">
      <View style={s.modalBg}>
        <View style={[s.modalCard, { backgroundColor: theme.backgroundElement }]}>
          <Text style={[s.modalTitle, { color: theme.text }]}>Steal a card</Text>
          <Text style={[s.modalBody, { color: theme.textSecondary }]}>
            Choose a player adjacent to the robber.
          </Text>
          <View style={s.targetRow}>
            {[0, 1, 2, 3].map(i => {
              if (!(state.stealEligibleMask & (1 << i))) return null;
              return (
                <Pressable
                  key={i}
                  onPress={() => sendInput({ action: PlayerAction.STEAL_FROM, targetPlayer: i })}
                  style={[s.targetBtn, { backgroundColor: theme.primary }]}>
                  <Text style={[s.targetBtnLabel, { color: '#fff' }]}>P{i + 1}</Text>
                  <Text style={[s.targetBtnSub, { color: '#ffffffcc' }]}>
                    {playerTotalCards(state, i)} cards
                  </Text>
                </Pressable>
              );
            })}
          </View>
          <Pressable
            onPress={() => sendInput({ action: PlayerAction.SKIP_ROBBER })}
            style={[s.btn, { backgroundColor: theme.backgroundSelected, marginTop: Spacing.three }]}>
            <Text style={[s.btnText, { color: theme.text }]}>Skip</Text>
          </Pressable>
        </View>
      </View>
    </Modal>
  );
}

// ── Discard modal ───────────────────────────────────────────────────────────

export function DiscardOverlay({
  state, myId, sendInput, theme,
}: CommonProps) {
  const required = state.discardRequiredCount[myId] ?? 0;
  const inMask = (state.discardRequiredMask & (1 << myId)) !== 0;
  const [counts, setCounts] = useState<number[]>([0, 0, 0, 0, 0]);

  useEffect(() => {
    if (state.phase !== GamePhase.DISCARD || !inMask) {
      setCounts([0, 0, 0, 0, 0]);
    }
  }, [state.phase, inMask]);

  if (state.phase !== GamePhase.DISCARD || !inMask) return null;

  const my = playerResources(state, myId);
  const total = counts.reduce((a, b) => a + b, 0);

  const adjust = (i: number, delta: number) => {
    setCounts(prev => {
      const next = prev.slice();
      const v = Math.max(0, Math.min(my[i], (prev[i] ?? 0) + delta));
      next[i] = v;
      return next;
    });
  };

  return (
    <Modal visible transparent animationType="fade">
      <View style={s.modalBg}>
        <View style={[s.modalCard, { backgroundColor: theme.backgroundElement }]}>
          <Text style={[s.modalTitle, { color: theme.text }]}>
            Discard {required} cards
          </Text>
          <Text style={[s.modalBody, { color: theme.textSecondary }]}>
            Selected {total} of {required}.
          </Text>
          {RESOURCES.map((r, i) => (
            <View key={r.key} style={s.discardRow}>
              <Text style={s.discardEmoji}>{r.emoji}</Text>
              <Text style={[s.discardLabel, { color: theme.text }]}>{r.label}</Text>
              <Text style={[s.discardHave, { color: theme.textSecondary }]}>have {my[i]}</Text>
              <Pressable
                disabled={counts[i] === 0}
                onPress={() => adjust(i, -1)}
                style={[s.smallBtn, { backgroundColor: theme.background }]}>
                <Text style={[s.btnText, { color: theme.text }]}>−</Text>
              </Pressable>
              <Text style={[s.discardCount, { color: theme.text }]}>{counts[i]}</Text>
              <Pressable
                disabled={counts[i] >= my[i] || total >= required}
                onPress={() => adjust(i, 1)}
                style={[s.smallBtn, { backgroundColor: theme.background }]}>
                <Text style={[s.btnText, { color: theme.text }]}>＋</Text>
              </Pressable>
            </View>
          ))}
          <Pressable
            disabled={total !== required}
            onPress={() => sendInput({
              action: PlayerAction.DISCARD,
              resLumber: counts[0],
              resWool:   counts[1],
              resGrain:  counts[2],
              resBrick:  counts[3],
              resOre:    counts[4],
            })}
            style={btnStyle(total === required, theme)}>
            <Text style={[s.btnText, { color: total === required ? '#fff' : theme.textSecondary }]}>
              Confirm Discard
            </Text>
          </Pressable>
        </View>
      </View>
    </Modal>
  );
}

// ── Trade panel + incoming offer dialog ────────────────────────────────────

// Helper: render a compact summary row for a trade (offer → want).
function TradeSummaryRow({ offer, want, theme }: { offer: number[]; want: number[]; theme: Theme }) {
  const giveItems = RESOURCES.filter((_, i) => (offer[i] ?? 0) > 0);
  const wantItems = RESOURCES.filter((_, i) => (want[i] ?? 0) > 0);
  return (
    <View style={s.tradeSummaryRow}>
      <View style={s.tradeSummaryGroup}>
        {giveItems.map((r, idx) => (
          <Text key={r.key} style={[s.tradeSummaryChip, { color: theme.text }]}>
            {r.emoji}×{offer[RESOURCES.indexOf(r)]}
          </Text>
        ))}
      </View>
      <Text style={[s.tradeSummaryArrow, { color: theme.textSecondary }]}>→</Text>
      <View style={s.tradeSummaryGroup}>
        {wantItems.map(r => (
          <Text key={r.key} style={[s.tradeSummaryChip, { color: theme.text }]}>
            {r.emoji}×{want[RESOURCES.indexOf(r)]}
          </Text>
        ))}
      </View>
    </View>
  );
}

export function TradePanel({
  state, myId, myTurn, sendInput, theme,
}: CommonProps) {
  const [open, setOpen] = useState<'bank' | 'p2p' | null>(null);
  const my = playerResources(state, myId);
  const trade = state.trade;
  const hasPending = trade.fromPlayer !== NO_PLAYER;
  const iMyOffer = hasPending && trade.fromPlayer === myId;

  // Can start a new bank trade: my turn, playing, rolled, no pending p2p trade.
  const canBankTrade = myTurn && state.phase === GamePhase.PLAYING && state.hasRolled && !hasPending;
  // Can start a new p2p offer: same conditions.
  const canOffer = canBankTrade;

  return (
    <View style={s.section}>
      <Text style={[s.sectionLabel, { color: theme.textSecondary }]}>Trade</Text>
      <View style={[s.card, { backgroundColor: theme.backgroundElement }]}>
        {/* ── Active outgoing offer ── */}
        {iMyOffer ? (
          <View style={[s.pendingOfferCard, { backgroundColor: theme.background, borderColor: theme.primary }]}>
            <View style={s.pendingOfferHeader}>
              <Text style={[s.pendingOfferTitle, { color: theme.text }]}>Offer pending…</Text>
              <Text style={[s.pendingOfferSub, { color: theme.textSecondary }]}>
                {trade.toPlayer === NO_PLAYER
                  ? 'Sent to all players'
                  : `Sent to Player ${trade.toPlayer + 1}`}
              </Text>
            </View>
            <TradeSummaryRow offer={trade.offer} want={trade.want} theme={theme} />
            <Pressable
              onPress={() => sendInput({ action: PlayerAction.TRADE_CANCEL })}
              style={[s.btn, { backgroundColor: theme.backgroundSelected, marginTop: Spacing.two }]}>
              <Text style={[s.btnText, { color: theme.text }]}>Cancel Offer</Text>
            </Pressable>
            <Text style={[s.tradeBlockHint, { color: theme.textSecondary }]}>
              Resolve this trade before ending your turn.
            </Text>
          </View>
        ) : (
          /* ── Trade buttons ── */
          <View style={s.tradePanelButtons}>
            <Pressable
              disabled={!canBankTrade}
              onPress={() => setOpen('bank')}
              style={[btnStyle(canBankTrade, theme), { flex: 1 }]}>
              <Text style={[s.btnText, { color: canBankTrade ? '#fff' : theme.textSecondary }]}>
                Trade with Bank
              </Text>
            </Pressable>
            <Pressable
              disabled={!canOffer}
              onPress={() => setOpen('p2p')}
              style={[btnStyle(canOffer, theme), { flex: 1 }]}>
              <Text style={[s.btnText, { color: canOffer ? '#fff' : theme.textSecondary }]}>
                Trade with Players
              </Text>
            </Pressable>
          </View>
        )}
      </View>

      <TradeComposer
        kind={open}
        state={state}
        myId={myId}
        sendInput={sendInput}
        onClose={() => setOpen(null)}
        theme={theme}
      />
    </View>
  );
}

export function IncomingTradeDialog({
  state, myId, sendInput, theme,
}: CommonProps) {
  const t = state.trade;

  // Visibility: there must be a live offer, not from myself, and directed at
  // me specifically or broadcast to all opponents.
  const visible =
    t.fromPlayer !== NO_PLAYER &&
    t.fromPlayer !== myId &&
    (t.toPlayer === NO_PLAYER || t.toPlayer === myId);

  const my = playerResources(state, myId);
  const canAfford = t.want.every((w, i) => (my[i] ?? 0) >= w);

  // Missing resources for the "want" side — shown when they can't afford.
  const missing = RESOURCES.filter((_, i) => (t.want[i] ?? 0) > (my[i] ?? 0));

  if (!visible) return null;

  const isOpenOffer = t.toPlayer === NO_PLAYER;

  return (
    <Modal visible transparent animationType="fade">
      <View style={s.modalBg}>
        <View style={[s.modalCard, { backgroundColor: theme.backgroundElement }]}>

          {/* Header */}
          <View style={s.tradeDialogHeader}>
            <Text style={[s.modalTitle, { color: theme.text }]}>
              Trade offer from Player {t.fromPlayer + 1}
            </Text>
            {isOpenOffer && (
              <View style={[s.openOfferBadge, { backgroundColor: theme.backgroundSelected }]}>
                <Text style={[s.openOfferBadgeText, { color: theme.textSecondary }]}>
                  Open offer · first to accept wins
                </Text>
              </View>
            )}
          </View>

          {/* They give */}
          <View style={s.tradeDialogSection}>
            <Text style={[s.tradeDialogLabel, { color: theme.textSecondary }]}>
              They offer you
            </Text>
            <View style={[s.tradeDialogResBox, { backgroundColor: theme.background }]}>
              <ResourceBadgeRow values={t.offer} theme={theme} />
            </View>
          </View>

          {/* They want */}
          <View style={s.tradeDialogSection}>
            <Text style={[s.tradeDialogLabel, { color: theme.textSecondary }]}>
              They want from you
            </Text>
            <View style={[s.tradeDialogResBox, { backgroundColor: theme.background }]}>
              <ResourceBadgeRow
                values={t.want}
                highlight={t.want.map((w, i) => (my[i] ?? 0) < w ? 1 : 0)}
                theme={theme}
              />
            </View>
          </View>

          {/* Can't-afford notice */}
          {!canAfford && (
            <View style={[s.tradeAffordRow, { backgroundColor: theme.background }]}>
              <Text style={[s.tradeAffordText, { color: theme.textSecondary }]}>
                You don't have enough{' '}
                {missing.map(r => r.emoji).join(' ')}{' '}
                to accept this trade.
              </Text>
            </View>
          )}

          {/* Actions */}
          <View style={[s.tradeButtons, { marginTop: Spacing.two }]}>
            <Pressable
              onPress={() => sendInput({ action: PlayerAction.TRADE_DECLINE })}
              style={[s.btn, { flex: 1, backgroundColor: theme.backgroundSelected }]}>
              <Text style={[s.btnText, { color: theme.text }]}>Decline</Text>
            </Pressable>
            <Pressable
              disabled={!canAfford}
              onPress={() => sendInput({ action: PlayerAction.TRADE_ACCEPT })}
              style={[s.btn, { flex: 1, backgroundColor: canAfford ? theme.primary : theme.backgroundElement, opacity: canAfford ? 1 : 0.4 }]}>
              <Text style={[s.btnText, { color: canAfford ? '#fff' : theme.textSecondary }]}>
                Accept
              </Text>
            </Pressable>
          </View>
        </View>
      </View>
    </Modal>
  );
}

// ── Trade composer ──────────────────────────────────────────────────────────

// p2p compose flow has three named steps in order.
type P2PStep = 'give' | 'want' | 'target';

function TradeComposer({
  kind, state, myId, sendInput, onClose, theme,
}: {
  kind: 'bank' | 'p2p' | null;
  state: BoardState;
  myId: number;
  sendInput: SendInput;
  onClose: () => void;
  theme: Theme;
}) {
  const [give, setGive] = useState<number[]>([0, 0, 0, 0, 0]);
  const [want, setWant] = useState<number[]>([0, 0, 0, 0, 0]);
  // p2p target: a specific player index, or NO_PLAYER for "everyone".
  // We use a sentinel null to mean "not yet chosen" so we can force the user
  // to make an explicit selection.
  const [target, setTarget] = useState<number | null>(null);
  const [p2pStep, setP2PStep] = useState<P2PStep>('give');

  useEffect(() => {
    if (!kind) {
      setGive([0, 0, 0, 0, 0]);
      setWant([0, 0, 0, 0, 0]);
      setTarget(null);
      setP2PStep('give');
    }
  }, [kind]);

  if (!kind) return null;

  const my = playerResources(state, myId);
  const rates = kind === 'bank' ? bankTradeRates(state, myId) : [1, 1, 1, 1, 1];

  const credits   = give.reduce((acc, g, r) => acc + Math.floor(g / rates[r]), 0);
  const wantTotal = want.reduce((a, b) => a + b, 0);
  const giveTotal = give.reduce((a, b) => a + b, 0);

  const giveValid = kind === 'bank'
    ? credits > 0 && give.every((g, r) => g === 0 || g % rates[r] === 0) && give.every((g, i) => my[i] >= g)
    : giveTotal > 0 && give.every((g, i) => my[i] >= g);

  const wantValid = kind === 'bank'
    ? wantTotal === credits
    : wantTotal > 0;

  const canSubmitBank = giveValid && wantValid;

  const adjustGive = (i: number, dir: 1 | -1) => {
    setGive(prev => {
      const next = prev.slice();
      const step = rates[i];
      const max  = Math.floor(my[i] / step) * step;
      next[i] = Math.max(0, Math.min(max, prev[i] + dir * step));
      return next;
    });
  };

  const adjustWant = (i: number, dir: 1 | -1) => {
    setWant(prev => {
      const next = prev.slice();
      const n = prev[i] + dir;
      if (n < 0) return prev;
      if (dir > 0 && kind === 'bank' && wantTotal >= credits) return prev;
      next[i] = n;
      return next;
    });
  };

  const adjustP2pGive = (i: number, dir: 1 | -1) => {
    setGive(prev => {
      // Can't give a resource that is already in the want side.
      if (dir > 0 && want[i] > 0) return prev;
      const next = prev.slice();
      next[i] = Math.max(0, Math.min(my[i], prev[i] + dir));
      return next;
    });
  };

  const adjustP2pWant = (i: number, dir: 1 | -1) => {
    setWant(prev => {
      // Can't want a resource that is already in the give side.
      if (dir > 0 && give[i] > 0) return prev;
      const next = prev.slice();
      next[i] = Math.max(0, prev[i] + dir);
      return next;
    });
  };

  const submitBank = () => {
    sendInput({
      action: PlayerAction.BANK_TRADE,
      resLumber: give[0], resWool: give[1], resGrain: give[2], resBrick: give[3], resOre: give[4],
      wantLumber: want[0], wantWool: want[1], wantGrain: want[2], wantBrick: want[3], wantOre: want[4],
    });
    onClose();
  };

  const submitP2P = () => {
    sendInput({
      action: PlayerAction.TRADE_OFFER,
      targetPlayer: target ?? NO_PLAYER,
      resLumber: give[0], resWool: give[1], resGrain: give[2], resBrick: give[3], resOre: give[4],
      wantLumber: want[0], wantWool: want[1], wantGrain: want[2], wantBrick: want[3], wantOre: want[4],
    });
    onClose();
  };

  // ── Bank trade layout ────────────────────────────────────────────────────
  if (kind === 'bank') {
    return (
      <Modal visible transparent animationType="fade" onRequestClose={onClose}>
        <View style={s.modalBg}>
          <View style={[s.modalCard, { backgroundColor: theme.backgroundElement, maxHeight: '90%' }]}>
            <ScrollView
              bounces={false}
              keyboardShouldPersistTaps="handled"
              showsVerticalScrollIndicator={false}
              contentContainerStyle={s.modalInnerScroll}>

              <Text style={[s.modalTitle, { color: theme.text }]}>Trade with Bank</Text>

              {/* You give */}
              <Text style={[s.tradeDialogLabel, { color: theme.textSecondary, marginTop: Spacing.one }]}>
                You give
              </Text>
              {RESOURCES.map((r, i) => {
                const rate = rates[i];
                const maxGive = Math.floor(my[i] / rate) * rate;
                return (
                  <View key={`g-${r.key}`} style={s.discardRow}>
                    <Text style={s.discardEmoji}>{r.emoji}</Text>
                    <Text style={[s.discardLabel, { color: theme.text }]}>{r.label}</Text>
                    <Text style={[s.discardHave, { color: theme.textSecondary }]}>
                      have {my[i]} · {rate}:1
                    </Text>
                    <Pressable disabled={give[i] === 0} onPress={() => adjustGive(i, -1)}
                      style={[s.smallBtn, { backgroundColor: theme.background }]}>
                      <Text style={[s.btnText, { color: theme.text }]}>−</Text>
                    </Pressable>
                    <Text style={[s.discardCount, { color: theme.text }]}>{give[i]}</Text>
                    <Pressable disabled={give[i] >= maxGive} onPress={() => adjustGive(i, 1)}
                      style={[s.smallBtn, { backgroundColor: theme.background }]}>
                      <Text style={[s.btnText, { color: theme.text }]}>＋</Text>
                    </Pressable>
                  </View>
                );
              })}

              {/* Credits indicator */}
              <View style={[s.creditsRow, { backgroundColor: theme.background }]}>
                <Text style={[s.tradeMini, { color: theme.textSecondary }]}>Credits to spend</Text>
                <Text style={[s.creditsCount, { color: credits > 0 ? theme.primary : theme.textSecondary }]}>
                  {credits - wantTotal} / {credits}
                </Text>
              </View>

              {/* You receive */}
              <Text style={[s.tradeDialogLabel, { color: theme.textSecondary }]}>
                You receive
              </Text>
              {RESOURCES.map((r, i) => (
                <View key={`w-${r.key}`} style={s.discardRow}>
                  <Text style={s.discardEmoji}>{r.emoji}</Text>
                  <Text style={[s.discardLabel, { color: theme.text }]}>{r.label}</Text>
                  <Pressable disabled={want[i] === 0} onPress={() => adjustWant(i, -1)}
                    style={[s.smallBtn, { backgroundColor: theme.background }]}>
                    <Text style={[s.btnText, { color: theme.text }]}>−</Text>
                  </Pressable>
                  <Text style={[s.discardCount, { color: theme.text }]}>{want[i]}</Text>
                  <Pressable disabled={wantTotal >= credits} onPress={() => adjustWant(i, 1)}
                    style={[s.smallBtn, { backgroundColor: theme.background }]}>
                    <Text style={[s.btnText, { color: theme.text }]}>＋</Text>
                  </Pressable>
                </View>
              ))}

              <View style={[s.tradeButtons, { marginTop: Spacing.three }]}>
                <Pressable onPress={onClose} style={[s.btn, { flex: 1, backgroundColor: theme.backgroundSelected }]}>
                  <Text style={[s.btnText, { color: theme.text }]}>Cancel</Text>
                </Pressable>
                <Pressable disabled={!canSubmitBank} onPress={submitBank} style={[btnStyle(canSubmitBank, theme), { flex: 1 }]}>
                  <Text style={[s.btnText, { color: canSubmitBank ? '#fff' : theme.textSecondary }]}>Trade</Text>
                </Pressable>
              </View>
            </ScrollView>
          </View>
        </View>
      </Modal>
    );
  }

  // ── Player-to-player trade — stepped layout ──────────────────────────────
  const otherPlayers = [0, 1, 2, 3].filter(i => i !== myId && i < state.numPlayers);

  return (
    <Modal visible transparent animationType="fade" onRequestClose={onClose}>
      <View style={s.modalBg}>
        <View style={[s.modalCard, { backgroundColor: theme.backgroundElement, maxHeight: '90%' }]}>
          <ScrollView
            bounces={false}
            keyboardShouldPersistTaps="handled"
            showsVerticalScrollIndicator={false}
            contentContainerStyle={s.modalInnerScroll}>

            {/* ── Step indicator ── */}
            <View style={s.stepIndicator}>
              {(['give', 'want', 'target'] as P2PStep[]).map((step, idx) => {
                const active = p2pStep === step;
                const done =
                  (step === 'give' && giveTotal > 0 && p2pStep !== 'give') ||
                  (step === 'want' && wantTotal > 0 && p2pStep === 'target');
                return (
                  <React.Fragment key={step}>
                    {idx > 0 && (
                      <View style={[s.stepDivider, { backgroundColor: done ? theme.primary : theme.backgroundSelected }]} />
                    )}
                    <Pressable
                      onPress={() => {
                        if (step === 'want' && giveTotal === 0) return;
                        if (step === 'target' && (giveTotal === 0 || wantTotal === 0)) return;
                        setP2PStep(step);
                      }}
                      style={[
                        s.stepDot,
                        {
                          backgroundColor: active ? theme.primary : done ? theme.primary : theme.backgroundSelected,
                          opacity: active ? 1 : 0.6,
                        },
                      ]}>
                      <Text style={[s.stepDotText, { color: active || done ? '#fff' : theme.textSecondary }]}>
                        {idx + 1}
                      </Text>
                    </Pressable>
                  </React.Fragment>
                );
              })}
            </View>

            {/* ── Step 1: Give ── */}
            {p2pStep === 'give' && (
              <>
                <Text style={[s.modalTitle, { color: theme.text }]}>What will you offer?</Text>
                <Text style={[s.modalBody, { color: theme.textSecondary }]}>
                  Select at least 1 resource you will give.
                </Text>
                {RESOURCES.map((r, i) => (
                  <View key={`g-${r.key}`} style={s.discardRow}>
                    <Text style={s.discardEmoji}>{r.emoji}</Text>
                    <Text style={[s.discardLabel, { color: theme.text }]}>{r.label}</Text>
                    <Text style={[s.discardHave, { color: theme.textSecondary }]}>have {my[i]}</Text>
                    <Pressable disabled={give[i] === 0} onPress={() => adjustP2pGive(i, -1)}
                      style={[s.smallBtn, { backgroundColor: theme.background }]}>
                      <Text style={[s.btnText, { color: theme.text }]}>−</Text>
                    </Pressable>
                    <Text style={[s.discardCount, { color: theme.text }]}>{give[i]}</Text>
                    <Pressable disabled={give[i] >= my[i] || want[i] > 0} onPress={() => adjustP2pGive(i, 1)}
                      style={[s.smallBtn, { backgroundColor: theme.background }]}>
                      <Text style={[s.btnText, { color: theme.text }]}>＋</Text>
                    </Pressable>
                  </View>
                ))}
                <View style={[s.tradeButtons, { marginTop: Spacing.three }]}>
                  <Pressable onPress={onClose} style={[s.btn, { flex: 1, backgroundColor: theme.backgroundSelected }]}>
                    <Text style={[s.btnText, { color: theme.text }]}>Cancel</Text>
                  </Pressable>
                  <Pressable
                    disabled={giveTotal === 0}
                    onPress={() => setP2PStep('want')}
                    style={[btnStyle(giveTotal > 0, theme), { flex: 1 }]}>
                    <Text style={[s.btnText, { color: giveTotal > 0 ? '#fff' : theme.textSecondary }]}>
                      Next →
                    </Text>
                  </Pressable>
                </View>
              </>
            )}

            {/* ── Step 2: Want ── */}
            {p2pStep === 'want' && (
              <>
                <Text style={[s.modalTitle, { color: theme.text }]}>What do you want?</Text>
                <Text style={[s.modalBody, { color: theme.textSecondary }]}>
                  Select at least 1 resource you want in return.
                </Text>
                {/* Compact offer summary */}
                <View style={[s.tradeOfferRow, { backgroundColor: theme.background, borderRadius: 10, padding: Spacing.two }]}>
                  <Text style={[s.tradeMini, { color: theme.textSecondary }]}>You're offering: </Text>
                  <TradeSummaryRow offer={give} want={[0,0,0,0,0]} theme={theme} />
                </View>
                {RESOURCES.map((r, i) => (
                  <View key={`w-${r.key}`} style={s.discardRow}>
                    <Text style={s.discardEmoji}>{r.emoji}</Text>
                    <Text style={[s.discardLabel, { color: theme.text }]}>{r.label}</Text>
                    <Pressable disabled={want[i] === 0} onPress={() => adjustP2pWant(i, -1)}
                      style={[s.smallBtn, { backgroundColor: theme.background }]}>
                      <Text style={[s.btnText, { color: theme.text }]}>−</Text>
                    </Pressable>
                    <Text style={[s.discardCount, { color: theme.text }]}>{want[i]}</Text>
                    <Pressable disabled={give[i] > 0} onPress={() => adjustP2pWant(i, 1)}
                      style={[s.smallBtn, { backgroundColor: theme.background }]}>
                      <Text style={[s.btnText, { color: theme.text }]}>＋</Text>
                    </Pressable>
                  </View>
                ))}
                <View style={[s.tradeButtons, { marginTop: Spacing.three }]}>
                  <Pressable onPress={() => setP2PStep('give')} style={[s.btn, { flex: 1, backgroundColor: theme.backgroundSelected }]}>
                    <Text style={[s.btnText, { color: theme.text }]}>← Back</Text>
                  </Pressable>
                  <Pressable
                    disabled={wantTotal === 0}
                    onPress={() => setP2PStep('target')}
                    style={[btnStyle(wantTotal > 0, theme), { flex: 1 }]}>
                    <Text style={[s.btnText, { color: wantTotal > 0 ? '#fff' : theme.textSecondary }]}>
                      Next →
                    </Text>
                  </Pressable>
                </View>
              </>
            )}

            {/* ── Step 3: Target ── */}
            {p2pStep === 'target' && (
              <>
                <Text style={[s.modalTitle, { color: theme.text }]}>Who do you want to trade with?</Text>
                {/* Trade summary */}
                <View style={[s.tradeOfferRow, { backgroundColor: theme.background, borderRadius: 10, padding: Spacing.two }]}>
                  <TradeSummaryRow offer={give} want={want} theme={theme} />
                </View>
                {/* Target selection */}
                <View style={s.targetRow}>
                  {otherPlayers.map(i => (
                    <Pressable
                      key={i}
                      onPress={() => setTarget(i)}
                      style={[
                        s.targetBtn,
                        { backgroundColor: target === i ? theme.primary : theme.backgroundSelected },
                      ]}>
                      <Text style={[s.targetBtnLabel, { color: target === i ? '#fff' : theme.text }]}>
                        P{i + 1}
                      </Text>
                    </Pressable>
                  ))}
                  {/* "Everyone" broadcasts to all other connected players */}
                  <Pressable
                    onPress={() => setTarget(NO_PLAYER)}
                    style={[
                      s.targetBtn,
                      { backgroundColor: target === NO_PLAYER ? theme.primary : theme.backgroundSelected },
                    ]}>
                    <Text style={[s.targetBtnLabel, { color: target === NO_PLAYER ? '#fff' : theme.text }]}>
                      Everyone
                    </Text>
                  </Pressable>
                </View>
                {target === NO_PLAYER && (
                  <Text style={[s.tradeBlockHint, { color: theme.textSecondary }]}>
                    Open offer — any opponent can accept. First to accept wins.
                  </Text>
                )}
                <View style={[s.tradeButtons, { marginTop: Spacing.three }]}>
                  <Pressable onPress={() => setP2PStep('want')} style={[s.btn, { flex: 1, backgroundColor: theme.backgroundSelected }]}>
                    <Text style={[s.btnText, { color: theme.text }]}>← Back</Text>
                  </Pressable>
                  <Pressable
                    disabled={target === null || give.some((g, i) => g > 0 && want[i] > 0)}
                    onPress={submitP2P}
                    style={[btnStyle(target !== null && !give.some((g, i) => g > 0 && want[i] > 0), theme), { flex: 1 }]}>
                    <Text style={[s.btnText, { color: target !== null && !give.some((g, i) => g > 0 && want[i] > 0) ? '#fff' : theme.textSecondary }]}>
                      Send Offer
                    </Text>
                  </Pressable>
                </View>
              </>
            )}
          </ScrollView>
        </View>
      </View>
    </Modal>
  );
}

// ── Year-of-Plenty / Monopoly resource pickers ─────────────────────────────

function YearOfPlentyModal({
  visible, onClose, onPick, theme,
}: {
  visible: boolean;
  onClose: () => void;
  onPick: (a: Resource, b: Resource) => void;
  theme: Theme;
}) {
  const [picks, setPicks] = useState<Resource[]>([]);
  useEffect(() => { if (!visible) setPicks([]); }, [visible]);
  if (!visible) return null;
  const select = (r: Resource) => {
    setPicks(prev => {
      if (prev.length >= 2) return prev;
      const next = [...prev, r];
      if (next.length === 2) onPick(next[0], next[1]);
      return next;
    });
  };
  return (
    <Modal visible transparent animationType="fade" onRequestClose={onClose}>
      <View style={s.modalBg}>
        <View style={[s.modalCard, { backgroundColor: theme.backgroundElement }]}>
          <Text style={[s.modalTitle, { color: theme.text }]}>Year of Plenty</Text>
          <Text style={[s.modalBody, { color: theme.textSecondary }]}>
            Pick {2 - picks.length} more resource{picks.length === 1 ? '' : 's'}.
          </Text>
          <View style={s.resPickerRow}>
            {RESOURCES.map(r => (
              <Pressable
                key={r.key}
                onPress={() => select(r.index)}
                style={[s.resPickerBtn, { backgroundColor: theme.background }]}>
                <Text style={s.resPickerEmoji}>{r.emoji}</Text>
              </Pressable>
            ))}
          </View>
          <Pressable onPress={onClose} style={[s.btn, { backgroundColor: theme.backgroundSelected }]}>
            <Text style={[s.btnText, { color: theme.text }]}>Cancel</Text>
          </Pressable>
        </View>
      </View>
    </Modal>
  );
}

function MonopolyModal({
  visible, onClose, onPick, theme,
}: {
  visible: boolean;
  onClose: () => void;
  onPick: (r: Resource) => void;
  theme: Theme;
}) {
  if (!visible) return null;
  return (
    <Modal visible transparent animationType="fade" onRequestClose={onClose}>
      <View style={s.modalBg}>
        <View style={[s.modalCard, { backgroundColor: theme.backgroundElement }]}>
          <Text style={[s.modalTitle, { color: theme.text }]}>Monopoly</Text>
          <Text style={[s.modalBody, { color: theme.textSecondary }]}>
            Choose a resource — every opponent gives you all they have.
          </Text>
          <View style={s.resPickerRow}>
            {RESOURCES.map(r => (
              <Pressable
                key={r.key}
                onPress={() => onPick(r.index)}
                style={[s.resPickerBtn, { backgroundColor: theme.background }]}>
                <Text style={s.resPickerEmoji}>{r.emoji}</Text>
              </Pressable>
            ))}
          </View>
          <Pressable onPress={onClose} style={[s.btn, { backgroundColor: theme.backgroundSelected }]}>
            <Text style={[s.btnText, { color: theme.text }]}>Cancel</Text>
          </Pressable>
        </View>
      </View>
    </Modal>
  );
}

// ── Dice roll popup ─────────────────────────────────────────────────────────

export function DiceRollPopup({
  visible,
  die1,
  die2,
  onDismiss,
  theme,
}: {
  visible: boolean;
  die1: number;
  die2: number;
  onDismiss: () => void;
  theme: Theme;
}) {
  const [face1, setFace1] = useState(1);
  const [face2, setFace2] = useState(1);
  const [revealed, setRevealed] = useState(false);
  const revealScale = useRef(new Animated.Value(0.4)).current;
  const die1Scale   = useRef(new Animated.Value(1)).current;
  const die2Scale   = useRef(new Animated.Value(1)).current;

  useEffect(() => {
    if (!visible) {
      setFace1(1);
      setFace2(1);
      setRevealed(false);
      revealScale.setValue(0.4);
      die1Scale.setValue(1);
      die2Scale.setValue(1);
      return;
    }

    // Bounce the dice while spinning
    const bounceAnim = Animated.loop(
      Animated.sequence([
        Animated.parallel([
          Animated.timing(die1Scale, { toValue: 0.82, duration: 45, useNativeDriver: true }),
          Animated.timing(die2Scale, { toValue: 0.82, duration: 45, useNativeDriver: true }),
        ]),
        Animated.parallel([
          Animated.timing(die1Scale, { toValue: 1.18, duration: 45, useNativeDriver: true }),
          Animated.timing(die2Scale, { toValue: 1.18, duration: 45, useNativeDriver: true }),
        ]),
      ]),
    );
    bounceAnim.start();

    // Cycle random faces for ~280 ms (7 frames × 40 ms)
    const FRAMES = 7;
    let count = 0;
    const spinInterval = setInterval(() => {
      count++;
      setFace1(Math.floor(Math.random() * 6) + 1);
      setFace2(Math.floor(Math.random() * 6) + 1);
      if (count >= FRAMES) {
        clearInterval(spinInterval);
        bounceAnim.stop();

        // Snap to final values
        setFace1(die1);
        setFace2(die2);
        setRevealed(true);

        // Land thump on each die
        Animated.sequence([
          Animated.timing(die1Scale, { toValue: 1.35, duration: 70, useNativeDriver: true }),
          Animated.timing(die1Scale, { toValue: 1.0,  duration: 110, useNativeDriver: true }),
        ]).start();
        Animated.sequence([
          Animated.timing(die2Scale, { toValue: 1.35, duration: 70, useNativeDriver: true }),
          Animated.timing(die2Scale, { toValue: 1.0,  duration: 110, useNativeDriver: true }),
        ]).start();

        // Total badge springs in
        Animated.spring(revealScale, {
          toValue: 1,
          friction: 4,
          tension: 280,
          useNativeDriver: true,
        }).start();
      }
    }, 40);

    // Auto-dismiss after 1.8 s
    const dismissTimer = setTimeout(onDismiss, 1800);

    return () => {
      clearInterval(spinInterval);
      clearTimeout(dismissTimer);
      bounceAnim.stop();
    };
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [visible]);

  if (!visible) return null;

  return (
    <Modal visible transparent animationType="fade" onRequestClose={onDismiss}>
      <Pressable style={s.diceModalBg} onPress={onDismiss}>
        <View style={[s.dicePopupCard, { backgroundColor: theme.backgroundElement }]}>
          <Text style={[s.dicePopupTitle, { color: theme.textSecondary }]}>
            {revealed ? 'Rolled!' : 'Rolling…'}
          </Text>
          <View style={s.dicePopupFaceRow}>
            <Animated.Text style={[s.dicePopupFace, { color: theme.text, transform: [{ scale: die1Scale }] }]}>
              {DIE_FACES[face1]}
            </Animated.Text>
            <Animated.Text style={[s.dicePopupFace, { color: theme.text, transform: [{ scale: die2Scale }] }]}>
              {DIE_FACES[face2]}
            </Animated.Text>
          </View>
          <Animated.View
            style={[
              s.dicePopupTotal,
              { backgroundColor: theme.primary, transform: [{ scale: revealScale }] },
            ]}>
            <Text style={s.dicePopupTotalText}>{die1 + die2}</Text>
          </Animated.View>
          <Text style={[s.dicePopupHint, { color: theme.textSecondary }]}>Tap to dismiss</Text>
        </View>
      </Pressable>
    </Modal>
  );
}

// ── Scoreboard ──────────────────────────────────────────────────────────────

export function Scoreboard({ state, myId, theme }: CommonProps) {
  const np = state.numPlayers || 1;
  return (
    <View style={s.section}>
      <Text style={[s.sectionLabel, { color: theme.textSecondary }]}>Scoreboard</Text>
      <View style={[s.scoreboardCard, { backgroundColor: theme.backgroundElement }]}>
        {Array.from({ length: np }, (_, i) => {
          const isMe = i === myId;
          const isWinner = state.winnerId === i;
          const isLA = state.largestArmyPlayer === i;
          const isLR = state.longestRoadPlayer === i;
          return (
            <View
              key={i}
              style={[
                s.scoreRow,
                { backgroundColor: isMe ? theme.backgroundSelected : 'transparent' },
              ]}>
              <Text style={[s.scoreName, { color: theme.text }]}>
                {isWinner ? '🏆 ' : ''}{isMe ? 'You' : `P${i + 1}`}
              </Text>
              <View style={s.scoreBadges}>
                {isLA && <Text style={[s.miniBadge, { color: theme.primary }]}>⚔ LA</Text>}
                {isLR && <Text style={[s.miniBadge, { color: theme.primary }]}>🛤 LR</Text>}
                <Text style={[s.miniBadge, { color: theme.textSecondary }]}>
                  {playerTotalCards(state, i)} cards
                </Text>
                <Text style={[s.miniBadge, { color: theme.textSecondary }]}>
                  {state.knightsPlayed[i] ?? 0}⚔
                </Text>
              </View>
              <Text style={[s.scoreVp, { color: theme.text }]}>{state.vp[i] ?? 0}</Text>
            </View>
          );
        })}
      </View>
    </View>
  );
}

// ── Styles ──────────────────────────────────────────────────────────────────

const s = StyleSheet.create({
  section: { gap: Spacing.two },
  sectionLabel: {
    fontSize: 12, fontWeight: '700', textTransform: 'uppercase', letterSpacing: 0.8,
  },
  sectionHeaderRow: {
    flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between',
  },
  totalLabel: { fontSize: 12, fontWeight: '600' },
  card: { borderRadius: 16, padding: Spacing.three, gap: Spacing.two },

  // Resource badges
  resourceRow: {
    flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between',
    gap: Spacing.two,
  },
  resBadge: {
    flex: 1, alignItems: 'center', paddingVertical: Spacing.two,
    borderRadius: 10,
  },
  resBadgeEmoji: { fontSize: 20 },
  resBadgeCount: { fontSize: 16, fontWeight: '800', marginTop: 2 },

  // Distribution toast
  distToast: {
    flexDirection: 'row', alignItems: 'center', gap: Spacing.two,
    borderRadius: 12, paddingHorizontal: Spacing.three, paddingVertical: Spacing.two,
  },
  distLabel: { fontSize: 12, fontWeight: '700' },
  distChips: { flexDirection: 'row', gap: Spacing.one, flexWrap: 'wrap' },
  distChip: {
    flexDirection: 'row', alignItems: 'center', gap: 4,
    paddingHorizontal: Spacing.two, paddingVertical: 4, borderRadius: 8,
  },
  distChipEmoji: { fontSize: 14 },
  distChipCount: { fontSize: 13, fontWeight: '700' },

  // Store rows
  storeRow: {
    flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between',
    paddingHorizontal: Spacing.three, paddingVertical: Spacing.two, borderRadius: 10,
  },
  storeLabel: { fontSize: 15, fontWeight: '700' },
  storeHint:  { fontSize: 11, textAlign: 'right' },
  costLine:   { flexDirection: 'row', gap: Spacing.two },
  costEmoji:  { fontSize: 13, fontWeight: '600' },

  // Banner
  banner: {
    borderRadius: 10, padding: Spacing.two, borderWidth: 1, marginTop: Spacing.one,
  },
  bannerText: { fontSize: 13, fontWeight: '600' },

  // Dev rows
  devRow: {
    flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between',
    paddingHorizontal: Spacing.three, paddingVertical: Spacing.two, borderRadius: 10,
  },
  devLabel: { fontSize: 14, fontWeight: '600' },
  devPlayCol: { alignItems: 'flex-end', gap: 2 },
  devNewHint: { fontSize: 10, fontWeight: '600', textTransform: 'uppercase', letterSpacing: 0.4 },

  // Drawn card modal
  drawnCardBadge: {
    flexDirection: 'row', alignItems: 'center', gap: Spacing.two,
    borderRadius: 12, paddingHorizontal: Spacing.three, paddingVertical: Spacing.two,
  },
  drawnCardEmoji: { fontSize: 32 },
  drawnCardName:  { fontSize: 20, fontWeight: '800' },
  drawnCardHint:  { fontSize: 12, fontStyle: 'italic' },

  // Buttons
  btn: {
    paddingHorizontal: Spacing.three, paddingVertical: 12,
    borderRadius: 10, alignItems: 'center', justifyContent: 'center', minWidth: 80,
  },
  smallBtn: {
    paddingHorizontal: Spacing.two, paddingVertical: 8, borderRadius: 8,
    minWidth: 40, alignItems: 'center', justifyContent: 'center',
  },
  btnText: { fontSize: 15, fontWeight: '700', textAlign: 'center' },

  // Modal
  modalBg: {
    flex: 1, justifyContent: 'center', alignItems: 'center',
    padding: Spacing.four, backgroundColor: '#000a',
  },
  modalCard: { borderRadius: 16, padding: Spacing.four, width: '100%', maxWidth: 480, gap: Spacing.two },
  // Robber picker: wider card to accommodate the hex map
  robberCard: {
    borderRadius: 16, padding: Spacing.three, width: '100%', maxWidth: 480,
    gap: Spacing.two, alignItems: 'center',
  },
  robberHint: { fontSize: 11, fontWeight: '500' },
  modalTitle: { fontSize: 18, fontWeight: '800' },
  modalBody:  { fontSize: 13, fontWeight: '500' },

  // Tile grid
  tileGrid: { flexDirection: 'row', flexWrap: 'wrap', gap: Spacing.two, justifyContent: 'center' },
  tileBtn: {
    width: 64, height: 64, borderRadius: 12, alignItems: 'center', justifyContent: 'center', gap: 2,
  },
  tileBtnLabel: { fontSize: 10, fontWeight: '600' },
  tileBtnNum:   { fontSize: 20, fontWeight: '800' },

  // Steal targets
  targetRow: { flexDirection: 'row', gap: Spacing.two, flexWrap: 'wrap', marginTop: Spacing.two },
  targetBtn: {
    paddingHorizontal: Spacing.three, paddingVertical: Spacing.two, borderRadius: 12,
    alignItems: 'center', minWidth: 80,
  },
  targetBtnLabel: { fontSize: 16, fontWeight: '800' },
  targetBtnSub:   { fontSize: 11, fontWeight: '600' },

  // Discard rows
  discardRow: {
    flexDirection: 'row', alignItems: 'center', gap: Spacing.two,
  },
  discardEmoji: { fontSize: 18, width: 28, textAlign: 'center' },
  discardLabel: { fontSize: 13, fontWeight: '600', flex: 1 },
  discardHave:  { fontSize: 11, fontWeight: '500', marginRight: Spacing.one },
  discardCount: { fontSize: 14, fontWeight: '800', minWidth: 24, textAlign: 'center' },

  // Trade
  tradePanelButtons: { flexDirection: 'row', gap: Spacing.two },
  tradeButtons:    { flexDirection: 'row', gap: Spacing.two, flexWrap: 'wrap' },
  tradeMini:       { fontSize: 12, fontWeight: '600' },
  tradeOfferRow:   { gap: Spacing.one, marginVertical: Spacing.one },
  pendingTradeRow: {
    flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between',
    marginTop: Spacing.two,
  },

  // Pending offer card (shown when offerer is waiting for a response)
  pendingOfferCard: {
    borderRadius: 12, borderWidth: 1, padding: Spacing.three,
    gap: Spacing.two, marginTop: Spacing.two,
  },
  pendingOfferHeader: { gap: 2 },
  pendingOfferTitle:  { fontSize: 14, fontWeight: '800' },
  pendingOfferSub:    { fontSize: 12, fontWeight: '500' },
  tradeBlockHint: { fontSize: 11, fontStyle: 'italic', textAlign: 'center' },

  // Credits indicator (bank trade)
  creditsRow: {
    flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between',
    borderRadius: 10, paddingHorizontal: Spacing.three, paddingVertical: Spacing.two,
    marginVertical: Spacing.one,
  },
  creditsCount: { fontSize: 16, fontWeight: '800' },

  // Trade summary (compact give → want row)
  tradeSummaryRow:   { flexDirection: 'row', alignItems: 'center', gap: Spacing.two, flexWrap: 'wrap' },
  tradeSummaryGroup: { flexDirection: 'row', gap: 4, flexWrap: 'wrap' },
  tradeSummaryChip:  { fontSize: 13, fontWeight: '700' },
  tradeSummaryArrow: { fontSize: 14, fontWeight: '700' },

  // Incoming trade dialog
  tradeDialogHeader:  { gap: Spacing.one },
  tradeDialogSection: { gap: Spacing.one },
  tradeDialogLabel:   { fontSize: 12, fontWeight: '600' },
  tradeDialogResBox:  { borderRadius: 10, padding: Spacing.two },
  openOfferBadge: {
    alignSelf: 'flex-start', paddingHorizontal: Spacing.two, paddingVertical: 3, borderRadius: 6,
  },
  openOfferBadgeText: { fontSize: 11, fontWeight: '600' },
  tradeAffordRow: {
    borderRadius: 10, padding: Spacing.two,
  },
  tradeAffordText: { fontSize: 12, fontWeight: '500' },

  // Step indicator for p2p composer
  stepIndicator: {
    flexDirection: 'row', alignItems: 'center', justifyContent: 'center',
    gap: 0, marginBottom: Spacing.two,
  },
  stepDot: {
    width: 28, height: 28, borderRadius: 14,
    alignItems: 'center', justifyContent: 'center',
  },
  stepDotText:  { fontSize: 12, fontWeight: '800' },
  stepDivider:  { height: 2, flex: 1, marginHorizontal: 4 },

  // Inner scroll content (inside modal card)
  modalInnerScroll: { gap: Spacing.two },

  // Resource picker
  resPickerRow: {
    flexDirection: 'row', justifyContent: 'space-around', marginVertical: Spacing.two,
  },
  resPickerBtn: {
    width: 56, height: 56, borderRadius: 12, alignItems: 'center', justifyContent: 'center',
  },
  resPickerEmoji: { fontSize: 26 },

  // Scoreboard
  scoreboardCard: { borderRadius: 16, padding: Spacing.two, gap: 2 },
  scoreRow: {
    flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between',
    paddingHorizontal: Spacing.two, paddingVertical: Spacing.two, borderRadius: 10,
  },
  scoreName:   { fontSize: 14, fontWeight: '700', minWidth: 60 },
  scoreBadges: { flexDirection: 'row', gap: Spacing.two, flex: 1, justifyContent: 'flex-end' },
  miniBadge:   { fontSize: 11, fontWeight: '700' },
  scoreVp:     { fontSize: 20, fontWeight: '900', marginLeft: Spacing.two, minWidth: 28, textAlign: 'right' },

  // Dice roll popup
  diceModalBg: {
    flex: 1, justifyContent: 'center', alignItems: 'center',
    backgroundColor: '#000a',
  },
  dicePopupCard: {
    borderRadius: 24, paddingHorizontal: Spacing.five, paddingVertical: Spacing.four,
    alignItems: 'center', gap: Spacing.three, minWidth: 220,
    shadowColor: '#000', shadowOffset: { width: 0, height: 8 }, shadowOpacity: 0.3, shadowRadius: 20, elevation: 10,
  },
  dicePopupTitle: { fontSize: 14, fontWeight: '700', textTransform: 'uppercase', letterSpacing: 1 },
  dicePopupFaceRow: { flexDirection: 'row', gap: Spacing.four },
  dicePopupFace:   { fontSize: 72, lineHeight: 84 },
  dicePopupTotal: {
    width: 80, height: 80, borderRadius: 40,
    alignItems: 'center', justifyContent: 'center',
  },
  dicePopupTotalText: { fontSize: 40, fontWeight: '900', color: '#fff' },
  dicePopupHint: { fontSize: 11, fontWeight: '500' },
});
