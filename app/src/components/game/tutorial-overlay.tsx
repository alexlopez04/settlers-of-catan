// =============================================================================
// tutorial-overlay.tsx — Bottom-sheet tutorial card shown at key game moments.
//
// Each step has a short title + body (bold phrases wrapped in **...**) plus
// progress dots.  "Got it" advances/dismisses; "Skip Tutorial" skips all.
// =============================================================================

import React, { useEffect, useRef, useState } from 'react';
import {
  Animated,
  Easing,
  Pressable,
  StyleProp,
  StyleSheet,
  Text,
  type TextStyle,
  View,
} from 'react-native';

import { Spacing } from '@/constants/theme';
import type { useTheme } from '@/hooks/use-theme';
import {
  TUTORIAL_STEP_IDS,
  TUTORIAL_TOTAL,
  type TutorialStepId,
} from '@/context/tutorial-context';

// ── Step content definitions ─────────────────────────────────────────────────

interface StepContent {
  icon:  string;
  title: string;
  /** Body text. Wrap phrases in **double asterisks** for bold. */
  body:  string;
}

export const TUTORIAL_STEP_CONTENT: Record<TutorialStepId, StepContent> = {
  welcome: {
    icon:  '🏝️',
    title: 'Welcome to Catan!',
    body:  'Connect your phone to the board. When your friends join, tap **Start Game** to kick things off.',
  },
  board_setup: {
    icon:  '🗺️',
    title: 'Setting Up the Board',
    body:  'The physical board is assembling now. Tiles and ports are randomized every game — no two games are the same.',
  },
  number_reveal: {
    icon:  '🔢',
    title: 'Number Tokens',
    body:  'Numbers are being placed on each hex. Roll that number → everyone with a settlement next to it collects a resource. **6 and 8** are rolled most often, so settle nearby!',
  },
  initial_placement: {
    icon:  '🏘️',
    title: 'First Placements',
    body:  'Each player places **2 settlements + 2 roads** — first in order, then in reverse. Pick spots touching high numbers and different resource types. Your 2nd settlement earns immediate resources.',
  },
  first_roll: {
    icon:  '🎲',
    title: 'Start Your Turn',
    body:  'Tap **Roll Dice** to begin each turn. Every player with a settlement adjacent to the rolled number collects one resource from that tile.',
  },
  use_resources: {
    icon:  '🏗️',
    title: 'Build & Trade',
    body:  'Spend resources to build roads 🛣️, settlements 🏠, cities 🏙️, or buy dev cards 🃏. Trade **4:1 with the bank**, or haggle with other players. Tap **End Turn** when you\'re done.',
  },
  robber: {
    icon:  '🦹',
    title: 'The Robber!',
    body:  'Someone rolled a 7! Tap a hex on the map to move the robber — it blocks that tile while it sits there. Players holding **8+ cards** must discard half. Then steal one card from a neighbor.',
  },
};

// ── Helpers ──────────────────────────────────────────────────────────────────

type Theme = ReturnType<typeof useTheme>;

/** Render body text, bolding any **phrase** spans. */
function BodyText({
  text,
  style,
  boldStyle,
}: {
  text: string;
  style: StyleProp<TextStyle>;
  boldStyle: TextStyle;
}) {
  const parts = text.split(/\*\*(.*?)\*\*/g);
  return (
    <Text style={style}>
      {parts.map((part, i) =>
        i % 2 === 1
          ? <Text key={i} style={boldStyle}>{part}</Text>
          : part,
      )}
    </Text>
  );
}

// ── Animation constants ───────────────────────────────────────────────────────

const SLIDE_OFF    = 600; // px — well past the bottom of any device screen
const FADE_DURATION  = 200;
const EXIT_DURATION  = 280;

// ── TutorialOverlay ──────────────────────────────────────────────────────────

interface TutorialOverlayProps {
  /** Currently active step id, or null when not showing. */
  stepId: TutorialStepId | null;
  onDismiss: () => void;
  onSkip:    () => void;
  theme: Theme;
}

export function TutorialOverlay({
  stepId,
  onDismiss,
  onSkip,
  theme,
}: TutorialOverlayProps) {
  const [mounted,  setMounted]  = useState(false);
  // visible=true while the tutorial is actively presented (not in exit animation).
  // Used to switch pointerEvents so the game is immediately interactive on dismiss.
  const [visible,  setVisible]  = useState(false);
  const [content,  setContent]  = useState<StepContent | null>(null);
  const [idxSnap,  setIdxSnap]  = useState(0);

  const slideAnim = useRef(new Animated.Value(SLIDE_OFF)).current;
  const fadeAnim  = useRef(new Animated.Value(0)).current;

  // Ref guards against a rapid enter during an in-flight exit animation setting
  // mounted=false after the new step has already set mounted=true.
  const pendingUnmountRef = useRef(false);

  useEffect(() => {
    if (stepId) {
      pendingUnmountRef.current = false;
      setContent(TUTORIAL_STEP_CONTENT[stepId]);
      setIdxSnap(TUTORIAL_STEP_IDS.indexOf(stepId));
      // Always reset position before entering so re-triggers start from below.
      slideAnim.setValue(SLIDE_OFF);
      fadeAnim.setValue(0);
      setMounted(true);
      setVisible(true);
      // Enter: backdrop fades in + card springs up from off-screen bottom.
      Animated.parallel([
        Animated.timing(fadeAnim, {
          toValue:        1,
          duration:       FADE_DURATION,
          useNativeDriver: true,
        }),
        Animated.spring(slideAnim, {
          toValue:        0,
          damping:        22,
          stiffness:      280,
          mass:           1,
          useNativeDriver: true,
        }),
      ]).start();
    } else if (mounted) {
      // Immediately restore game interactivity — exit animation plays decoratively.
      setVisible(false);
      pendingUnmountRef.current = true;
      Animated.parallel([
        Animated.timing(fadeAnim, {
          toValue:        0,
          duration:       FADE_DURATION,
          easing:         Easing.out(Easing.ease),
          useNativeDriver: true,
        }),
        Animated.timing(slideAnim, {
          toValue:        SLIDE_OFF,
          duration:       EXIT_DURATION,
          easing:         Easing.in(Easing.ease),
          useNativeDriver: true,
        }),
      ]).start(() => {
        if (pendingUnmountRef.current) setMounted(false);
      });
    }
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [stepId]);

  if (!mounted || !content) return null;

  const stepNum = idxSnap + 1;

  // When visible=false (exit animation), the whole subtree is non-interactive so
  // the player can resume using the game immediately without waiting for the
  // animation to finish. This uses React Native's pointerEvents on a plain View
  // (no Modal) which correctly passes touches through at the native layer.
  return (
    <View
      style={ov.root}
      pointerEvents={visible ? 'box-none' : 'none'}>

      {/* Dim backdrop — captures touches while tutorial is active */}
      <Animated.View
        style={[ov.backdrop, { opacity: fadeAnim }]}
        pointerEvents={visible ? 'auto' : 'none'}
      />

      {/* Bottom card */}
      <View style={ov.sheet} pointerEvents="box-none">
        <Animated.View
          style={[
            ov.card,
            { backgroundColor: theme.backgroundElement, transform: [{ translateY: slideAnim }] },
          ]}>

          {/* Progress dots */}
          <View style={ov.dotsRow}>
            {TUTORIAL_STEP_IDS.map((_, i) => (
              <View
                key={i}
                style={[
                  ov.dot,
                  {
                    backgroundColor: i <= idxSnap ? theme.primary : theme.background,
                    width:  i === idxSnap ? 18 : 6,
                  },
                ]}
              />
            ))}
          </View>

          {/* Step counter */}
          <Text style={[ov.counter, { color: theme.textSecondary }]}>
            Step {stepNum} of {TUTORIAL_TOTAL}
          </Text>

          {/* Icon */}
          <Text style={ov.icon}>{content.icon}</Text>

          {/* Title */}
          <Text style={[ov.title, { color: theme.text }]}>{content.title}</Text>

          {/* Body */}
          <BodyText
            text={content.body}
            style={[ov.body, { color: theme.textSecondary }]}
            boldStyle={{ color: theme.text, fontWeight: '700' }}
          />

          {/* Primary button */}
          <Pressable
            style={({ pressed }) => [
              ov.gotItBtn,
              { backgroundColor: theme.primary, opacity: pressed ? 0.82 : 1 },
            ]}
            onPress={onDismiss}
            accessibilityLabel="Got it"
            accessibilityRole="button">
            <Text style={ov.gotItText}>
              {idxSnap === TUTORIAL_TOTAL - 1 ? 'Done  ✓' : 'Got it  →'}
            </Text>
          </Pressable>

          {/* Skip link */}
          <Pressable
            onPress={onSkip}
            hitSlop={12}
            accessibilityLabel="Skip tutorial"
            accessibilityRole="button"
            style={({ pressed }) => [ov.skipBtn, { opacity: pressed ? 0.5 : 1 }]}>
            <Text style={[ov.skipText, { color: theme.textSecondary }]}>
              Skip Tutorial
            </Text>
          </Pressable>
        </Animated.View>
      </View>
    </View>
  );
}

// ── Styles ───────────────────────────────────────────────────────────────────

const ov = StyleSheet.create({
  root: {
    ...StyleSheet.absoluteFillObject,
    zIndex: 60,
  },
  backdrop: {
    ...StyleSheet.absoluteFillObject,
    backgroundColor: 'rgba(0,0,0,0.45)',
  },
  sheet: {
    ...StyleSheet.absoluteFillObject,
    justifyContent: 'flex-end',
  },
  card: {
    marginHorizontal: Spacing.three,
    marginBottom:     Spacing.four + 10,
    borderRadius:     24,
    paddingHorizontal: Spacing.four,
    paddingTop:       Spacing.three,
    paddingBottom:    Spacing.three,
    // Depth
    shadowColor:    '#000',
    shadowOffset:   { width: 0, height: 6 },
    shadowOpacity:  0.18,
    shadowRadius:   16,
    elevation:      10,
    gap:            Spacing.two,
  },
  dotsRow: {
    flexDirection:  'row',
    alignItems:     'center',
    justifyContent: 'center',
    gap:            Spacing.one,
    marginBottom:   2,
  },
  dot: {
    height:       6,
    borderRadius: 3,
  },
  counter: {
    fontSize:   12,
    fontWeight: '600',
    textAlign:  'center',
  },
  icon: {
    fontSize:  42,
    textAlign: 'center',
    marginVertical: Spacing.one,
  },
  title: {
    fontSize:   22,
    fontWeight: '800',
    textAlign:  'center',
    lineHeight: 28,
  },
  body: {
    fontSize:   15,
    lineHeight: 22,
    textAlign:  'center',
    marginTop:  Spacing.one,
  },
  gotItBtn: {
    marginTop:      Spacing.two,
    borderRadius:   14,
    paddingVertical: Spacing.three,
    alignItems:     'center',
  },
  gotItText: {
    color:      '#fff',
    fontSize:   17,
    fontWeight: '700',
  },
  skipBtn: {
    alignItems: 'center',
    paddingVertical: Spacing.one,
  },
  skipText: {
    fontSize:   13,
    fontWeight: '500',
  },
});
