import { Chess, type Move, type PieceSymbol, type Square } from "chess.js";
import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
  type CSSProperties,
  type PointerEvent as ReactPointerEvent,
} from "react";

import {
  applyMoveToPieces,
  createPiecesFromFen,
  findPieceAtSquare,
  getCaptureSquare,
  getKingSquare,
  previewMoveOnPieces,
  toPieceCode,
  type PieceInstance,
} from "./boardState";
import styles from "./Board.module.css";
import {
  buildBoardSquares,
  EMPTY_BOARD_FEN,
  getPieceGlyph,
  getPieceName,
  indexesToSquare,
  squareToIndexes,
  type PieceCode,
} from "./boardUtils";

const BOARD_SQUARES = buildBoardSquares(EMPTY_BOARD_FEN);
const PROMOTION_OPTIONS: PieceSymbol[] = ["q", "r", "b", "n"];
const DRAG_THRESHOLD_PX = 6;

interface BoardMoveInput {
  from: Square;
  to: Square;
  promotion?: PieceSymbol;
}

interface BoardProps {
  fen: string;
  lastMove: Move | null;
  disabled?: boolean;
  onMove(move: BoardMoveInput): void;
}

interface Point {
  x: number;
  y: number;
}

interface DragState {
  pieceId: string;
  from: Square;
  pieceCode: PieceCode;
  wasSelected: boolean;
  pointerOffset: Point;
  originPosition: Point;
  currentPosition: Point;
  hoveredSquare: Square | null;
  squareSize: number;
  hasMoved: boolean;
}

interface ReturningDrag {
  pieceId: string;
  pieceCode: PieceCode;
  start: Point;
  target: Point;
  squareSize: number;
  animate: boolean;
}

interface PendingPromotion {
  from: Square;
  to: Square;
  color: "w" | "b";
  captureSquare: Square | null;
}

export default function Board({
  fen,
  lastMove,
  disabled = false,
  onMove,
}: BoardProps) {
  const boardRef = useRef<HTMLDivElement | null>(null);
  const previousFenRef = useRef(fen);
  const [renderPieces, setRenderPieces] = useState<PieceInstance[]>(() =>
    createPiecesFromFen(fen),
  );
  const [selectedSquare, setSelectedSquare] = useState<Square | null>(null);
  const [dragState, setDragState] = useState<DragState | null>(null);
  const [returningDrag, setReturningDrag] = useState<ReturningDrag | null>(null);
  const [pendingPromotion, setPendingPromotion] = useState<PendingPromotion | null>(null);

  const game = useMemo(() => new Chess(fen), [fen]);
  const turn = game.turn();
  const canInteract = !disabled && pendingPromotion === null;
  const selectedMoves = useMemo(
    () =>
      selectedSquare === null
        ? []
        : (game.moves({ square: selectedSquare, verbose: true }) as Move[]),
    [game, selectedSquare],
  );
  const legalTargets = useMemo(() => {
    const nextTargets = new Map<Square, Move[]>();

    for (const move of selectedMoves) {
      const currentMoves = nextTargets.get(move.to) ?? [];
      currentMoves.push(move);
      nextTargets.set(move.to, currentMoves);
    }

    return nextTargets;
  }, [selectedMoves]);
  const committedPieces = useMemo(() => createPiecesFromFen(fen), [fen]);
  const displayPieces = useMemo(() => {
    if (pendingPromotion === null) {
      return renderPieces;
    }

    return previewMoveOnPieces(renderPieces, {
      from: pendingPromotion.from,
      to: pendingPromotion.to,
      captureSquare: pendingPromotion.captureSquare,
    });
  }, [pendingPromotion, renderPieces]);
  const hiddenPieceId = dragState?.pieceId ?? returningDrag?.pieceId ?? null;
  const checkSquare = useMemo(() => {
    if (!game.isCheck()) {
      return null;
    }

    return getKingSquare(committedPieces, turn);
  }, [committedPieces, game, turn]);

  useEffect(() => {
    if (previousFenRef.current === fen) {
      return;
    }

    setRenderPieces((currentPieces) => {
      if (
        lastMove !== null &&
        lastMove.before === previousFenRef.current &&
        lastMove.after === fen
      ) {
        return applyMoveToPieces(currentPieces, lastMove);
      }

      return createPiecesFromFen(fen);
    });
    previousFenRef.current = fen;
    setSelectedSquare(null);
    setDragState(null);
    setReturningDrag(null);
    setPendingPromotion(null);
  }, [fen, lastMove]);

  useEffect(() => {
    if (returningDrag === null || returningDrag.animate) {
      return;
    }

    const frameId = window.requestAnimationFrame(() => {
      setReturningDrag((currentReturningDrag) =>
        currentReturningDrag === null
          ? null
          : {
              ...currentReturningDrag,
              animate: true,
            },
      );
    });

    return () => window.cancelAnimationFrame(frameId);
  }, [returningDrag]);

  const tryMove = useCallback(
    (from: Square, to: Square): boolean => {
      if (!canInteract) {
        return false;
      }

      const candidateMoves = (
        game.moves({ square: from, verbose: true }) as Move[]
      ).filter((move) => move.to === to);

      if (candidateMoves.length === 0) {
        return false;
      }

      const promotionMoves = candidateMoves.filter(
        (move) => move.promotion !== undefined,
      );

      if (promotionMoves.length > 0) {
        const movingPiece = findPieceAtSquare(renderPieces, from);

        setPendingPromotion({
          from,
          to,
          color: movingPiece?.color ?? turn,
          captureSquare: promotionMoves[0]?.captured
            ? getCaptureSquare(promotionMoves[0])
            : null,
        });
        setSelectedSquare(null);
        return true;
      }

      onMove({ from, to });
      setSelectedSquare(null);
      return true;
    },
    [canInteract, game, onMove, renderPieces, turn],
  );

  const handleSquarePress = useCallback(
    (square: Square) => {
      if (!canInteract) {
        return;
      }

      const occupant = findPieceAtSquare(displayPieces, square);

      if (selectedSquare === null) {
        if (occupant?.color === turn) {
          setSelectedSquare(square);
        }

        return;
      }

      if (occupant?.color === turn) {
        setSelectedSquare((currentSelectedSquare) =>
          currentSelectedSquare === square ? null : square,
        );
        return;
      }

      if (tryMove(selectedSquare, square)) {
        return;
      }

      setSelectedSquare(null);
    },
    [canInteract, displayPieces, selectedSquare, tryMove, turn],
  );

  useEffect(() => {
    if (dragState === null) {
      return undefined;
    }

    const finishDrag = (
      activeDrag: DragState,
      destinationSquare: Square | null,
    ) => {
      if (!activeDrag.hasMoved || destinationSquare === activeDrag.from) {
        setSelectedSquare(activeDrag.wasSelected ? null : activeDrag.from);
        return;
      }

      if (destinationSquare !== null && tryMove(activeDrag.from, destinationSquare)) {
        return;
      }

      const targetPoint = getSquareClientPoint(boardRef.current, activeDrag.from);
      if (targetPoint !== null) {
        setReturningDrag({
          pieceId: activeDrag.pieceId,
          pieceCode: activeDrag.pieceCode,
          start: activeDrag.currentPosition,
          target: targetPoint,
          squareSize: activeDrag.squareSize,
          animate: false,
        });
      }
    };

    const handlePointerMove = (event: PointerEvent) => {
      setDragState((currentDragState) => {
        if (currentDragState === null) {
          return null;
        }

        const nextPosition = {
          x: event.clientX - currentDragState.pointerOffset.x,
          y: event.clientY - currentDragState.pointerOffset.y,
        };
        const hoveredSquare = getSquareFromClientPoint(
          boardRef.current,
          event.clientX,
          event.clientY,
        );
        const distanceMoved = Math.hypot(
          nextPosition.x - currentDragState.originPosition.x,
          nextPosition.y - currentDragState.originPosition.y,
        );

        return {
          ...currentDragState,
          currentPosition: nextPosition,
          hoveredSquare,
          hasMoved: currentDragState.hasMoved || distanceMoved >= DRAG_THRESHOLD_PX,
        };
      });
    };

    const handlePointerUp = (event: PointerEvent) => {
      const activeDrag = dragState;
      if (activeDrag === null) {
        return;
      }

      setDragState(null);
      finishDrag(
        activeDrag,
        getSquareFromClientPoint(boardRef.current, event.clientX, event.clientY),
      );
    };

    const handlePointerCancel = () => {
      const activeDrag = dragState;
      if (activeDrag === null) {
        return;
      }

      setDragState(null);
      finishDrag(activeDrag, null);
    };

    window.addEventListener("pointermove", handlePointerMove);
    window.addEventListener("pointerup", handlePointerUp);
    window.addEventListener("pointercancel", handlePointerCancel);

    return () => {
      window.removeEventListener("pointermove", handlePointerMove);
      window.removeEventListener("pointerup", handlePointerUp);
      window.removeEventListener("pointercancel", handlePointerCancel);
    };
  }, [dragState, handleSquarePress, tryMove]);

  const handlePiecePointerDown = useCallback(
    (piece: PieceInstance, event: ReactPointerEvent<HTMLButtonElement>) => {
      if (!canInteract || piece.color !== turn) {
        return;
      }

      event.preventDefault();

      const boardElement = boardRef.current;
      if (boardElement === null) {
        return;
      }

      const boardRect = boardElement.getBoundingClientRect();
      const squareSize = boardRect.width / 8;
      const { fileIndex, rankIndex } = squareToIndexes(piece.square);
      const pieceOrigin = {
        x: boardRect.left + fileIndex * squareSize,
        y: boardRect.top + rankIndex * squareSize,
      };
      const nextDragState: DragState = {
        pieceId: piece.id,
        from: piece.square,
        pieceCode: toPieceCode(piece),
        wasSelected: selectedSquare === piece.square,
        pointerOffset: {
          x: event.clientX - pieceOrigin.x,
          y: event.clientY - pieceOrigin.y,
        },
        originPosition: pieceOrigin,
        currentPosition: pieceOrigin,
        hoveredSquare: piece.square,
        squareSize,
        hasMoved: false,
      };

      setSelectedSquare(piece.square);
      setReturningDrag(null);
      setDragState(nextDragState);
    },
    [canInteract, selectedSquare, turn],
  );

  const handlePromotionSelection = useCallback(
    (promotion: PieceSymbol) => {
      if (pendingPromotion === null) {
        return;
      }

      const nextMove = {
        from: pendingPromotion.from,
        to: pendingPromotion.to,
        promotion,
      };

      setPendingPromotion(null);
      onMove(nextMove);
      setSelectedSquare(null);
    },
    [onMove, pendingPromotion],
  );

  return (
    <div className={styles.boardShell}>
      <div className={styles.boardFrame}>
        <div
          aria-label="Interactive chess board with white pieces at the bottom"
          className={styles.board}
          ref={boardRef}
          role="grid"
        >
          {BOARD_SQUARES.map((square) => {
            const squareMoves = legalTargets.get(square.square as Square) ?? [];
            const occupant = findPieceAtSquare(displayPieces, square.square as Square);
            const isCaptureTarget =
              occupant !== null && occupant.color !== turn
                ? squareMoves.length > 0
                : squareMoves.some((move) => move.captured !== undefined);
            const isSelected = selectedSquare === square.square;
            const isLastMoveSquare =
              lastMove !== null &&
              (lastMove.from === square.square || lastMove.to === square.square);
            const isCheckSquare = checkSquare === square.square;
            const isDragTarget =
              dragState !== null &&
              dragState.hoveredSquare === square.square &&
              legalTargets.has(square.square as Square);
            const squareClasses = [
              styles.square,
              square.isLight ? styles.light : styles.dark,
              isSelected ? styles.squareSelected : "",
              isLastMoveSquare ? styles.squareLastMove : "",
              isCheckSquare ? styles.squareCheck : "",
              isDragTarget ? styles.squareDragTarget : "",
            ]
              .filter(Boolean)
              .join(" ");

            return (
              <button
                aria-label={`square ${square.square}`}
                className={squareClasses}
                disabled={!canInteract}
                key={square.square}
                onClick={() => handleSquarePress(square.square as Square)}
                type="button"
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

                {squareMoves.length > 0 ? (
                  <span
                    aria-hidden="true"
                    className={
                      isCaptureTarget ? styles.captureIndicator : styles.moveIndicator
                    }
                  />
                ) : null}
              </button>
            );
          })}

          <div className={styles.pieceLayer}>
            {displayPieces.map((piece) => {
              const { fileIndex, rankIndex } = squareToIndexes(piece.square);
              const pieceCode = toPieceCode(piece);
              const pieceClasses = [
                styles.pieceButton,
                piece.color === "w" ? styles.pieceWhite : styles.pieceBlack,
                hiddenPieceId === piece.id ? styles.pieceHidden : "",
                piece.color === turn && canInteract ? styles.pieceMovable : "",
              ]
                .filter(Boolean)
                .join(" ");
              const pieceStyle = {
                "--piece-file": `${fileIndex * 100}%`,
                "--piece-rank": `${rankIndex * 100}%`,
              } as CSSProperties;

              return (
                <button
                  aria-label={`${getPieceName(pieceCode)} on ${piece.square}`}
                  className={pieceClasses}
                  key={piece.id}
                  onPointerDown={(event) => handlePiecePointerDown(piece, event)}
                  style={pieceStyle}
                  type="button"
                >
                  <span aria-hidden="true" className={styles.pieceGlyph}>
                    {getPieceGlyph(pieceCode)}
                  </span>
                </button>
              );
            })}
          </div>

          {dragState !== null ? (
            <div
              aria-hidden="true"
              className={`${styles.dragPiece} ${styles.dragPieceActive}`}
              style={{
                width: `${dragState.squareSize}px`,
                height: `${dragState.squareSize}px`,
                transform: `translate(${dragState.currentPosition.x}px, ${dragState.currentPosition.y}px)`,
              }}
            >
              <span
                className={`${styles.pieceGlyph} ${
                  dragState.pieceCode === dragState.pieceCode.toUpperCase()
                    ? styles.pieceWhite
                    : styles.pieceBlack
                }`}
              >
                {getPieceGlyph(dragState.pieceCode)}
              </span>
            </div>
          ) : null}

          {returningDrag !== null ? (
            <div
              aria-hidden="true"
              className={styles.dragPiece}
              onTransitionEnd={() => setReturningDrag(null)}
              style={{
                width: `${returningDrag.squareSize}px`,
                height: `${returningDrag.squareSize}px`,
                transform: `translate(${
                  returningDrag.animate
                    ? returningDrag.target.x
                    : returningDrag.start.x
                }px, ${
                  returningDrag.animate
                    ? returningDrag.target.y
                    : returningDrag.start.y
                }px)`,
                transition: returningDrag.animate
                  ? "transform 240ms cubic-bezier(0.22, 1, 0.36, 1)"
                  : "none",
              }}
            >
              <span
                className={`${styles.pieceGlyph} ${
                  returningDrag.pieceCode === returningDrag.pieceCode.toUpperCase()
                    ? styles.pieceWhite
                    : styles.pieceBlack
                }`}
              >
                {getPieceGlyph(returningDrag.pieceCode)}
              </span>
            </div>
          ) : null}
        </div>
      </div>

      {pendingPromotion !== null ? (
        <div className={styles.promotionOverlay} role="presentation">
          <div
            aria-labelledby="promotion-title"
            aria-modal="true"
            className={styles.promotionDialog}
            role="dialog"
          >
            <p className={styles.promotionEyebrow}>Promotion</p>
            <h2 className={styles.promotionTitle} id="promotion-title">
              Choose a piece
            </h2>
            <div className={styles.promotionOptions}>
              {PROMOTION_OPTIONS.map((promotion) => {
                const pieceCode =
                  pendingPromotion.color === "w"
                    ? (promotion.toUpperCase() as PieceCode)
                    : (promotion as PieceCode);

                return (
                  <button
                    className={styles.promotionButton}
                    key={promotion}
                    onClick={() => handlePromotionSelection(promotion)}
                    type="button"
                  >
                    <span
                      aria-hidden="true"
                      className={`${styles.promotionGlyph} ${
                        pendingPromotion.color === "w"
                          ? styles.pieceWhite
                          : styles.pieceBlack
                      }`}
                    >
                      {getPieceGlyph(pieceCode)}
                    </span>
                    <span className={styles.promotionLabel}>
                      {getPieceName(pieceCode).replace(`${pendingPromotion.color === "w" ? "white" : "black"} `, "")}
                    </span>
                  </button>
                );
              })}
            </div>
          </div>
        </div>
      ) : null}
    </div>
  );
}

function getSquareFromClientPoint(
  boardElement: HTMLDivElement | null,
  clientX: number,
  clientY: number,
): Square | null {
  if (boardElement === null) {
    return null;
  }

  const boardRect = boardElement.getBoundingClientRect();

  if (
    clientX < boardRect.left ||
    clientX > boardRect.right ||
    clientY < boardRect.top ||
    clientY > boardRect.bottom
  ) {
    return null;
  }

  const squareSize = boardRect.width / 8;
  const fileIndex = Math.min(
    7,
    Math.max(0, Math.floor((clientX - boardRect.left) / squareSize)),
  );
  const rankIndex = Math.min(
    7,
    Math.max(0, Math.floor((clientY - boardRect.top) / squareSize)),
  );

  return indexesToSquare(fileIndex, rankIndex);
}

function getSquareClientPoint(
  boardElement: HTMLDivElement | null,
  square: Square,
): Point | null {
  if (boardElement === null) {
    return null;
  }

  const boardRect = boardElement.getBoundingClientRect();
  const squareSize = boardRect.width / 8;
  const { fileIndex, rankIndex } = squareToIndexes(square);

  return {
    x: boardRect.left + fileIndex * squareSize,
    y: boardRect.top + rankIndex * squareSize,
  };
}
