import type { CSSProperties } from "react";

export type ThemeKey = "classic-wood" | "dark-marble" | "light-minimalist";

interface ThemeColors {
  background: string;
  backgroundSecondary: string;
  backgroundAccent: string;
  panel: string;
  panelStrong: string;
  panelMuted: string;
  panelBorder: string;
  text: string;
  textMuted: string;
  textSoft: string;
  heading: string;
  accent: string;
  accentStrong: string;
  accentSoft: string;
  buttonStart: string;
  buttonEnd: string;
  buttonBorder: string;
  buttonText: string;
  buttonHover: string;
  buttonShadow: string;
  boardFrame: string;
  boardFrameEdge: string;
  boardInset: string;
  boardShadow: string;
  squareLight: string;
  squareDark: string;
  squareHighlight: string;
  squareSelected: string;
  squareCheck: string;
  squareTarget: string;
  squareIndicator: string;
  squareIndicatorRing: string;
  labelLight: string;
  labelDark: string;
  pieceWhite: string;
  pieceWhiteShadow: string;
  pieceBlack: string;
  pieceBlackShadow: string;
  evalDark: string;
  evalLight: string;
  evalDivider: string;
  overlay: string;
  spinnerTrack: string;
  spinnerHead: string;
  scrollbarThumb: string;
  chip: string;
}

export interface BoardTheme {
  key: ThemeKey;
  label: string;
  mood: string;
  description: string;
  loadingLabel: string;
  colorScheme: "dark" | "light";
  colors: ThemeColors;
}

type ThemeStyle = CSSProperties & Record<`--${string}`, string>;

export const BOARD_THEMES = [
  {
    key: "classic-wood",
    label: "Classic Wood",
    mood: "Warm, club-room craftsmanship",
    description:
      "Honeyed light squares, walnut dark squares, and amber-lit panels inspired by handcrafted sets.",
    loadingLabel: "Seasoning the walnut board",
    colorScheme: "dark",
    colors: {
      background: "#140d08",
      backgroundSecondary: "#2a1a0f",
      backgroundAccent: "rgba(238, 190, 120, 0.18)",
      panel: "rgba(35, 23, 14, 0.84)",
      panelStrong: "rgba(53, 33, 19, 0.94)",
      panelMuted: "rgba(61, 39, 21, 0.72)",
      panelBorder: "rgba(241, 206, 154, 0.18)",
      text: "#f7efe2",
      textMuted: "#ddcbb5",
      textSoft: "#b8966f",
      heading: "#fff7ea",
      accent: "#d7a160",
      accentStrong: "#f0c788",
      accentSoft: "rgba(215, 161, 96, 0.18)",
      buttonStart: "#8d6032",
      buttonEnd: "#4e2f18",
      buttonBorder: "rgba(244, 214, 165, 0.26)",
      buttonText: "#fff8ed",
      buttonHover: "#f0c788",
      buttonShadow: "rgba(25, 13, 5, 0.42)",
      boardFrame: "#4b301b",
      boardFrameEdge: "rgba(255, 236, 202, 0.14)",
      boardInset: "rgba(114, 68, 31, 0.78)",
      boardShadow: "rgba(6, 2, 0, 0.58)",
      squareLight: "#e7c48f",
      squareDark: "#8f5a2f",
      squareHighlight: "rgba(247, 196, 88, 0.28)",
      squareSelected: "rgba(255, 239, 200, 0.26)",
      squareCheck: "rgba(180, 43, 43, 0.34)",
      squareTarget: "rgba(108, 180, 142, 0.34)",
      squareIndicator: "rgba(63, 37, 15, 0.28)",
      squareIndicatorRing: "rgba(61, 35, 13, 0.48)",
      labelLight: "rgba(109, 62, 22, 0.88)",
      labelDark: "rgba(255, 244, 221, 0.94)",
      pieceWhite: "#fff7ea",
      pieceWhiteShadow: "rgba(98, 58, 24, 0.46)",
      pieceBlack: "#2d1b0f",
      pieceBlackShadow: "rgba(255, 238, 209, 0.28)",
      evalDark: "#2b1a0e",
      evalLight: "#fff3e1",
      evalDivider: "#f0c788",
      overlay: "rgba(14, 8, 4, 0.78)",
      spinnerTrack: "rgba(255, 229, 193, 0.24)",
      spinnerHead: "#f0c788",
      scrollbarThumb: "rgba(221, 177, 108, 0.36)",
      chip: "rgba(75, 46, 22, 0.74)",
    },
  },
  {
    key: "dark-marble",
    label: "Dark Marble",
    mood: "Luxurious slate and stone",
    description:
      "Polished charcoal surfaces, muted cream squares, and silver-blue highlights for a dramatic premium look.",
    loadingLabel: "Polishing the marble table",
    colorScheme: "dark",
    colors: {
      background: "#060b12",
      backgroundSecondary: "#111a27",
      backgroundAccent: "rgba(168, 190, 204, 0.14)",
      panel: "rgba(12, 18, 28, 0.84)",
      panelStrong: "rgba(19, 27, 39, 0.95)",
      panelMuted: "rgba(28, 36, 49, 0.78)",
      panelBorder: "rgba(201, 215, 225, 0.16)",
      text: "#f3f6f8",
      textMuted: "#d2d9df",
      textSoft: "#9aa7b6",
      heading: "#fcfcfd",
      accent: "#9ec0cf",
      accentStrong: "#d6e3ea",
      accentSoft: "rgba(158, 192, 207, 0.16)",
      buttonStart: "#233344",
      buttonEnd: "#0f1722",
      buttonBorder: "rgba(209, 223, 234, 0.18)",
      buttonText: "#f4f7f9",
      buttonHover: "#d6e3ea",
      buttonShadow: "rgba(0, 0, 0, 0.45)",
      boardFrame: "#121926",
      boardFrameEdge: "rgba(255, 255, 255, 0.08)",
      boardInset: "rgba(48, 57, 71, 0.8)",
      boardShadow: "rgba(0, 0, 0, 0.6)",
      squareLight: "#d5d0c3",
      squareDark: "#3d4654",
      squareHighlight: "rgba(158, 192, 207, 0.28)",
      squareSelected: "rgba(235, 240, 244, 0.18)",
      squareCheck: "rgba(190, 67, 67, 0.32)",
      squareTarget: "rgba(87, 156, 139, 0.34)",
      squareIndicator: "rgba(7, 12, 20, 0.3)",
      squareIndicatorRing: "rgba(230, 236, 243, 0.34)",
      labelLight: "rgba(84, 89, 95, 0.9)",
      labelDark: "rgba(238, 241, 244, 0.94)",
      pieceWhite: "#fbfaf7",
      pieceWhiteShadow: "rgba(67, 74, 83, 0.5)",
      pieceBlack: "#111720",
      pieceBlackShadow: "rgba(239, 244, 248, 0.22)",
      evalDark: "#0c1118",
      evalLight: "#ece7de",
      evalDivider: "#d6e3ea",
      overlay: "rgba(6, 10, 16, 0.82)",
      spinnerTrack: "rgba(214, 227, 234, 0.2)",
      spinnerHead: "#d6e3ea",
      scrollbarThumb: "rgba(166, 185, 197, 0.34)",
      chip: "rgba(16, 23, 35, 0.8)",
    },
  },
  {
    key: "light-minimalist",
    label: "Light Minimalist",
    mood: "Airy, gallery-clean surfaces",
    description:
      "Near-white squares, soft slate greys, and bright editorial spacing for a crisp modern presentation.",
    loadingLabel: "Setting the gallery board",
    colorScheme: "light",
    colors: {
      background: "#f4efe7",
      backgroundSecondary: "#eaf0f5",
      backgroundAccent: "rgba(70, 110, 139, 0.12)",
      panel: "rgba(255, 255, 255, 0.88)",
      panelStrong: "rgba(255, 255, 255, 0.96)",
      panelMuted: "rgba(241, 245, 248, 0.9)",
      panelBorder: "rgba(102, 121, 140, 0.16)",
      text: "#15212d",
      textMuted: "#334155",
      textSoft: "#64748b",
      heading: "#0f1c28",
      accent: "#4c7695",
      accentStrong: "#29536b",
      accentSoft: "rgba(76, 118, 149, 0.14)",
      buttonStart: "#ffffff",
      buttonEnd: "#edf3f7",
      buttonBorder: "rgba(96, 117, 140, 0.22)",
      buttonText: "#183141",
      buttonHover: "#29536b",
      buttonShadow: "rgba(146, 160, 177, 0.24)",
      boardFrame: "#ffffff",
      boardFrameEdge: "rgba(255, 255, 255, 0.9)",
      boardInset: "rgba(230, 236, 241, 0.96)",
      boardShadow: "rgba(124, 138, 156, 0.28)",
      squareLight: "#faf8f4",
      squareDark: "#ced7df",
      squareHighlight: "rgba(76, 118, 149, 0.22)",
      squareSelected: "rgba(76, 118, 149, 0.18)",
      squareCheck: "rgba(215, 77, 77, 0.24)",
      squareTarget: "rgba(113, 162, 141, 0.24)",
      squareIndicator: "rgba(37, 82, 110, 0.18)",
      squareIndicatorRing: "rgba(37, 82, 110, 0.28)",
      labelLight: "rgba(122, 133, 142, 0.9)",
      labelDark: "rgba(34, 45, 55, 0.88)",
      pieceWhite: "#ffffff",
      pieceWhiteShadow: "rgba(150, 161, 170, 0.42)",
      pieceBlack: "#22313f",
      pieceBlackShadow: "rgba(250, 250, 250, 0.66)",
      evalDark: "#22313f",
      evalLight: "#ffffff",
      evalDivider: "#29536b",
      overlay: "rgba(244, 239, 231, 0.84)",
      spinnerTrack: "rgba(41, 83, 107, 0.16)",
      spinnerHead: "#29536b",
      scrollbarThumb: "rgba(106, 129, 152, 0.3)",
      chip: "rgba(241, 245, 248, 0.96)",
    },
  },
] as const satisfies readonly BoardTheme[];

export function getBoardTheme(themeKey: ThemeKey): BoardTheme {
  return (
    BOARD_THEMES.find((theme) => theme.key === themeKey) ??
    BOARD_THEMES[0]
  );
}

export function getBoardThemeStyle(theme: BoardTheme): ThemeStyle {
  return {
    colorScheme: theme.colorScheme,
    "--theme-background": theme.colors.background,
    "--theme-background-secondary": theme.colors.backgroundSecondary,
    "--theme-background-accent": theme.colors.backgroundAccent,
    "--theme-panel": theme.colors.panel,
    "--theme-panel-strong": theme.colors.panelStrong,
    "--theme-panel-muted": theme.colors.panelMuted,
    "--theme-panel-border": theme.colors.panelBorder,
    "--theme-text": theme.colors.text,
    "--theme-text-muted": theme.colors.textMuted,
    "--theme-text-soft": theme.colors.textSoft,
    "--theme-heading": theme.colors.heading,
    "--theme-accent": theme.colors.accent,
    "--theme-accent-strong": theme.colors.accentStrong,
    "--theme-accent-soft": theme.colors.accentSoft,
    "--theme-button-start": theme.colors.buttonStart,
    "--theme-button-end": theme.colors.buttonEnd,
    "--theme-button-border": theme.colors.buttonBorder,
    "--theme-button-text": theme.colors.buttonText,
    "--theme-button-hover": theme.colors.buttonHover,
    "--theme-button-shadow": theme.colors.buttonShadow,
    "--theme-board-frame": theme.colors.boardFrame,
    "--theme-board-frame-edge": theme.colors.boardFrameEdge,
    "--theme-board-inset": theme.colors.boardInset,
    "--theme-board-shadow": theme.colors.boardShadow,
    "--theme-square-light": theme.colors.squareLight,
    "--theme-square-dark": theme.colors.squareDark,
    "--theme-square-highlight": theme.colors.squareHighlight,
    "--theme-square-selected": theme.colors.squareSelected,
    "--theme-square-check": theme.colors.squareCheck,
    "--theme-square-target": theme.colors.squareTarget,
    "--theme-square-indicator": theme.colors.squareIndicator,
    "--theme-square-indicator-ring": theme.colors.squareIndicatorRing,
    "--theme-label-light": theme.colors.labelLight,
    "--theme-label-dark": theme.colors.labelDark,
    "--theme-piece-white": theme.colors.pieceWhite,
    "--theme-piece-white-shadow": theme.colors.pieceWhiteShadow,
    "--theme-piece-black": theme.colors.pieceBlack,
    "--theme-piece-black-shadow": theme.colors.pieceBlackShadow,
    "--theme-eval-dark": theme.colors.evalDark,
    "--theme-eval-light": theme.colors.evalLight,
    "--theme-eval-divider": theme.colors.evalDivider,
    "--theme-overlay": theme.colors.overlay,
    "--theme-spinner-track": theme.colors.spinnerTrack,
    "--theme-spinner-head": theme.colors.spinnerHead,
    "--theme-scrollbar-thumb": theme.colors.scrollbarThumb,
    "--theme-chip": theme.colors.chip,
  } satisfies ThemeStyle;
}
