const FILES = ["a", "b", "c", "d", "e", "f", "g", "h"] as const;
const RANKS = ["8", "7", "6", "5", "4", "3", "2", "1"] as const;

export type PieceCode =
  | "P"
  | "N"
  | "B"
  | "R"
  | "Q"
  | "K"
  | "p"
  | "n"
  | "b"
  | "r"
  | "q"
  | "k";

export interface BoardSquare {
  square: string;
  piece: PieceCode | null;
  isLight: boolean;
  fileLabel?: string;
  rankLabel?: string;
}

export const PIECE_GLYPHS: Record<PieceCode, string> = {
  P: "♙",
  N: "♘",
  B: "♗",
  R: "♖",
  Q: "♕",
  K: "♔",
  p: "♟",
  n: "♞",
  b: "♝",
  r: "♜",
  q: "♛",
  k: "♚",
};

const PIECE_NAMES: Record<PieceCode, string> = {
  P: "white pawn",
  N: "white knight",
  B: "white bishop",
  R: "white rook",
  Q: "white queen",
  K: "white king",
  p: "black pawn",
  n: "black knight",
  b: "black bishop",
  r: "black rook",
  q: "black queen",
  k: "black king",
};

export function getPieceGlyph(piece: PieceCode): string {
  return PIECE_GLYPHS[piece];
}

export function getPieceName(piece: PieceCode): string {
  return PIECE_NAMES[piece];
}

function expandFenRow(row: string): Array<PieceCode | null> {
  const squares: Array<PieceCode | null> = [];

  for (const symbol of row) {
    if (/\d/.test(symbol)) {
      const emptySquares = Number(symbol);
      for (let count = 0; count < emptySquares; count += 1) {
        squares.push(null);
      }
      continue;
    }

    squares.push(symbol as PieceCode);
  }

  if (squares.length !== 8) {
    throw new Error(`Invalid FEN row "${row}" did not expand to 8 files.`);
  }

  return squares;
}

export function parseFenPlacement(fen: string): Array<Array<PieceCode | null>> {
  const [placement] = fen.trim().split(/\s+/);
  const rows = placement.split("/");

  if (rows.length !== 8) {
    throw new Error("Invalid FEN placement must contain 8 ranks.");
  }

  return rows.map(expandFenRow);
}

export function buildBoardSquares(fen: string): BoardSquare[] {
  const board = parseFenPlacement(fen);

  return board.flatMap((rank, rankIndex) =>
    rank.map((piece, fileIndex) => ({
      square: `${FILES[fileIndex]}${RANKS[rankIndex]}`,
      piece,
      isLight: (fileIndex + rankIndex) % 2 === 0,
      fileLabel: rankIndex === RANKS.length - 1 ? FILES[fileIndex] : undefined,
      rankLabel: fileIndex === 0 ? RANKS[rankIndex] : undefined,
    })),
  );
}
