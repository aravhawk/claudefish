import { Chess, type PieceSymbol, type Square } from "chess.js";

export const SEARCH_MATE_SCORE = 30000;
const MATE_DISPLAY_THRESHOLD = 29900;

export const DIFFICULTY_LEVELS = [
  {
    key: "easy",
    label: "Easy",
    depth: 2,
    timeMs: 200,
    summary: "Fast replies and lighter calculation.",
  },
  {
    key: "medium",
    label: "Medium",
    depth: 4,
    timeMs: 1000,
    summary: "Balanced strength with quick responses.",
  },
  {
    key: "hard",
    label: "Hard",
    depth: 6,
    timeMs: 3000,
    summary: "Deeper search for stronger practical play.",
  },
  {
    key: "maximum",
    label: "Maximum",
    depth: 12,
    timeMs: 5000,
    summary: "Longest think time and strongest search.",
  },
] as const;

export type DifficultyKey = (typeof DIFFICULTY_LEVELS)[number]["key"];

export interface ParsedUciMove {
  from: Square;
  to: Square;
  promotion?: PieceSymbol;
}

export interface GameResult {
  tone: "win" | "loss" | "draw";
  title: string;
  detail: string;
}

export function getDifficultyConfig(level: DifficultyKey) {
  return (
    DIFFICULTY_LEVELS.find((option) => option.key === level) ?? DIFFICULTY_LEVELS[1]
  );
}

export function parseUciMove(move: string): ParsedUciMove | null {
  if (!/^[a-h][1-8][a-h][1-8][qrbn]?$/.test(move)) {
    return null;
  }

  const parsedMove: ParsedUciMove = {
    from: move.slice(0, 2) as Square,
    to: move.slice(2, 4) as Square,
  };

  if (move.length === 5) {
    parsedMove.promotion = move.slice(4, 5) as PieceSymbol;
  }

  return parsedMove;
}

export function getPlayedPlyCount(fen: string): number {
  const fields = fen.split(" ");
  const fullmove = Number(fields[5] ?? "1");
  const sideToMove = fields[1];

  if (!Number.isFinite(fullmove) || fullmove < 1) {
    return 0;
  }

  return (fullmove - 1) * 2 + (sideToMove === "b" ? 1 : 0);
}

export function shouldEngineMove(fen: string): boolean {
  return fen.split(" ")[1] === "b";
}

export function toWhiteCentipawns(score: number, fen: string): number {
  return fen.split(" ")[1] === "b" ? -score : score;
}

export function isMateScore(score: number): boolean {
  return Math.abs(score) >= MATE_DISPLAY_THRESHOLD;
}

export function formatEvaluationLabel(score: number | null): string {
  if (score === null) {
    return "…";
  }

  if (isMateScore(score)) {
    const pliesToMate = SEARCH_MATE_SCORE - Math.abs(score);
    const movesToMate = Math.max(1, Math.ceil(pliesToMate / 2));
    return `${score < 0 ? "-" : ""}M${movesToMate}`;
  }

  return `${score >= 0 ? "+" : ""}${(score / 100).toFixed(1)}`;
}

export function getEvaluationFill(score: number | null): number {
  if (score === null) {
    return 50;
  }

  if (isMateScore(score)) {
    return score > 0 ? 100 : 0;
  }

  const boundedScore = Math.max(-1200, Math.min(1200, score));
  const normalized = Math.tanh(boundedScore / 450);

  return Math.max(0, Math.min(100, 50 + normalized * 50));
}

export function getGameResult(game: Chess): GameResult | null {
  if (game.isCheckmate()) {
    return game.turn() === "b"
      ? {
          tone: "win",
          title: "Checkmate — You win!",
          detail: "Claudefish has no legal reply.",
        }
      : {
          tone: "loss",
          title: "Checkmate — Claudefish wins.",
          detail: "Your king has been mated.",
        };
  }

  if (game.isStalemate()) {
    return {
      tone: "draw",
      title: "Stalemate — Draw",
      detail: "The side to move has no legal move, but is not in check.",
    };
  }

  if (game.isThreefoldRepetition()) {
    return {
      tone: "draw",
      title: "Threefold repetition — Draw",
      detail: "The same position repeated three times.",
    };
  }

  if (game.isInsufficientMaterial()) {
    return {
      tone: "draw",
      title: "Insufficient material — Draw",
      detail: "Neither side has enough material to force mate.",
    };
  }

  if (game.isDrawByFiftyMoves()) {
    return {
      tone: "draw",
      title: "50-move rule — Draw",
      detail: "Fifty moves passed without a pawn move or capture.",
    };
  }

  if (game.isDraw()) {
    return {
      tone: "draw",
      title: "Draw",
      detail: "The game ended in a draw.",
    };
  }

  return null;
}
