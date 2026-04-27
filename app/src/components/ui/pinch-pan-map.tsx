import React from 'react';
import { StyleProp, StyleSheet, View, ViewStyle } from 'react-native';
import { Gesture, GestureDetector } from 'react-native-gesture-handler';
import Reanimated, {
  useAnimatedStyle,
  useSharedValue,
  withSpring,
} from 'react-native-reanimated';

/**
 * Pinch-to-zoom + pan container that never creates a UIScrollView.
 *
 * `size`           — width/height of the content rendered inside.
 * `containerStyle` — style for the outer gesture-capture area.
 *                    Defaults to a `size × size` square.
 *                    Pass `{ flex: 1, alignSelf: 'stretch' }` to fill the parent.
 */
export function PinchPanMap({
  size,
  containerStyle,
  children,
}: {
  size: number;
  containerStyle?: StyleProp<ViewStyle>;
  children: React.ReactNode;
}) {
  const MIN_SCALE = 1;
  const MAX_SCALE = 3;

  const scale      = useSharedValue(1);
  const translateX = useSharedValue(0);
  const translateY = useSharedValue(0);

  const savedScale      = useSharedValue(1);
  const savedTranslateX = useSharedValue(0);
  const savedTranslateY = useSharedValue(0);

  const pinch = Gesture.Pinch()
    .onStart(() => {
      savedScale.value = scale.value;
    })
    .onUpdate((e) => {
      scale.value = Math.min(MAX_SCALE, Math.max(MIN_SCALE, savedScale.value * e.scale));
    })
    .onEnd(() => {
      if (scale.value < MIN_SCALE) scale.value = withSpring(MIN_SCALE);
      if (scale.value <= 1) {
        translateX.value = withSpring(0);
        translateY.value = withSpring(0);
      }
    });

  const pan = Gesture.Pan()
    .minPointers(1)
    .onStart(() => {
      savedTranslateX.value = translateX.value;
      savedTranslateY.value = translateY.value;
    })
    .onUpdate((e) => {
      const maxOffset = (size * (scale.value - 1)) / 2;
      translateX.value = Math.min(maxOffset, Math.max(-maxOffset, savedTranslateX.value + e.translationX));
      translateY.value = Math.min(maxOffset, Math.max(-maxOffset, savedTranslateY.value + e.translationY));
    })
    .onEnd(() => {
      if (scale.value <= 1) {
        translateX.value = withSpring(0);
        translateY.value = withSpring(0);
      }
    });

  const composed = Gesture.Simultaneous(pinch, pan);

  const animatedStyle = useAnimatedStyle(() => ({
    transform: [
      { translateX: translateX.value },
      { translateY: translateY.value },
      { scale:      scale.value },
    ],
  }));

  return (
    <GestureDetector gesture={composed}>
      <View style={[styles.defaultContainer(size), containerStyle]}>
        <Reanimated.View style={animatedStyle}>
          {children}
        </Reanimated.View>
      </View>
    </GestureDetector>
  );
}

const styles = {
  defaultContainer: (size: number): ViewStyle => ({
    width:    size,
    height:   size,
    overflow: 'hidden',
    alignItems:     'center',
    justifyContent: 'center',
  }),
};
