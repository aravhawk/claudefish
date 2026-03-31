"use client";

import { Chess, type Move, type PieceSymbol, type Square } from "chess.js";
import { useCallback, useEffect, useMemo, useRef, useState, type CSSProperties } from "react";

import Board from "@/components/Board/Board";
import CapturedPieces from "@/components/CapturedPieces/CapturedPieces";
import MoveHistory from "@/components/MoveHistory/MoveHistory";
import { useChessEngine } from "@/hooks/useChessEngine";

import styles from "./page.module.css";
import {
  buildMoveHistoryRows,
  collectCapturedPieces,
  DIFFICULTY_LEVELS,
  formatEvaluationLabel,
  getDifficultyConfig,
  getEvaluationFill,
  getGameResult,
  getUndoPlyCount,
  parseUciMove,
  replayGame,
  shouldEngineMove,
  toWhiteCentipawns,
  type DifficultyKey,
  type GameResult,
  type ParsedUciMove,
} from "./gameUtils";
import {
  BOARD_THEMES,
  getBoardTheme,
  getBoardThemeStyle,
  type ThemeKey,
} from "./themes";

const LOADING_PREVIEW_SQUARES = Array.from({ length: 16 }, (_, index) => index);

export default function Home() {
  const {
    error: engineError,
    evaluatePosition,
    isReady,
    isThinking,
    resetEngine,
    searchBestMove,
  } = useChessEngine();
  const [playedMoves, setPlayedMoves] = useState<ParsedUciMove[]>([]);
  const [difficulty, setDifficulty] = useState<DifficultyKey>("medium");
  const [themeKey, setThemeKey] = useState<ThemeKey>("classic-wood");
  const [evaluation, setEvaluation] = useState<number | null>(null);
  const [positionError, setPositionError] = useState<string | null>(null);
  const [loadingDismissed, setLoadingDismissed] = useState(false);
  const difficultyRef = useRef<DifficultyKey>(difficulty);
  const evaluationRequestIdRef = useRef(0);
  const engineSearchIdRef = useRef(0);

  const theme = useMemo(() => getBoardTheme(themeKey), [themeKey]);
  const themeStyle = useMemo(() => getBoardThemeStyle(theme), [theme]);
  const game = useMemo(() => replayGame(playedMoves), [playedMoves]);
  const history = useMemo(() => game.history({ verbose: true }) as Move[], [game]);
  const currentFen = game.fen();
  const lastMove = history.at(-1) ?? null;
  const result = getGameResult(game);
  const historyRows = useMemo(() => buildMoveHistoryRows(history), [history]);
  const capturedPieces = useMemo(() => collectCapturedPieces(history), [history]);
  const difficultyConfig = getDifficultyConfig(difficulty);
  const evaluationLabel = formatEvaluationLabel(evaluation);
  const evaluationFill = getEvaluationFill(evaluation);
  const moveCount = history.length;
  const undoPlyCount = getUndoPlyCount(currentFen, moveCount);
  const boardDisabled = !isReady || isThinking || result !== null;
  const showLoadingScreen = !isReady || !loadingDismissed;
  const loadingScreenClosing = isReady && !loadingDismissed;
  const statusLabel = getStatusLabel({
    engineError,
    game,
    isReady,
    isThinking,
    result,
  });

  useEffect(() => {
    difficultyRef.current = difficulty;
  }, [difficulty]);

  useEffect(() => {
    if (!isReady) {
      const frameId = window.requestAnimationFrame(() => {
        setLoadingDismissed(false);
      });

      return () => window.cancelAnimationFrame(frameId);
    }

    const timeoutId = window.setTimeout(() => {
      setLoadingDismissed(true);
    }, 320);

    return () => {
      window.clearTimeout(timeoutId);
    };
  }, [isReady]);

  useEffect(() => {
    if (!isReady) {
      return;
    }

    const requestId = evaluationRequestIdRef.current + 1;
    evaluationRequestIdRef.current = requestId;

    void evaluatePosition(currentFen)
      .then((score) => {
        if (evaluationRequestIdRef.current !== requestId) {
          return;
        }

        setEvaluation(toWhiteCentipawns(score, currentFen));
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
  }, [currentFen, evaluatePosition, isReady]);

  useEffect(() => {
    if (!isReady || result !== null || !shouldEngineMove(currentFen)) {
      return;
    }

    const searchId = engineSearchIdRef.current + 1;
    const searchFen = currentFen;
    const currentDifficulty = getDifficultyConfig(difficultyRef.current);
    engineSearchIdRef.current = searchId;

    void searchBestMove(searchFen, currentDifficulty.depth, currentDifficulty.timeMs)
      .then((bestMoveUci) => {
        if (engineSearchIdRef.current !== searchId) {
          return;
        }

        const parsedMove = parseUciMove(bestMoveUci);
        if (parsedMove === null) {
          throw new Error(`Engine returned an invalid move: "${bestMoveUci}"`);
        }

        const nextGame = new Chess(searchFen);
        const move = nextGame.move(parsedMove);

        if (move === null) {
          throw new Error(`Engine returned an illegal move for the current board: "${bestMoveUci}"`);
        }

        setPlayedMoves((currentMoves) => {
          if (replayGame(currentMoves).fen() !== searchFen) {
            return currentMoves;
          }

          return [...currentMoves, parsedMove];
        });
      })
      .catch((error: unknown) => {
        if (engineSearchIdRef.current !== searchId) {
          return;
        }

        setPositionError(
          error instanceof Error ? error.message : "Failed to apply the engine response.",
        );
      });
  }, [currentFen, isReady, result, searchBestMove]);

  const cancelPendingEngineWork = useCallback(() => {
    engineSearchIdRef.current += 1;
    evaluationRequestIdRef.current += 1;

    if (!isThinking) {
      return;
    }

    void resetEngine().catch((error: unknown) => {
      setPositionError(
        error instanceof Error ? error.message : "Failed to restart the chess engine worker.",
      );
    });
  }, [isThinking, resetEngine]);

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

    const nextGame = new Chess(currentFen);
    const move = nextGame.move({
      from,
      to,
      ...(promotion ? { promotion } : {}),
    });

    if (move === null) {
      return;
    }

    setPlayedMoves((currentMoves) => [
      ...currentMoves,
      { from, to, ...(promotion ? { promotion } : {}) },
    ]);
  };

  const handleNewGame = useCallback(() => {
    cancelPendingEngineWork();
    setEvaluation(0);
    setPositionError(null);
    setPlayedMoves([]);
  }, [cancelPendingEngineWork]);

  const handleUndo = useCallback(() => {
    if (undoPlyCount === 0) {
      return;
    }

    cancelPendingEngineWork();
    setEvaluation(null);
    setPositionError(null);
    setPlayedMoves((currentMoves) =>
      currentMoves.slice(0, Math.max(0, currentMoves.length - undoPlyCount)),
    );
  }, [cancelPendingEngineWork, undoPlyCount]);

  return (
    <main className={styles.page} data-theme={theme.key} style={themeStyle}>
      <div aria-hidden="true" className={styles.backdropGlow} />
      <div aria-hidden="true" className={styles.backdropMesh} />

      {showLoadingScreen ? (
        <div
          aria-live="polite"
          className={`${styles.loadingOverlay} ${
            loadingScreenClosing ? styles.loadingOverlayExit : ""
          }`}
          role="status"
        >
          <div className={styles.loadingPanel}>
            <p className={styles.eyebrow}>Initializing Claudefish</p>
            <h2 className={styles.loadingTitle}>{theme.loadingLabel}</h2>
            <p className={styles.loadingCopy}>
              Streaming the WebAssembly engine, preparing the board theme, and
              bringing the controls online.
            </p>

            <div aria-hidden="true" className={styles.loadingPreview}>
              {LOADING_PREVIEW_SQUARES.map((squareIndex) => (
                <span
                  className={
                    squareIndex % 2 === 0
                      ? styles.loadingPreviewLight
                      : styles.loadingPreviewDark
                  }
                  key={squareIndex}
                />
              ))}
            </div>

            <div className={styles.loadingStatusRow}>
              <span aria-hidden="true" className={styles.loadingSpinner} />
              <div>
                <strong>WASM engine booting</strong>
                <span className={styles.loadingStatusMeta}>
                  The board will become interactive as soon as the worker is ready.
                </span>
              </div>
            </div>
          </div>
        </div>
      ) : null}

      <div className={styles.layout}>
        <section className={`${styles.panel} ${styles.heroPanel}`}>
          <p className={styles.eyebrow}>Claudefish</p>
          <h1 className={styles.heroTitle}>Play against the engine in a polished match room.</h1>
          <p className={styles.copy}>
            Your moves are validated by <code>chess.js</code>, then Claudefish searches
            for a reply in a Web Worker so the interface stays responsive while the
            engine thinks. Switch themes at any moment without interrupting the
            position, move list, captures, or evaluation.
          </p>
          <div className={styles.heroStats}>
            <div className={styles.heroStat}>
              <span className={styles.captionLabel}>You play</span>
              <span className={styles.heroStatValue}>White</span>
            </div>
            <div className={styles.heroStat}>
              <span className={styles.captionLabel}>Active theme</span>
              <span className={styles.heroStatValue}>{theme.label}</span>
            </div>
            <div className={styles.heroStat}>
              <span className={styles.captionLabel}>Current level</span>
              <span className={styles.heroStatValue}>{difficultyConfig.label}</span>
            </div>
          </div>

          <div className={styles.controlsBlock}>
            <div className={styles.selectorHeader}>
              <div>
                <h2 className={styles.sectionTitle}>Board theme</h2>
                <p className={styles.sectionCopy}>
                  All colors update instantly while your current game stays exactly in
                  place.
                </p>
              </div>
              <div className={styles.selectorMeta}>3 curated looks</div>
            </div>

            <div className={styles.themeGrid}>
              {BOARD_THEMES.map((option) => {
                const isSelected = option.key === themeKey;

                return (
                  <button
                    aria-pressed={isSelected}
                    className={`${styles.themeButton} ${
                      isSelected ? styles.themeButtonActive : ""
                    }`}
                    key={option.key}
                    onClick={() => setThemeKey(option.key)}
                    style={
                      {
                        "--theme-preview-light": option.colors.squareLight,
                        "--theme-preview-dark": option.colors.squareDark,
                        "--theme-preview-accent": option.colors.accentStrong,
                      } as CSSProperties
                    }
                    type="button"
                  >
                    <span aria-hidden="true" className={styles.themePreview}>
                      <span className={styles.themePreviewLight} />
                      <span className={styles.themePreviewDark} />
                      <span className={styles.themePreviewAccent} />
                    </span>
                    <span className={styles.themeButtonBody}>
                      <span className={styles.themeLabel}>{option.label}</span>
                      <span className={styles.themeMood}>{option.mood}</span>
                      <span className={styles.themeDescription}>{option.description}</span>
                    </span>
                  </button>
                );
              })}
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

          <div className={styles.controlsBlock}>
            <div className={styles.selectorHeader}>
              <div>
                <h2 className={styles.sectionTitle}>Game controls</h2>
                <p className={styles.sectionCopy}>
                  Undo rewinds the latest player/engine exchange. If Claudefish is
                  thinking, the current search is cancelled first.
                </p>
              </div>
              <div className={styles.selectorMeta}>History: {moveCount} ply</div>
            </div>
            <div className={styles.controlRow}>
              <button
                className={styles.actionButton}
                disabled={undoPlyCount === 0}
                onClick={handleUndo}
                type="button"
              >
                Undo
              </button>
              <button className={styles.actionButton} onClick={handleNewGame} type="button">
                New Game
              </button>
            </div>
          </div>
        </section>

        <section className={styles.boardColumn}>
          {result !== null ? (
            <div
              className={`${styles.panel} ${styles.resultBanner} ${
                result.tone === "win"
                  ? styles.resultBannerWin
                  : result.tone === "loss"
                    ? styles.resultBannerLoss
                    : styles.resultBannerDraw
              }`}
            >
              <div>
                <p className={styles.eyebrow}>Game over</p>
                <h2 className={styles.resultTitle}>{result.title}</h2>
                <p className={styles.resultDetail}>{result.detail}</p>
              </div>
              <button className={styles.actionButton} onClick={handleNewGame} type="button">
                New Game
              </button>
            </div>
          ) : null}

          <div className={styles.boardStage}>
            <Board
              disabled={boardDisabled}
              fen={currentFen}
              lastMove={lastMove}
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
              <span className={styles.captionValue}>{lastMove?.san ?? "None yet"}</span>
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
                    : shouldEngineMove(currentFen) && result === null
                      ? "Awaiting a queued engine reply."
                      : "Ready for the next move."
                  : "Initializing the WebAssembly engine…"}
              </span>
            </div>
          </div>
        </section>

        <aside className={styles.sideColumn}>
          <section className={`${styles.panel} ${styles.historyPanel}`}>
            <MoveHistory rows={historyRows} />
          </section>

          <section className={`${styles.panel} ${styles.capturePanel}`}>
            <CapturedPieces captures={capturedPieces} />
          </section>

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

          {positionError !== null || engineError !== null ? (
            <section className={`${styles.panel} ${styles.detailPanel}`}>
              <p className={styles.eyebrow}>Engine status</p>
              <h2 className={styles.sectionTitle}>Attention needed</h2>
              <p className={styles.errorText}>{positionError ?? engineError}</p>
            </section>
          ) : null}
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
