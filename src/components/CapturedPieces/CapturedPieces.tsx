import type { CapturedPiecesByColor } from "@/app/gameUtils";
import { getPieceGlyph, getPieceName, type PieceCode } from "@/components/Board/boardUtils";

import styles from "./CapturedPieces.module.css";

interface CapturedPiecesProps {
  captures: CapturedPiecesByColor;
}

export default function CapturedPieces({ captures }: CapturedPiecesProps) {
  return (
    <section className={styles.panel}>
      <div>
        <p className={styles.eyebrow}>Captured pieces</p>
        <h2 className={styles.title}>Material taken</h2>
      </div>

      <div className={styles.groups}>
        <CaptureGroup
          title="White's captures"
          subtitle="Black pieces taken by White"
          pieces={captures.white}
        />
        <CaptureGroup
          title="Black's captures"
          subtitle="White pieces taken by Black"
          pieces={captures.black}
        />
      </div>
    </section>
  );
}

function CaptureGroup({
  title,
  subtitle,
  pieces,
}: {
  title: string;
  subtitle: string;
  pieces: readonly PieceCode[];
}) {
  return (
    <div className={styles.group}>
      <div>
        <h3 className={styles.groupTitle}>{title}</h3>
        <p className={styles.groupSubtitle}>{subtitle}</p>
      </div>

      <div className={styles.pieceRow}>
        {pieces.length === 0 ? (
          <span className={styles.emptyState}>None yet</span>
        ) : (
          pieces.map((piece, index) => (
            <span
              aria-label={getPieceName(piece)}
              className={styles.captureChip}
              key={`${piece}-${index}`}
              title={getPieceName(piece)}
            >
              <span
                aria-hidden="true"
                className={`${styles.pieceGlyph} ${
                  piece === piece.toUpperCase() ? styles.whitePiece : styles.blackPiece
                }`}
              >
                {getPieceGlyph(piece)}
              </span>
            </span>
          ))
        )}
      </div>
    </div>
  );
}
