"use client";

import { Chess } from "chess.js";
import { useState } from "react";

import Board from "@/components/Board/Board";

import styles from "./page.module.css";

const boardHighlights = [
  "8×8 board rendered from chess.js FEN state",
  "All 32 opening pieces shown in their correct starting squares",
  "Responsive layout sized for desktop, tablet, and smaller screens",
];

const upcomingPanels = [
  "Move history will live in this side panel next.",
  "Game controls and engine actions will fill the opposite panel.",
  "This first pass focuses on rendering only, with no piece interaction yet.",
];

export default function Home() {
  const [game] = useState(() => new Chess());
  const fen = game.fen();

  return (
    <main className={styles.page}>
      <div className={styles.layout}>
        <section className={styles.panel}>
          <p className={styles.eyebrow}>Claudefish</p>
          <h1 className={styles.heroTitle}>Board foundation ready for play.</h1>
          <p className={styles.copy}>
            The opening position is sourced from a live <code>chess.js</code> game
            instance and rendered as a square-first responsive board with white at
            the bottom.
          </p>
          <ul className={styles.statusList}>
            {boardHighlights.map((item) => (
              <li key={item}>{item}</li>
            ))}
          </ul>
        </section>

        <section className={styles.boardColumn}>
          <div className={styles.boardStage}>
            <Board fen={fen} />
          </div>
          <div className={`${styles.panel} ${styles.boardCaption}`}>
            <div>
              <span className={styles.captionLabel}>Position</span>
              <span className={styles.captionValue}>White to move · standard setup</span>
            </div>
            <div>
              <span className={styles.captionLabel}>FEN</span>
              <span className={styles.captionValue}>{fen}</span>
            </div>
          </div>
        </section>

        <section className={`${styles.panel} ${styles.placeholderPanel}`}>
          <p className={styles.eyebrow}>Next up</p>
          <h2 className={styles.placeholderTitle}>Reserved side-panel space</h2>
          <p className={styles.placeholderText}>
            The overall page layout already leaves room for move history, controls,
            and engine status without crowding the board.
          </p>
          <ul className={styles.featureList}>
            {upcomingPanels.map((item) => (
              <li key={item}>{item}</li>
            ))}
          </ul>
        </section>
      </div>
    </main>
  );
}
