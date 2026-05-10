import type { CSSProperties } from "react";

export type BoardThemeKey = "classic-wood" | "dark-marble" | "light-minimalist";

export interface BoardTheme {
  key: BoardThemeKey;
  label: string;
  colorScheme: "light" | "dark";
  colors: {
    squareLight: string;
    squareDark: string;
    panel: string;
    accent: string;
  };
}

type BoardThemeStyle = CSSProperties & {
  "--theme-square-light": string;
  "--theme-square-dark": string;
  "--theme-panel": string;
  "--theme-accent": string;
};

export const BOARD_THEMES: BoardTheme[] = [
  {
    key: "classic-wood",
    label: "Classic Wood",
    colorScheme: "light",
    colors: {
      squareLight: "#efd9b5",
      squareDark: "#a66a43",
      panel: "rgba(255, 250, 240, 0.86)",
      accent: "#8f4f2f",
    },
  },
  {
    key: "dark-marble",
    label: "Dark Marble",
    colorScheme: "dark",
    colors: {
      squareLight: "#9aa4b2",
      squareDark: "#3d4654",
      panel: "rgba(12, 18, 28, 0.84)",
      accent: "#8bd3ff",
    },
  },
  {
    key: "light-minimalist",
    label: "Light Minimalist",
    colorScheme: "light",
    colors: {
      squareLight: "#f4f1ea",
      squareDark: "#7d8f8a",
      panel: "rgba(255, 255, 255, 0.88)",
      accent: "#536d63",
    },
  },
];

export function getBoardTheme(key: BoardThemeKey): BoardTheme {
  return BOARD_THEMES.find((theme) => theme.key === key) ?? BOARD_THEMES[0];
}

export function getBoardThemeStyle(theme: BoardTheme): BoardThemeStyle {
  return {
    "--theme-square-light": theme.colors.squareLight,
    "--theme-square-dark": theme.colors.squareDark,
    "--theme-panel": theme.colors.panel,
    "--theme-accent": theme.colors.accent,
    colorScheme: theme.colorScheme,
  };
}
