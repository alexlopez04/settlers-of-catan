import { router } from 'expo-router';
import React from 'react';
import {
  Pressable,
  ScrollView,
  StyleSheet,
  Switch,
  Text,
  View,
} from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { DebugSettings, useSettings } from '@/context/settings-context';
import { useTutorial } from '@/context/tutorial-context';
import { Fonts, Spacing } from '@/constants/theme';
import { useTheme } from '@/hooks/use-theme';
import { SFSymbolIcon } from '@/components/ui/symbol';

// ── About data ────────────────────────────────────────────────────────────────

const TEAM_MEMBERS = [
  { name: 'Alexander Lopez',  discipline: 'CmpE', hometown: 'Tampa, FL'         },
  { name: 'Alyssa Nomura',    discipline: 'EE',   hometown: 'Houston, TX'        },
  { name: 'Andrew Lemons',    discipline: 'CmpE', hometown: 'LaFayette, GA'      },
  { name: 'Laura Huff',       discipline: 'EE',   hometown: 'Aiken, SC'          },
  { name: 'Rashika Marpaung', discipline: 'EE',   hometown: 'Jakarta, Indonesia' },
];

// ── Debug toggle definitions ──────────────────────────────────────────────────
//
// To add a new debug toggle, add an entry to this array.
// The key must match a field in DebugSettings (settings-context.tsx).
// No other changes are required.

interface DebugToggleDef {
  key: keyof DebugSettings;
  label: string;
  description: string;
}

const DEBUG_TOGGLES: DebugToggleDef[] = [
  {
    key: 'numberOverlay',
    label: 'Number Overlay',
    description: 'Show tile, vertex, and edge index numbers on the board map.',
  },
  {
    key: 'simulatedBoard',
    label: 'Simulated Board',
    description: 'Show a "Simulated Board" option on the scan screen so all app functionality can be tested without hardware.',
  },
  {
    key: 'alwaysOfferTutorial',
    label: 'Always Offer Tutorial',
    description: 'Show the in-game tutorial on every new game.',
  },
];

// ── Sub-components ────────────────────────────────────────────────────────────

type Theme = ReturnType<typeof import('@/hooks/use-theme').useTheme>;

function SectionHeader({ title, theme }: { title: string; theme: Theme }) {
  return (
    <Text style={[s.sectionTitle, { color: theme.textSecondary }]}>{title}</Text>
  );
}

function ToggleRow({
  label,
  description,
  value,
  onToggle,
  divider,
  theme,
}: {
  label: string;
  description: string;
  value: boolean;
  onToggle: (v: boolean) => void;
  divider?: boolean;
  theme: Theme;
}) {
  return (
    <View
      style={[
        s.toggleRow,
        divider && { borderTopWidth: StyleSheet.hairlineWidth, borderTopColor: theme.background },
      ]}>
      <View style={s.toggleText}>
        <Text style={[s.toggleLabel, { color: theme.text }]}>{label}</Text>
        <Text style={[s.toggleDesc, { color: theme.textSecondary }]}>{description}</Text>
      </View>
      <Switch
        value={value}
        onValueChange={onToggle}
        trackColor={{ true: theme.primary }}
      />
    </View>
  );
}

function Card({ children, theme }: { children: React.ReactNode; theme: Theme }) {
  return (
    <View style={[s.card, { backgroundColor: theme.backgroundElement }]}>
      {children}
    </View>
  );
}

// ── Screen ────────────────────────────────────────────────────────────────────

export default function SettingsScreen() {
  const theme = useTheme();
  const { debug, setDebug } = useSettings();
  const { reset: resetTutorial } = useTutorial();

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
          <Text style={[s.title, { color: theme.text }]}>Settings</Text>
          {/* Spacer to keep title centred */}
          <View style={s.headerSide} />
        </View>

        <ScrollView
          style={s.scroll}
          contentContainerStyle={s.scrollContent}
          showsVerticalScrollIndicator={false}>

          {/* ── Debug ────────────────────────────────────────────── */}
          <View style={s.section}>
            <SectionHeader title="DEBUG" theme={theme} />
            <Card theme={theme}>
              {DEBUG_TOGGLES.map((def, i) => (
                <ToggleRow
                  key={def.key}
                  label={def.label}
                  description={def.description}
                  value={debug[def.key]}
                  onToggle={v => setDebug(def.key, v)}
                  divider={i > 0}
                  theme={theme}
                />
              ))}
            </Card>
            {/* Reset tutorial progress */}
            <Pressable
              onPress={resetTutorial}
              style={({ pressed }) => [
                s.resetBtn,
                { backgroundColor: theme.backgroundElement, opacity: pressed ? 0.6 : 1 },
              ]}
              accessibilityRole="button"
              accessibilityLabel="Reset tutorial progress">
              <Text style={[s.resetBtnText, { color: theme.textSecondary }]}>
                Reset Tutorial Progress
              </Text>
            </Pressable>
          </View>

          {/* ── About ────────────────────────────────────────────── */}
          <View style={s.section}>
            <SectionHeader title="ABOUT" theme={theme} />
            <Card theme={theme}>
              <View style={s.aboutTaglineRow}>
                <Text style={[s.aboutTagline, { color: theme.textSecondary }]}>
                  Developed for a Capstone Design project at{' '}
                  <Text style={[s.aboutBold, { color: theme.text }]}>
                    The Georgia Institute of Technology
                  </Text>
                  , Spring 2026.
                </Text>
              </View>
              <Text style={[s.membersLabel, { color: theme.textSecondary, borderTopColor: theme.background }]}>
                TEAM MEMBERS
              </Text>
              {TEAM_MEMBERS.map((m, i) => (
                <View
                  key={m.name}
                  style={[
                    s.memberRow,
                  ]}>
                  <View style={s.memberLeft}>
                    <Text style={[s.memberName, { color: theme.text }]}>{m.name}</Text>
                    <Text style={[s.memberSub, { color: theme.textSecondary }]}>{m.hometown}</Text>
                  </View>
                  <View style={[s.disciplineBadge, { backgroundColor: theme.backgroundSelected }]}>
                    <Text style={[s.disciplineText, { color: theme.primary }]}>{m.discipline}</Text>
                  </View>
                </View>
              ))}
              <View style={[s.publisherRow, { borderTopColor: theme.background }]}>
                <Text style={[s.publisher, { color: theme.textSecondary }]}>
                  Published by{' '}
                  <Text style={[s.aboutBold, { color: theme.text }]}>Lemony Click, LLC.</Text>
                </Text>
              </View>
            </Card>
          </View>

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
  title:      { fontSize: 18, fontWeight: '700' },

  scroll:        { flex: 1 },
  scrollContent: { padding: Spacing.four, gap: Spacing.two, paddingBottom: Spacing.six },

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
  toggleRow: {
    flexDirection: 'row',
    alignItems: 'center',
    paddingHorizontal: Spacing.three,
    paddingVertical: Spacing.three,
    gap: Spacing.three,
  },
  toggleText:  { flex: 1 },
  toggleLabel: { fontSize: 16 },
  toggleDesc:  { fontSize: 13, marginTop: 2 },

  aboutTaglineRow: {
    paddingHorizontal: Spacing.three,
    paddingVertical: Spacing.three,
  },
  aboutTagline: { fontSize: 14, lineHeight: 20 },
  aboutBold:    { fontWeight: '700' },

  membersLabel: {
    fontSize: 11,
    fontWeight: '700',
    letterSpacing: 0.8,
    textTransform: 'uppercase',
    paddingHorizontal: Spacing.three,
    paddingTop: Spacing.two,
    paddingBottom: Spacing.two,
    borderTopWidth: StyleSheet.hairlineWidth,
  },
  memberRow: {
    flexDirection: 'row',
    alignItems: 'center',
    paddingHorizontal: Spacing.three,
    paddingVertical: Spacing.two + 2,
    gap: Spacing.two,
  },
  memberLeft:      { flex: 1 },
  memberName:      { fontSize: 15, fontWeight: '600' },
  memberSub:       { fontSize: 13, marginTop: 1 },
  disciplineBadge: { paddingHorizontal: 10, paddingVertical: 4, borderRadius: 8 },
  disciplineText:  { fontSize: 13, fontWeight: '700' },
  publisherRow: {
    paddingHorizontal: Spacing.three,
    paddingVertical: Spacing.three,
    borderTopWidth: StyleSheet.hairlineWidth,
    alignItems: 'center',
  },
  publisher: { fontSize: 13 },

  resetBtn: {
    borderRadius: 12,
    paddingVertical: Spacing.two + 2,
    paddingHorizontal: Spacing.three,
    alignItems: 'center',
  },
  resetBtnText: { fontSize: 14, fontWeight: '500' },
});
