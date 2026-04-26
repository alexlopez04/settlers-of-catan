// =============================================================================
// game-actions.tsx — v7 store / dev cards / trade / robber / discard panels.
//
// The Mega is authoritative: every interaction here just sends a PlayerInput
// over BLE (or to the simulator). The UI displays the current BoardState
// values read-only; no local mirroring of VP or resources.
// =============================================================================

import React, { useEffect, useMemo, useState } from 'react';
import {
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

import { Spacing } from '@/constants/theme';
import { RESOURCES } from '@/constants/game';
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

// ── Types ───────────────────────────────────────────────────────────────────

type SendInput = (input: { action: PlayerAction; [k: string]: unknown }) => Promise<void>;

interface CommonProps {
  state: BoardState;
  myId: number;
  myTurn: boolean;
  sendInput: SendInput;
  theme: any;
}

// ── Helpers ─────────────────────────────────────────────────────────────────

function ResourceBadgeRow({
  values,
  highlight,
  theme,
}: {
  values: number[];
  highlight?: number[];
  theme: any;
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

function btnStyle(enabled: boolean, theme: any) {
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

const ROAD_COST       = [1, 0, 0, 1, 0];
const SETTLEMENT_COST = [1, 1, 1, 1, 0];
const CITY_COST       = [0, 0, 2, 0, 3];
const DEV_CARD_COST   = [0, 1, 1, 0, 1];

function canAfford(my: number[], cost: number[]): boolean {
  for (let i = 0; i < 5; i++) if ((my[i] ?? 0) < cost[i]) return false;
  return true;
}

function CostLine({ cost, theme }: { cost: number[]; theme: any }) {
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
  const items: { label: string; cost: number[]; action: PlayerAction; available: boolean }[] = [
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
  if (c > 0) parts.push(`${c} city${c === 1 ? 'y' : 'ies'} ready`);
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
  state, myId, myTurn, sendInput, theme,
}: CommonProps) {
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
          const enabled = canPlay && !isVp;
          return (
            <View key={card} style={[s.devRow, { backgroundColor: theme.background }]}>
              <Text style={[s.devLabel, { color: theme.text }]}>
                {DEV_LABEL[card]} ×{n}
              </Text>
              {!isVp && (
                <Pressable
                  disabled={!enabled}
                  onPress={() => {
                    if (card === DevCard.KNIGHT)         onPlay(PlayerAction.PLAY_KNIGHT);
                    else if (card === DevCard.ROAD_BUILDING) onPlay(PlayerAction.PLAY_ROAD_BUILDING);
                    else if (card === DevCard.YEAR_OF_PLENTY) setYopOpen(true);
                    else if (card === DevCard.MONOPOLY)  setMonoOpen(true);
                  }}
                  style={btnStyle(enabled, theme)}>
                  <Text style={[s.btnText, { color: enabled ? '#fff' : theme.textSecondary }]}>
                    Play
                  </Text>
                </Pressable>
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

          {/* Zoomable / pannable hex map */}
          <ScrollView
            style={{ width: mapSize, height: mapSize }}
            contentContainerStyle={{ width: mapSize, height: mapSize }}
            minimumZoomScale={1}
            maximumZoomScale={3}
            bouncesZoom
            showsVerticalScrollIndicator={false}
            showsHorizontalScrollIndicator={false}
            centerContent
            scrollsToTop={false}>
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
          </ScrollView>

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

export function TradePanel({
  state, myId, myTurn, sendInput, theme,
}: CommonProps) {
  const [open, setOpen] = useState<'bank' | 'p2p' | null>(null);

  const canOffer =
    myTurn &&
    state.phase === GamePhase.PLAYING &&
    state.hasRolled &&
    state.trade.fromPlayer === NO_PLAYER;

  return (
    <View style={s.section}>
      <Text style={[s.sectionLabel, { color: theme.textSecondary }]}>Trade</Text>
      <View style={[s.card, { backgroundColor: theme.backgroundElement }]}>
        <View style={s.tradeButtons}>
          <Pressable
            disabled={!canOffer}
            onPress={() => setOpen('bank')}
            style={btnStyle(canOffer, theme)}>
            <Text style={[s.btnText, { color: canOffer ? '#fff' : theme.textSecondary }]}>
              Bank
            </Text>
          </Pressable>
          <Pressable
            disabled={!canOffer}
            onPress={() => setOpen('p2p')}
            style={btnStyle(canOffer, theme)}>
            <Text style={[s.btnText, { color: canOffer ? '#fff' : theme.textSecondary }]}>
              Players
            </Text>
          </Pressable>
        </View>
        {state.trade.fromPlayer === myId && (
          <View style={s.pendingTradeRow}>
            <Text style={[s.tradeMini, { color: theme.textSecondary }]}>
              You have an open offer
            </Text>
            <Pressable
              onPress={() => sendInput({ action: PlayerAction.TRADE_CANCEL })}
              style={[s.smallBtn, { backgroundColor: theme.backgroundSelected }]}>
              <Text style={[s.btnText, { color: theme.text }]}>Cancel</Text>
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
  if (t.fromPlayer === NO_PLAYER) return null;
  if (t.fromPlayer === myId) return null;
  // Only show to the targeted player (or to all opponents on an open offer).
  if (t.toPlayer !== NO_PLAYER && t.toPlayer !== myId) return null;

  const my = playerResources(state, myId);
  const canAcceptResources = t.want.every((w, i) => my[i] >= w);

  return (
    <Modal visible transparent animationType="fade">
      <View style={s.modalBg}>
        <View style={[s.modalCard, { backgroundColor: theme.backgroundElement }]}>
          <Text style={[s.modalTitle, { color: theme.text }]}>
            Trade offer from P{t.fromPlayer + 1}
          </Text>
          <View style={s.tradeOfferRow}>
            <Text style={[s.tradeMini, { color: theme.textSecondary }]}>Gives</Text>
            <ResourceBadgeRow values={t.offer} theme={theme} />
          </View>
          <View style={s.tradeOfferRow}>
            <Text style={[s.tradeMini, { color: theme.textSecondary }]}>Wants</Text>
            <ResourceBadgeRow values={t.want} theme={theme} />
          </View>
          <View style={s.tradeButtons}>
            <Pressable
              disabled={!canAcceptResources}
              onPress={() => sendInput({ action: PlayerAction.TRADE_ACCEPT })}
              style={btnStyle(canAcceptResources, theme)}>
              <Text style={[s.btnText, { color: canAcceptResources ? '#fff' : theme.textSecondary }]}>
                Accept
              </Text>
            </Pressable>
            <Pressable
              onPress={() => sendInput({ action: PlayerAction.TRADE_DECLINE })}
              style={[s.btn, { backgroundColor: theme.backgroundSelected }]}>
              <Text style={[s.btnText, { color: theme.text }]}>Decline</Text>
            </Pressable>
          </View>
        </View>
      </View>
    </Modal>
  );
}

// ── Trade composer ──────────────────────────────────────────────────────────

function TradeComposer({
  kind, state, myId, sendInput, onClose, theme,
}: {
  kind: 'bank' | 'p2p' | null;
  state: BoardState;
  myId: number;
  sendInput: SendInput;
  onClose: () => void;
  theme: any;
}) {
  const [give, setGive] = useState<number[]>([0, 0, 0, 0, 0]);
  const [want, setWant] = useState<number[]>([0, 0, 0, 0, 0]);
  const [target, setTarget] = useState<number>(NO_PLAYER);

  useEffect(() => {
    if (!kind) {
      setGive([0, 0, 0, 0, 0]);
      setWant([0, 0, 0, 0, 0]);
      setTarget(NO_PLAYER);
    }
  }, [kind]);

  if (!kind) return null;

  const my = playerResources(state, myId);
  const giveTotal = give.reduce((a, b) => a + b, 0);
  const wantTotal = want.reduce((a, b) => a + b, 0);

  const canSubmit =
    kind === 'bank'
      ? giveTotal > 0 &&
        wantTotal > 0 &&
        // Default 4:1 — the board enforces actual ratios via ports.
        giveTotal >= wantTotal * 2 &&
        give.every((g, i) => my[i] >= g)
      : giveTotal > 0 &&
        wantTotal > 0 &&
        give.every((g, i) => my[i] >= g);

  const adjust = (
    set: React.Dispatch<React.SetStateAction<number[]>>,
    i: number,
    delta: number,
    cap?: number,
  ) => {
    set(prev => {
      const next = prev.slice();
      const v = Math.max(0, (prev[i] ?? 0) + delta);
      next[i] = cap !== undefined ? Math.min(cap, v) : v;
      return next;
    });
  };

  const submit = () => {
    if (kind === 'bank') {
      sendInput({
        action: PlayerAction.BANK_TRADE,
        resLumber: give[0], resWool: give[1], resGrain: give[2], resBrick: give[3], resOre: give[4],
        wantLumber: want[0], wantWool: want[1], wantGrain: want[2], wantBrick: want[3], wantOre: want[4],
      });
    } else {
      sendInput({
        action: PlayerAction.TRADE_OFFER,
        targetPlayer: target,
        resLumber: give[0], resWool: give[1], resGrain: give[2], resBrick: give[3], resOre: give[4],
        wantLumber: want[0], wantWool: want[1], wantGrain: want[2], wantBrick: want[3], wantOre: want[4],
      });
    }
    onClose();
  };

  return (
    <Modal visible transparent animationType="fade" onRequestClose={onClose}>
      <View style={s.modalBg}>
        <View style={[s.modalCard, { backgroundColor: theme.backgroundElement }]}>
          <Text style={[s.modalTitle, { color: theme.text }]}>
            {kind === 'bank' ? 'Trade with bank' : 'Trade with players'}
          </Text>

          <Text style={[s.tradeMini, { color: theme.textSecondary }]}>You give</Text>
          {RESOURCES.map((r, i) => (
            <View key={`g-${r.key}`} style={s.discardRow}>
              <Text style={s.discardEmoji}>{r.emoji}</Text>
              <Text style={[s.discardHave, { color: theme.textSecondary }]}>have {my[i]}</Text>
              <Pressable
                disabled={give[i] === 0}
                onPress={() => adjust(setGive, i, -1)}
                style={[s.smallBtn, { backgroundColor: theme.background }]}>
                <Text style={[s.btnText, { color: theme.text }]}>−</Text>
              </Pressable>
              <Text style={[s.discardCount, { color: theme.text }]}>{give[i]}</Text>
              <Pressable
                disabled={give[i] >= my[i]}
                onPress={() => adjust(setGive, i, 1, my[i])}
                style={[s.smallBtn, { backgroundColor: theme.background }]}>
                <Text style={[s.btnText, { color: theme.text }]}>＋</Text>
              </Pressable>
            </View>
          ))}

          <Text style={[s.tradeMini, { color: theme.textSecondary, marginTop: Spacing.two }]}>
            You want
          </Text>
          {RESOURCES.map((r, i) => (
            <View key={`w-${r.key}`} style={s.discardRow}>
              <Text style={s.discardEmoji}>{r.emoji}</Text>
              <Pressable
                disabled={want[i] === 0}
                onPress={() => adjust(setWant, i, -1)}
                style={[s.smallBtn, { backgroundColor: theme.background }]}>
                <Text style={[s.btnText, { color: theme.text }]}>−</Text>
              </Pressable>
              <Text style={[s.discardCount, { color: theme.text }]}>{want[i]}</Text>
              <Pressable
                onPress={() => adjust(setWant, i, 1)}
                style={[s.smallBtn, { backgroundColor: theme.background }]}>
                <Text style={[s.btnText, { color: theme.text }]}>＋</Text>
              </Pressable>
            </View>
          ))}

          {kind === 'p2p' && (
            <View style={[s.tradeButtons, { marginTop: Spacing.two }]}>
              {[0, 1, 2, 3].filter(i => i !== myId && i < state.numPlayers).map(i => (
                <Pressable
                  key={i}
                  onPress={() => setTarget(i)}
                  style={[
                    s.smallBtn,
                    { backgroundColor: target === i ? theme.primary : theme.background },
                  ]}>
                  <Text style={[s.btnText, { color: target === i ? '#fff' : theme.text }]}>
                    P{i + 1}
                  </Text>
                </Pressable>
              ))}
              <Pressable
                onPress={() => setTarget(NO_PLAYER)}
                style={[
                  s.smallBtn,
                  { backgroundColor: target === NO_PLAYER ? theme.primary : theme.background },
                ]}>
                <Text style={[s.btnText, { color: target === NO_PLAYER ? '#fff' : theme.text }]}>
                  Open
                </Text>
              </Pressable>
            </View>
          )}

          <View style={[s.tradeButtons, { marginTop: Spacing.three }]}>
            <Pressable onPress={onClose} style={[s.btn, { backgroundColor: theme.backgroundSelected }]}>
              <Text style={[s.btnText, { color: theme.text }]}>Cancel</Text>
            </Pressable>
            <Pressable disabled={!canSubmit} onPress={submit} style={btnStyle(canSubmit, theme)}>
              <Text style={[s.btnText, { color: canSubmit ? '#fff' : theme.textSecondary }]}>
                {kind === 'bank' ? 'Trade' : 'Send Offer'}
              </Text>
            </Pressable>
          </View>
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
  theme: any;
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
  theme: any;
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

  // Buttons
  btn: {
    paddingHorizontal: Spacing.three, paddingVertical: Spacing.two,
    borderRadius: 10, alignItems: 'center', justifyContent: 'center', minWidth: 80,
  },
  smallBtn: {
    paddingHorizontal: Spacing.two, paddingVertical: 6, borderRadius: 8,
    minWidth: 36, alignItems: 'center', justifyContent: 'center',
  },
  btnText: { fontSize: 14, fontWeight: '700' },

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
  tradeButtons:    { flexDirection: 'row', gap: Spacing.two, flexWrap: 'wrap' },
  tradeMini:       { fontSize: 12, fontWeight: '600' },
  tradeOfferRow:   { gap: Spacing.one, marginVertical: Spacing.one },
  pendingTradeRow: {
    flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between',
    marginTop: Spacing.two,
  },

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
});
