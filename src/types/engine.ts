export interface ChessEngineAPI {
  initEngine(): Promise<number>;
  setPosition(fen: string): Promise<number>;
  searchBestMove(depth: number, timeMs: number): Promise<string>;
  evaluatePosition(): Promise<number>;
  getLegalMoves(): Promise<string[]>;
}

export interface EngineService extends ChessEngineAPI {
  isReady(): Promise<boolean>;
  resetGame(): Promise<void>;
}

export interface EngineSearchResult {
  bestMove: string;
  depth: number;
  timeMs: number;
}

export interface UseChessEngineResult {
  isReady: boolean;
  isThinking: boolean;
  error: string | null;
  searchBestMove(fen: string, depth: number, timeMs: number): Promise<string>;
  evaluatePosition(fen: string): Promise<number>;
  resetEngine(): Promise<void>;
  newGame(): Promise<void>;
}
