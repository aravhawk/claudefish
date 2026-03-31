"use client";

import { useEffect, useRef } from "react";

import type { MoveHistoryRow } from "@/app/gameUtils";

import styles from "./MoveHistory.module.css";

interface MoveHistoryProps {
  rows: readonly MoveHistoryRow[];
}

export default function MoveHistory({ rows }: MoveHistoryProps) {
  const scrollRef = useRef<HTMLDivElement | null>(null);

  useEffect(() => {
    const container = scrollRef.current;
    if (container === null) {
      return;
    }

    container.scrollTop = container.scrollHeight;
  }, [rows.length]);

  return (
    <section className={styles.panel}>
      <div className={styles.header}>
        <div>
          <p className={styles.eyebrow}>Move history</p>
          <h2 className={styles.title}>Algebraic notation</h2>
        </div>
        <span className={styles.meta}>
          {rows.length} {rows.length === 1 ? "full move" : "full moves"}
        </span>
      </div>

      <div className={styles.scrollArea} ref={scrollRef}>
        {rows.length === 0 ? (
          <p className={styles.emptyState}>Moves will appear here after the game begins.</p>
        ) : (
          <ol className={styles.list}>
            {rows.map((row) => (
              <li className={styles.row} key={row.moveNumber}>
                <span className={styles.moveNumber}>{row.moveNumber}.</span>
                <span className={styles.moveCell}>{row.white}</span>
                <span className={styles.moveCell}>{row.black ?? "—"}</span>
              </li>
            ))}
          </ol>
        )}
      </div>
    </section>
  );
}
