import { Chess, type Move, type PieceSymbol, type Square } from "chess.js";

import type { PieceCode } from "@/components/Board/boardUtils";

export const SEARCH_MATE_SCORE = 30000;
const MATE_DISPLAY_THRESHOLD = 29900;

/* ELO-based difficulty system. The slider ranges from 800 to 3200 ELO.
   ELO maps to search depth and time via interpolation. */

export const ELO_MIN = 800;
export const ELO_MAX = 3200;
export const ELO_DEFAULT = 1500;

export interface EloDifficultyConfig {
  elo: number;
  depth: number;
  timeMs: number;
  label: string;
}

export function getEloConfig(elo: number): EloDifficultyConfig {
  const clamped = Math.max(ELO_MIN, Math.min(ELO_MAX, elo));
  const t = (clamped - ELO_MIN) / (ELO_MAX - ELO_MIN); /* 0..1 */

  /* Depth: 1 at 800 ELO -> 16 at 3200 ELO */
  const depth = Math.round(1 + t * t * 15 + t * 4);

  /* Time: 50ms at 800 ELO -> 8000ms at 3200 ELO */
  const timeMs = Math.round(50 + t * t * 7950);

  /* Label: rounded ELO with descriptive tier */
  let label: string;
  if (clamped < 1000) label = `ELO ${clamped} · Beginner`;
  else if (clamped < 1400) label = `ELO ${clamped} · Casual`;
  else if (clamped < 1800) label = `ELO ${clamped} · Club`;
  else if (clamped < 2200) label = `ELO ${clamped} · Expert`;
  else if (clamped < 2600) label = `ELO ${clamped} · Master`;
  else label = `ELO ${clamped} · Grandmaster`;

  return { elo: clamped, depth, timeMs, label };
}

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

export interface MoveHistoryRow {
  moveNumber: number;
  white: string;
  black: string | null;
}

export interface CapturedPiecesByColor {
  white: PieceCode[];
  black: PieceCode[];
}

export const STARTING_POSITION_FEN = new Chess().fen();

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

export function replayGame(
  moves: readonly ParsedUciMove[],
  initialFen = STARTING_POSITION_FEN,
): Chess {
  const game = new Chess(initialFen);

  for (const move of moves) {
    game.move(move);
  }

  return game;
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

export function shouldShowLoadingOverlay({
  engineError,
  isReady,
  loadingDismissed,
}: {
  engineError: string | null;
  isReady: boolean;
  loadingDismissed: boolean;
}): boolean {
  return engineError === null && (!isReady || !loadingDismissed);
}

export function getUndoPlyCount(fen: string, historyLength: number): number {
  if (historyLength === 0) {
    return 0;
  }

  return shouldEngineMove(fen) ? 1 : Math.min(2, historyLength);
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

export function buildMoveHistoryRows(moves: readonly Pick<Move, "san">[]): MoveHistoryRow[] {
  const rows: MoveHistoryRow[] = [];

  for (let index = 0; index < moves.length; index += 2) {
    rows.push({
      moveNumber: Math.floor(index / 2) + 1,
      white: moves[index]?.san ?? "",
      black: moves[index + 1]?.san ?? null,
    });
  }

  return rows;
}

export function collectCapturedPieces(
  moves: readonly Pick<Move, "captured" | "color">[],
): CapturedPiecesByColor {
  return moves.reduce<CapturedPiecesByColor>(
    (captures, move) => {
      if (move.captured === undefined) {
        return captures;
      }

      const capturedPiece =
        move.color === "w"
          ? (move.captured as PieceCode)
          : (move.captured.toUpperCase() as PieceCode);

      captures[move.color === "w" ? "white" : "black"].push(capturedPiece);
      return captures;
    },
    { white: [], black: [] },
  );
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
