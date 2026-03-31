import type { GameResult } from "@/app/gameUtils";

import styles from "./GameOverOverlay.module.css";

interface GameOverOverlayProps {
  result: GameResult;
  onNewGame(): void;
}

export default function GameOverOverlay({
  result,
  onNewGame,
}: GameOverOverlayProps) {
  return (
    <div aria-live="polite" className={styles.overlay}>
      <div
        className={`${styles.card} ${
          result.tone === "win"
            ? styles.cardWin
            : result.tone === "loss"
              ? styles.cardLoss
              : styles.cardDraw
        }`}
        role="status"
      >
        <p className={styles.eyebrow}>Game over</p>
        <h2 className={styles.title}>{result.title}</h2>
        <p className={styles.detail}>{result.detail}</p>
        <button className={styles.actionButton} onClick={onNewGame} type="button">
          New Game
        </button>
      </div>
    </div>
  );
}
