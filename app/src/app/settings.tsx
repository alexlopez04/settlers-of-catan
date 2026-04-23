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
import { Spacing } from '@/constants/theme';
import { useTheme } from '@/hooks/use-theme';
import { SFSymbolIcon } from '@/components/ui/symbol';

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
    key: 'vertexOverlay',
    label: 'Vertex Number Overlay',
    description: 'Show vertex indices on the board overview map.',
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
});
