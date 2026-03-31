"use client";

import { Chess, type Move, type PieceSymbol, type Square } from "chess.js";
import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
  type CSSProperties,
} from "react";

import Board from "@/components/Board/Board";
import { useChessEngine } from "@/hooks/useChessEngine";

import styles from "./page.module.css";
import {
  DIFFICULTY_LEVELS,
  formatEvaluationLabel,
  getDifficultyConfig,
  getEvaluationFill,
  getGameResult,
  getPlayedPlyCount,
  parseUciMove,
  shouldEngineMove,
  toWhiteCentipawns,
  type DifficultyKey,
  type GameResult,
} from "./gameUtils";

interface GameSnapshot {
  fen: string;
  lastMove: Move | null;
  result: GameResult | null;
}

export default function Home() {
  const { error: engineError, evaluatePosition, isReady, isThinking, searchBestMove } =
    useChessEngine();
  const [gameSnapshot, setGameSnapshot] = useState<GameSnapshot>(() => {
    const game = new Chess();

    return {
      fen: game.fen(),
      lastMove: null,
      result: null,
    };
  });
  const [difficulty, setDifficulty] = useState<DifficultyKey>("medium");
  const [evaluation, setEvaluation] = useState<number | null>(null);
  const [positionError, setPositionError] = useState<string | null>(null);
  const difficultyRef = useRef<DifficultyKey>(difficulty);
  const evaluationRequestIdRef = useRef(0);
  const engineSearchIdRef = useRef(0);

  const game = useMemo(() => new Chess(gameSnapshot.fen), [gameSnapshot.fen]);
  const difficultyConfig = getDifficultyConfig(difficulty);
  const evaluationLabel = formatEvaluationLabel(evaluation);
  const evaluationFill = getEvaluationFill(evaluation);
  const moveCount = getPlayedPlyCount(gameSnapshot.fen);
  const boardDisabled = !isReady || isThinking || gameSnapshot.result !== null;
  const statusLabel = getStatusLabel({
    engineError,
    game,
    isReady,
    isThinking,
    result: gameSnapshot.result,
  });

  useEffect(() => {
    difficultyRef.current = difficulty;
  }, [difficulty]);

  useEffect(() => {
    if (!isReady) {
      return;
    }

    const requestId = evaluationRequestIdRef.current + 1;
    evaluationRequestIdRef.current = requestId;

    void evaluatePosition(gameSnapshot.fen)
      .then((score) => {
        if (evaluationRequestIdRef.current !== requestId) {
          return;
        }

        setEvaluation(toWhiteCentipawns(score, gameSnapshot.fen));
        setPositionError(null);
      })
      .catch((error: unknown) => {
        if (evaluationRequestIdRef.current !== requestId) {
          return;
        }

        setPositionError(
          error instanceof Error ? error.message : "Failed to evaluate the current position.",
        );
      });
  }, [evaluatePosition, gameSnapshot.fen, isReady]);

  useEffect(() => {
    if (!isReady || gameSnapshot.result !== null || !shouldEngineMove(gameSnapshot.fen)) {
      return;
    }

    const searchId = engineSearchIdRef.current + 1;
    const currentFen = gameSnapshot.fen;
    const currentDifficulty = getDifficultyConfig(difficultyRef.current);
    engineSearchIdRef.current = searchId;

    void searchBestMove(currentFen, currentDifficulty.depth, currentDifficulty.timeMs)
      .then((bestMoveUci) => {
        if (engineSearchIdRef.current !== searchId) {
          return;
        }

        const parsedMove = parseUciMove(bestMoveUci);
        if (parsedMove === null) {
          throw new Error(`Engine returned an invalid move: "${bestMoveUci}"`);
        }

        const nextGame = new Chess(currentFen);
        const move = nextGame.move(parsedMove);

        if (move === null) {
          throw new Error(`Engine returned an illegal move for the current board: "${bestMoveUci}"`);
        }

        setGameSnapshot((currentSnapshot) =>
          currentSnapshot.fen !== currentFen
            ? currentSnapshot
            : {
                fen: nextGame.fen(),
                lastMove: move,
                result: getGameResult(nextGame),
              },
        );
      })
      .catch((error: unknown) => {
        if (engineSearchIdRef.current !== searchId) {
          return;
        }

        setPositionError(
          error instanceof Error ? error.message : "Failed to apply the engine response.",
        );
      });
  }, [gameSnapshot.fen, gameSnapshot.result, isReady, searchBestMove]);

  const handleMove = ({
    from,
    to,
    promotion,
  }: {
    from: Square;
    to: Square;
    promotion?: PieceSymbol;
  }) => {
    if (boardDisabled) {
      return;
    }

    const nextGame = new Chess(gameSnapshot.fen);
    const move = nextGame.move({
      from,
      to,
      ...(promotion ? { promotion } : {}),
    });

    if (move === null) {
      return;
    }

    setGameSnapshot({
      fen: nextGame.fen(),
      lastMove: move,
      result: getGameResult(nextGame),
    });
  };

  const handleNewGame = useCallback(() => {
    const nextGame = new Chess();
    engineSearchIdRef.current += 1;
    evaluationRequestIdRef.current += 1;
    setEvaluation(null);
    setPositionError(null);
    setGameSnapshot({
      fen: nextGame.fen(),
      lastMove: null,
      result: null,
    });
  }, []);

  return (
    <main className={styles.page}>
      <div className={styles.layout}>
        <section className={`${styles.panel} ${styles.heroPanel}`}>
          <p className={styles.eyebrow}>Claudefish</p>
          <h1 className={styles.heroTitle}>Play against the engine.</h1>
          <p className={styles.copy}>
            Your moves are validated by <code>chess.js</code>, then Claudefish searches
            for a reply in a Web Worker so the interface stays responsive while the
            engine thinks.
          </p>
          <div className={styles.heroStats}>
            <div className={styles.heroStat}>
              <span className={styles.captionLabel}>You play</span>
              <span className={styles.heroStatValue}>White</span>
            </div>
            <div className={styles.heroStat}>
              <span className={styles.captionLabel}>Current level</span>
              <span className={styles.heroStatValue}>{difficultyConfig.label}</span>
            </div>
          </div>

          <div className={styles.selectorHeader}>
            <div>
              <h2 className={styles.sectionTitle}>Difficulty</h2>
              <p className={styles.sectionCopy}>
                Switching levels mid-game takes effect on the next engine move.
              </p>
            </div>
            <div className={styles.selectorMeta}>
              Depth {difficultyConfig.depth} · {formatTime(difficultyConfig.timeMs)}
            </div>
          </div>
          <div className={styles.difficultyGrid}>
            {DIFFICULTY_LEVELS.map((option) => {
              const isSelected = option.key === difficulty;

              return (
                <button
                  aria-pressed={isSelected}
                  className={`${styles.difficultyButton} ${
                    isSelected ? styles.difficultyButtonActive : ""
                  }`}
                  key={option.key}
                  onClick={() => setDifficulty(option.key)}
                  type="button"
                >
                  <span className={styles.difficultyLabel}>{option.label}</span>
                  <span className={styles.difficultyHint}>
                    Depth {option.depth} · {formatTime(option.timeMs)}
                  </span>
                  <span className={styles.difficultySummary}>{option.summary}</span>
                </button>
              );
            })}
          </div>
        </section>

        <section className={styles.boardColumn}>
          {gameSnapshot.result !== null ? (
            <div
              className={`${styles.panel} ${styles.resultBanner} ${
                gameSnapshot.result.tone === "win"
                  ? styles.resultBannerWin
                  : gameSnapshot.result.tone === "loss"
                    ? styles.resultBannerLoss
                    : styles.resultBannerDraw
              }`}
            >
              <div>
                <p className={styles.eyebrow}>Game over</p>
                <h2 className={styles.resultTitle}>{gameSnapshot.result.title}</h2>
                <p className={styles.resultDetail}>{gameSnapshot.result.detail}</p>
              </div>
              <button className={styles.actionButton} onClick={handleNewGame} type="button">
                New Game
              </button>
            </div>
          ) : null}

          <div className={styles.boardStage}>
            <Board
              disabled={boardDisabled}
              fen={gameSnapshot.fen}
              lastMove={gameSnapshot.lastMove}
              onMove={handleMove}
            />
            {isThinking ? (
              <div className={styles.thinkingBadge}>
                <span aria-hidden="true" className={styles.spinner} />
                <div>
                  <strong>Claudefish is thinking…</strong>
                  <span className={styles.thinkingMeta}>
                    {difficultyConfig.label} · Depth {difficultyConfig.depth} ·{" "}
                    {formatTime(difficultyConfig.timeMs)}
                  </span>
                </div>
              </div>
            ) : null}
          </div>

          <div className={`${styles.panel} ${styles.boardCaption}`}>
            <div>
              <span className={styles.captionLabel}>Status</span>
              <span className={styles.captionValue}>{statusLabel}</span>
            </div>
            <div>
              <span className={styles.captionLabel}>Evaluation</span>
              <span className={styles.captionValue}>{evaluationLabel}</span>
            </div>
            <div>
              <span className={styles.captionLabel}>Last move</span>
              <span className={styles.captionValue}>
                {gameSnapshot.lastMove?.san ?? "None yet"}
              </span>
            </div>
            <div>
              <span className={styles.captionLabel}>Moves played</span>
              <span className={styles.captionValue}>{moveCount}</span>
            </div>
            <div className={styles.captionWide}>
              <span className={styles.captionLabel}>Engine state</span>
              <span className={styles.captionValue}>
                {isReady
                  ? isThinking
                    ? "Searching in the worker thread."
                    : "Ready for the next move."
                  : "Initializing the WebAssembly engine…"}
              </span>
            </div>
          </div>
        </section>

        <aside className={styles.sideColumn}>
          <section className={`${styles.panel} ${styles.evalPanel}`}>
            <div className={styles.evalHeader}>
              <div>
                <p className={styles.eyebrow}>Evaluation</p>
                <h2 className={styles.sectionTitle}>Position balance</h2>
              </div>
              <div className={styles.evalValue}>{evaluationLabel}</div>
            </div>

            <div className={styles.evalLayout}>
              <div
                aria-label={`Evaluation bar ${evaluationLabel}`}
                className={styles.evalBar}
                style={
                  {
                    "--eval-fill": `${evaluationFill}%`,
                  } as CSSProperties
                }
              >
                <div className={styles.evalBarBlack} />
                <div className={styles.evalBarWhite} />
                <div className={styles.evalDivider} />
              </div>

              <div className={styles.evalNotes}>
                <p className={styles.evalCopy}>
                  Positive values favor White. Mate scores are shown as <code>M#</code>{" "}
                  instead of raw centipawns.
                </p>
                <p className={styles.evalCopy}>
                  The bar updates after each move while the engine continues searching in a
                  non-blocking worker.
                </p>
              </div>
            </div>
          </section>

          <section className={`${styles.panel} ${styles.detailPanel}`}>
            <p className={styles.eyebrow}>Live match</p>
            <h2 className={styles.sectionTitle}>What&apos;s active now</h2>
            <dl className={styles.detailList}>
              <div>
                <dt>Board lock</dt>
                <dd>
                  The board is disabled while Claudefish thinks, then reopens after the
                  engine move lands.
                </dd>
              </div>
              <div>
                <dt>Engine response</dt>
                <dd>
                  Each legal player move sends the current FEN to the Web Worker with the
                  selected depth and time limit.
                </dd>
              </div>
              <div>
                <dt>Endgame handling</dt>
                <dd>
                  Checkmate, stalemate, repetition, insufficient material, and 50-move
                  draws all stop play and surface a result banner.
                </dd>
              </div>
            </dl>
            {positionError !== null || engineError !== null ? (
              <p className={styles.errorText}>{positionError ?? engineError}</p>
            ) : null}
          </section>
        </aside>
      </div>
    </main>
  );
}

function getStatusLabel({
  engineError,
  game,
  isReady,
  isThinking,
  result,
}: {
  engineError: string | null;
  game: Chess;
  isReady: boolean;
  isThinking: boolean;
  result: GameResult | null;
}): string {
  if (engineError !== null) {
    return engineError;
  }

  if (!isReady) {
    return "Initializing Claudefish…";
  }

  if (result !== null) {
    return result.title;
  }

  if (isThinking) {
    return "Claudefish is calculating a reply.";
  }

  if (game.isCheck()) {
    return game.turn() === "w"
      ? "Your king is in check."
      : "Claudefish is in check.";
  }

  return game.turn() === "w" ? "Your move." : "Claudefish to move.";
}

function formatTime(timeMs: number): string {
  return timeMs >= 1000 ? `${(timeMs / 1000).toFixed(0)}s` : `${timeMs}ms`;
}
