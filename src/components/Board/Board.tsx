import styles from "./Board.module.css";
import { buildBoardSquares, getPieceGlyph, getPieceName } from "./boardUtils";

interface BoardProps {
  fen: string;
}

export default function Board({ fen }: BoardProps) {
  const squares = buildBoardSquares(fen);

  return (
    <div className={styles.boardShell}>
      <div className={styles.boardFrame}>
        <div
          aria-label="Chess board with white pieces at the bottom"
          className={styles.board}
          role="grid"
        >
          {squares.map((square) => {
            const squareColorClass = square.isLight ? styles.light : styles.dark;
            const pieceColorClass =
              square.piece === null
                ? ""
                : square.piece === square.piece.toUpperCase()
                  ? styles.pieceWhite
                  : styles.pieceBlack;
            const pieceLabel =
              square.piece === null
                ? `empty square ${square.square}`
                : `${getPieceName(square.piece)} on ${square.square}`;

            return (
              <div
                aria-label={pieceLabel}
                className={`${styles.square} ${squareColorClass}`}
                key={square.square}
                role="gridcell"
              >
                {square.rankLabel ? (
                  <span aria-hidden="true" className={styles.rankLabel}>
                    {square.rankLabel}
                  </span>
                ) : null}

                {square.fileLabel ? (
                  <span aria-hidden="true" className={styles.fileLabel}>
                    {square.fileLabel}
                  </span>
                ) : null}

                {square.piece ? (
                  <span
                    aria-hidden="true"
                    className={`${styles.piece} ${pieceColorClass}`}
                  >
                    {getPieceGlyph(square.piece)}
                  </span>
                ) : null}
              </div>
            );
          })}
        </div>
      </div>
    </div>
  );
}
