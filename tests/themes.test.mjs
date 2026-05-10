import assert from "node:assert/strict";
import test from "node:test";

const themes = await import("../src/app/themes.ts");

const {
  BOARD_THEMES,
  getBoardTheme,
  getBoardThemeStyle,
} = themes.default ?? themes;

test("board themes expose the three required premium presets", () => {
  assert.deepEqual(
    BOARD_THEMES.map(({ key, label }) => ({ key, label })),
    [
      { key: "classic-wood", label: "Classic Wood" },
      { key: "dark-marble", label: "Dark Marble" },
      { key: "light-minimalist", label: "Light Minimalist" },
    ],
  );
});

test("board theme palettes keep the square colors visually distinct", () => {
  const palettePairs = BOARD_THEMES.map((theme) => [
    theme.colors.squareLight,
    theme.colors.squareDark,
  ]);

  assert.equal(new Set(palettePairs.flat()).size, palettePairs.length * 2);
});

test("theme style mapping exposes the board and panel CSS variables", () => {
  const darkMarbleStyle = getBoardThemeStyle(getBoardTheme("dark-marble"));

  assert.equal(darkMarbleStyle["--theme-square-dark"], "#3d4654");
  assert.equal(darkMarbleStyle["--theme-panel"], "rgba(12, 18, 28, 0.84)");
  assert.equal(darkMarbleStyle.colorScheme, "dark");
});
