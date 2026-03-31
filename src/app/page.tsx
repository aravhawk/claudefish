"use client";

import { Chess, type Move, type PieceSymbol, type Square } from "chess.js";
import { useMemo, useState } from "react";

import Board from "@/components/Board/Board";

import styles from "./page.module.css";

const interactionHighlights = [
  "Drag any side-to-move piece and drop it onto a legal square.",
  "Click a piece to select it, then click a highlighted target square to move.",
  "Promotion, castling, en passant, check, and last-move highlights are all live.",
];

interface GameSnapshot {
  fen: string;
  lastMove: Move | null;
}

export default function Home() {
  const [gameSnapshot, setGameSnapshot] = useState<GameSnapshot>(() => {
    const game = new Chess();

    return {
      fen: game.fen(),
      lastMove: null,
    };
  });

  const game = useMemo(() => new Chess(gameSnapshot.fen), [gameSnapshot.fen]);
  const turnLabel = game.turn() === "w" ? "White" : "Black";
  const statusLabel = game.isCheck()
    ? `${turnLabel} to move — in check`
    : `${turnLabel} to move`;
  const moveCount = getPlayedPlyCount(gameSnapshot.fen);

  const handleMove = ({
    from,
    to,
    promotion,
  }: {
    from: Square;
    to: Square;
    promotion?: PieceSymbol;
  }) => {
    const nextGame = new Chess(gameSnapshot.fen);
    const move = nextGame.move({
      from,
      to,
      ...(promotion ? { promotion } : {}),
    });

    if (move === null) {
      return;
    }

    setGameSnapshot({
      fen: nextGame.fen(),
      lastMove: move,
    });
  };

  return (
    <main className={styles.page}>
      <div className={styles.layout}>
        <section className={styles.panel}>
          <p className={styles.eyebrow}>Claudefish</p>
          <h1 className={styles.heroTitle}>Board interaction is live.</h1>
          <p className={styles.copy}>
            The board now supports full local play: drag-and-drop, click-to-move,
            legal move indicators, last-move and check highlights, and promotion
            selection powered by <code>chess.js</code>.
          </p>
          <ul className={styles.statusList}>
            {interactionHighlights.map((item) => (
              <li key={item}>{item}</li>
            ))}
          </ul>
        </section>

        <section className={styles.boardColumn}>
          <div className={styles.boardStage}>
            <Board
              fen={gameSnapshot.fen}
              lastMove={gameSnapshot.lastMove}
              onMove={handleMove}
            />
          </div>
          <div className={`${styles.panel} ${styles.boardCaption}`}>
            <div>
              <span className={styles.captionLabel}>Status</span>
              <span className={styles.captionValue}>{statusLabel}</span>
            </div>
            <div>
              <span className={styles.captionLabel}>Last move</span>
              <span className={styles.captionValue}>
                {gameSnapshot.lastMove?.san ?? "None yet"}
              </span>
            </div>
            <div>
              <span className={styles.captionLabel}>Moves played</span>
              <span className={styles.captionValue}>{moveCount}</span>
            </div>
            <div className={styles.captionWide}>
              <span className={styles.captionLabel}>FEN</span>
              <span className={styles.captionValue}>{gameSnapshot.fen}</span>
            </div>
          </div>
        </section>

        <section className={`${styles.panel} ${styles.placeholderPanel}`}>
          <p className={styles.eyebrow}>Interaction notes</p>
          <h2 className={styles.placeholderTitle}>Everything stays legal.</h2>
          <p className={styles.placeholderText}>
            Only the side to move can interact, pinned pieces expose only their
            legal squares, and illegal drags animate back to their origin.
          </p>
          <dl className={styles.detailList}>
            <div>
              <dt>Drag feedback</dt>
              <dd>Pieces lift with a shadow, track the cursor, and animate into place.</dd>
            </div>
            <div>
              <dt>Promotion flow</dt>
              <dd>The board locks until you pick Q, R, B, or N in the modal dialog.</dd>
            </div>
            <div>
              <dt>Special moves</dt>
              <dd>Castling animates both pieces, and en passant removes the correct pawn.</dd>
            </div>
          </dl>
        </section>
      </div>
    </main>
  );
}

function getPlayedPlyCount(fen: string): number {
  const fields = fen.split(" ");
  const fullmove = Number(fields[5] ?? "1");
  const sideToMove = fields[1];

  if (!Number.isFinite(fullmove) || fullmove < 1) {
    return 0;
  }

  return (fullmove - 1) * 2 + (sideToMove === "b" ? 1 : 0);
}
