"use client";

import { useEffect, useRef } from "react";

import type { MoveHistoryRow } from "@/app/gameUtils";

import styles from "./MoveHistory.module.css";

interface MoveHistoryProps {
  rows: readonly MoveHistoryRow[];
}

export default function MoveHistory({ rows }: MoveHistoryProps) {
  const scrollRef = useRef<HTMLDivElement | null>(null);
  const latestMoveKey =
    rows.length === 0
      ? "empty"
      : `${rows.at(-1)?.moveNumber ?? 0}:${rows.at(-1)?.white ?? ""}:${rows.at(-1)?.black ?? ""}`;

  useEffect(() => {
    const container = scrollRef.current;
    if (container === null) {
      return;
    }

    container.scrollTop = container.scrollHeight;
  }, [latestMoveKey]);

  return (
    <section className={styles.panel}>
      <div className={styles.header}>
        <h2 className={styles.title}>Moves</h2>
        <span className={styles.count}>{rows.length}</span>
      </div>

      <div className={styles.scrollArea} ref={scrollRef}>
        {rows.length === 0 ? (
          <p className={styles.emptyState}>No moves yet</p>
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
