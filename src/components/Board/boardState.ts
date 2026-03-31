import type { Color, Move, PieceSymbol, Square } from "chess.js";

import { indexesToSquare, parseFenPlacement, type PieceCode } from "./boardUtils.ts";

export interface PieceInstance {
  id: string;
  color: Color;
  type: PieceSymbol;
  square: Square;
}

interface PieceTransition {
  from: Square;
  to: Square;
  captureSquare?: Square | null;
  rookMove?: {
    from: Square;
    to: Square;
  };
  nextType?: PieceSymbol;
}

export function createPiecesFromFen(fen: string): PieceInstance[] {
  const board = parseFenPlacement(fen);

  return board.flatMap((rank, rankIndex) =>
    rank.flatMap((piece, fileIndex) => {
      if (piece === null) {
        return [];
      }

      const square = indexesToSquare(fileIndex, rankIndex);
      const color: Color = piece === piece.toUpperCase() ? "w" : "b";
      const type = piece.toLowerCase() as PieceSymbol;

      return [
        {
          id: `${piece}-${square}`,
          color,
          type,
          square,
        },
      ];
    }),
  );
}

export function findPieceAtSquare(
  pieces: readonly PieceInstance[],
  square: Square,
): PieceInstance | null {
  return pieces.find((piece) => piece.square === square) ?? null;
}

export function toPieceCode(piece: PieceInstance): PieceCode {
  return (piece.color === "w" ? piece.type.toUpperCase() : piece.type) as PieceCode;
}

export function getKingSquare(
  pieces: readonly PieceInstance[],
  color: Color,
): Square | null {
  return pieces.find((piece) => piece.color === color && piece.type === "k")?.square ?? null;
}

export function getCaptureSquare(move: Move): Square {
  if (move.flags.includes("e")) {
    return `${move.to[0]}${move.from[1]}` as Square;
  }

  return move.to;
}

export function applyMoveToPieces(
  pieces: readonly PieceInstance[],
  move: Move,
): PieceInstance[] {
  return applyPieceTransition(pieces, {
    from: move.from,
    to: move.to,
    captureSquare: move.captured ? getCaptureSquare(move) : null,
    rookMove: getCastlingRookMove(move),
    nextType: move.promotion,
  });
}

export function previewMoveOnPieces(
  pieces: readonly PieceInstance[],
  transition: PieceTransition,
): PieceInstance[] {
  return applyPieceTransition(pieces, transition);
}

function applyPieceTransition(
  pieces: readonly PieceInstance[],
  transition: PieceTransition,
): PieceInstance[] {
  const movingPiece = findPieceAtSquare(pieces, transition.from);

  if (movingPiece === null) {
    return [...pieces];
  }

  return pieces.flatMap((piece) => {
    if (
      transition.captureSquare !== null &&
      transition.captureSquare !== undefined &&
      piece.id !== movingPiece.id &&
      piece.square === transition.captureSquare
    ) {
      return [];
    }

    if (piece.id === movingPiece.id) {
      return [
        {
          ...piece,
          square: transition.to,
          type: transition.nextType ?? piece.type,
        },
      ];
    }

    if (
      transition.rookMove !== undefined &&
      piece.square === transition.rookMove.from &&
      piece.color === movingPiece.color &&
      piece.type === "r"
    ) {
      return [
        {
          ...piece,
          square: transition.rookMove.to,
        },
      ];
    }

    return [piece];
  });
}

function getCastlingRookMove(
  move: Move,
): PieceTransition["rookMove"] | undefined {
  if (!move.flags.includes("k") && !move.flags.includes("q")) {
    return undefined;
  }

  switch (move.to) {
    case "g1":
      return { from: "h1", to: "f1" };
    case "c1":
      return { from: "a1", to: "d1" };
    case "g8":
      return { from: "h8", to: "f8" };
    case "c8":
      return { from: "a8", to: "d8" };
    default:
      return undefined;
  }
}
