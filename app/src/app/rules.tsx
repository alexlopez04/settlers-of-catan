import { router } from 'expo-router';
import React, { useMemo, useState } from 'react';
import {
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  TextInput,
  View,
} from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { Spacing } from '@/constants/theme';
import { useTheme } from '@/hooks/use-theme';
import { SFSymbolIcon } from '@/components/ui/symbol';

// ── Rules data ────────────────────────────────────────────────────────────────

interface RuleEntry {
  title: string;
  body: string;
}

interface RuleSection {
  section: string;
  rules: RuleEntry[];
}

const RULES_DATA: RuleSection[] = [
  {
    section: 'Objective',
    rules: [
      {
        title: 'Goal',
        body: 'Be the first player to reach 10 Victory Points (VP) on your turn to win the game.',
      },
    ],
  },
  {
    section: 'Setup',
    rules: [
      {
        title: 'Board Assembly',
        body: 'Randomly arrange the 19 terrain hexes (4 forest, 4 pasture, 4 fields, 3 hills, 3 mountains, 1 desert) to form the island. Place number tokens A–R on all non-desert hexes.',
      },
      {
        title: 'Initial Placement',
        body: 'In turn order, each player places 1 settlement and 1 road. Then in reverse turn order, each player places a second settlement and road. Players receive 1 resource card for each terrain hex adjacent to their second settlement.',
      },
    ],
  },
  {
    section: 'Turn Structure',
    rules: [
      {
        title: '1 — Roll the Dice',
        body: 'Roll both dice at the start of your turn. Every player collects resources for each settlement (1 card) or city (2 cards) adjacent to a hex matching the number rolled. On a 7, see Robber.',
      },
      {
        title: '2 — Trade',
        body: 'You may trade resources with other players (any deal is legal) or with the bank. The default bank rate is 4:1. Harbor settlements allow 3:1 or 2:1 rates for specific resources.',
      },
      {
        title: '3 — Build',
        body: 'You may build roads, settlements, cities, or buy development cards in any order and quantity, as long as you have the required resources.',
      },
    ],
  },
  {
    section: 'Resources',
    rules: [
      {
        title: 'Brick (Hills)',
        body: 'Used to build roads and settlements.',
      },
      {
        title: 'Lumber (Forest)',
        body: 'Used to build roads and settlements.',
      },
      {
        title: 'Ore (Mountains)',
        body: 'Used to build cities and buy development cards.',
      },
      {
        title: 'Grain (Fields)',
        body: 'Used to build settlements and cities, and to buy development cards.',
      },
      {
        title: 'Wool (Pasture)',
        body: 'Used to build settlements and buy development cards.',
      },
    ],
  },
  {
    section: 'Building Costs',
    rules: [
      {
        title: 'Road',
        body: '1 Brick + 1 Lumber. Roads must connect to your existing roads, settlements, or cities. You need the longest continuous road for Longest Road.',
      },
      {
        title: 'Settlement',
        body: '1 Brick + 1 Lumber + 1 Wool + 1 Grain. Must be placed on an unoccupied intersection connected to one of your roads. Cannot be adjacent to another settlement or city (distance rule). Worth 1 VP.',
      },
      {
        title: 'City',
        body: '3 Ore + 2 Grain. Replaces one of your existing settlements. Produces 2 resource cards per matching roll instead of 1. Worth 2 VP.',
      },
      {
        title: 'Development Card',
        body: '1 Ore + 1 Wool + 1 Grain. Draw the top card from the shuffled deck. Cannot be played the same turn it is bought.',
      },
    ],
  },
  {
    section: 'The Robber',
    rules: [
      {
        title: 'Rolling a 7',
        body: 'No resources are produced. Any player holding more than 7 resource cards must discard half (rounded down). The active player then moves the robber to any terrain hex and may steal 1 resource at random from one player with a settlement or city on that hex.',
      },
      {
        title: 'Knight Card',
        body: 'Playing a Knight card has the same effect as rolling a 7 — move the robber and optionally steal — without discarding. Knights count toward the Largest Army special card.',
      },
      {
        title: 'Blocked Hex',
        body: 'While the robber occupies a hex, that hex produces no resources when its number is rolled.',
      },
    ],
  },
  {
    section: 'Development Cards',
    rules: [
      {
        title: 'Knight',
        body: 'Move the robber and optionally steal 1 resource from an adjacent opponent. The player who plays the most Knights can claim the Largest Army card (2 VP). Minimum 3 Knights required.',
      },
      {
        title: 'Victory Point',
        body: 'Worth 1 VP. Keep hidden until you have enough points to win, then reveal.',
      },
      {
        title: 'Road Building',
        body: 'Place 2 new roads for free, following normal road-placement rules.',
      },
      {
        title: 'Year of Plenty',
        body: 'Take any 2 resource cards from the bank (can be different types).',
      },
      {
        title: 'Monopoly',
        body: 'Name 1 resource type. Every other player must give you all of their cards of that type.',
      },
    ],
  },
  {
    section: 'Special Cards',
    rules: [
      {
        title: 'Longest Road',
        body: 'Awarded to the first player to build a continuous road of at least 5 segments. Worth 2 VP. If another player builds a longer road they take the card.',
      },
      {
        title: 'Largest Army',
        body: 'Awarded to the first player to play at least 3 Knight cards. Worth 2 VP. If another player plays more Knights they take the card.',
      },
    ],
  },
  {
    section: 'Harbors & Trading',
    rules: [
      {
        title: 'Bank 4:1',
        body: 'At any time during your turn you may trade 4 identical resource cards to the bank for 1 card of any type, even without a harbor.',
      },
      {
        title: 'Generic Harbor 3:1',
        body: 'A settlement on a 3:1 harbor allows you to trade any 3 identical resource cards for 1 card of any type.',
      },
      {
        title: 'Specific Harbor 2:1',
        body: 'A settlement on a resource-specific harbor (e.g. Wool harbor) lets you trade 2 of that resource for 1 card of any type.',
      },
      {
        title: 'Player Trading',
        body: 'You may freely negotiate with other players during your turn. Any deal involving resources, future promises, or any other terms is allowed — but only resource cards exchanged in the current turn are binding.',
      },
    ],
  },
  {
    section: 'Victory Points',
    rules: [
      {
        title: 'Scoring',
        body: '1 VP per settlement, 2 VP per city, 2 VP for Longest Road, 2 VP for Largest Army, 1 VP per Victory Point development card.',
      },
      {
        title: 'Winning',
        body: 'If you reach 10 VP during your own turn, announce it and reveal your cards to prove it. You win immediately — even mid-action.',
      },
    ],
  },
];

// ── Helpers ───────────────────────────────────────────────────────────────────

function matches(text: string, query: string): boolean {
  return text.toLowerCase().includes(query.toLowerCase());
}

function filterRules(query: string): RuleSection[] {
  if (!query.trim()) return RULES_DATA;
  return RULES_DATA.reduce<RuleSection[]>((acc, section) => {
    const rules = section.rules.filter(
      r => matches(r.title, query) || matches(r.body, query) || matches(section.section, query),
    );
    if (rules.length > 0) acc.push({ ...section, rules });
    return acc;
  }, []);
}

// ── Sub-components ────────────────────────────────────────────────────────────

type Theme = ReturnType<typeof import('@/hooks/use-theme').useTheme>;

function SectionHeader({ title, theme }: { title: string; theme: Theme }) {
  return (
    <Text style={[s.sectionTitle, { color: theme.textSecondary }]}>{title}</Text>
  );
}

function RuleRow({
  title,
  body,
  divider,
  theme,
}: {
  title: string;
  body: string;
  divider?: boolean;
  theme: Theme;
}) {
  return (
    <View
      style={[
        s.ruleRow,
        divider && { borderTopWidth: StyleSheet.hairlineWidth, borderTopColor: theme.background },
      ]}>
      <Text style={[s.ruleTitle, { color: theme.text }]}>{title}</Text>
      <Text style={[s.ruleBody, { color: theme.textSecondary }]}>{body}</Text>
    </View>
  );
}

// ── Screen ────────────────────────────────────────────────────────────────────

export default function RulesScreen() {
  const theme = useTheme();
  const [query, setQuery] = useState('');

  const filtered = useMemo(() => filterRules(query), [query]);

  return (
    <View style={[s.root, { backgroundColor: theme.background }]}>
      <SafeAreaView style={s.safeArea} edges={['top', 'left', 'right']}>

        {/* Header */}
        <View style={[s.header, { borderBottomColor: theme.backgroundElement }]}>
          <Pressable
            onPress={() => router.back()}
            hitSlop={12}
            style={({ pressed }) => [s.headerSide, { opacity: pressed ? 0.5 : 1 }]}>
            <SFSymbolIcon name="chevron.left" size={20} color={theme.primary} weight="semibold" fallback="‹" />
          </Pressable>
          <Text style={[s.title, { color: theme.text }]}>Rules</Text>
          <View style={s.headerSide} />
        </View>

        {/* Search bar */}
        <View style={[s.searchContainer, { backgroundColor: theme.backgroundElement }]}>
          <SFSymbolIcon name="magnifyingglass" size={16} color={theme.textSecondary} fallback="🔍" />
          <TextInput
            style={[s.searchInput, { color: theme.text }]}
            placeholder="Search rules…"
            placeholderTextColor={theme.textSecondary}
            value={query}
            onChangeText={setQuery}
            returnKeyType="search"
            clearButtonMode="while-editing"
            autoCorrect={false}
          />
        </View>

        <ScrollView
          style={s.scroll}
          contentContainerStyle={s.scrollContent}
          keyboardDismissMode="on-drag"
          showsVerticalScrollIndicator={false}>

          {filtered.length === 0 ? (
            <View style={s.emptyState}>
              <SFSymbolIcon name="doc.text.magnifyingglass" size={40} color={theme.textSecondary} fallback="🔍" />
              <Text style={[s.emptyText, { color: theme.textSecondary }]}>
                No rules match "{query}"
              </Text>
            </View>
          ) : (
            filtered.map(section => (
              <View key={section.section} style={s.section}>
                <SectionHeader title={section.section.toUpperCase()} theme={theme} />
                <View style={[s.card, { backgroundColor: theme.backgroundElement }]}>
                  {section.rules.map((rule, i) => (
                    <RuleRow
                      key={rule.title}
                      title={rule.title}
                      body={rule.body}
                      divider={i > 0}
                      theme={theme}
                    />
                  ))}
                </View>
              </View>
            ))
          )}
        </ScrollView>
      </SafeAreaView>
    </View>
  );
}

// ── Styles ────────────────────────────────────────────────────────────────────

const s = StyleSheet.create({
  root:     { flex: 1 },
  safeArea: { flex: 1 },

  header: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingHorizontal: Spacing.three,
    paddingVertical: Spacing.three,
    borderBottomWidth: StyleSheet.hairlineWidth,
  },
  headerSide: { width: 44, alignItems: 'flex-start' },
  title: { fontSize: 18, fontWeight: '700' },

  searchContainer: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: Spacing.two,
    marginHorizontal: Spacing.three,
    marginTop: Spacing.three,
    paddingHorizontal: Spacing.three,
    paddingVertical: Spacing.two + 2,
    borderRadius: 12,
  },
  searchInput: {
    flex: 1,
    fontSize: 16,
    paddingVertical: 0,
  },

  scroll:        { flex: 1, marginTop: Spacing.three },
  scrollContent: { padding: Spacing.three, gap: Spacing.three, paddingBottom: Spacing.six },

  section: { gap: Spacing.one },
  sectionTitle: {
    fontSize: 12,
    fontWeight: '700',
    letterSpacing: 0.8,
    textTransform: 'uppercase',
    paddingHorizontal: Spacing.two,
    paddingBottom: Spacing.one,
  },
  card: {
    borderRadius: 16,
    overflow: 'hidden',
  },
  ruleRow: {
    paddingHorizontal: Spacing.three,
    paddingVertical: Spacing.three,
    gap: Spacing.one,
  },
  ruleTitle: { fontSize: 15, fontWeight: '600' },
  ruleBody:  { fontSize: 14, lineHeight: 20 },

  emptyState: {
    alignItems: 'center',
    paddingTop: Spacing.six,
    gap: Spacing.three,
  },
  emptyText: { fontSize: 15 },
});
